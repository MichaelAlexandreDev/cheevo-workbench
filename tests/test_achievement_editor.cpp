#include "cw/devtools/achievement_editor.hpp"
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

cw::AchievementLogic make_logic(std::string_view text = "0xH000001=1") {
    std::string error;
    auto logic = cw::parse_achievement_logic(text, error);
    if (!logic)
        throw std::runtime_error("Could not parse synthetic achievement logic: " + error);
    return std::move(*logic);
}

cw::LocalAchievement make_achievement(std::string title, uint32_t id = 0) {
    cw::LocalAchievement achievement;
    achievement.id = id;
    achievement.logic = make_logic();
    achievement.title = std::move(title);
    achievement.description = "Synthetic test data";
    achievement.author = "Test author";
    return achievement;
}

cw::LocalLeaderboard make_leaderboard(std::string title, uint32_t id = 0) {
    cw::LocalLeaderboard leaderboard;
    leaderboard.id = id;
    leaderboard.start = make_logic("0xH000010=1");
    leaderboard.cancel = make_logic("0xH000011=1");
    leaderboard.submit = make_logic("0xH000012=1");
    leaderboard.title = std::move(title);
    leaderboard.description = "Synthetic leaderboard";
    return leaderboard;
}

std::string serialize(const cw::UserFileDocument& document) {
    std::string error;
    auto result = cw::serialize_user_file(document, error);
    if (!result)
        throw std::runtime_error("Could not serialize synthetic user file: " + error);
    return std::move(*result);
}

cw::UserFileDocument make_document() {
    cw::UserFileDocument document;
    document.title = "Synthetic game";

    cw::UserFileEntry entry;
    entry.asset = make_achievement("First achievement");
    document.entries.push_back(std::move(entry));
    return document;
}

const cw::LocalAchievement* find_achievement(const cw::UserFileDocument& document,
                                             std::string_view title) {
    for (const auto& entry : document.entries) {
        if (const auto* achievement = std::get_if<cw::LocalAchievement>(&entry.asset);
            achievement && achievement->title == title)
            return achievement;
    }
    return nullptr;
}

} // namespace

TEST_CASE("The editor adds, saves, and reloads an achievement draft") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    cw::AchievementEditor editor;
    std::string error;

    REQUIRE(editor.load(file, error));
    CHECK(editor.loaded());
    CHECK_FALSE(editor.dirty());
    CHECK(editor.document().entries.empty());
    CHECK_FALSE(std::filesystem::exists(file));

    const uint64_t before_add = editor.revision();
    REQUIRE(editor.add_achievement(error));
    REQUIRE(editor.selection().has_value());
    CHECK(editor.revision() == before_add + 1);
    CHECK(editor.dirty());

    auto* draft = editor.selected();
    REQUIRE(draft != nullptr);
    CHECK(draft->id == 0);
    CHECK(draft->title == "New achievement");
    CHECK(draft->points == 5);
    REQUIRE(draft->logic.core.conditions.size() == 1);
    const auto& default_condition = draft->logic.core.conditions.front();
    CHECK(default_condition.left.type == cw::LogicOperandType::Address);
    CHECK(default_condition.left.size == cw::LogicMemorySize::EightBit);
    CHECK(default_condition.left.value == 0);
    CHECK(default_condition.op == cw::LogicOperator::Equal);
    REQUIRE(default_condition.right.has_value());
    CHECK(default_condition.right->type == cw::LogicOperandType::Integer);
    CHECK(default_condition.right->value == 0);

    draft->title = "Edited synthetic achievement";
    draft->description = "A local draft with core and alternative conditions";
    draft->points = 10;
    draft->logic = make_logic("0xH000020=3S0xH000030=4");
    editor.changed();
    CHECK(editor.can_save());
    REQUIRE(editor.save(error));
    CHECK(error.empty());
    CHECK_FALSE(editor.dirty());
    CHECK(editor.selected() != nullptr);
    CHECK(editor.selected()->title == "Edited synthetic achievement");
    CHECK(std::filesystem::exists(file));

    cw::AchievementEditor reloaded;
    REQUIRE(reloaded.load(file, error));
    REQUIRE(reloaded.selection().has_value());
    const auto* saved = reloaded.selected();
    REQUIRE(saved != nullptr);
    CHECK(saved->id == 0);
    CHECK(saved->title == "Edited synthetic achievement");
    CHECK(saved->description == "A local draft with core and alternative conditions");
    CHECK(saved->points == 10);
    REQUIRE(saved->logic.core.conditions.size() == 1);
    CHECK(saved->logic.core.conditions.front().left.value == 0x20);
    REQUIRE(saved->logic.alternatives.size() == 1);
    REQUIRE(saved->logic.alternatives.front().conditions.size() == 1);
    CHECK(saved->logic.alternatives.front().conditions.front().left.value == 0x30);
}

