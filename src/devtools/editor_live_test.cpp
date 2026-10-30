#include "cw/devtools/editor_live_test.hpp"

#include <algorithm>

namespace cw {

EditorLiveTest::~EditorLiveTest() {
    stop();
}

bool EditorLiveTest::start(const AchievementEditor& editor, uint64_t game_session,
                           const MemoryReader& memory, std::string& error) {
    stop();
    const auto* achievement = editor.selected();
    if (!achievement || !editor.input_error().empty()) {
        error = "Select an achievement and fix invalid inputs before starting live testing.";
        return false;
    }
    auto logic = serialize_achievement_logic(achievement->logic, error);
    if (!logic || logic->empty()) {
        if (error.empty())
            error = "Add a condition before starting live testing.";
        return false;
    }
    auto id = tester_.add("[Editor] " + achievement->title, *logic, error);
    if (!id)
        return false;
    id_ = id;
    path_ = editor.path();
    selection_ = editor.selection();
    logic_ = std::move(*logic);
    game_session_ = game_session;
    selection_version_ = editor.selection_version();
    edit_version_ = editor.edit_version();
    tester_.validate_addresses(memory, 0);
    message_.clear();
    return true;
}

void EditorLiveTest::stop() {
    if (id_)
        tester_.remove(*id_);
    id_.reset();
    message_.clear();
}

void EditorLiveTest::sync(const AchievementEditor& editor, uint64_t game_session,
                          bool game_loaded) {
    if (!id_)
        return;
    std::string reason;
    if (!game_loaded || game_session != game_session_)
        reason = "Live test stopped: the game session changed.";
    else if (path_ != editor.path() || selection_ != editor.selection() ||
             selection_version_ != editor.selection_version())
        reason = "Live test stopped: the selected achievement changed.";
    else if (std::none_of(tester_.achievements().begin(), tester_.achievements().end(),
                          [&](const auto& achievement) { return achievement.id == *id_; }))
        reason = "Live test stopped: its runtime entry was removed.";
    else if (edit_version_ != editor.edit_version()) {
        std::string error;
        const auto* achievement = editor.selected();
        const auto logic =
            achievement ? serialize_achievement_logic(achievement->logic, error) : std::nullopt;
        if (!editor.input_error().empty() || !logic || *logic != logic_)
            reason = "Live test stopped: the logic changed. Start again to test the new draft.";
        edit_version_ = editor.edit_version();
    }
    if (!reason.empty()) {
        stop();
        message_ = std::move(reason);
    }
}

bool EditorLiveTest::rearm() {
    return id_ && tester_.rearm(*id_);
}

std::optional<AchievementStatus> EditorLiveTest::snapshot() const {
    return id_ ? tester_.snapshot(*id_) : std::nullopt;
}

} // namespace cw
