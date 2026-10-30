#include "cw/devtools/achievement_editor.hpp"
#include "cw/devtools/code_note_draft.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using cw_test::TempDir;

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Could not read test file: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(contents.data(), static_cast<std::streamsize>(contents.size())))
        throw std::runtime_error("Could not write test file: " + path.string());
}

cw::AchievementLogic parse_logic(std::string_view source) {
    std::string error;
    auto logic = cw::parse_achievement_logic(source, error);
    if (!logic)
        throw std::runtime_error("Could not parse synthetic achievement logic: " + error);
    return std::move(*logic);
}

std::filesystem::path server_notes_path(const std::filesystem::path& cache_dir,
                                        uint32_t game_id = 18238) {
    return cw::CodeNotes::server_file_path(cache_dir, game_id);
}

} // namespace

TEST_CASE("Code note drafts prefer local text and retain the server note author") {
    TempDir directory;
    const auto data_dir = directory.path() / "data";
    const auto cache_dir = directory.path() / "RACache";
    write_bytes(server_notes_path(cache_dir),
                R"([{"Address":"0x000100","User":"alice","Note":"Server version"},)"
                R"({"Address":"0x000200","User":"bob","Note":"Server only"}])");

    cw::CodeNotes notes;
    notes.load(18238, data_dir, cache_dir);
    std::string error;
    REQUIRE(notes.set_note(0x100, "Local version", error));

    cw::CodeNoteDraft draft;
    REQUIRE(draft.begin(notes, 0x100, error));
    CHECK(draft.text() == "Local version");
    REQUIRE(draft.server_note().has_value());
    CHECK(draft.server_note()->note == "Server version");
    CHECK(draft.server_note()->author == "alice");
    draft.cancel();

    REQUIRE(draft.begin(notes, 0x200, error));
    CHECK(draft.text() == "Server only");
    REQUIRE(draft.server_note().has_value());
    CHECK(draft.server_note()->author == "bob");
}

TEST_CASE("Saving a code note draft persists UTF-8 and leaves RA cache files unchanged") {
    TempDir directory;
    const auto data_dir = directory.path() / "data";
    const auto cache_dir = directory.path() / "RACache";
    const auto notes_cache = server_notes_path(cache_dir);
    const auto user_file = cache_dir / "Data" / "18238-User.txt";
    const std::string notes_cache_bytes =
        R"([{"Address":"0x000010","User":"alice","Note":"Old server note"}])";
    const std::string user_file_bytes = "synthetic RAIntegration user file bytes\n";
    write_bytes(notes_cache, notes_cache_bytes);
    write_bytes(user_file, user_file_bytes);

    cw::CodeNotes notes;
    notes.load(18238, data_dir, cache_dir);
    cw::CodeNoteDraft draft;
    std::string error;
    REQUIRE(draft.begin(notes, 0x10, error));
    CHECK(draft.text() == "Old server note");

    const std::string updated_note = "Vida extra: coração\n第二行 — UTF-8";
    draft.text() = updated_note;
    CHECK(draft.dirty());
    REQUIRE(draft.save(notes, error));
    CHECK(error.empty());
    CHECK_FALSE(draft.open());
    REQUIRE(notes.user_note_at(0x10) != nullptr);
    CHECK(notes.user_note_at(0x10)->note == updated_note);
    CHECK(notes.user_note_at(0x10)->author.empty());

    const auto pending = notes.notes_to_publish();
    REQUIRE(pending.size() == 1);
    CHECK(pending.front().address == 0x10);
    CHECK(pending.front().note == updated_note);
    cw::CodeNotes reloaded;
    reloaded.load(18238, data_dir, cache_dir);
    REQUIRE(reloaded.user_note_at(0x10) != nullptr);
    CHECK(reloaded.user_note_at(0x10)->note == updated_note);
    CHECK(read_bytes(notes_cache) == notes_cache_bytes);
    CHECK(read_bytes(user_file) == user_file_bytes);
}

