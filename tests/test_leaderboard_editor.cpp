#include "cw/devtools/leaderboard_editor.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using cw_test::TempDir;

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Could not open test file for reading: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(contents.data(), static_cast<std::streamsize>(contents.size())))
        throw std::runtime_error("Could not write test file: " + path.string());
}

std::string board_line(std::string_view id, std::string_view start = "0xH000010=1",
                       std::string_view cancel = "0xH000011=1",
                       std::string_view submit = "0xH000012=1",
                       std::string_view value = "0xH000013", std::string_view format = "VALUE",
                       std::string_view title = "Synthetic leaderboard",
                       std::string_view description = "Synthetic record", bool lower = false,
                       std::string_view tail = {}) {
    std::string line = "L" + std::string(id) + ":\"" + std::string(start) + "\":\"" +
                       std::string(cancel) + "\":\"" + std::string(submit) + "\":\"" +
                       std::string(value) + "\":" + std::string(format) + ":\"" +
                       std::string(title) + "\":\"" + std::string(description) +
                       "\":" + (lower ? "1" : "0");
    if (!tail.empty())
        line += ":" + std::string(tail);
    return line + "\r\n";
}

std::string fixture(std::string_view first_value = "0xH000013",
                    std::string_view first_format = "CUSTOM") {
    return "synthetic writer\r\n"
           "Synthetic game title\r\n"
           "opaque future record:keep bytes\r\n"
           "12:\"0xH000001=1\":Achievement title:Preserve "
           "this:::progression:Tester:10:::::00123\r\n" +
           board_line("0", "0xH000010=1", "0xH000011=1", "0xH000012=1", first_value, first_format,
                      "Original board", "Keep metadata", false, "future-field") +
           board_line("0", "0xH000020=1", "0xH000021=1", "0xH000022=1", "0xH000023", "VALUE",
                      "Second board", "Second description", true);
}

const cw::LocalLeaderboard* leaderboard_at(const cw::UserFileDocument& document, size_t index) {
    if (index >= document.entries.size())
        return nullptr;
    return std::get_if<cw::LocalLeaderboard>(&document.entries[index].asset);
}

} // namespace

TEST_CASE("Leaderboard editor edits and saves only the selected record") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    const auto original = fixture();
    write_bytes(file, original);

    cw::LeaderboardEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    CHECK(editor.loaded());
    CHECK_FALSE(editor.dirty());
    REQUIRE(editor.selection() == 2);
    CHECK(editor.draft().title == "Original board");
    CHECK(editor.draft().format == "CUSTOM");
    CHECK(
        editor.can_save()); // An unchanged unknown format retains UserFileStore fallback behavior.

    const uint64_t selection_before = editor.selection_version();
    const uint64_t edit_before = editor.edit_version();
    editor.draft().title = "Edited board";
    editor.draft().description = "Edited description";
    editor.draft().lower_is_better = true;
    editor.changed();
    CHECK(editor.edit_version() == edit_before + 1);
    CHECK(editor.dirty());
    CHECK(editor.can_save());

    REQUIRE(editor.select(3, error));
    CHECK(editor.selection() == 3);
    CHECK(editor.selection_version() == selection_before + 1);
    CHECK(editor.dirty());
    REQUIRE(editor.select(2, error));
    CHECK(editor.draft().title == "Edited board");

    REQUIRE(editor.save(error));
    CHECK_FALSE(editor.dirty());
    CHECK(editor.selection() == 2);

    const std::string saved = read_bytes(file);
    CHECK(saved.find("opaque future record:keep bytes\r\n") != std::string::npos);
    CHECK(saved.find("12:\"0xH000001=1\":Achievement title:Preserve "
                     "this:::progression:Tester:10:::::00123\r\n") != std::string::npos);
    CHECK(saved.find(board_line("0", "0xH000020=1", "0xH000021=1", "0xH000022=1", "0xH000023",
                                "VALUE", "Second board", "Second description", true)) !=
          std::string::npos);
    CHECK(saved.find(":future-field\r\n") != std::string::npos);
    CHECK(saved.find(":CUSTOM:Edited board:Edited description:1:future-field\r\n") !=
          std::string::npos);

    cw::LeaderboardEditor reloaded;
    REQUIRE(reloaded.load(file, error));
    REQUIRE(reloaded.selection() == 2);
    CHECK(reloaded.draft().title == "Edited board");
    CHECK(reloaded.draft().format == "CUSTOM");
    CHECK(reloaded.draft().lower_is_better);
    const auto* zero_id_board = leaderboard_at(reloaded.document(), *reloaded.selection());
    REQUIRE(zero_id_board != nullptr);
    CHECK(zero_id_board->id == 0);
}