TEST_CASE("Invalid models and rejected text input block saving") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    const auto original = serialize(make_document());
    write_bytes(file, original);

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    REQUIRE(editor.selected() != nullptr);
    editor.selected()->logic.core.conditions.front().flag = static_cast<cw::LogicFlag>(255);
    editor.changed();
    CHECK_FALSE(editor.can_save());
    CHECK_FALSE(editor.validation_error().empty());
    CHECK_FALSE(editor.save(error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(file) == original);

    editor.discard_changes();
    CHECK_FALSE(editor.dirty());
    editor.set_input_error("Points must be a valid number");
    CHECK(editor.dirty());
    CHECK_FALSE(editor.can_save());
    CHECK_FALSE(editor.save(error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(file) == original);

    editor.set_input_error({});
    CHECK(editor.dirty());
    CHECK(editor.can_save());
}

TEST_CASE("Invalid typed models remain in the document while selection changes") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    auto document = make_document();
    cw::UserFileEntry second;
    second.asset = make_achievement("Second achievement");
    document.entries.push_back(std::move(second));
    write_bytes(file, serialize(document));

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    REQUIRE(editor.selection() == 0);
    auto* first = std::get_if<cw::LocalAchievement>(&editor.document().entries[0].asset);
    REQUIRE(first != nullptr);
    first->logic.core.conditions.front().flag = static_cast<cw::LogicFlag>(255);
    editor.changed();

    REQUIRE(editor.select(1, error));
    REQUIRE(editor.selected() != nullptr);
    CHECK(editor.selected()->title == "Second achievement");
    CHECK_FALSE(editor.can_save());
    CHECK(std::get<cw::LocalAchievement>(editor.document().entries[0].asset)
              .logic.core.conditions.front()
              .flag == static_cast<cw::LogicFlag>(255));
}

