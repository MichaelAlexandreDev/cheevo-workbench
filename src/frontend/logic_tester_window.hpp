#pragma once

#include "cw/devtools/logic_tester.hpp"
#include "ui_widgets.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace cw {

class LibretroCore;

struct LoggedLogicEvent {
    LogicEvent event;
    std::string title;
};

struct AchievementTesterUi {
    bool open = false;
    std::string title;
    std::string logic;
    std::string add_error;
    std::deque<LoggedLogicEvent> log; // newest first, capped
    // The game session the tester was last reset and validated for: a new game (or the same
    // one reloaded) must not inherit hit counts counted against the previous memory.
    std::optional<uint64_t> synced_generation;
};

// Resets and revalidates the tester when `game_session_generation` shows new content was loaded.
void sync_achievement_tester(LogicTester& tester, AchievementTesterUi& state,
                             uint64_t game_session_generation, const LibretroCore& core,
                             Notice& notice);
void record_logic_events(const std::vector<LogicEvent>& events, const LogicTester& tester,
                         AchievementTesterUi& state, Notice& notice);
void draw_achievement_tester(LogicTester& tester, AchievementTesterUi& state,
                             const LibretroCore& core, Notice& notice);

} // namespace cw
