#pragma once

#include "cw/devtools/achievement_editor.hpp"
#include "cw/devtools/logic_tester.hpp"

namespace cw {

// A selected draft registered in the shared offline runtime. Its progress participates in
// save states, and it is evaluated by the host's existing once-per-emulated-frame call.
// The tester must outlive this session. No runtime pointers are retained across restores.
class EditorLiveTest {
  public:
    explicit EditorLiveTest(LogicTester& tester) : tester_(tester) {}
    ~EditorLiveTest();
    EditorLiveTest(const EditorLiveTest&) = delete;
    EditorLiveTest& operator=(const EditorLiveTest&) = delete;

    bool start(const AchievementEditor& editor, uint64_t game_session, const MemoryReader& memory,
               std::string& error);
    void stop();
    // Call before evaluating frames and after editing. Changes require an explicit restart.
    void sync(const AchievementEditor& editor, uint64_t game_session, bool game_loaded);
    bool rearm();
    bool active() const { return id_.has_value(); }
    std::optional<uint32_t> id() const { return id_; }
    std::optional<AchievementStatus> snapshot() const;
    const std::string& message() const { return message_; }

  private:
    LogicTester& tester_;
    std::optional<uint32_t> id_;
    std::filesystem::path path_;
    std::optional<size_t> selection_;
    uint64_t game_session_ = 0;
    uint64_t selection_version_ = 0;
    uint64_t edit_version_ = 0;
    std::string logic_;
    std::string message_;
};

} // namespace cw
