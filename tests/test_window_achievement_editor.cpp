// Characterization tests for the Achievement Editor window (achievement_editor_window.cpp), driven
// through its real widgets. The editing model is unit-tested in test_achievement_editor.cpp and the
// file format in the format corpus; these tests freeze what the window's buttons and keys do.

#include "test_support.hpp"
#include "window_test_support.hpp"

#include "achievement_editor_window.hpp"

#include "cw/devtools/achievement_editor.hpp"
#include "cw/devtools/code_notes.hpp"
#include "cw/devtools/logic_tester.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

constexpr const char* kEditor = "Achievement Editor";
constexpr const char* kConfirm = "Unsaved achievement changes";

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct EditorFixture {
    cw_test::TempDir dir;
    cw::LogicTester tester;
    cw::CodeNotes notes;
    cw::AchievementEditorWindow window{tester, notes};
    std::filesystem::path file = dir.path() / "18238-User.txt";
    // A second window, so a test can move the focus away from the editor.
    bool elsewhere_clicked = false;
    cw_test::UiDriver driver{[this] {
        window.draw(file, "Synthetic game");
        ImGui::SetNextWindowPos(ImVec2(1300.0f, 900.0f));
        ImGui::Begin("Elsewhere");
        if (ImGui::Button("Elsewhere"))
            elsewhere_clicked = true;
        ImGui::End();
    }};

    EditorFixture() {
        window.open = true;
        driver.frame();
        ImGui::SetWindowPos(kEditor, ImVec2(0.0f, 0.0f));
        driver.frame();
    }

    cw::AchievementEditor& editor() { return window.session(); }

    // Opens the suggested file, adds one achievement and saves it.
    void create_saved() {
        driver.click(kEditor, "Current game");
        driver.click(kEditor, "New achievement");
        driver.click(kEditor, "Save");
        REQUIRE(std::filesystem::exists(file));
    }

    void retitle(const char* title) {
        driver.click(kEditor, "Title");
        driver.type(title);
    }
};

} // namespace

TEST_CASE("Achievement Editor opens the current game's file without creating it") {
    EditorFixture app;
    CHECK_FALSE(app.driver.has(kEditor, "New achievement"));

    app.driver.click(kEditor, "Current game");
    CHECK(app.editor().loaded());
    CHECK(app.editor().path() == app.file);
    CHECK(app.editor().document().title == "Synthetic game"); // the suggested title, for a new file
    CHECK(app.driver.has(kEditor, "New achievement"));
    CHECK_FALSE(std::filesystem::exists(app.file));
}

TEST_CASE("Achievement Editor creates, retitles and saves an achievement") {
    EditorFixture app;
    app.driver.click(kEditor, "Current game");
    app.driver.click(kEditor, "New achievement");
    CHECK(app.driver.has(kEditor, "New achievement###asset"));
    CHECK(app.editor().dirty());

    app.retitle("Opening");
    CHECK(app.driver.has(kEditor, "Opening###asset"));
    app.driver.click(kEditor, "Save");
    CHECK_FALSE(app.editor().dirty());
    // A new local achievement has ID 0, one placeholder condition and 5 points.
    CHECK(read_file(app.file) == "cheevo-workbench 0.1.0\nSynthetic game\n"
                                 "0:\"0xH000000=0\":Opening::::::5:::::00000\n");
}

TEST_CASE("Achievement Editor deletes the selected achievement and saves the removal") {
    EditorFixture app;
    app.create_saved();
    app.driver.click(kEditor, "New achievement");
    app.retitle("Second");
    app.driver.click(kEditor, "Save");
    REQUIRE(read_file(app.file).find("Second") != std::string::npos);

    app.driver.click(kEditor, "New achievement###asset");
    app.driver.click(kEditor, "Delete achievement");
    CHECK_FALSE(app.driver.has(kEditor, "New achievement###asset"));
    CHECK(app.driver.has(kEditor, "Second###asset"));
    CHECK(read_file(app.file).find("New achievement") != std::string::npos); // not saved yet

    app.driver.click(kEditor, "Save");
    const std::string saved = read_file(app.file);
    CHECK(saved.find("New achievement") == std::string::npos);
    CHECK(saved.find("Second") != std::string::npos);
}

TEST_CASE("Ctrl+S saves the Achievement Editor only while it has the focus") {
    EditorFixture app;
    app.create_saved();
    app.retitle("Typed");
    app.driver.key(ImGuiKey_S, ImGuiMod_Ctrl); // still typing in the title field
    CHECK_FALSE(app.editor().dirty());
    CHECK(read_file(app.file).find("Typed") != std::string::npos);

    app.retitle("Unfocused");
    app.driver.click("Elsewhere", "Elsewhere");
    REQUIRE(app.elsewhere_clicked);
    app.driver.key(ImGuiKey_S, ImGuiMod_Ctrl);
    CHECK(app.editor().dirty());
    CHECK(read_file(app.file).find("Unfocused") == std::string::npos);
}

TEST_CASE("Achievement Editor asks before reloading over unsaved edits") {
    EditorFixture app;
    app.create_saved();
    app.retitle("Unsaved");

    app.driver.click(kEditor, "Reload");
    REQUIRE(app.driver.has(kConfirm, "Cancel"));
    app.driver.click(kConfirm, "Cancel");
    CHECK_FALSE(app.driver.has(kConfirm, "Cancel"));
    CHECK(app.driver.has(kEditor, "Unsaved###asset")); // the edit survives

    app.driver.click(kEditor, "Reload");
    app.driver.click(kConfirm, "Discard");
    CHECK(app.driver.has(kEditor, "New achievement###asset"));
    CHECK_FALSE(app.editor().dirty());

    app.retitle("Kept");
    app.driver.click(kEditor, "Reload");
    app.driver.click(kConfirm, "Save");
    CHECK_FALSE(app.editor().dirty());
    CHECK(read_file(app.file).find("Kept") != std::string::npos);
}