TEST_CASE("An unchanged server note can be adopted locally without becoming publish work") {
    TempDir directory;
    const auto data_dir = directory.path() / "data";
    const auto cache_dir = directory.path() / "RACache";
    write_bytes(server_notes_path(cache_dir),
                R"([{"Address":"0x000020","User":"alice","Note":"Shared text"}])");

    cw::CodeNotes notes;
    notes.load(18238, data_dir, cache_dir);
    cw::CodeNoteDraft draft;
    std::string error;
    REQUIRE(draft.begin(notes, 0x20, error));
    CHECK(draft.text() == "Shared text");
    CHECK_FALSE(draft.dirty());
    REQUIRE(draft.save(notes, error));

    REQUIRE(notes.user_note_at(0x20) != nullptr);
    CHECK(notes.user_note_at(0x20)->note == "Shared text");
    CHECK(notes.notes_to_publish().empty());
}

TEST_CASE("Code note draft conflicts are limited to the edited address") {
    TempDir directory;
    cw::CodeNotes notes;
    notes.load(18238, directory.path() / "data", directory.path() / "RACache");
    std::string error;
    REQUIRE(notes.set_note(0x100, "Original", error));

    cw::CodeNoteDraft draft;
    REQUIRE(draft.begin(notes, 0x100, error));
    draft.text() = "Draft update";
    REQUIRE(notes.set_note(0x200, "Unrelated edit", error));
    REQUIRE(draft.save(notes, error));
    REQUIRE(notes.user_note_at(0x100) != nullptr);
    CHECK(notes.user_note_at(0x100)->note == "Draft update");

    REQUIRE(draft.begin(notes, 0x100, error));
    draft.text() = "Stale edit";
    REQUIRE(notes.set_note(0x100, "Concurrent edit", error));
    CHECK_FALSE(draft.save(notes, error));
    CHECK_FALSE(error.empty());
    CHECK(draft.open());
    CHECK(draft.dirty());
    CHECK(draft.text() == "Stale edit");
    REQUIRE(notes.user_note_at(0x100) != nullptr);
    CHECK(notes.user_note_at(0x100)->note == "Concurrent edit");

    draft.cancel();
    REQUIRE(draft.begin(notes, 0x100, error));
    draft.text() = "Draft after removal";
    REQUIRE(notes.remove_note(0x100, error));
    CHECK_FALSE(draft.save(notes, error));
    CHECK(draft.open());
    CHECK(draft.text() == "Draft after removal");
    CHECK(notes.user_note_at(0x100) == nullptr);
}

TEST_CASE("Code note drafts are bound to the game and notes storage session") {
    TempDir directory;
    const auto data_a = directory.path() / "data-a";
    const auto data_b = directory.path() / "data-b";
    const auto cache_dir = directory.path() / "RACache";
    std::string error;

    cw::CodeNotes notes;
    notes.load(18238, data_a, cache_dir);
    cw::CodeNoteDraft changed_game;
    REQUIRE(changed_game.begin(notes, 0x10, error));
    changed_game.text() = "Draft for original game";
    notes.load(123, data_a, cache_dir);
    CHECK_FALSE(changed_game.save(notes, error));
    CHECK(changed_game.open());
    CHECK(changed_game.text() == "Draft for original game");
    CHECK_FALSE(std::filesystem::exists(cw::CodeNotes::user_file_path(data_a, 18238)));

    notes.load(18238, data_a, cache_dir);
    cw::CodeNoteDraft changed_path;
    REQUIRE(changed_path.begin(notes, 0x20, error));
    changed_path.text() = "Draft for original path";
    notes.load(18238, data_b, cache_dir);
    CHECK_FALSE(changed_path.save(notes, error));
    CHECK(changed_path.open());
    CHECK(changed_path.text() == "Draft for original path");
    CHECK_FALSE(std::filesystem::exists(cw::CodeNotes::user_file_path(data_a, 18238)));
    CHECK_FALSE(std::filesystem::exists(cw::CodeNotes::user_file_path(data_b, 18238)));

    notes.load(18238, data_a, cache_dir);
    cw::CodeNoteDraft reloaded_session;
    REQUIRE(reloaded_session.begin(notes, 0x30, error));
    reloaded_session.text() = "Draft before reload";
    notes.load(18238, data_a, cache_dir);
    CHECK_FALSE(reloaded_session.save(notes, error));
    CHECK(reloaded_session.open());
    CHECK(reloaded_session.text() == "Draft before reload");
    CHECK_FALSE(std::filesystem::exists(cw::CodeNotes::user_file_path(data_a, 18238)));
}

