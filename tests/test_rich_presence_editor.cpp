#include "cw/devtools/rich_presence_editor.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using cw_test::TempDir;

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open test file for reading: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(contents.data(), static_cast<std::streamsize>(contents.size()))) {
        throw std::runtime_error("Could not write test file: " + path.string());
    }
}

void check_no_temporary_files(const std::filesystem::path& directory,
                              const std::filesystem::path& destination) {
    const std::string prefix = "." + destination.filename().string() + ".tmp.";
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        CHECK(entry.path().filename().string().rfind(prefix, 0) != 0);
    }
}

constexpr std::string_view kValidScript = "Display:\nHello, world!\n";

} // namespace

TEST_CASE("Rich Presence normalization matches RAIntegration line and BOM behavior") {
    const std::string with_bom = "\xEF\xBB\xBF"
                                 "Display:\r\nHello\rWorld";
    CHECK(cw::normalize_rich_presence(with_bom) == "Display:\nHelloWorld\n");
    CHECK(cw::normalize_rich_presence("\r\n \t") == "");
    CHECK(cw::normalize_rich_presence("Display:\n"
                                      "Olá, mundo") == "Display:\nOlá, mundo\n");
    CHECK(cw::normalize_rich_presence("Display:\nDone\n") == "Display:\nDone\n");
}

TEST_CASE("Rich Presence validation accepts empty and Unicode scripts and reports parser lines") {
    std::string error = "stale";
    CHECK(cw::validate_rich_presence("", error));
    CHECK(error.empty());
    CHECK(cw::validate_rich_presence("\xEF\xBB\xBF"
                                     "Display:\r\nOlá, mundo",
                                     error));
    CHECK(error.empty());

    const std::string invalid = "Display:\n?I:0x0x0000=1?Bad\nDefault";
    CHECK_FALSE(cw::validate_rich_presence(invalid, error));
    CHECK(error.find("Parse error -2 (line 2):") != std::string::npos);
    CHECK(error.find("Invalid memory operand") != std::string::npos);
}

TEST_CASE("Rich Presence validation preserves built-in macros, Format definitions, and Lookups") {
    const std::string script = "Format:Points\n"
                               "FormatType=VALUE\n"
                               "\n"
                               "Lookup:Place\n"
                               "0=Zero\n"
                               "1=One\n"
                               "\n"
                               "Display:\n"
                               "@Points(0xH01) @Place(0xH01) @Number(0xH01)";
    std::string error;

    CHECK(cw::validate_rich_presence(script, error));
    CHECK(error.empty());
}

TEST_CASE("Rich Presence validation rejects unknown macros before runtime rendering") {
    std::string error;
    CHECK_FALSE(cw::validate_rich_presence("Display:\n@Unknown(0xH01)", error));
    CHECK(error.find("Unknown Rich Presence macro '@Unknown'") != std::string::npos);

    const std::string long_macro(4096, 'M');
    CHECK_FALSE(cw::validate_rich_presence("Display:\n@" + long_macro + "(0xH01)", error));
    CHECK(error.find("Unknown Rich Presence macro '@" + long_macro.substr(0, 80) + "...'") !=
          std::string::npos);
    CHECK(error.size() < 128);
}

TEST_CASE("Rich Presence validation rejects embedded NUL and scripts over the byte limit") {
    std::string error;
    const std::string with_nul("Display:\nHello\0there", 19);
    CHECK_FALSE(cw::validate_rich_presence(with_nul, error));
    CHECK(error.find("NUL") != std::string::npos);

    const std::string oversized(cw::kMaxRichPresenceSize + 1, 'x');
    CHECK_FALSE(cw::validate_rich_presence(oversized, error));
    CHECK(error.find("65,535") != std::string::npos);
}

