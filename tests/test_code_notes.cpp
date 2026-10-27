#include "cw/devtools/code_notes.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace {

using cw_test::TempDir;

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

} // namespace

TEST_CASE("Code notes derive per-game user and RAIntegration cache paths") {
    const std::filesystem::path data("/xdg/data/cheevo-workbench");
    const std::filesystem::path cache("/home/user/RA/RACache");
    CHECK(cw::CodeNotes::user_file_path(data, 18238) == data / "notes/18238.json");
    CHECK(cw::CodeNotes::server_file_path(cache, 18238) == cache / "Data/18238-Notes.json");
    CHECK(cw::CodeNotes::user_file_path({}, 18238).empty());
    CHECK(cw::CodeNotes::server_file_path(cache, 0).empty());
}

TEST_CASE("Code notes round-trip quotes, slashes, newlines, and UTF-8") {
    TempDir dir;
    const std::string note_text = "line one: \"quoted\" and \\path\nsecond line\n日本語のメモ";
    const auto data_dir = dir.path() / "data";
    cw::CodeNotes notes;
    notes.load(18238, data_dir, {});
    REQUIRE(notes.user_notes().empty());
    std::string error;
    REQUIRE(notes.set_note(0x0D9C24, note_text, error));
    CHECK(error.empty());

    const auto user_file = cw::CodeNotes::user_file_path(data_dir, 18238);
    const std::string saved = read_bytes(user_file);
    CHECK(saved.find("cheevo-workbench-code-notes") != std::string::npos);
    CHECK(saved.find("0x0D9C24") != std::string::npos);

    cw::CodeNotes reloaded;
    reloaded.load(18238, data_dir, {});
    REQUIRE(reloaded.user_notes().size() == 1);
    CHECK(reloaded.user_note_at(0x0D9C24)->note == note_text);
    CHECK(reloaded.user_note_at(0x0D9C24)->author.empty());
}

TEST_CASE("A missing game ID or missing files produces empty note lists") {
    TempDir dir;
    cw::CodeNotes notes;
    notes.load(std::nullopt, dir.path() / "data", dir.path() / "RACache");
    CHECK_FALSE(notes.game_id());
    CHECK(notes.user_notes().empty());
    CHECK(notes.server_notes().empty());
    CHECK(notes.user_error().empty());
    CHECK(notes.server_error().empty());

    notes.load(18238, dir.path() / "data", dir.path() / "RACache");
    CHECK(notes.user_notes().empty());
    CHECK(notes.server_notes().empty());
    CHECK(notes.user_error().empty());
    CHECK(notes.server_error().empty());
}

TEST_CASE("RAIntegration cache reads hexadecimal addresses and never changes the file") {
    TempDir dir;
    const auto cache_dir = dir.path() / "RACache";
    const auto cache_file = cw::CodeNotes::server_file_path(cache_dir, 18238);
    const std::string cache_json = R"([{"Address":"0x001234","User":"alice","Note":"first note"},)"
                                   R"({"Address":"ABCDEF","User":"bob","Note":"second note"},)"
                                   R"({"Address":"0x000010","User":"alice","Note":""},)"
                                   R"({"Address":"0x000011","User":"alice","Note":"''"}])";
    write_bytes(cache_file, cache_json);
    const std::string before = read_bytes(cache_file);

    cw::CodeNotes notes;
    notes.load(18238, dir.path() / "data", cache_dir);
    CHECK(notes.server_error().empty());
    REQUIRE(notes.server_notes().size() == 2);
    CHECK(notes.server_note_at(0x1234)->note == "first note");
    CHECK(notes.server_note_at(0x1234)->author == "alice");
    CHECK(notes.server_note_at(0xABCDEF)->note == "second note");
    CHECK(read_bytes(cache_file) == before);
}

TEST_CASE("Malformed RAIntegration cache is reported and remains byte-identical") {
    TempDir dir;
    const auto cache_file = cw::CodeNotes::server_file_path(dir.path() / "RACache", 18238);
    const std::string malformed = R"([{"Address":"xyz","User":"alice","Note":"bad"}])";
    write_bytes(cache_file, malformed);

    cw::CodeNotes notes;
    notes.load(18238, dir.path() / "data", dir.path() / "RACache");
    CHECK_FALSE(notes.server_error().empty());
    CHECK(notes.server_notes().empty());
    CHECK(read_bytes(cache_file) == malformed);
}

TEST_CASE("Malformed user notes are reported and cannot be overwritten") {
    TempDir dir;
    const auto user_file = cw::CodeNotes::user_file_path(dir.path() / "data", 18238);
    const std::string malformed = "{ this is not valid JSON\n";
    write_bytes(user_file, malformed);

    cw::CodeNotes notes;
    notes.load(18238, dir.path() / "data", {});
    CHECK_FALSE(notes.user_error().empty());
    std::string error;
    CHECK_FALSE(notes.set_note(0x1234, "replacement", error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(user_file) == malformed);
}

TEST_CASE("Publish list contains only new or changed notes") {
    TempDir dir;
    const auto cache_file = cw::CodeNotes::server_file_path(dir.path() / "RACache", 18238);
    write_bytes(cache_file, R"([{"Address":"10","User":"alice","Note":"unchanged"},)"
                            R"({"Address":"20","User":"bob","Note":"server version"},)"
                            R"({"Address":"30","User":"alice","Note":"server only"}])");

    cw::CodeNotes notes;
    notes.load(18238, dir.path() / "data", dir.path() / "RACache");
    std::string error;
    REQUIRE(notes.set_note(0x10, "unchanged", error));
    REQUIRE(notes.set_note(0x20, "workbench version", error));
    REQUIRE(notes.set_note(0x40, "new note", error));
    REQUIRE(notes.set_note(0x50, "removed before publish", error));
    REQUIRE(notes.remove_note(0x50, error));

    const std::vector<cw::CodeNote> to_publish = notes.notes_to_publish();
    REQUIRE(to_publish.size() == 2);
    CHECK(to_publish[0].address == 0x20);
    CHECK(to_publish[0].note == "workbench version");
    CHECK(to_publish[1].address == 0x40);
    CHECK(to_publish[1].note == "new note");
}

TEST_CASE("Hexadecimal addresses accept an optional prefix and reject invalid values") {
    uint32_t address = 0;
    REQUIRE(cw::CodeNotes::parse_hex_address("0x0D9C24", address));
    CHECK(address == 0x0D9C24);
    REQUIRE(cw::CodeNotes::parse_hex_address("0D9C24", address));
    CHECK(address == 0x0D9C24);
    CHECK_FALSE(cw::CodeNotes::parse_hex_address("0x", address));
    CHECK_FALSE(cw::CodeNotes::parse_hex_address("12g", address));
    CHECK_FALSE(cw::CodeNotes::parse_hex_address("100000000", address));
}
