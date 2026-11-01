#include "cw/devtools/logic_tester.hpp"
#include "cw/devtools/leaderboard_editor.hpp"
#include "cw/devtools/rich_presence_editor.hpp"

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
    case RC_RUNTIME_EVENT_LBOARD_STARTED:
        return LogicEventKind::LeaderboardStarted;
    case RC_RUNTIME_EVENT_LBOARD_CANCELED:
        return LogicEventKind::LeaderboardCanceled;
    case RC_RUNTIME_EVENT_LBOARD_UPDATED:
        return LogicEventKind::LeaderboardUpdated;
    case RC_RUNTIME_EVENT_LBOARD_TRIGGERED:
        return LogicEventKind::LeaderboardSubmitted;
    case RC_RUNTIME_EVENT_LBOARD_DISABLED:
        return LogicEventKind::LeaderboardDisabled;
    default:
        return std::nullopt;
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

// The pinned sized deserializer checks chunk boundaries, but not its nested readers.
// Mirror their reads with a bounded cursor before passing any file bytes to rcheevos.
struct ProgressCursor {
    std::span<const uint8_t> data;
    size_t offset = 0;
    bool skip(size_t size) {
        if (size > data.size() - offset)
            return false;
        offset += size;
        return true;
    }
    std::optional<uint32_t> number() {
        if (data.size() - offset < 4)
            return std::nullopt;
        const auto value = read_u32(data, offset);
        offset += 4;
        return value;
    }
    bool done() const { return offset == data.size(); }
};

bool valid_condset_progress(const rc_condset_t* group, ProgressCursor& cursor) {
    if (!group || !cursor.skip(4))
        return false;
    for (auto* condition = group->conditions; condition; condition = condition->next) {
        if (!cursor.skip(4))
            return false;
        const auto flags = cursor.number();
        if (!flags)
            return false;
        const rc_operand_t* operands[]{&condition->operand1, &condition->operand2};
        constexpr uint32_t indirect_flags[]{0x00010000, 0x00100000};
        for (size_t i = 0; i < 2; ++i) {
            if (*flags & indirect_flags[i]) {
                if (!rc_operand_is_memref(operands[i]) || !operands[i]->value.memref ||
                    !cursor.skip(8))
                    return false;
            }
        }
    }
    return true;
}

bool read_trigger_progress(const rc_trigger_t& trigger, ProgressCursor& cursor) {
    const auto state = cursor.number();
    if (!state || *state > RC_TRIGGER_STATE_DISABLED || !cursor.skip(4))
        return false;
    if (trigger.requirement && !valid_condset_progress(trigger.requirement, cursor))
        return false;
    for (auto* group = trigger.alternative; group; group = group->next)
        if (!valid_condset_progress(group, cursor))
            return false;
    return true;
}

bool valid_trigger_progress(const rc_trigger_t& trigger, std::span<const uint8_t> data) {
    ProgressCursor cursor{data};
    return read_trigger_progress(trigger, cursor) && cursor.done();
}

bool read_value_progress(const rc_value_t& value, ProgressCursor& cursor) {
    const auto flags = cursor.number();
    if (!flags || !cursor.skip(8))
        return false;
    return !(*flags & 0x01000000) || valid_condset_progress(value.conditions, cursor);
}

const rc_runtime_lboard_t* matching_lboard(const rc_runtime_t& runtime,
                                           std::span<const uint8_t> data) {
    for (uint32_t i = 0; i < runtime.lboard_count; ++i) {
        const auto& entry = runtime.lboards[i];
        if (entry.lboard && entry.id == read_u32(data, 0) &&
            std::memcmp(entry.md5, data.data() + 4, 16) == 0)
            return &entry;
    }
    return nullptr;
}

