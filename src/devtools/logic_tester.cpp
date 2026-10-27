#include "cw/devtools/logic_tester.hpp"

#include <rc_error.h>
#include <rc_runtime.h>
#include <rc_runtime_types.h>

#include <algorithm>
#include <array>

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
    achievements_.push_back({.id = id, .title = std::move(title), .logic = std::move(logic)});
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
    if (found == achievements_.end()) {
        return false;
    }
    // Activating the same id with the same logic only resets it (rcheevos compares checksums).
    return rc_runtime_activate_achievement(runtime_.get(), id, found->logic.c_str(), nullptr, 0) ==
           RC_OK;
}

void LogicTester::reset() {
    rc_runtime_reset(runtime_.get());
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