TEST_CASE("Missing game IDs and invalid draft saves preserve editor state and files") {
    TempDir directory;
    const auto data_dir = directory.path() / "data";
    const auto cache_dir = directory.path() / "RACache";
    std::string error;

    cw::CodeNotes no_game;
    no_game.load(std::nullopt, data_dir, cache_dir);
    cw::CodeNoteDraft draft;
    CHECK_FALSE(draft.begin(no_game, 0x10, error));
    CHECK_FALSE(draft.open());
    CHECK_FALSE(error.empty());

    cw::CodeNotes notes;
    notes.load(18238, data_dir, cache_dir);
    REQUIRE(draft.begin(notes, 0x10, error));
    draft.text() = "Unsaved draft";
    CHECK_FALSE(draft.begin(notes, 0x20, error));
    CHECK(draft.open());
    CHECK(draft.address() == 0x10);
    CHECK(draft.text() == "Unsaved draft");
    CHECK(draft.dirty());
    draft.cancel();
    CHECK_FALSE(std::filesystem::exists(cw::CodeNotes::user_file_path(data_dir, 18238)));

    REQUIRE(draft.begin(notes, 0x10, error));
    draft.text().clear();
    CHECK_FALSE(draft.save(notes, error));
    CHECK(draft.open());
    CHECK(draft.text().empty());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(std::filesystem::exists(cw::CodeNotes::user_file_path(data_dir, 18238)));
}

TEST_CASE("Malformed user notes and write failures preserve the code note draft") {
    TempDir directory;
    const auto data_dir = directory.path() / "data";
    const auto user_file = cw::CodeNotes::user_file_path(data_dir, 18238);
    const std::string malformed = "{ not valid JSON\n";
    write_bytes(user_file, malformed);

    cw::CodeNotes malformed_notes;
    malformed_notes.load(18238, data_dir, directory.path() / "RACache");
    CHECK_FALSE(malformed_notes.user_error().empty());
    cw::CodeNoteDraft malformed_draft;
    std::string error;
    REQUIRE(malformed_draft.begin(malformed_notes, 0x10, error));
    malformed_draft.text() = "Keep this draft";
    CHECK_FALSE(malformed_draft.save(malformed_notes, error));
    CHECK(malformed_draft.open());
    CHECK(malformed_draft.text() == "Keep this draft");
    CHECK(read_bytes(user_file) == malformed);

    const auto blocked_data_dir = directory.path() / "blocked-data";
    cw::CodeNotes blocked_notes;
    blocked_notes.load(18238, blocked_data_dir, directory.path() / "RACache");
    cw::CodeNoteDraft blocked_draft;
    REQUIRE(blocked_draft.begin(blocked_notes, 0x20, error));
    blocked_draft.text() = "Keep this draft too";
    const auto notes_directory = blocked_data_dir / "notes";
    write_bytes(notes_directory, "not a directory");
    const std::string blocker_bytes = read_bytes(notes_directory);

    CHECK_FALSE(blocked_draft.save(blocked_notes, error));
    CHECK_FALSE(error.empty());
    CHECK(blocked_draft.open());
    CHECK(blocked_draft.text() == "Keep this draft too");
    CHECK(read_bytes(notes_directory) == blocker_bytes);
    CHECK(blocked_notes.user_note_at(0x20) == nullptr);
}

TEST_CASE("The achievement editor resolves direct addresses for all memory operand modifiers") {
    const auto logic = parse_logic("0xH0010=0xH0020_d0xH0011=p0xH0021_b0xH0012=~0xH0022_"
                                   "~0xH0013=b0xH0023_p0xH0014=0xH0024");
    const auto& group = logic.core;
    REQUIRE(group.conditions.size() == 5);

    const uint32_t left_addresses[] = {0x10, 0x11, 0x12, 0x13, 0x14};
    const uint32_t right_addresses[] = {0x20, 0x21, 0x22, 0x23, 0x24};
    for (size_t row = 0; row < group.conditions.size(); ++row) {
        CAPTURE(row);
        const auto left = cw::achievement_note_address(group, row, false);
        const auto right = cw::achievement_note_address(group, row, true);
        REQUIRE(left.has_value());
        REQUIRE(right.has_value());
        CHECK(*left == left_addresses[row]);
        CHECK(*right == right_addresses[row]);
    }
}