// CWRC carries a script checksum even when the RP has no conditional-display RICH chunk.
// Unknown extension chunks are ignored by standard RAP readers, like our existing CWST.
// Older RAPs with a matching RICH checksum can also restore their variables safely.
bool matching_presence_progress(const rc_runtime_t& runtime, std::span<const uint8_t> data) {
    if (!runtime.richpresence || !runtime.richpresence->richpresence)
        return false;
    std::optional<bool> extension;
    bool native_match = false;
    for (size_t offset = 4; data.size() - offset >= 8;) {
        const size_t size = read_u32(data, offset + 4);
        if (size > data.size() - offset - 8)
            return false;
        const auto payload = data.subspan(offset + 8, size);
        if (std::memcmp(data.data() + offset, "CWRC", 4) == 0 && size == 16)
            extension = std::memcmp(payload.data(), runtime.richpresence->md5, 16) == 0;
        else if (std::memcmp(data.data() + offset, "RICH", 4) == 0 && size >= 16)
            native_match |= std::memcmp(payload.data(), runtime.richpresence->md5, 16) == 0;
        offset += 8 + size;
    }
    return extension.value_or(native_match);
}

bool valid_variables_progress(const rc_runtime_t& runtime, std::span<const uint8_t> data) {
    ProgressCursor cursor{data};
    const auto count = cursor.number();
    if (!count)
        return false;
    std::vector<std::pair<uint32_t, const rc_value_t*>> pending;
    for (auto* value = runtime.richpresence->richpresence->values; value; value = value->next)
        pending.emplace_back(rc_djb2(value->name), value);
    // Match the native pending-array search/removal order, including possible hash collisions.
    std::reverse(pending.begin(), pending.end());
    if (*count > pending.size())
        return false;
    for (uint32_t i = 0; i < *count; ++i) {
        const auto hash = cursor.number();
        if (!hash)
            return false;
        size_t index = pending.size();
        while (index > 0 && pending[index - 1].first != *hash)
            --index;
        if (index == 0 || !read_value_progress(*pending[index - 1].second, cursor))
            return false;
        pending[index - 1] = pending.back();
        pending.pop_back();
    }
    return cursor.done();
}

bool valid_progress(const rc_runtime_t& runtime, std::span<const uint8_t> data) {
    if (data.size() < 28 || data.size() > LogicTester::kMaxProgressSize ||
        std::memcmp(data.data(), "RAP\n", 4) != 0)
        return false;
    const bool presence_matches = matching_presence_progress(runtime, data);
    size_t offset = 4;
    while (data.size() - offset >= 8) {
        const auto header = data.subspan(offset, 8);
        const size_t size = read_u32(header, 4);
        offset += 8;
        if (size > data.size() - offset || size % 4 != 0)
            return false;
        const auto payload = data.subspan(offset, size);
        if (std::memcmp(header.data(), "DONE", 4) == 0) {
            if (size != 16 || offset + size != data.size())
                return false;
            const auto digest = progress_checksum(data.first(offset));
            return std::equal(digest.begin(), digest.end(), payload.begin());
        }
        if (std::memcmp(header.data(), "MREF", 4) == 0) {
            if (size % 16 != 0)
                return false;
        } else if (std::memcmp(header.data(), "ACHV", 4) == 0) {
            if (size < 20)
                return false;
            if (const auto* entry = matching_trigger(runtime, payload);
                entry && !valid_trigger_progress(*entry->trigger, payload.subspan(20)))
                return false;
        } else if (std::memcmp(header.data(), "CWST", 4) == 0) {
            if (size != 28 || !terminal_state(read_u32(payload, 20)))
                return false;
        } else if (std::memcmp(header.data(), "CWLB", 4) == 0) {
            if (size != 28 || (read_u32(payload, 20) != RC_LBOARD_STATE_DISABLED &&
                               read_u32(payload, 20) != RC_LBOARD_STATE_INACTIVE))
                return false;
        } else if (std::memcmp(header.data(), "CWRC", 4) == 0) {
            if (size != 16)
                return false;
        } else if (std::memcmp(header.data(), "LBRD", 4) == 0) {
            if (size < 20)
                return false;
            if (const auto* entry = matching_lboard(runtime, payload)) {
                ProgressCursor cursor{payload.subspan(20)};
                const auto state = cursor.number();
                if (!state || *state < RC_LBOARD_STATE_WAITING ||
                    *state > RC_LBOARD_STATE_TRIGGERED ||
                    !read_trigger_progress(entry->lboard->start, cursor) ||
                    !read_trigger_progress(entry->lboard->submit, cursor) ||
                    !read_trigger_progress(entry->lboard->cancel, cursor) ||
                    !read_value_progress(entry->lboard->value, cursor) || !cursor.done())
                    return false;
            }
        } else if (std::memcmp(header.data(), "VARS", 4) == 0) {
            if (size < 4 || (presence_matches && !valid_variables_progress(runtime, payload)))
                return false;
        } else if (std::memcmp(header.data(), "RICH", 4) == 0) {
            if (size < 16)
                return false;
            if (presence_matches && runtime.richpresence &&
                std::memcmp(payload.data(), runtime.richpresence->md5, 16) == 0) {
                ProgressCursor cursor{payload.subspan(16)};
                for (auto* display = runtime.richpresence->richpresence->first_display;
                     display && display->next; display = display->next)
                    if (!read_trigger_progress(display->trigger, cursor))
                        return false;
                if (!cursor.done())
                    return false;
            }
        } else if (size >= 0x10000)
            return false;
        offset += size;
    }
    return false;
}