TEST_CASE("Adding a leaderboard uses valid defaults and local ID zero") {
    TempDir directory;
    const auto file = directory.path() / "new-user.txt";
    cw::LeaderboardEditor editor;
    std::string error;

    REQUIRE(editor.load(file, error));
    CHECK_FALSE(std::filesystem::exists(file));
    REQUIRE(editor.add_leaderboard(error));
    REQUIRE(editor.selection() == 0);
    CHECK(editor.dirty());
    CHECK(editor.draft().title == "New leaderboard");
    CHECK(editor.draft().start == "0xH000010=1");
    CHECK(editor.draft().cancel == "0xH000011=1");
    CHECK(editor.draft().submit == "0xH000012=1");
    CHECK(editor.draft().value == "0xH000013");
    CHECK(editor.draft().format == "VALUE");
    const auto definition = editor.selected_definition(error);
    REQUIRE(definition.has_value());
    CHECK(definition->id == 0);

    REQUIRE(editor.save(error));
    const auto* saved = leaderboard_at(editor.document(), 0);
    REQUIRE(saved != nullptr);
    CHECK(saved->id == 0);
    CHECK(read_bytes(file).find("L0:") != std::string::npos);
}

TEST_CASE("Invalid raw fields and empty values block save and switching") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    const auto original = fixture("", "CUSTOM");
    write_bytes(file, original);

    cw::LeaderboardEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    CHECK_FALSE(editor.can_save());
    CHECK_FALSE(editor.validation_error().empty());

    editor.draft().value = "0xH000013";
    editor.draft().start = "not an achievement trigger";
    editor.changed();
    CHECK_FALSE(editor.can_save());
    CHECK_FALSE(editor.selected_definition(error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(editor.save(error));
    CHECK(read_bytes(file) == original);
    CHECK_FALSE(editor.select(3, error));
    CHECK(editor.selection() == 2);

    editor.draft().start = "0xH000010=1";
    editor.draft().value.clear();
    editor.changed();
    CHECK_FALSE(editor.can_save());
    CHECK_FALSE(editor.selected_definition(error).has_value());

    editor.draft().value = "0xH000013";
    editor.draft().format = "NOPE";
    editor.changed();
    CHECK_FALSE(editor.can_save());

    editor.draft().format = "FLOAT2";
    editor.changed();
    CHECK(editor.can_save());
    const auto parsed = editor.selected_definition(error);
    REQUIRE(parsed.has_value());
    CHECK(parsed->format == "FLOAT2");
    REQUIRE(editor.save(error));
    CHECK(editor.draft().format == "FLOAT2");
}

TEST_CASE("Deleting explicitly discards an invalid selected draft") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    write_bytes(file, fixture());

    cw::LeaderboardEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    editor.draft().start = "broken";
    editor.changed();
    CHECK_FALSE(editor.can_save());

    REQUIRE(editor.remove_selected(error));
    CHECK(editor.selection() == 2);
    CHECK(editor.draft().title == "Second board");
    CHECK(editor.can_save());
    CHECK(editor.dirty());
    REQUIRE(editor.save(error));
    CHECK(read_bytes(file).find("Original board") == std::string::npos);
    CHECK(read_bytes(file).find("Second board") != std::string::npos);
}