TEST_CASE("The achievement editor ignores AddAddress offsets and resets note lookup per group") {
    const auto logic = parse_logic("I:0xH0001+2_I:0xH0003+4_0xH0005=6_0xH0007=8S0xH0010=1");
    const auto& core = logic.core;
    REQUIRE(core.conditions.size() == 4);

    const auto root = cw::achievement_note_address(core, 0, false);
    REQUIRE(root.has_value());
    CHECK(*root == 0x1);
    CHECK_FALSE(cw::achievement_note_address(core, 0, true).has_value());
    CHECK_FALSE(cw::achievement_note_address(core, 1, false).has_value());
    CHECK_FALSE(cw::achievement_note_address(core, 2, false).has_value());
    CHECK_FALSE(cw::achievement_note_address(core, 2, true).has_value());

    const auto reset_row = cw::achievement_note_address(core, 3, false);
    REQUIRE(reset_row.has_value());
    CHECK(*reset_row == 0x7);

    REQUIRE(logic.alternatives.size() == 1);
    const auto& alternative = logic.alternatives.front();
    REQUIRE(alternative.conditions.size() == 1);
    const auto alternative_address = cw::achievement_note_address(alternative, 0, false);
    REQUIRE(alternative_address.has_value());
    CHECK(*alternative_address == 0x10);
}

TEST_CASE("Constants and unsupported operands do not produce code note addresses") {
    const auto constants = parse_logic("10=0xH0010_0xH0011=20");
    REQUIRE(constants.core.conditions.size() == 2);
    CHECK_FALSE(cw::achievement_note_address(constants.core, 0, false).has_value());
    const auto first_right = cw::achievement_note_address(constants.core, 0, true);
    REQUIRE(first_right.has_value());
    CHECK(*first_right == 0x10);
    const auto second_left = cw::achievement_note_address(constants.core, 1, false);
    REQUIRE(second_left.has_value());
    CHECK(*second_left == 0x11);
    CHECK_FALSE(cw::achievement_note_address(constants.core, 1, true).has_value());

    cw::LogicGroup unsupported;
    for (const auto type : {cw::LogicOperandType::Integer, cw::LogicOperandType::Float,
                            cw::LogicOperandType::Recall, cw::LogicOperandType::Function}) {
        cw::LogicCondition condition;
        condition.left.type = type;
        condition.left.value = 0x30;
        condition.right.emplace();
        condition.right->type = type;
        condition.right->value = 0x40;
        unsupported.conditions.push_back(condition);
    }
    for (size_t row = 0; row < unsupported.conditions.size(); ++row) {
        CAPTURE(row);
        CHECK_FALSE(cw::achievement_note_address(unsupported, row, false).has_value());
        CHECK_FALSE(cw::achievement_note_address(unsupported, row, true).has_value());
    }
}

TEST_CASE("Code note lookups are exact and do not infer note subranges") {
    TempDir directory;
    cw::CodeNotes notes;
    notes.load(18238, directory.path() / "data", directory.path() / "RACache");
    std::string error;
    REQUIRE(notes.set_note(0x100, "[8 bytes] Synthetic note", error));

    const auto logic = parse_logic("0xH0101=1");
    const auto address = cw::achievement_note_address(logic.core, 0, false);
    REQUIRE(address.has_value());
    CHECK(*address == 0x101);
    CHECK(notes.display_note_at(0x100) != nullptr);
    CHECK(notes.display_note_at(*address) == nullptr);
}

TEST_CASE("A note draft cannot be saved through a different notes service") {
    TempDir directory;
    cw::CodeNotes original;
    cw::CodeNotes other;
    original.load(18238, directory.path() / "original", {});
    other.load(18238, directory.path() / "other", {});
    REQUIRE(original.session_version() == other.session_version());
    cw::CodeNoteDraft draft;
    std::string error;
    REQUIRE(draft.begin(original, 0x10, error));
    draft.text() = "Belongs to original";
    CHECK_FALSE(draft.save(other, error));
    CHECK(draft.open());
    CHECK(draft.text() == "Belongs to original");
    CHECK_FALSE(
        std::filesystem::exists(cw::CodeNotes::user_file_path(directory.path() / "other", 18238)));
}