std::vector<uint8_t> native_progress(const rc_runtime_t& runtime, std::span<const uint8_t> data) {
    const bool presence_matches = matching_presence_progress(runtime, data);
    std::vector<uint8_t> filtered(data.begin(), data.begin() + 4);
    for (size_t offset = 4; offset + 8 <= data.size();) {
        const size_t size = read_u32(data, offset + 4);
        const auto* header = data.data() + offset;
        // v12.5 MREF assumes a nonempty destination. VARS cannot skip unknown records safely;
        // a changed RP script starts with fresh variables/displays instead of reading old shapes.
        const bool drop = (std::memcmp(header, "MREF", 4) == 0 &&
                           rc_memrefs_count_memrefs(runtime.memrefs) == 0) ||
                          (!presence_matches && (std::memcmp(header, "VARS", 4) == 0 ||
                                                 std::memcmp(header, "RICH", 4) == 0));
        if (!drop)
            filtered.insert(filtered.end(), data.begin() + offset,
                            data.begin() + offset + 8 + size);
        offset += 8 + size;
    }
    update_progress_checksum(filtered);
    return filtered;
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
    std::unique_ptr<rc_runtime_t, decltype(&destroy_runtime)> fresh(new rc_runtime_t,
                                                                    &destroy_runtime);
    rc_runtime_init(fresh.get());
    std::string error;
    if (!activate_definitions(fresh.get(), error))
        return;
    runtime_ = std::move(fresh);
    for (auto& achievement : achievements_)
        achievement.last_frame.reset();
    for (auto& leaderboard : leaderboards_)
        leaderboard.last_frame.reset();
    rich_presence_output_.clear();
    rich_presence_frame_.reset();
    rich_presence_truncated_ = false;
}

