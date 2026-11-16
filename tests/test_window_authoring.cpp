// Characterization tests for the Leaderboard and Rich Presence authoring windows
// (authoring_window.cpp), driven through their real widgets. The editing models are unit-tested in
// test_leaderboard_editor.cpp and test_rich_presence_editor.cpp, the file formats in the format
// corpus, and the quit confirmation in test_authoring_ui.cpp; these tests freeze what the windows'
// buttons and fields do.

#include "test_support.hpp"
#include "window_test_support.hpp"

#include "authoring_window.hpp"

#include "cw/devtools/leaderboard_editor.hpp"
#include "cw/devtools/logic_tester.hpp"
#include "cw/devtools/rich_presence_editor.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

constexpr const char* kBoards = "Leaderboard authoring";
constexpr const char* kPresence = "Rich Presence authoring";

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// The list row of the leaderboard at `index` in the file; new leaderboards have local ID 0.
std::string board_row(const char* title, int index) {
    return std::string("L0  ") + title + "###leaderboard_0_" + std::to_string(index);
}

// One window open at a time, so the other cannot cover its widgets.
struct AuthoringFixture {
    cw_test::TempDir dir;
    cw::LogicTester tester;
    cw::AuthoringWindow window{tester};
    std::filesystem::path rich_file = dir.path() / "18238-Rich.txt";
    std::filesystem::path user_file = dir.path() / "18238-User.txt";
    cw_test::UiDriver driver{[this] { window.draw(rich_file, user_file); }};

    explicit AuthoringFixture(bool leaderboards) {
        window.leaderboards_open = leaderboards;
        window.rich_presence_open = !leaderboards;
        driver.frame();
    }

    cw::LeaderboardEditor& boards() { return window.leaderboard_session(); }
    cw::RichPresenceEditor& presence() { return window.presence_session(); }

    void edit(const char* window_name, const char* field, const char* text) {
        driver.click(window_name, field);
        driver.type(text);
    }
};

} // namespace

TEST_CASE("Leaderboard authoring adds, retitles and saves a leaderboard") {
    AuthoringFixture app(true);
    CHECK_FALSE(app.driver.has(kBoards, "##leaderboard_title"));
    app.driver.click(kBoards, "Current game");
    CHECK(app.boards().loaded());
    CHECK_FALSE(std::filesystem::exists(app.user_file));

    app.driver.click(kBoards, "New leaderboard");
    CHECK(app.driver.has(kBoards, board_row("New leaderboard", 0)));
    app.edit(kBoards, "##leaderboard_title", "Fastest lap");
    CHECK(app.driver.has(kBoards, board_row("Fastest lap", 0)));
    CHECK(app.boards().dirty());

    app.driver.click(kBoards, "Save");
    CHECK_FALSE(app.boards().dirty());
    // A new file has an empty game title; a new leaderboard has placeholder triggers.
    CHECK(read_file(app.user_file) ==
          "cheevo-workbench 0.1.0\n\n"
          "L0:\"0xH000010=1\":\"0xH000011=1\":\"0xH000012=1\":\"0xH000013\":VALUE:"
          "Fastest lap::0\n");
}

TEST_CASE("Leaderboard authoring selects and deletes leaderboards") {
    AuthoringFixture app(true);
    app.driver.click(kBoards, "Current game");
    app.driver.click(kBoards, "New leaderboard");
    app.edit(kBoards, "##leaderboard_title", "First");
    app.driver.click(kBoards, "New leaderboard");
    app.edit(kBoards, "##leaderboard_title", "Second");
    REQUIRE(app.boards().selection() == 1u);

    app.driver.click(kBoards, board_row("First", 0));
    CHECK(app.boards().selection() == 0u);
    CHECK(app.boards().draft().title == "First");

    app.driver.click(kBoards, "Delete selected");
    CHECK_FALSE(app.driver.has(kBoards, board_row("First", 0)));
    CHECK(app.driver.has(kBoards, board_row("Second", 0)));
    app.driver.click(kBoards, "Save");
    const std::string saved = read_file(app.user_file);
    CHECK(saved.find("First") == std::string::npos);
    CHECK(saved.find("Second") != std::string::npos);
}

TEST_CASE("Rich Presence authoring edits and saves a script") {
    AuthoringFixture app(false);
    app.driver.click(kPresence, "Current game");
    REQUIRE(app.presence().loaded());
    app.edit(kPresence, "##presence_script", "Display:\nExploring\n");
    CHECK(app.presence().dirty());

    app.driver.click(kPresence, "Save");
    CHECK_FALSE(app.presence().dirty());
    CHECK(app.window.presence_status() == "Saved local file.");
    CHECK(read_file(app.rich_file) == "Display:\nExploring\n");
}

TEST_CASE("Rich Presence authoring blocks saving an invalid script") {
    AuthoringFixture app(false);
    app.driver.click(kPresence, "Current game");
    app.edit(kPresence, "##presence_script", "Display:\nExploring\n");
    app.driver.click(kPresence, "Save");

    app.edit(kPresence, "##presence_script", "Display:\n@Missing(0xH0000)\n");
    CHECK_FALSE(app.presence().validation_error().empty());
    app.driver.click(kPresence, "Save"); // disabled: nothing happens
    CHECK(app.presence().dirty());
    CHECK(read_file(app.rich_file) == "Display:\nExploring\n");
}