TEST_CASE("Failed loads and external writes leave the current editor session intact") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    const auto invalid_file = directory.path() / "invalid-user.txt";
    const auto original = fixture();
    write_bytes(file, original);
    write_bytes(invalid_file, "bad header\nonly one line\nL1:\n");

    cw::LeaderboardEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    editor.draft().title = "Unsaved title";
    editor.changed();
    CHECK_FALSE(editor.load(invalid_file, error));
    CHECK(editor.path() == std::filesystem::absolute(file));
    CHECK(editor.selection() == 2);
    CHECK(editor.draft().title == "Unsaved title");

    CHECK_FALSE(editor.load(invalid_file, error)); // Dirty sessions cannot be replaced.
    editor.discard_changes();
    CHECK_FALSE(editor.dirty());
    CHECK(editor.draft().title == "Original board");
    CHECK_FALSE(editor.load(invalid_file, error));
    CHECK(editor.path() == std::filesystem::absolute(file));
    CHECK(editor.draft().title == "Original board");

    const std::string external = "external bytes\n";
    write_bytes(file, external);
    editor.draft().title = "New local title";
    editor.changed();
    CHECK_FALSE(editor.save(error));
    CHECK(read_bytes(file) == external);
    CHECK(editor.draft().title == "New local title");
    CHECK(leaderboard_at(editor.document(), 2)->title == "Original board");
}

TEST_CASE("Combined leaderboard definitions enforce full-size and NUL bounds") {
    cw::LocalLeaderboard leaderboard;
    std::string error;
    auto trigger = cw::parse_achievement_logic("0xH000010=1", error);
    REQUIRE(trigger.has_value());
    leaderboard.start = *trigger;
    leaderboard.cancel = *trigger;
    leaderboard.submit = *trigger;
    leaderboard.value = "0xH000013";
    leaderboard.format = "VALUE";

    const auto valid = cw::leaderboard_definition(leaderboard, error);
    REQUIRE(valid.has_value());
    CHECK(valid->find("STA:0xH000010=1::SUB:0xH000010=1::CAN:0xH000010=1::VAL:0xH000013") == 0);

    leaderboard.value = std::string("0xH000013\0ignored", 18);
    CHECK_FALSE(cw::leaderboard_definition(leaderboard, error).has_value());
    CHECK_FALSE(error.empty());

    leaderboard.value = "0xH000013";
    std::string large_logic = "0xH000010=1";
    for (size_t i = 0; i < 6000; ++i)
        large_logic += "_0xH000010=1";
    auto oversized = cw::parse_achievement_logic(large_logic, error);
    REQUIRE(oversized.has_value());
    leaderboard.start = std::move(*oversized);
    CHECK_FALSE(cw::leaderboard_definition(leaderboard, error).has_value());
    CHECK(error.find("65535") != std::string::npos);
}

TEST_CASE("Unchanged incomplete leaderboard records do not block editing other records") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    const auto original = fixture("");
    write_bytes(file, original);
    cw::LeaderboardEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    REQUIRE(editor.selection() == 2);
    CHECK_FALSE(editor.validation_error().empty());

    SUBCASE("Select and edit another leaderboard") {
        REQUIRE_MESSAGE(editor.select(3, error), error);
        editor.draft().title = "Edited second board";
        editor.changed();
    }
    SUBCASE("Add a new valid leaderboard") {
        REQUIRE_MESSAGE(editor.add_leaderboard(error), error);
        editor.draft().title = "New valid board";
        editor.changed();
    }
    REQUIRE_MESSAGE(editor.save(error), error);
    const auto saved = read_bytes(file);
    const auto original_line =
        board_line("0", "0xH000010=1", "0xH000011=1", "0xH000012=1", "", "CUSTOM", "Original board",
                   "Keep metadata", false, "future-field");
    CHECK(saved.find(original_line) != std::string::npos);
}