bool LogicTester::activate_definitions(rc_runtime_t* runtime, std::string& error) const {
    for (const auto& achievement : achievements_) {
        const int result = rc_runtime_activate_achievement(runtime, achievement.id,
                                                           achievement.logic.c_str(), nullptr, 0);
        if (result != RC_OK) {
            error = std::string("Could not rebuild achievement: ") + rc_error_str(result);
            return false;
        }
    }
    for (const auto& leaderboard : leaderboards_) {
        const int result = rc_runtime_activate_lboard(runtime, leaderboard.id,
                                                      leaderboard.logic.c_str(), nullptr, 0);
        if (result != RC_OK) {
            error = std::string("Could not rebuild leaderboard: ") + rc_error_str(result);
            return false;
        }
    }
    if (!rich_presence_script_.empty()) {
        const int result =
            rc_runtime_activate_richpresence(runtime, rich_presence_script_.c_str(), nullptr, 0);
        if (result != RC_OK) {
            error = std::string("Could not rebuild Rich Presence: ") + rc_error_str(result);
            return false;
        }
    }
    return true;
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
    for (uint32_t i = 0; i < runtime_->lboard_count; ++i) {
        const auto& entry = runtime_->lboards[i];
        if (!entry.lboard || (entry.lboard->state != RC_LBOARD_STATE_DISABLED &&
                              entry.lboard->state != RC_LBOARD_STATE_INACTIVE))
            continue;
        if (data.size() + 36 + done.size() > kMaxProgressSize) {
            error = "Runtime progress is too large";
            return false;
        }
        data.insert(data.end(), {'C', 'W', 'L', 'B'});
        append_u32(data, 28);
        append_u32(data, entry.id);
        data.insert(data.end(), entry.md5, entry.md5 + 16);
        append_u32(data, entry.lboard->state);
        append_u32(data, static_cast<uint32_t>(entry.value));
    }
    if (runtime_->richpresence && runtime_->richpresence->richpresence) {
        if (data.size() + 24 + done.size() > kMaxProgressSize) {
            error = "Runtime progress is too large";
            return false;
        }
        data.insert(data.end(), {'C', 'W', 'R', 'C'});
        append_u32(data, 16);
        data.insert(data.end(), runtime_->richpresence->md5, runtime_->richpresence->md5 + 16);
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
    if (!activate_definitions(restored.get(), error))
        return false;
    if (!valid_progress(*restored, data)) {
        error = "Invalid or truncated runtime progress";
        return false;
    }
    const auto native_data = native_progress(*restored, data);
    const int result = rc_runtime_deserialize_progress_sized(
        restored.get(), native_data.data(), static_cast<uint32_t>(native_data.size()), nullptr);
    if (result != RC_OK) {
        error = std::string("Could not restore runtime progress: ") + rc_error_str(result);
        return false;
    }
    for (uint32_t i = 0; i < restored->lboard_count; ++i) {
        auto& entry = restored->lboards[i];
        if (entry.lboard)
            entry.value = static_cast<int32_t>(entry.lboard->value.value.value);
    }
    for (size_t offset = 4; offset + 8 <= data.size();) {
        const size_t size = read_u32(data, offset + 4);
        if (std::memcmp(data.data() + offset, "CWST", 4) == 0) {
            const auto payload = data.subspan(offset + 8, size);
            if (const auto* entry = matching_trigger(*restored, payload)) {
                entry->trigger->state = static_cast<uint8_t>(read_u32(payload, 20));
                entry->trigger->measured_value = read_u32(payload, 24);
            }
        } else if (std::memcmp(data.data() + offset, "CWLB", 4) == 0) {
            const auto payload = data.subspan(offset + 8, size);
            if (const auto* entry = matching_lboard(*restored, payload)) {
                entry->lboard->state = static_cast<uint8_t>(read_u32(payload, 20));
                for (uint32_t i = 0; i < restored->lboard_count; ++i)
                    if (restored->lboards[i].id == entry->id)
                        restored->lboards[i].value = static_cast<int32_t>(read_u32(payload, 24));
            }
        }
        offset += 8 + size;
    }
    runtime_ = std::move(restored);
    for (auto& achievement : achievements_)
        achievement.last_frame.reset(); // restored progress has no emulated frame timestamp
    for (auto& leaderboard : leaderboards_)
        leaderboard.last_frame.reset();
    rich_presence_output_.clear();
    rich_presence_frame_.reset();
    rich_presence_truncated_ = false;
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
    for (auto& leaderboard : leaderboards_) {
        const auto* board = rc_runtime_get_lboard(runtime_.get(), leaderboard.id);
        if (board && board->state != RC_LBOARD_STATE_INACTIVE &&
            board->state != RC_LBOARD_STATE_DISABLED)
            leaderboard.last_frame = frame;
    }
    auto events = collecting(frame, [&] {
        rc_runtime_do_frame(runtime_.get(), &collect_event, &peek,
                            const_cast<MemoryReader*>(&memory), nullptr);
    });
    if (!rich_presence_script_.empty()) {
        std::array<char, 16385> buffer{};
        const int length =
            rc_runtime_get_richpresence(runtime_.get(), buffer.data(), buffer.size(), &peek,
                                        const_cast<MemoryReader*>(&memory), nullptr);
        rich_presence_output_ = buffer.data();
        rich_presence_truncated_ = length >= static_cast<int>(buffer.size());
        if (rich_presence_truncated_) {
            size_t end = rich_presence_output_.size();
            // Remove the last character only when truncation split its UTF-8 sequence.
            while (end > 0 && (static_cast<unsigned char>(buffer[end - 1]) & 0xC0) == 0x80)
                --end;
            if (end > 0) {
                const auto lead = static_cast<unsigned char>(buffer[end - 1]);
                const size_t expected = lead >= 0xF0 && lead <= 0xF4   ? 4
                                        : lead >= 0xE0 && lead <= 0xEF ? 3
                                        : lead >= 0xC2 && lead <= 0xDF ? 2
                                                                       : 1;
                if (expected > rich_presence_output_.size() - (end - 1))
                    rich_presence_output_.resize(end - 1);
            }
        }
        rich_presence_frame_ = frame;
    }
    return events;
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

std::optional<uint32_t> LogicTester::add_leaderboard(const LocalLeaderboard& definition,
                                                     std::string& error) {
    const auto logic = leaderboard_definition(definition, error);
    if (!logic)
        return std::nullopt;
    const auto id = next_id_;
    const int result = rc_runtime_activate_lboard(runtime_.get(), id, logic->c_str(), nullptr, 0);
    if (result != RC_OK) {
        error = std::string("Invalid leaderboard: ") + rc_error_str(result);
        return std::nullopt;
    }
    ++next_id_;
    leaderboards_.push_back({.id = id,
                             .definition = definition,
                             .logic = *logic,
                             .format = rc_parse_format(definition.format.c_str()),
                             .last_frame = std::nullopt});
    error.clear();
    return id;
}

bool LogicTester::remove_leaderboard(uint32_t id) {
    const auto found = std::find_if(leaderboards_.begin(), leaderboards_.end(),
                                    [id](const auto& board) { return board.id == id; });
    if (found == leaderboards_.end())
        return false;
    rc_runtime_deactivate_lboard(runtime_.get(), id);
    leaderboards_.erase(found);
    return true;
}

bool LogicTester::rearm_leaderboard(uint32_t id) {
    const auto found = std::find_if(leaderboards_.begin(), leaderboards_.end(),
                                    [id](const auto& board) { return board.id == id; });
    auto* current = rc_runtime_get_lboard(runtime_.get(), id);
    if (found == leaderboards_.end() || !current || current->state == RC_LBOARD_STATE_DISABLED)
        return false;
    if (rc_runtime_activate_lboard(runtime_.get(), id, found->logic.c_str(), nullptr, 0) != RC_OK)
        return false;
    found->last_frame.reset();
    for (uint32_t i = 0; i < runtime_->lboard_count; ++i)
        if (runtime_->lboards[i].id == id)
            runtime_->lboards[i].value = 0;
    return true;
}

std::optional<LeaderboardStatus> LogicTester::leaderboard_snapshot(uint32_t id) const {
    const auto* board = rc_runtime_get_lboard(runtime_.get(), id);
    if (!board)
        return std::nullopt;
    LeaderboardStatus output;
    switch (board->state) {
    case RC_LBOARD_STATE_WAITING:
        output.state = LeaderboardState::Waiting;
        break;
    case RC_LBOARD_STATE_ACTIVE:
        output.state = LeaderboardState::Active;
        break;
    case RC_LBOARD_STATE_STARTED:
        output.state = LeaderboardState::Started;
        break;
    case RC_LBOARD_STATE_CANCELED:
        output.state = LeaderboardState::Canceled;
        break;
    case RC_LBOARD_STATE_TRIGGERED:
        output.state = LeaderboardState::Triggered;
        break;
    case RC_LBOARD_STATE_DISABLED:
        output.state = LeaderboardState::Disabled;
        break;
    default:
        output.state = LeaderboardState::Inactive;
        break;
    }
    for (uint32_t i = 0; i < runtime_->lboard_count; ++i)
        if (runtime_->lboards[i].id == id)
            output.value = runtime_->lboards[i].value;
    // The native cache updates on STARTED/UPDATED, but not on the submit event. The value
    // expression itself has already captured that final frame, including a changed score.
    if (board->state == RC_LBOARD_STATE_TRIGGERED)
        output.value = static_cast<int32_t>(board->value.value.value);
    for (const auto& definition : leaderboards_) {
        if (definition.id != id)
            continue;
        output.last_frame = definition.last_frame;
        std::array<char, 128> formatted{};
        rc_runtime_format_lboard_value(formatted.data(), static_cast<int>(formatted.size()),
                                       output.value, definition.format);
        output.formatted_value = formatted.data();
        break;
    }
    return output;
}

bool LogicTester::set_rich_presence(std::string script, std::string& error) {
    if (!validate_rich_presence(script, error))
        return false;
    script = normalize_rich_presence(script);
    if (script == rich_presence_script_) {
        error.clear();
        return true;
    }
    std::vector<uint8_t> progress;
    if (!serialize_progress(progress, error))
        return false;
    std::vector<std::optional<uint64_t>> achievement_frames, leaderboard_frames;
    for (const auto& achievement : achievements_)
        achievement_frames.push_back(achievement.last_frame);
    for (const auto& leaderboard : leaderboards_)
        leaderboard_frames.push_back(leaderboard.last_frame);
    auto previous = std::move(rich_presence_script_);
    rich_presence_script_ = std::move(script);
    // Rebuilding safely removes the old script's variables and memref graph. Existing
    // achievements/leaderboards retain compatible progress instead of being reset.
    if (!deserialize_progress(progress, error)) {
        rich_presence_script_ = std::move(previous);
        return false;
    }
    for (size_t i = 0; i < achievements_.size(); ++i)
        achievements_[i].last_frame = achievement_frames[i];
    for (size_t i = 0; i < leaderboards_.size(); ++i)
        leaderboards_[i].last_frame = leaderboard_frames[i];
    return true;
}

const char* leaderboard_state_name(LeaderboardState state) {
    switch (state) {
    case LeaderboardState::Inactive:
        return "Inactive";
    case LeaderboardState::Waiting:
        return "Waiting";
    case LeaderboardState::Active:
        return "Active";
    case LeaderboardState::Started:
        return "Started";
    case LeaderboardState::Canceled:
        return "Canceled";
    case LeaderboardState::Triggered:
        return "Completed (offline)";
    case LeaderboardState::Disabled:
        return "Disabled";
    }
    return "?";
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
    case LogicEventKind::LeaderboardStarted:
        return "leaderboard started";
    case LogicEventKind::LeaderboardCanceled:
        return "leaderboard canceled";
    case LogicEventKind::LeaderboardUpdated:
        return "leaderboard value";
    case LogicEventKind::LeaderboardSubmitted:
        return "leaderboard completed (offline)";
    case LogicEventKind::LeaderboardDisabled:
        return "leaderboard disabled";
    }
    return "?";
}

} // namespace cw
