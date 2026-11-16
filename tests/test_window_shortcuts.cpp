// Characterization tests for the global keyboard shortcuts (main_menu.cpp), pressed as real keys
// through ImGui. main() only calls these handlers while no text field has keyboard focus; that
// rule lives in the main loop and is characterized when main() is split (refactoring step R27).

#include "test_support.hpp"
#include "window_test_support.hpp"

#include "app_state.hpp"
#include "assisted_search_window.hpp"
#include "authoring_window.hpp"
#include "code_notes_window.hpp"
#include "content_dialogs.hpp"
#include "game_view.hpp"
#include "libretro_core.hpp"
#include "logic_tester_window.hpp"
#include "main_menu.hpp"
#include "memory_search_window.hpp"
#include "memory_viewer_window.hpp"
#include "recent_files.hpp"
#include "save_states.hpp"
#include "ui_appearance.hpp"
#include "watch_list_window.hpp"

#include "cw/devtools/logic_tester.hpp"

#include <filesystem>

namespace {

// The emulation and save-state shortcuts around one synthetic game, as main() calls them.
struct ShortcutFixture {
    cw_test::SyntheticContent content;
    cw_test::TempDir dir;
    cw::LibretroCore core;
    cw::SaveStates states{dir.path() / "states"};
    cw::UiState ui;
    cw::Notice notice;
    cw::LogicTester tester;
    cw::AchievementTesterUi tester_ui;
    cw_test::UiDriver driver{[this] {
        cw::handle_emulation_hotkeys(ui, notice, core, tester, tester_ui);
        cw::handle_state_hotkeys(core, states, ui, notice, tester);
    }};

    explicit ShortcutFixture(bool load_game = true) {
        std::string error;
        REQUIRE(core.load(cw_test::test_core_path(), {}, error));
        if (load_game) {
            REQUIRE(core.load_game(content.path(), error));
            core.run_frame();
        }
        driver.frame();
    }
    ~ShortcutFixture() { core.unload(); }
    ShortcutFixture(const ShortcutFixture&) = delete;
    ShortcutFixture& operator=(const ShortcutFixture&) = delete;

    size_t state_files() const {
        const auto folder = dir.path() / "states";
        if (!std::filesystem::exists(folder))
            return 0;
        return static_cast<size_t>(std::distance(std::filesystem::directory_iterator(folder),
                                                 std::filesystem::directory_iterator()));
    }
};

} // namespace

TEST_CASE("Shift+F1 to F4 save a slot and F1 to F4 load it") {
    ShortcutFixture app;
    app.driver.key(ImGuiKey_F1, ImGuiMod_Shift);
    CHECK(app.ui.status_message == "Saved slot 1");
    CHECK_FALSE(app.ui.status_is_error);
    app.driver.key(ImGuiKey_F4, ImGuiMod_Shift);
    CHECK(app.ui.status_message == "Saved slot 4");
    CHECK(app.state_files() == 2);
    CHECK(app.ui.memory_history_generation == 0);

    app.driver.key(ImGuiKey_F1);
    CHECK(app.ui.status_message == "Loaded slot 1");
    CHECK_FALSE(app.ui.status_is_error);
    CHECK(app.ui.memory_history_generation == 1);

    // A failed load still breaks the passive observation history.
    app.driver.key(ImGuiKey_F2);
    CHECK(app.ui.status_message == "Slot 2 is empty");
    CHECK(app.ui.status_is_error);
    CHECK(app.ui.memory_history_generation == 2);
}

TEST_CASE("P pauses and resumes, and N advances one frame only while paused") {
    ShortcutFixture app;
    app.driver.key(ImGuiKey_N);
    CHECK_FALSE(app.ui.session.paused());
    CHECK(app.ui.status_message.empty());

    app.driver.key(ImGuiKey_P);
    CHECK(app.ui.session.paused());
    CHECK(app.ui.status_message == "Paused");

    app.driver.key(ImGuiKey_N);
    CHECK(app.ui.session.frame_advance_pending());
    CHECK(app.ui.status_message == "Advanced one frame");

    app.ui.session.finish_frame_advance();
    app.driver.key(ImGuiKey_P);
    CHECK_FALSE(app.ui.session.paused());
    CHECK(app.ui.status_message == "Resumed");
}

TEST_CASE("Ctrl+R resets the emulation and breaks the observation history") {
    ShortcutFixture app;
    app.driver.key(ImGuiKey_R);
    CHECK(app.ui.status_message.empty());

    app.driver.key(ImGuiKey_R, ImGuiMod_Ctrl);
    CHECK(app.ui.status_message == "Reset emulation");
    CHECK_FALSE(app.ui.status_is_error);
    CHECK(app.ui.memory_history_generation == 1);
}

TEST_CASE("The emulation and save-state shortcuts do nothing without a loaded game") {
    ShortcutFixture app(false);
    app.driver.key(ImGuiKey_P);
    app.driver.key(ImGuiKey_R, ImGuiMod_Ctrl);
    app.driver.key(ImGuiKey_F1, ImGuiMod_Shift);
    app.driver.key(ImGuiKey_F1);
    CHECK_FALSE(app.ui.session.paused());
    CHECK(app.ui.status_message.empty());
    CHECK(app.ui.memory_history_generation == 0);
    CHECK(app.state_files() == 0);
}

TEST_CASE("Ctrl+Q quits from the main menu, even without a loaded game") {
    cw_test::TempDir dir;
    cw::LibretroCore core;
    cw::SaveStates states(dir.path() / "states");
    cw::RecentFiles recent(dir.path() / "recent-files.txt");
    cw::GameView game_view;
    cw::UiState ui;
    cw::Notice notice;
    cw::MemoryViewerState memory_viewer;
    cw::MemorySearchState memory_search;
    cw::WatchListUiState watch_list;
    cw::CodeNotesUiState code_notes;
    cw::AchievementTesterUi tester_ui;
    cw::LogicTester tester;
    cw::AuthoringWindow authoring(tester);
    cw::AssistedSearchWindow assisted_search;
    bool achievement_editor_open = false;
    std::optional<cw::UiAppearance> appearance;
    cw_test::UiDriver driver([&] {
        cw::draw_main_menu(ui, notice, core, states, recent, game_view, cw::dialog_queue(), nullptr,
                           dir.path(), memory_viewer, memory_search, watch_list, code_notes,
                           tester_ui, tester, achievement_editor_open, authoring, assisted_search,
                           *appearance);
    });
    appearance.emplace(dir.path() / "ui-settings.txt", 1.0f, false);

    driver.frame();
    driver.key(ImGuiKey_Q);
    CHECK_FALSE(ui.quit);
    driver.key(ImGuiKey_Q, ImGuiMod_Ctrl);
    CHECK(ui.quit);
}
