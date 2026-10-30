#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace cw {
class AchievementEditor;
class LogicTester;
class MemoryReader;
class CodeNotes;

// Local achievement authoring window. Its file session is independent of the running game.
class AchievementEditorWindow {
  public:
    AchievementEditorWindow(LogicTester& tester, CodeNotes& notes);
    ~AchievementEditorWindow();
    bool open = false;
    void draw(const std::filesystem::path& suggested_file, const std::string& suggested_title);
    bool allow_quit();
    bool take_quit_request();
    AchievementEditor& session();
    // Synchronize before running emulated frames, including while this window is hidden.
    void sync_live(uint64_t game_session, bool game_loaded, const MemoryReader& memory);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cw
