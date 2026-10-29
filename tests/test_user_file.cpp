#include "cw/devtools/user_file.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

bool round_trips_exactly(std::string_view source, std::string& error) {
    auto document = cw::parse_user_file(source, error);
    if (!document) {
        return false;
    }

    auto serialized = cw::serialize_user_file(*document, error);
    if (!serialized) {
        return false;
    }

    if (*serialized != source) {
        error = "Serialized bytes differ from the input";
        return false;
    }

    return true;
}

} // namespace

TEST_CASE("User files parse achievement and leaderboard layouts and round-trip byte for byte") {
    const std::string fixture = "an opaque future writer/version 9\r\n"
                                "Game title: subtitle\r\n"
                                "17|42:\"0xH0001=1\":Title:Description:::progression:Author:10:::::"
                                "123:future-achievement\r\n"
                                "L21|8:\"0xH0002=1\":\"0xH0003=1\":\"0xH0004=1\":\"M:0xH0005\":"
                                "VALUE:Board::1:future-leaderboard";

    std::string error;
    const auto document = cw::parse_user_file(fixture, error);
    REQUIRE_MESSAGE(document.has_value(), error);
    CHECK(error.empty());
    REQUIRE(document->version == "an opaque future writer/version 9");
    REQUIRE(document->title == "Game title: subtitle");
    REQUIRE(document->entries.size() == 2);

    const auto* achievement = std::get_if<cw::LocalAchievement>(&document->entries[0].asset);
    REQUIRE(achievement != nullptr);
    CHECK(achievement->id == 17);
    REQUIRE(achievement->subset_id.has_value());
    CHECK(*achievement->subset_id == 42);
    CHECK(achievement->title == "Title");
    CHECK(achievement->description == "Description");
    CHECK(achievement->type == "progression");
    CHECK(achievement->author == "Author");
    CHECK(achievement->points == 10);
    CHECK(achievement->badge == "00123");

    const auto* leaderboard = std::get_if<cw::LocalLeaderboard>(&document->entries[1].asset);
    REQUIRE(leaderboard != nullptr);
    CHECK(leaderboard->id == 21);
    REQUIRE(leaderboard->subset_id.has_value());
    CHECK(*leaderboard->subset_id == 8);
    CHECK(leaderboard->value == "M:0xH0005");
    CHECK(leaderboard->format == "VALUE");
    CHECK(leaderboard->title == "Board");
    CHECK(leaderboard->description.empty());
    CHECK(leaderboard->lower_is_better);

    const auto serialized = cw::serialize_user_file(*document, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(error.empty());
    CHECK(*serialized == fixture);
}

TEST_CASE("User files accept ID zero, subset IDs, and optional leaderboard fields") {
    const std::string fixture =
        "unknown version\n"
        "Game\n"
        "0|99:\"0xH0001=1\":Zero::::::0:::::00000\n"
        "L0|77:\"0xH0002=1\":\"0xH0003=1\":\"0xH0004=1\":\"0xH0005\":VALUE:Legacy\n"
        "L8:\"0xH0006=1\":\"0xH0007=1\":\"0xH0008=1\":\"M:0xH0009\":VALUE:Measured:Description:1";

    std::string error;
    const auto document = cw::parse_user_file(fixture, error);
    REQUIRE_MESSAGE(document.has_value(), error);
    REQUIRE(document->entries.size() == 3);

    const auto* achievement = std::get_if<cw::LocalAchievement>(&document->entries[0].asset);
    REQUIRE(achievement != nullptr);
    CHECK(achievement->id == 0);
    REQUIRE(achievement->subset_id.has_value());
    CHECK(*achievement->subset_id == 99);

    const auto* legacy = std::get_if<cw::LocalLeaderboard>(&document->entries[1].asset);
    REQUIRE(legacy != nullptr);
    CHECK(legacy->id == 0);
    REQUIRE(legacy->subset_id.has_value());
    CHECK(*legacy->subset_id == 77);
    CHECK(legacy->value == "0xH0005");
    CHECK(legacy->description.empty());
    CHECK_FALSE(legacy->lower_is_better);

    const auto* measured = std::get_if<cw::LocalLeaderboard>(&document->entries[2].asset);
    REQUIRE(measured != nullptr);
    CHECK(measured->value == "M:0xH0009");
    CHECK(measured->description == "Description");
    CHECK(measured->lower_is_better);

    const auto serialized = cw::serialize_user_file(*document, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(*serialized == fixture);
}

TEST_CASE("User files preserve quotes, escaped delimiters, RA tokenizer escapes, and UTF-8") {
    const std::string encoded_title =
        "\"Title: \\\"quoted\\\" \\\\ slash \\nline \\rreturn \\ttab \\q unknown 世界\"";
    const std::string fixture = "version\nGame\n"
                                "7:\"0xH0010=1\":" +
                                encoded_title + ":Description:::missable:Author:5:::::00007:future";

    std::string error;
    const auto document = cw::parse_user_file(fixture, error);
    REQUIRE_MESSAGE(document.has_value(), error);
    REQUIRE(document->entries.size() == 1);

    const auto* achievement = std::get_if<cw::LocalAchievement>(&document->entries[0].asset);
    REQUIRE(achievement != nullptr);
    CHECK(achievement->title == "Title: \"quoted\" \\ slash \nline \rreturn \ttab q unknown 世界");

    const auto serialized = cw::serialize_user_file(*document, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(*serialized == fixture);
}

TEST_CASE("User files preserve LF, CRLF, mixed endings, and a missing final newline") {
    const std::vector<std::string> fixtures = {
        "version\r\nGame\r\nN:opaque note\r\n",
        "version\nGame\nN:opaque note\n",
        "version\r\nGame\nN:opaque note\r\n",
        "version\r\nGame\nN:opaque note",
    };

    for (const auto& fixture : fixtures) {
        CAPTURE(fixture);
        std::string error;
        CHECK_MESSAGE(round_trips_exactly(fixture, error), error);
    }
}

TEST_CASE("User file headers are present but opaque, including blank headers") {
    const std::vector<std::string> fixtures = {
        "writer format that RAIntegration does not recognize\nGame\n",
        "\n\n",
        "\r\n\r\nN:note without a schema version",
    };

    for (const auto& fixture : fixtures) {
        CAPTURE(fixture);
        std::string error;
        CHECK_MESSAGE(round_trips_exactly(fixture, error), error);
    }
}

TEST_CASE("Editing known fields preserves passthrough lines and unused or future fields") {
    const std::string fixture =
        "future writer\n"
        "Game\n"
        "N:note data stays verbatim\n"
        "M:region data stays verbatim\n"
        "BadgeDir=/some/previous/badge/directory\n"
        "X:unknown record stays verbatim\n"
        "\n"
        "9:\"0xH0010=1\":Old title:Old "
        "description:unused-progress:unused-max:custom-type:unused-author:12:unused-created:unused-"
        "modified:unused-upvotes:unused-downvotes:ABCDE:future-one:future-two\n"
        "L4:\"0xH0020=1\":\"0xH0021=1\":\"0xH0022=1\":\"0xH0023\":CUSTOM:Old board:Old board "
        "description:0:future-board-field";

    std::string error;
    auto document = cw::parse_user_file(fixture, error);
    REQUIRE_MESSAGE(document.has_value(), error);
    REQUIRE(document->entries.size() == 7);

    auto* achievement = std::get_if<cw::LocalAchievement>(&document->entries[5].asset);
    REQUIRE(achievement != nullptr);
    CHECK(achievement->type == "custom-type");
    achievement->points = 13;

    auto* leaderboard = std::get_if<cw::LocalLeaderboard>(&document->entries[6].asset);
    REQUIRE(leaderboard != nullptr);
    CHECK(leaderboard->format == "CUSTOM");
    leaderboard->title = "Edited board";
    leaderboard->lower_is_better = true;

    const auto serialized = cw::serialize_user_file(*document, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(serialized->find("N:note data stays verbatim\n") != std::string::npos);
    CHECK(serialized->find("M:region data stays verbatim\n") != std::string::npos);
    CHECK(serialized->find("BadgeDir=/some/previous/badge/directory\n") != std::string::npos);
    CHECK(serialized->find("X:unknown record stays verbatim\n") != std::string::npos);
    CHECK(serialized->find(
              "\n\n9:\"0xH0010=1\":Old title:Old "
              "description:unused-progress:unused-max:custom-type:unused-author:13:unused-created:"
              "unused-modified:unused-upvotes:unused-downvotes:ABCDE:future-one:future-two\n") !=
          std::string::npos);
    CHECK(serialized->find("L4:\"0xH0020=1\":\"0xH0021=1\":\"0xH0022=1\":\"0xH0023\":CUSTOM:Edited "
                           "board:Old board description:1:future-board-field") !=
          std::string::npos);

    const auto reparsed = cw::parse_user_file(*serialized, error);
    REQUIRE_MESSAGE(reparsed.has_value(), error);
    const auto* updated_achievement =
        std::get_if<cw::LocalAchievement>(&reparsed->entries[5].asset);
    REQUIRE(updated_achievement != nullptr);
    CHECK(updated_achievement->points == 13);
    CHECK(updated_achievement->type == "custom-type");
    const auto* updated_leaderboard =
        std::get_if<cw::LocalLeaderboard>(&reparsed->entries[6].asset);
    REQUIRE(updated_leaderboard != nullptr);
    CHECK(updated_leaderboard->title == "Edited board");
    CHECK(updated_leaderboard->lower_is_better);
    CHECK(updated_leaderboard->format == "CUSTOM");
}

TEST_CASE("New user assets serialize with canonical field order and quoted logic fields") {
    cw::UserFileDocument document;
    document.title = "New game";

    std::string error;
    const auto achievement_logic = cw::parse_achievement_logic("0xH0001=1", error);
    REQUIRE_MESSAGE(achievement_logic.has_value(), error);
    cw::LocalAchievement achievement;
    achievement.id = 0;
    achievement.subset_id = 42;
    achievement.logic = *achievement_logic;
    achievement.title = "Fresh achievement";
    achievement.description = "A new record";
    achievement.type = "missable";
    achievement.author = "Tester";
    achievement.points = 15;
    achievement.badge = "12345";
    cw::UserFileEntry achievement_entry;
    achievement_entry.asset = achievement;
    document.entries.push_back(achievement_entry);

    const auto start = cw::parse_achievement_logic("0xH0002=1", error);
    REQUIRE_MESSAGE(start.has_value(), error);
    const auto submit = cw::parse_achievement_logic("0xH0003=1", error);
    REQUIRE_MESSAGE(submit.has_value(), error);
    cw::LocalLeaderboard leaderboard;
    leaderboard.id = 3;
    leaderboard.start = *start;
    leaderboard.cancel = *start;
    leaderboard.submit = *submit;
    leaderboard.value = "M:0xH0004";
    leaderboard.format = "VALUE";
    leaderboard.title = "Fresh leaderboard";
    leaderboard.description = "Record description";
    leaderboard.lower_is_better = true;
    cw::UserFileEntry leaderboard_entry;
    leaderboard_entry.asset = leaderboard;
    document.entries.push_back(leaderboard_entry);

    const auto serialized = cw::serialize_user_file(document, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(*serialized ==
          "cheevo-workbench 0.1.0\n"
          "New game\n"
          "0|42:\"0xH0001=1\":Fresh achievement:A new record:::missable:Tester:15:::::12345\n"
          "L3:\"0xH0002=1\":\"0xH0002=1\":\"0xH0003=1\":\"M:0xH0004\":VALUE:Fresh "
          "leaderboard:Record description:1\n");
}

TEST_CASE("Malformed known records, unclosed quotes, and overflowing integers are rejected") {
    const std::vector<std::string> malformed = {
        "version\nGame\n7:\"0xH0001=1\":Missing required fields\n",
        "version\nGame\nL7:\"0xH0001=1\":unquoted cancel:\"0xH0003=1\":\"0xH0004\":VALUE:Board\n",
        "version\nGame\n7:\"0xH0001=1\":\"unclosed "
        "title:Description:::missable:Author:5:::::00007\n",
        "version\nGame\n4294967296:\"0xH0001=1\":Title:Description:::missable:Author:5:::::00007\n",
        "version\nGame\n7:\"0xH0001=1\":Title:Description:::missable:Author:4294967296:::::00007\n",
    };

    for (const auto& fixture : malformed) {
        CAPTURE(fixture);
        std::string error;
        CHECK_FALSE(cw::parse_user_file(fixture, error).has_value());
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("User file parser rejects embedded NUL bytes and files over the size limit") {
    const char nul_bytes[] = "version\nGame\nN:before\0after";
    const std::string with_nul(nul_bytes, sizeof(nul_bytes) - 1);
    std::string error;
    CHECK_FALSE(cw::parse_user_file(with_nul, error).has_value());
    CHECK_FALSE(error.empty());

    const std::string oversized(cw::kMaxUserFileSize + 1, 'x');
    error.clear();
    CHECK_FALSE(cw::parse_user_file(oversized, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE(
    "User file serializer rejects invalid logic, invalid leaderboard values, and newline headers") {
    const std::string fixture =
        "version\nGame\n"
        "1:\"0xH0001=1\":Title:Description:::missable:Author:5:::::00001\n"
        "L2:\"0xH0002=1\":\"0xH0003=1\":\"0xH0004=1\":\"0xH0005\":VALUE:Board";

    std::string error;
    auto invalid_logic_document = cw::parse_user_file(fixture, error);
    REQUIRE_MESSAGE(invalid_logic_document.has_value(), error);
    auto* achievement =
        std::get_if<cw::LocalAchievement>(&invalid_logic_document->entries[0].asset);
    REQUIRE(achievement != nullptr);
    REQUIRE(achievement->logic.core.conditions.size() == 1);
    achievement->logic.core.conditions[0].left.size = static_cast<cw::LogicMemorySize>(255);
    CHECK_FALSE(cw::serialize_user_file(*invalid_logic_document, error).has_value());
    CHECK_FALSE(error.empty());

    auto invalid_value_document = cw::parse_user_file(fixture, error);
    REQUIRE_MESSAGE(invalid_value_document.has_value(), error);
    auto* leaderboard =
        std::get_if<cw::LocalLeaderboard>(&invalid_value_document->entries[1].asset);
    REQUIRE(leaderboard != nullptr);
    leaderboard->value = "not a valid leaderboard value";
    error.clear();
    CHECK_FALSE(cw::serialize_user_file(*invalid_value_document, error).has_value());
    CHECK_FALSE(error.empty());

    auto invalid_header_document = cw::parse_user_file(fixture, error);
    REQUIRE_MESSAGE(invalid_header_document.has_value(), error);
    invalid_header_document->title = "Game\nTitle";
    error.clear();
    CHECK_FALSE(cw::serialize_user_file(*invalid_header_document, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("New entries inherit CRLF and inserting after an unterminated line separates records") {
    std::string error;
    auto document = cw::parse_user_file("writer\r\nGame\r\nX:opaque", error);
    REQUIRE_MESSAGE(document.has_value(), error);
    cw::UserFileEntry entry;
    cw::LocalAchievement achievement;
    achievement.title = "New";
    entry.asset = achievement;
    document->entries.push_back(entry);
    const auto serialized = cw::serialize_user_file(*document, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(*serialized == "writer\r\nGame\r\nX:opaque\r\n0:\"\":New::::::5:::::00000\r\n");
}

TEST_CASE("Editing a final title to empty retains a second header line") {
    std::string error;
    auto document = cw::parse_user_file("writer\nGame", error);
    REQUIRE_MESSAGE(document.has_value(), error);
    document->title.clear();
    const auto serialized = cw::serialize_user_file(*document, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(*serialized == "writer\n\n");
}

TEST_CASE("Repeated nonzero IDs cannot merge assets silently while zero IDs remain distinct") {
    const std::string row = "7:\"\":Title:Description:::::5:::::00000\n";
    std::string error;
    CHECK_FALSE(cw::parse_user_file("writer\nGame\n" + row + row, error).has_value());
    CHECK_FALSE(error.empty());
    const std::string zero = "0:\"\":Title:Description:::::5:::::00000\n";
    auto parsed = cw::parse_user_file("writer\nGame\n" + zero + zero, error);
    REQUIRE_MESSAGE(parsed.has_value(), error);
    CHECK(parsed->entries.size() == 2);
    std::get<cw::LocalAchievement>(parsed->entries[0].asset).id = 7;
    std::get<cw::LocalAchievement>(parsed->entries[1].asset).id = 7;
    CHECK_FALSE(cw::serialize_user_file(*parsed, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("Legacy unquoted triggers and noncanonical numeric tokens remain intact until edited") {
    const std::string fixture = "writer\nGame\n"
                                "0007|0002:M:0xH1=5:Title:Description:::::0005:::::7\n"
                                "L7:\"\":\"\":\"\":\"\":CUSTOM:Title:Description:2";
    std::string error;
    auto document = cw::parse_user_file(fixture, error);
    REQUIRE_MESSAGE(document.has_value(), error);
    CHECK(cw::serialize_user_file(*document, error) == fixture);
    auto& achievement = std::get<cw::LocalAchievement>(document->entries[0].asset);
    achievement.logic.core.conditions[0].right->value = 6;
    const auto edited = cw::serialize_user_file(*document, error);
    REQUIRE_MESSAGE(edited.has_value(), error);
    CHECK(edited->find("0007|0002:\"M:0xH1=6\":Title") != std::string::npos);
    CHECK(edited->find(":0005:::::7\n") != std::string::npos);
    CHECK(edited->ends_with(":Description:2"));
}

TEST_CASE("Numeric fields cannot be quoted and value expressions must consume their entire field") {
    for (const std::string_view row : {"1:\"\":Title:Description:::::\"5\":::::00000",
                                       "L1:\"\":\"\":\"\":\"\":VALUE:Title:Description:\"1\"",
                                       "L1:\"\":\"\":\"\":\"M:0xH1)\":VALUE:Title"}) {
        std::string error;
        CHECK_FALSE(cw::parse_user_file("writer\nGame\n" + std::string(row), error).has_value());
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("User files derive the RAIntegration data path without writing it") {
    CHECK(cw::ra_user_file_path("/tmp/synthetic/RACache", 123) ==
          "/tmp/synthetic/RACache/Data/123-User.txt");
    CHECK(cw::ra_user_file_path({}, 123).empty());
    CHECK(cw::ra_user_file_path("/tmp/synthetic/RACache", 0).empty());
}

TEST_CASE("Opaque records preserve embedded carriage returns without interpreting them") {
    std::string error;
    CHECK_MESSAGE(round_trips_exactly("writer\nGame\nX:opaque\rdata\n", error), error);
}