TEST_CASE("Rich Presence load is read-only and save writes normalized UTF-8 text atomically") {
    TempDir dir;
    const auto file = dir.path() / "Data" / "18238-Rich.txt";
    const std::string original = "\xEF\xBB\xBF"
                                 "Display:\r\nOlá, mundo";
    std::filesystem::create_directories(file.parent_path());
    write_bytes(file, original);

    cw::RichPresenceEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    CHECK(error.empty());
    CHECK(editor.loaded());
    CHECK_FALSE(editor.dirty());
    CHECK_FALSE(editor.can_save());
    CHECK(editor.text() == "Display:\nOlá, mundo\n");
    CHECK(read_bytes(file) == original);

    editor.set_text("Display:\r\nOlá, arquivo");
    CHECK(editor.dirty());
    CHECK(editor.can_save());
    REQUIRE(editor.save(error));
    CHECK(error.empty());
    CHECK(read_bytes(file) == "Display:\nOlá, arquivo\n");
    CHECK(editor.text() == "Display:\nOlá, arquivo\n");
    CHECK_FALSE(editor.dirty());
    CHECK_FALSE(editor.can_save());
    check_no_temporary_files(file.parent_path(), file);
}

TEST_CASE("Rich Presence load accepts CRLF and BOM overhead up to the normalized script limit") {
    TempDir dir;
    const auto file = dir.path() / "18238-Rich.txt";
    std::string original = "\xEF\xBB\xBF"
                           "Display:\r\n";
    original.reserve(2 * cw::kMaxRichPresenceSize + 3);
    for (size_t i = 0; i < cw::kMaxRichPresenceSize - 10; ++i) {
        original.append("\rx");
    }
    REQUIRE(original.size() > cw::kMaxRichPresenceSize);
    write_bytes(file, original);

    cw::RichPresenceEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    CHECK(editor.text().size() == cw::kMaxRichPresenceSize);
    CHECK(editor.validation_error().empty());
    CHECK_FALSE(editor.dirty());
    CHECK(read_bytes(file) == original);
}

TEST_CASE("A missing Rich Presence file is created only by explicit save") {
    TempDir dir;
    const auto parent = dir.path() / "new" / "nested";
    const auto file = parent / "18238-Rich.txt";

    cw::RichPresenceEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    CHECK(editor.loaded());
    CHECK_FALSE(std::filesystem::exists(parent));
    CHECK(editor.can_save());

    REQUIRE(editor.save(error));
    CHECK(std::filesystem::exists(file));
    CHECK(read_bytes(file).empty());
    CHECK_FALSE(editor.dirty());
    CHECK_FALSE(editor.can_save());
    check_no_temporary_files(parent, file);
}

TEST_CASE("Malformed Rich Presence stays editable and can be discarded or corrected") {
    TempDir dir;
    const auto file = dir.path() / "18238-Rich.txt";
    const std::string original = "Display:Not Fixed";
    write_bytes(file, original);

    cw::RichPresenceEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    CHECK(error.empty());
    CHECK(editor.text() == "Display:Not Fixed\n");
    CHECK_FALSE(editor.validation_error().empty());
    CHECK(editor.validation_error().find("line 2") != std::string::npos);
    CHECK_FALSE(editor.can_save());
    CHECK(read_bytes(file) == original);

    const uint64_t loaded_revision = editor.revision();
    editor.set_text(std::string(kValidScript));
    CHECK(editor.revision() > loaded_revision);
    CHECK(editor.dirty());
    CHECK(editor.validation_error().empty());
    CHECK(editor.can_save());

    const auto draft = editor.text();
    const auto draft_revision = editor.revision();
    CHECK_FALSE(editor.load(dir.path() / "another.txt", error));
    CHECK(error.find("unsaved changes") != std::string::npos);
    CHECK(editor.text() == draft);
    CHECK(editor.revision() == draft_revision);
    CHECK(editor.path() == file);

    editor.discard_changes();
    CHECK(editor.revision() > draft_revision);
    CHECK_FALSE(editor.dirty());
    CHECK(editor.text() == "Display:Not Fixed\n");
    CHECK_FALSE(editor.validation_error().empty());
    CHECK_FALSE(editor.can_save());
    CHECK(read_bytes(file) == original);

    editor.set_text(std::string(kValidScript));
    REQUIRE(editor.can_save());
    REQUIRE(editor.save(error));
    CHECK(read_bytes(file) == kValidScript);
    CHECK(editor.validation_error().empty());
}