TEST_CASE(
    "Raw input errors block selection, while explicit removal drops only the selected asset") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    auto document = make_document();
    cw::UserFileEntry board_entry;
    board_entry.asset = make_leaderboard("Keep this leaderboard");
    document.entries.push_back(std::move(board_entry));
    cw::UserFileEntry note_entry;
    // Parse the note-only document so the opaque record is created by the file parser.
    std::string parse_error;
    auto note_document =
        cw::parse_user_file("writer\nSynthetic game\n; preserved note\n", parse_error);
    REQUIRE(note_document.has_value());
    REQUIRE(note_document->entries.size() == 1);
    note_entry = std::move(note_document->entries.front());
    document.entries.push_back(std::move(note_entry));
    cw::UserFileEntry second;
    second.asset = make_achievement("Second achievement");
    document.entries.push_back(std::move(second));
    const auto original = serialize(document);
    write_bytes(file, original);

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    REQUIRE(editor.select(3, error));
    REQUIRE(editor.selected() != nullptr);
    CHECK(editor.selected()->title == "Second achievement");

    editor.set_input_error("Points must be a valid number");
    CHECK_FALSE(editor.select(0, error));
    CHECK_FALSE(editor.save(error));
    CHECK(read_bytes(file) == original);

    const uint64_t before_remove = editor.revision();
    REQUIRE(editor.remove_selected(error));
    CHECK(editor.revision() == before_remove + 1);
    CHECK(editor.input_error().empty());
    REQUIRE(editor.selection() == 0);
    REQUIRE(editor.selected() != nullptr);
    CHECK(editor.selected()->title == "First achievement");

    auto parsed_original = cw::parse_user_file(original, parse_error);
    REQUIRE(parsed_original.has_value());
    auto expected_document = std::move(*parsed_original);
    REQUIRE(expected_document.entries.size() == 4);
    expected_document.entries.erase(expected_document.entries.begin() + 3);
    const auto expected = serialize(expected_document);
    REQUIRE(editor.save(error));
    CHECK(read_bytes(file) == expected);

    cw::AchievementEditor reloaded;
    REQUIRE(reloaded.load(file, error));
    CHECK(reloaded.document().entries.size() == 3);
    CHECK(find_achievement(reloaded.document(), "First achievement") != nullptr);
    CHECK(find_achievement(reloaded.document(), "Second achievement") == nullptr);
    bool leaderboard_preserved = false;
    bool note_preserved = false;
    for (const auto& entry : reloaded.document().entries) {
        if (const auto* leaderboard = std::get_if<cw::LocalLeaderboard>(&entry.asset))
            leaderboard_preserved = leaderboard->title == "Keep this leaderboard";
        if (const auto* raw = std::get_if<cw::PreservedUserLine>(&entry.asset))
            note_preserved = raw->text() == "; preserved note";
    }
    CHECK(leaderboard_preserved);
    CHECK(note_preserved);
}

TEST_CASE("Load refusal, discard, and failed candidate loads preserve the active session") {
    TempDir directory;
    const auto first_file = directory.path() / "first-user.txt";
    const auto second_file = directory.path() / "second-user.txt";
    const auto malformed_file = directory.path() / "malformed-user.txt";
    write_bytes(first_file, serialize(make_document()));
    auto second_document = make_document();
    second_document.title = "Another synthetic game";
    write_bytes(second_file, serialize(second_document));
    write_bytes(malformed_file, "writer\nSynthetic game\n1:\n");

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE(editor.load(first_file, error));
    const auto first_path = editor.path();
    REQUIRE(editor.selected() != nullptr);
    editor.selected()->title = "Unsaved title";
    editor.changed();
    CHECK_FALSE(editor.load(second_file, error));
    CHECK_FALSE(error.empty());
    CHECK(editor.path() == first_path);
    REQUIRE(editor.selected() != nullptr);
    CHECK(editor.selected()->title == "Unsaved title");

    const uint64_t before_discard = editor.revision();
    editor.set_input_error("Invalid numeric text");
    editor.discard_changes();
    CHECK(editor.revision() == before_discard + 1);
    CHECK_FALSE(editor.dirty());
    CHECK(editor.input_error().empty());
    REQUIRE(editor.selected() != nullptr);
    CHECK(editor.selected()->title == "First achievement");

    const uint64_t before_failed_load = editor.revision();
    CHECK_FALSE(editor.load(malformed_file, error));
    CHECK_FALSE(error.empty());
    CHECK(editor.path() == first_path);
    CHECK(editor.revision() == before_failed_load);
    REQUIRE(editor.selected() != nullptr);
    CHECK(editor.selected()->title == "First achievement");

    REQUIRE(editor.load(second_file, error));
    CHECK(editor.document().title == "Another synthetic game");
    REQUIRE(editor.selection() == 0);
}

TEST_CASE("External edits block editor saves without replacing the external file") {
    TempDir directory;
    const auto file = directory.path() / "18238-User.txt";
    write_bytes(file, serialize(make_document()));

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    REQUIRE(editor.selected() != nullptr);
    editor.selected()->title = "Local edit";
    editor.changed();

    const std::string external = "external writer\nExternal game\n";
    write_bytes(file, external);
    CHECK_FALSE(editor.save(error));
    CHECK_FALSE(error.empty());
    CHECK(editor.dirty());
    CHECK(read_bytes(file) == external);
}
