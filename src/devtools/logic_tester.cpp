#include "cw/devtools/logic_tester.hpp"

#include <rc_error.h>
#include <rc_runtime.h>
#include <rc_runtime_types.h>
#include <rcheevos/rc_internal.h>
#include <rhash/md5.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace cw {

namespace {

// rcheevos' event and address-validation callbacks take no "user data" argument, so the
// collection target and the memory being validated are handed over through these pointers for
// the duration of one call (the same pattern as LibretroCore's s_active).
thread_local std::vector<LogicEvent>* t_events = nullptr;
thread_local uint64_t t_frame = 0;
thread_local const MemoryReader* t_validated_memory = nullptr;

std::optional<LogicEventKind> to_kind(uint8_t type) {
    switch (type) {
    case RC_RUNTIME_EVENT_ACHIEVEMENT_ACTIVATED:
        return LogicEventKind::Activated;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_PAUSED:
        return LogicEventKind::Paused;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_RESET:
        return LogicEventKind::Reset;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED:
        return LogicEventKind::Primed;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED:
        return LogicEventKind::Unprimed;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED:
        return LogicEventKind::Triggered;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_DISABLED:
        return LogicEventKind::Disabled;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_PROGRESS_UPDATED:
        return LogicEventKind::Progress;
    default:
        return std::nullopt; // leaderboard events: step 6.2
    }
}

void RC_CCONV collect_event(const rc_runtime_event_t* event) {
    if (!t_events) {
        return;
    }
    if (const auto kind = to_kind(event->type)) {
        t_events->push_back(
            {.id = event->id, .kind = *kind, .value = event->value, .frame = t_frame});
    }
}

// rcheevos asks for 1, 2 or 4 bytes (24-bit values are read as 4 and masked) and expects them
// assembled little-endian. Bytes past the end of readable memory count as zero; addresses that
// cannot be read at all are caught by validate_addresses().
uint32_t RC_CCONV peek(uint32_t address, uint32_t num_bytes, void* ud) {
    const auto& memory = *static_cast<const MemoryReader*>(ud);
    std::array<uint8_t, 4> bytes{};
    memory.read(address, std::span(bytes.data(), std::min<uint32_t>(num_bytes, 4)));
    return uint32_t{bytes[0]} | uint32_t{bytes[1]} << 8 | uint32_t{bytes[2]} << 16 |
           uint32_t{bytes[3]} << 24;
}

int RC_CCONV address_is_readable(uint32_t address) {
    std::array<uint8_t, 1> byte{};
    return t_validated_memory && t_validated_memory->read(address, byte) == 1;
}

void destroy_runtime(rc_runtime_t* runtime) {
    rc_runtime_destroy(runtime);
    delete runtime;
}

uint32_t read_u32(std::span<const uint8_t> data, size_t offset) {
    return uint32_t{data[offset]} | uint32_t{data[offset + 1]} << 8 |
           uint32_t{data[offset + 2]} << 16 | uint32_t{data[offset + 3]} << 24;
}

void append_u32(std::vector<uint8_t>& data, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        data.push_back(static_cast<uint8_t>(value >> shift));
    }
}

std::array<uint8_t, 16> progress_checksum(std::span<const uint8_t> data) {
    std::array<uint8_t, 16> digest;
    md5_state_t checksum;
    md5_init(&checksum);
    md5_append(&checksum, data.data(), static_cast<int>(data.size()));
    md5_finish(&checksum, digest.data());
    return digest;
}

void update_progress_checksum(std::vector<uint8_t>& data) {
    const auto digest = progress_checksum(std::span(data).first(data.size() - 16));
    std::copy(digest.begin(), digest.end(), data.end() - 16);
}