TEST_CASE("Invalid Rich Presence drafts are preserved when validation blocks saving") {
    TempDir dir;
    const auto file = dir.path() / "18238-Rich.txt";
    write_bytes(file, kValidScript);

    cw::RichPresenceEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    const std::string invalid = "Display:\n?I:0x0x0000=1?Bad\nDefault";
    editor.set_text(invalid);

    CHECK_FALSE(editor.can_save());
    CHECK_FALSE(editor.save(error));
    CHECK(error.find("line 2") != std::string::npos);
    CHECK(editor.text() == invalid);
    CHECK(editor.dirty());
    CHECK(read_bytes(file) == kValidScript);

    editor.set_text("Display:\nCorrected draft");
    CHECK(editor.can_save());
    CHECK(editor.validation_error().empty());
}

TEST_CASE("Rich Presence save refuses external edits and keeps the draft intact") {
    TempDir dir;
    const auto file = dir.path() / "18238-Rich.txt";
    write_bytes(file, kValidScript);

    cw::RichPresenceEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    const std::string draft = "Display:\nLocal draft";
    editor.set_text(draft);
    write_bytes(file, "Display:\nExternal edit\n");

    CHECK_FALSE(editor.save(error));
    CHECK(error.find("changed after it was loaded") != std::string::npos);
    CHECK(editor.text() == draft);
    CHECK(editor.dirty());
    CHECK(read_bytes(file) == "Display:\nExternal edit\n");
    check_no_temporary_files(dir.path(), file);
}

TEST_CASE("Rich Presence path helper uses the active game Data filename") {
    CHECK(cw::ra_rich_presence_path("/cache/RACache", 18238) ==
          std::filesystem::path("/cache/RACache/Data/18238-Rich.txt"));
    CHECK(cw::ra_rich_presence_path("", 18238).empty());
    CHECK(cw::ra_rich_presence_path("/cache/RACache", 0).empty());
}

TEST_CASE("Rich Presence failed loads preserve the current clean editor and refuse symlinks") {
    TempDir dir;
    const auto file = dir.path() / "18238-Rich.txt";
    const auto target = dir.path() / "target.txt";
    const auto link = dir.path() / "linked-Rich.txt";
    write_bytes(file, kValidScript);
    write_bytes(target, "Display:\nProtected target\n");
    std::filesystem::create_symlink(target, link);

    cw::RichPresenceEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    const auto original_text = editor.text();
    const auto original_path = editor.path();
    const auto original_revision = editor.revision();

    CHECK_FALSE(editor.load(link, error));
    CHECK(error.find("symbolic link") != std::string::npos);
    CHECK(editor.text() == original_text);
    CHECK(editor.path() == original_path);
    CHECK(editor.revision() == original_revision);
    CHECK(read_bytes(target) == "Display:\nProtected target\n");
}

TEST_CASE("Rich Presence save preserves existing permissions") {
    TempDir dir;
    const auto file = dir.path() / "18238-Rich.txt";
    write_bytes(file, kValidScript);
    REQUIRE(::chmod(file.c_str(), 0640) == 0);

    cw::RichPresenceEditor editor;
    std::string error;
    REQUIRE(editor.load(file, error));
    editor.set_text("Display:\nEdited");
    REQUIRE(editor.save(error));

    struct stat file_status{};
    REQUIRE(::stat(file.c_str(), &file_status) == 0);
    CHECK((file_status.st_mode & 0777) == 0640);
}
