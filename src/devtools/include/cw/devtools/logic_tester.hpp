#pragma once

#include "cw/devtools/memory.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct rc_runtime_t;

namespace cw {

// What happened to an achievement during one emulated frame (rcheevos' RC_RUNTIME_EVENT_*).
enum class LogicEventKind {
    Activated, // left WAITING/PAUSED/PRIMED: from now on it can trigger
    Paused,    // a PauseIf condition is true
    Reset,     // a ResetIf condition cleared the hit counts
    Primed,    // every condition except the Trigger ones is true
    Unprimed,
    Triggered, // the achievement fired
    Disabled,  // it reads an address the memory cannot provide (value = that address)
    Progress,  // its measured value changed
};

struct LogicEvent {
    uint32_t id = 0;
    LogicEventKind kind = LogicEventKind::Activated;
    int32_t value = 0;  // the address for Disabled, the measured value for Progress
    uint64_t frame = 0; // emulated frame in which it happened
};

// The state of an achievement under test (rcheevos' RC_TRIGGER_STATE_*).
enum class LogicState { Inactive, Waiting, Active, Paused, Reset, Triggered, Primed, Disabled };

struct TestedAchievement {
    uint32_t id = 0;
    std::string title;
    std::string logic;
};

// Tests achievement logic against live memory with rcheevos' rc_runtime, entirely offline:
// definitions are typed in (no -User.txt yet, step 4.2), nothing is unlocked or submitted.
//
// Call do_frame() once per *emulated* frame, after the core ran it — the same rhythm in which
// RetroAchievements evaluates achievements — so frame advance evaluates exactly one frame.
class LogicTester {
  public:
    LogicTester();
    ~LogicTester();
    LogicTester(const LogicTester&) = delete;
    LogicTester& operator=(const LogicTester&) = delete;

    // Parses and activates `logic` (e.g. "0xH0010=5_0xH0011>3"). Returns the new id, or nullopt
    // with rcheevos' description of the problem in `error`.
    std::optional<uint32_t> add(std::string title, std::string logic, std::string& error);
    bool remove(uint32_t id);
    // Puts a triggered achievement back to Waiting, hit counts cleared. Refused for a Disabled
    // one: its address is unreadable, and only a memory that has it (after reset()) fixes that.
    bool rearm(uint32_t id);
    // Every achievement back to Waiting with cleared hit counts and re-enabled, e.g. when a game
    // is (re)loaded: nothing counted or disabled against the previous memory carries over. Call
    // validate_addresses() with the new memory afterwards.
    void reset();

    // Disables achievements that read addresses `memory` cannot provide and reports them as
    // Disabled events. Call after adding achievements and whenever the memory map changes.
    std::vector<LogicEvent> validate_addresses(const MemoryReader& memory, uint64_t frame);

    // Evaluates one emulated frame against `memory`; returns that frame's events.
    std::vector<LogicEvent> do_frame(const MemoryReader& memory, uint64_t frame);

    const std::vector<TestedAchievement>& achievements() const { return achievements_; }
    LogicState state(uint32_t id) const;
    // Current and target value of a measured achievement ("M:" condition), if it has one.
    std::optional<std::pair<uint32_t, uint32_t>> measured(uint32_t id) const;

  private:
    std::unique_ptr<rc_runtime_t, void (*)(rc_runtime_t*)> runtime_;
    std::vector<TestedAchievement> achievements_;
    uint32_t next_id_ = 1;
};

const char* logic_state_name(LogicState state);
const char* logic_event_name(LogicEventKind kind);

} // namespace cw