const rc_runtime_trigger_t* matching_trigger(const rc_runtime_t& runtime,
                                             std::span<const uint8_t> data) {
    for (uint32_t i = 0; i < runtime.trigger_count; ++i) {
        const auto& entry = runtime.triggers[i];
        if (entry.trigger && entry.id == read_u32(data, 0) &&
            std::memcmp(entry.md5, data.data() + 4, 16) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

bool terminal_state(uint32_t state) {
    return state == RC_TRIGGER_STATE_INACTIVE || state == RC_TRIGGER_STATE_TRIGGERED ||
           state == RC_TRIGGER_STATE_DISABLED;
}

// The pinned sized deserializer checks chunk boundaries, but its condition readers do not.
// Validate exactly the reads it will perform before handing any file data to those readers.
bool valid_trigger_progress(const rc_trigger_t& trigger, std::span<const uint8_t> data) {
    if (data.size() < 8 || read_u32(data, 0) > RC_TRIGGER_STATE_DISABLED) {
        return false;
    }
    size_t offset = 8;
    const auto check_group = [&](const rc_condset_t* group) {
        if (data.size() - offset < 4) {
            return false;
        }
        offset += 4; // paused flag
        for (const rc_condition_t* condition = group->conditions; condition;
             condition = condition->next) {
            if (data.size() - offset < 8) {
                return false;
            }
            const uint32_t flags = read_u32(data, offset + 4);
            offset += 8; // hits and flags
            const rc_operand_t* operands[] = {&condition->operand1, &condition->operand2};
            constexpr uint32_t indirect_flags[] = {0x00010000, 0x00100000};
            for (size_t i = 0; i < 2; ++i) {
                if (flags & indirect_flags[i]) {
                    if (!rc_operand_is_memref(operands[i]) || data.size() - offset < 8) {
                        return false;
                    }
                    offset += 8; // indirect value and prior
                }
            }
        }
        return true;
    };
    if (trigger.requirement && !check_group(trigger.requirement)) {
        return false;
    }
    for (const rc_condset_t* group = trigger.alternative; group; group = group->next) {
        if (!check_group(group)) {
            return false;
        }
    }
    return offset == data.size();
}

bool valid_progress(const rc_runtime_t& runtime, std::span<const uint8_t> data) {
    if (data.size() < 28 || data.size() > LogicTester::kMaxProgressSize ||
        std::memcmp(data.data(), "RAP\n", 4) != 0) {
        return false;
    }
    size_t offset = 4;
    while (data.size() - offset >= 8) {
        const auto header = data.subspan(offset, 8);
        const size_t size = read_u32(header, 4);
        offset += 8;
        if (size > data.size() - offset || size % 4 != 0) {
            return false;
        }
        const auto payload = data.subspan(offset, size);
        if (std::memcmp(header.data(), "DONE", 4) == 0) {
            if (size != 16 || offset + size != data.size()) {
                return false;
            }
            const auto digest = progress_checksum(data.first(offset));
            return std::equal(digest.begin(), digest.end(), payload.begin());
        }
        if (std::memcmp(header.data(), "MREF", 4) == 0) {
            if (size % 16 != 0) {
                return false;
            }
        } else if (std::memcmp(header.data(), "ACHV", 4) == 0) {
            if (size < 20) {
                return false;
            }
            if (const auto* entry = matching_trigger(runtime, payload);
                entry && !valid_trigger_progress(*entry->trigger, payload.subspan(20))) {
                return false;
            }
        } else if (std::memcmp(header.data(), "CWST", 4) == 0) {
            if (size != 28 || !terminal_state(read_u32(payload, 20))) {
                return false;
            }
        } else if (std::memcmp(header.data(), "VARS", 4) == 0 ||
                   std::memcmp(header.data(), "LBRD", 4) == 0) {
            // No Rich Presence or leaderboard definitions exist in this tester. Their readers
            // inspect only the leading count/id before skipping the rest.
            if (size < 4) {
                return false;
            }
        } else if (std::memcmp(header.data(), "RICH", 4) != 0 && size >= 0x10000) {
            return false; // rcheevos' limit for unknown extension chunks
        }
        offset += size;
    }
    return false;
}

// Runs `call` with rcheevos' callbacks collecting into `events` for `frame`.
template <typename Call> std::vector<LogicEvent> collecting(uint64_t frame, Call&& call) {
    std::vector<LogicEvent> events;
    t_events = &events;
    t_frame = frame;
    call();
    t_events = nullptr;
    return events;
}

} // namespace

LogicTester::LogicTester() : runtime_(new rc_runtime_t, &destroy_runtime) {
    rc_runtime_init(runtime_.get());
}

LogicTester::~LogicTester() = default;

std::optional<uint32_t> LogicTester::add(std::string title, std::string logic, std::string& error) {
    const uint32_t id = next_id_;
    const int result =
        rc_runtime_activate_achievement(runtime_.get(), id, logic.c_str(), nullptr, 0);
    if (result != RC_OK) {
        error = std::string("Invalid logic: ") + rc_error_str(result);
        return std::nullopt;
    }
    ++next_id_;
    achievements_.push_back({.id = id,
                             .title = std::move(title),
                             .logic = std::move(logic),
                             .last_frame = std::nullopt});
    error.clear();
    return id;
}

bool LogicTester::remove(uint32_t id) {
    const auto found = std::find_if(achievements_.begin(), achievements_.end(),
                                    [id](const TestedAchievement& a) { return a.id == id; });
    if (found == achievements_.end()) {
        return false;
    }
    rc_runtime_deactivate_achievement(runtime_.get(), id);
    achievements_.erase(found);
    return true;
}

bool LogicTester::rearm(uint32_t id) {
    const auto found = std::find_if(achievements_.begin(), achievements_.end(),
                                    [id](const TestedAchievement& a) { return a.id == id; });
    // A Disabled achievement reads an address the memory cannot provide; re-arming would hide
    // that without making the address readable. It comes back on reset() with a memory that
    // has the address.
    if (found == achievements_.end() || state(id) == LogicState::Disabled) {
        return false;
    }
    // Activating the same id with the same logic only resets it (rcheevos compares checksums).
    const bool ok = rc_runtime_activate_achievement(runtime_.get(), id, found->logic.c_str(),
                                                    nullptr, 0) == RC_OK;
    if (ok)
        found->last_frame.reset();
    return ok;
}

void LogicTester::reset() {
    // A fresh runtime rather than rc_runtime_reset(): the reset only clears hit counts, while
    // rc_runtime_validate_addresses() permanently marks the memory references of unreadable
    // addresses as "do not read" (RC_VALUE_TYPE_NONE, runtime.c). Kept, an address that the next
    // game does have would never be read again. Rebuilding re-parses every logic string.
    runtime_.reset(new rc_runtime_t);
    rc_runtime_init(runtime_.get());
    for (TestedAchievement& a : achievements_) {
        a.last_frame.reset();
        // The logic parsed when it was added, so it parses again.
        rc_runtime_activate_achievement(runtime_.get(), a.id, a.logic.c_str(), nullptr, 0);
    }
}

bool LogicTester::serialize_progress(std::vector<uint8_t>& out, std::string& error) const {
    const uint32_t size = rc_runtime_progress_size(runtime_.get(), nullptr);
    if (size < 28 || size > kMaxProgressSize) {
        error = "Runtime progress is too large or could not be measured";
        return false;
    }
    std::vector<uint8_t> data(size);
    const int result =
        rc_runtime_serialize_progress_sized(data.data(), size, runtime_.get(), nullptr);
    if (result != RC_OK) {
        error = std::string("Could not serialize runtime progress: ") + rc_error_str(result);
        return false;
    }

    // The native writer omits Triggered/Disabled/Inactive achievements. Record these offline
    // test states in unknown RAP chunks, which standard readers skip (runtime_progress.c).
    const std::vector<uint8_t> done(data.end() - 24, data.end());
    data.resize(data.size() - 24);
    for (uint32_t i = 0; i < runtime_->trigger_count; ++i) {
        const auto& entry = runtime_->triggers[i];
        if (!entry.trigger || !terminal_state(entry.trigger->state)) {
            continue;
        }
        if (data.size() + 36 + done.size() > kMaxProgressSize) {
            error = "Runtime progress is too large";
            return false;
        }
        data.insert(data.end(), {'C', 'W', 'S', 'T'});
        append_u32(data, 28);
        append_u32(data, entry.id);
        data.insert(data.end(), entry.md5, entry.md5 + 16);
        append_u32(data, entry.trigger->state);
        append_u32(data, entry.trigger->measured_value);
    }
    data.insert(data.end(), done.begin(), done.end());
    update_progress_checksum(data);
    out = std::move(data);
    error.clear();
    return true;
}

bool LogicTester::deserialize_progress(std::span<const uint8_t> data, std::string& error) {
    std::unique_ptr<rc_runtime_t, decltype(&destroy_runtime)> restored(new rc_runtime_t,
                                                                       &destroy_runtime);
    rc_runtime_init(restored.get());
    for (const TestedAchievement& achievement : achievements_) {
        const int result = rc_runtime_activate_achievement(restored.get(), achievement.id,
                                                           achievement.logic.c_str(), nullptr, 0);
        if (result != RC_OK) {
            error = std::string("Could not rebuild runtime: ") + rc_error_str(result);
            return false;
        }
    }
    if (!valid_progress(*restored, data)) {
        error = "Invalid or truncated runtime progress";
        return false;
    }
    std::vector<uint8_t> filtered;
    auto native_data = data;
    if (rc_memrefs_count_memrefs(restored->memrefs) == 0) {
        // v12.5.0's MREF reader assumes there is at least one destination memref. A state
        // from a previous set can contain MREF even after every memory-based test was removed.
        // The original checksum was validated above; omit only these now-unused values.
        filtered.assign(data.begin(), data.begin() + 4);
        for (size_t offset = 4; offset + 8 <= data.size();) {
            const size_t size = read_u32(data, offset + 4);
            if (std::memcmp(data.data() + offset, "MREF", 4) != 0) {
                filtered.insert(filtered.end(), data.begin() + offset,
                                data.begin() + offset + 8 + size);
            }
            offset += 8 + size;
        }
        update_progress_checksum(filtered);
        native_data = filtered;
    }
    const int result = rc_runtime_deserialize_progress_sized(
        restored.get(), native_data.data(), static_cast<uint32_t>(native_data.size()), nullptr);
    if (result != RC_OK) {
        error = std::string("Could not restore runtime progress: ") + rc_error_str(result);
        return false;
    }
    for (size_t offset = 4; offset + 8 <= data.size();) {
        const size_t size = read_u32(data, offset + 4);
        if (std::memcmp(data.data() + offset, "CWST", 4) == 0) {
            const auto payload = data.subspan(offset + 8, size);
            if (const auto* entry = matching_trigger(*restored, payload)) {
                entry->trigger->state = static_cast<uint8_t>(read_u32(payload, 20));
                entry->trigger->measured_value = read_u32(payload, 24);
            }
        }
        offset += 8 + size;
    }
    runtime_ = std::move(restored);
    for (auto& achievement : achievements_)
        achievement.last_frame.reset(); // restored progress has no emulated frame timestamp
    error.clear();
    return true;
}

std::vector<LogicEvent> LogicTester::validate_addresses(const MemoryReader& memory,
                                                        uint64_t frame) {
    t_validated_memory = &memory;
    auto events = collecting(frame, [&] {
        rc_runtime_validate_addresses(runtime_.get(), &collect_event, &address_is_readable);
    });
    t_validated_memory = nullptr;
    return events;
}

std::vector<LogicEvent> LogicTester::do_frame(const MemoryReader& memory, uint64_t frame) {
    for (auto& achievement : achievements_) {
        const auto current = state(achievement.id);
        if (current != LogicState::Inactive && current != LogicState::Disabled &&
            current != LogicState::Triggered)
            achievement.last_frame = frame;
    }
    return collecting(frame, [&] {
        rc_runtime_do_frame(runtime_.get(), &collect_event, &peek,
                            const_cast<MemoryReader*>(&memory), nullptr);
    });
}

LogicState LogicTester::state(uint32_t id) const {
    const rc_trigger_t* trigger = rc_runtime_get_achievement(runtime_.get(), id);
    if (!trigger) {
        return LogicState::Inactive;
    }
    switch (trigger->state) {
    case RC_TRIGGER_STATE_WAITING:
        return LogicState::Waiting;
    case RC_TRIGGER_STATE_ACTIVE:
        return LogicState::Active;
    case RC_TRIGGER_STATE_PAUSED:
        return LogicState::Paused;
    case RC_TRIGGER_STATE_RESET:
        return LogicState::Reset;
    case RC_TRIGGER_STATE_TRIGGERED:
        return LogicState::Triggered;
    case RC_TRIGGER_STATE_PRIMED:
        return LogicState::Primed;
    case RC_TRIGGER_STATE_DISABLED:
        return LogicState::Disabled;
    default:
        return LogicState::Inactive;
    }
}

std::optional<std::pair<uint32_t, uint32_t>> LogicTester::measured(uint32_t id) const {
    unsigned value = 0;
    unsigned target = 0;
    if (!rc_runtime_get_achievement_measured(runtime_.get(), id, &value, &target) || target == 0) {
        return std::nullopt;
    }
    return std::pair<uint32_t, uint32_t>{value, target};
}

std::optional<AchievementStatus> LogicTester::snapshot(uint32_t id) const {
    const auto* trigger = rc_runtime_get_achievement(runtime_.get(), id);
    if (!trigger)
        return std::nullopt;
    AchievementStatus result;
    result.state = state(id);
    for (const auto& achievement : achievements_)
        if (achievement.id == id)
            result.last_frame = achievement.last_frame;
    const auto append = [&](const rc_condset_t* group) {
        ConditionGroupStatus output;
        if (group) {
            output.paused = group->is_paused != 0;
            // condset.c links ->next in source order across its reordered backing array.
            for (auto* condition = group->conditions; condition; condition = condition->next) {
                const bool modifying = condition->type == RC_CONDITION_ADD_SOURCE ||
                                       condition->type == RC_CONDITION_SUB_SOURCE ||
                                       condition->type == RC_CONDITION_ADD_ADDRESS ||
                                       condition->type == RC_CONDITION_REMEMBER;
                output.conditions.push_back({.has_comparison = !modifying,
                                             .is_true = (condition->is_true & 1) != 0,
                                             .caused_reset = (condition->is_true & 2) != 0,
                                             .current_hits = condition->current_hits,
                                             .required_hits = condition->required_hits});
            }
        }
        result.groups.push_back(std::move(output));
    };
    append(trigger->requirement);
    for (auto* group = trigger->alternative; group; group = group->next)
        append(group);
    return result;
}

const char* logic_state_name(LogicState state) {
    switch (state) {
    case LogicState::Inactive:
        return "Inactive";
    case LogicState::Waiting:
        return "Waiting";
    case LogicState::Active:
        return "Active";
    case LogicState::Paused:
        return "Paused";
    case LogicState::Reset:
        return "Reset";
    case LogicState::Triggered:
        return "Triggered";
    case LogicState::Primed:
        return "Primed";
    case LogicState::Disabled:
        return "Disabled";
    }
    return "?";
}

const char* logic_event_name(LogicEventKind kind) {
    switch (kind) {
    case LogicEventKind::Activated:
        return "activated";
    case LogicEventKind::Paused:
        return "paused";
    case LogicEventKind::Reset:
        return "reset";
    case LogicEventKind::Primed:
        return "primed";
    case LogicEventKind::Unprimed:
        return "unprimed";
    case LogicEventKind::Triggered:
        return "TRIGGERED";
    case LogicEventKind::Disabled:
        return "disabled";
    case LogicEventKind::Progress:
        return "progress";
    }
    return "?";
}

} // namespace cw
