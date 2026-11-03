#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cw {
class LeaderboardEditor;
struct LogicEvent;
class LogicTester;
class MemoryReader;
class RichPresenceEditor;

// Local Rich Presence and leaderboard authoring windows. Runtime previews use the shared offline
// LogicTester; file sessions remain independent from the currently loaded game.
class AuthoringWindow {
  public:
    explicit AuthoringWindow(LogicTester& tester);
    ~AuthoringWindow();
    AuthoringWindow(const AuthoringWindow&) = delete;
    AuthoringWindow& operator=(const AuthoringWindow&) = delete;

    bool rich_presence_open = false;
    bool leaderboards_open = false;

    void draw(const std::filesystem::path& suggested_rich_file,
              const std::filesystem::path& suggested_user_file);
    // Call before emulated frames and before drawing, including while both windows are hidden.
    void sync_live(uint64_t game_session, bool game_loaded, const MemoryReader& memory,
                   uint64_t frame = 0);
    // Call after each LogicTester::do_frame(). Only events for this window's active leaderboard
    // test are retained.
    void record_events(const std::vector<LogicEvent>& events);

    bool allow_quit();
    bool take_quit_request();
    RichPresenceEditor& presence_session();
    const std::string& presence_status() const;
    LeaderboardEditor& leaderboard_session();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cw
