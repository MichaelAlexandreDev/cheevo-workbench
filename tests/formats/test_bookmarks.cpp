#include "cw/formats/bookmarks.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <iterator>
#include <string>

namespace {

using Json = nlohmann::ordered_json;
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

TEST_CASE("Bookmark paths match the workbench and RAIntegration layouts") {
    const std::filesystem::path data("/xdg/data/cheevo-workbench");
    const std::filesystem::path cache("/home/user/RACache");
    CHECK(cw::Bookmarks::user_file_path(data, 18238) == data / "bookmarks/18238-Bookmarks.json");
    CHECK(cw::Bookmarks::ra_cache_file_path(cache, 18238) ==
          cache / "Bookmarks/18238-Bookmarks.json");
    CHECK(cw::Bookmarks::user_file_path({}, 18238).empty());
    CHECK(cw::Bookmarks::ra_cache_file_path(cache, 0).empty());
}

TEST_CASE("Bookmarks load all three RAIntegration formats and legacy size mappings") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    const auto file = cw::Bookmarks::user_file_path(data_dir, 18238);
    write_bytes(file, R"json({"Bookmarks":[
        {"Description":"Type byte","Address":1234,"Type":1,"Decimal":true},
        {"Description":"Type word","Address":1235,"Type":2},
        {"Description":"Type dword","Address":1236,"Type":3},
        {"Address":1237,"Size":0},
        {"Address":1238,"Size":1},
        {"Address":1238,"Size":2},
        {"Address":1238,"Size":3},
        {"Address":1238,"Size":4},
        {"Address":1238,"Size":5},
        {"Address":1238,"Size":6},
        {"Address":1238,"Size":7},
        {"Address":1238,"Size":8},
        {"Address":1238,"Size":9},
        {"Address":1239,"Size":10},
        {"Address":1240,"Size":11},
        {"Address":1241,"Size":12},
        {"Address":1242,"Size":13},
        {"Address":1243,"Size":14,"Extra":"keep"},
        {"Address":1244,"Size":15},
        {"Description":"MemAddr","MemAddr":"0xH04d2","Decimal":true}
    ]})json");

    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir, 0x200400);
    CHECK(bookmarks.error().empty());
    REQUIRE(bookmarks.items().size() == 20);
    CHECK(bookmarks.items()[0].address == 1234u);
    CHECK(bookmarks.items()[0].size == cw::MemoryValueSize::EightBit);
    CHECK(bookmarks.items()[0].decimal);
    CHECK(bookmarks.items()[1].size == cw::MemoryValueSize::SixteenBit);
    CHECK(bookmarks.items()[2].size == cw::MemoryValueSize::ThirtyTwoBit);
    CHECK(bookmarks.items()[3].size == cw::MemoryValueSize::Bit0);
    CHECK(bookmarks.items()[4].size == cw::MemoryValueSize::Bit1);
    CHECK(bookmarks.items()[5].size == cw::MemoryValueSize::Bit2);
    CHECK(bookmarks.items()[6].size == cw::MemoryValueSize::Bit3);
    CHECK(bookmarks.items()[7].size == cw::MemoryValueSize::Bit4);
    CHECK(bookmarks.items()[8].size == cw::MemoryValueSize::Bit5);
    CHECK(bookmarks.items()[9].size == cw::MemoryValueSize::Bit6);
    CHECK(bookmarks.items()[10].size == cw::MemoryValueSize::Bit7);
    CHECK(bookmarks.items()[11].size == cw::MemoryValueSize::LowNibble);
    CHECK(bookmarks.items()[12].size == cw::MemoryValueSize::HighNibble);
    CHECK(bookmarks.items()[13].size == cw::MemoryValueSize::EightBit);
    CHECK(bookmarks.items()[14].size == cw::MemoryValueSize::SixteenBit);
    CHECK(bookmarks.items()[15].size == cw::MemoryValueSize::TwentyFourBit);
    CHECK(bookmarks.items()[16].size == cw::MemoryValueSize::ThirtyTwoBit);
    CHECK_FALSE(bookmarks.items()[17].supported());
    CHECK(bookmarks.items()[17].unsupported_reason == "BitCount");
    CHECK_FALSE(bookmarks.items()[18].supported());
    CHECK(bookmarks.items()[18].unsupported_reason == "Text");
    CHECK(bookmarks.items()[19].address == 1234u);
    CHECK(bookmarks.items()[19].size == cw::MemoryValueSize::EightBit);
}

TEST_CASE("RA bookmarks save current MemAddr format and reload in order") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir, 0x200400);
    std::string error;
    REQUIRE(bookmarks.add(0x0D9C24, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian,
                          "Lives", error));
    CHECK(error.empty());
    REQUIRE(bookmarks.add(0x0929, cw::MemoryValueSize::SixteenBit, cw::ByteOrder::LittleEndian, {},
                          error));
    REQUIRE(
        bookmarks.add(0x42, cw::MemoryValueSize::SixteenBit, cw::ByteOrder::BigEndian, {}, error));
    REQUIRE(bookmarks.set_decimal(1, true, error));
    REQUIRE(bookmarks.move(1, 0, error));

    const auto file = cw::Bookmarks::user_file_path(data_dir, 18238);
    const Json saved = Json::parse(read_bytes(file));
    REQUIRE(saved.at("Bookmarks").size() == 3);
    CHECK(saved.at("Bookmarks")[0].at("MemAddr") == "0x 000929");
    CHECK(saved.at("Bookmarks")[0].at("Decimal") == true);
    CHECK_FALSE(saved.at("Bookmarks")[0].contains("Description"));
    CHECK(saved.at("Bookmarks")[1].at("MemAddr") == "0xH0d9c24");
    CHECK(saved.at("Bookmarks")[1].at("Description") == "Lives");
    CHECK(saved.at("Bookmarks")[2].at("MemAddr") == "0xI000042");

    cw::Bookmarks reloaded;
    reloaded.load(18238, data_dir, 0x200400);
    REQUIRE(reloaded.items().size() == 3);
    CHECK(reloaded.items()[0].address == 0x0929u);
    CHECK(reloaded.items()[0].decimal);
    CHECK(reloaded.items()[1].address == 0x0D9C24u);
    CHECK(reloaded.items()[1].description == "Lives");
    CHECK(reloaded.items()[2].address == 0x42u);
    CHECK(reloaded.items()[2].size == cw::MemoryValueSize::SixteenBit);
    CHECK(reloaded.items()[2].byte_order == cw::ByteOrder::BigEndian);
}

TEST_CASE("Unsupported bookmark sizes and pointer chains survive local edits") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    const auto file = cw::Bookmarks::user_file_path(data_dir, 18238);
    const std::string original =
        R"({"Bookmarks":[{"MemAddr":"I:0x 0020_M:0xW0008","Description":"pointer","Decimal":true,"Vendor":{"keep":[1,2]}},)"
        R"({"Address":20,"Size":14,"Vendor":"bit-count"}]})";
    write_bytes(file, original);

    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir);
    REQUIRE(bookmarks.items().size() == 2);
    CHECK_FALSE(bookmarks.items()[0].supported());
    CHECK(bookmarks.items()[0].unsupported_reason == "Indirect address");
    CHECK_FALSE(bookmarks.items()[1].supported());
    CHECK(bookmarks.items()[1].unsupported_reason == "BitCount");

    std::string error;
    REQUIRE(bookmarks.add(0x30, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian, "new",
                          error));
    REQUIRE(bookmarks.set_description(0, "renamed pointer", error));
    const Json saved = Json::parse(read_bytes(file));
    REQUIRE(saved.at("Bookmarks").size() == 3);
    CHECK(saved.at("Bookmarks")[0].at("MemAddr") == "I:0x 0020_M:0xW0008");
    CHECK(saved.at("Bookmarks")[0].at("Description") == "renamed pointer");
    CHECK(saved.at("Bookmarks")[0].at("Vendor").at("keep") == Json::array({1, 2}));
    CHECK(saved.at("Bookmarks")[1].at("Size") == 14);
    CHECK(saved.at("Bookmarks")[1].at("Vendor") == "bit-count");
    CHECK(saved.at("Bookmarks")[2].at("MemAddr") == "0xH0030");
}

TEST_CASE("Malformed local bookmarks block changes without replacing the original") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    const auto file = cw::Bookmarks::user_file_path(data_dir, 18238);
    const std::string malformed = "{ this is not valid JSON\n";
    write_bytes(file, malformed);

    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir);
    CHECK_FALSE(bookmarks.writable());
    CHECK_FALSE(bookmarks.error().empty());
    std::string error;
    CHECK_FALSE(
        bookmarks.add(0x10, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian, {}, error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(file) == malformed);
}

TEST_CASE("Saving supported bookmarks preserves unknown item and document fields") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    const auto file = cw::Bookmarks::user_file_path(data_dir, 18238);
    write_bytes(file, R"({"Version":2,"Vendor":{"keep":[1,2]},"Bookmarks":[
        {"Address":16,"Type":1,"Vendor":{"color":"red"}},
        {"MemAddr":"0xI0020","Vendor":"word"}
    ]})");

    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir);
    REQUIRE(bookmarks.size() == 2);
    std::string error;
    REQUIRE(bookmarks.set_description(0, "renamed", error));
    REQUIRE(bookmarks.set_decimal(1, true, error));
    const Json saved = Json::parse(read_bytes(file));
    CHECK(saved.value("Version", 0) == 2);
    CHECK(saved.value("Vendor", Json()) == Json::parse(R"({"keep":[1,2]})"));
    const auto& first = saved.at("Bookmarks")[0];
    CHECK(first.value("Vendor", Json()) == Json::parse(R"({"color":"red"})"));
    CHECK(first.at("MemAddr") == "0xH0010");
    CHECK_FALSE(first.contains("Address"));
    CHECK_FALSE(first.contains("Type"));
    CHECK(saved.at("Bookmarks")[1].value("Vendor", "") == "word");
}

TEST_CASE("Import preserves document metadata and a failed import leaves it unchanged") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    const auto cache = dir.path() / "cache/bookmarks.json";
    write_bytes(cache, R"({"Version":7,"Bookmarks":[{"MemAddr":"0xH0001"}]})");
    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir);
    std::string error;
    REQUIRE(bookmarks.import_file(cache, error));
    write_bytes(cache, R"({"Version":8,"Bookmarks":false})");
    CHECK_FALSE(bookmarks.import_file(cache, error));
    REQUIRE(bookmarks.set_description(0, "kept", error));
    const auto exported = dir.path() / "export/bookmarks.json";
    REQUIRE(bookmarks.export_file(exported, false, error));
    CHECK(Json::parse(read_bytes(exported)).value("Version", 0) == 7);
    bookmarks.load(12345, data_dir);
    REQUIRE(
        bookmarks.add(1, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian, {}, error));
    CHECK_FALSE(Json::parse(read_bytes(cw::Bookmarks::user_file_path(data_dir, 12345)))
                    .contains("Version"));
}

TEST_CASE("A failed bookmark write leaves the live list and imported metadata unchanged") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    const auto file = cw::Bookmarks::user_file_path(data_dir, 18238);
    write_bytes(file, R"({"Version":1,"Bookmarks":[{"MemAddr":"0xH0001"}]})");
    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir);
    const auto before = read_bytes(file);
    // A directory cannot be replaced by the atomic rename of a regular file.
    std::filesystem::remove(file);
    std::filesystem::create_directory(file);
    const auto imported = dir.path() / "import/bookmarks.json";
    write_bytes(imported, R"({"Version":2,"Bookmarks":[{"MemAddr":"0xH0002"}]})");
    std::string error;
    CHECK_FALSE(bookmarks.set_description(0, "not saved", error));
    CHECK_FALSE(error.empty());
    CHECK(bookmarks.items()[0].description.empty());
    CHECK_FALSE(bookmarks.import_file(imported, error));
    REQUIRE(bookmarks.size() == 1);
    CHECK(bookmarks.items()[0].address == 1u);
    const auto exported = dir.path() / "export/bookmarks.json";
    REQUIRE(bookmarks.export_file(exported, false, error));
    CHECK(Json::parse(read_bytes(exported)) == Json::parse(before));
    for (const auto& entry : std::filesystem::directory_iterator(file.parent_path())) {
        CHECK(entry.path() == file); // No failed-write temporary files remain.
    }
}

TEST_CASE("Current MemAddr sizes round trip with their memory interpretation") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    const auto file = cw::Bookmarks::user_file_path(data_dir, 18238);
    const std::array prefixes{'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'L',
                              'U', 'H', ' ', 'W', 'X', 'I', 'J', 'G'};
    Json root = {{"Bookmarks", Json::array()}};
    for (char prefix : prefixes) {
        root["Bookmarks"].push_back({{"MemAddr", std::string("0x") + prefix + "0000"}});
    }
    root["Bookmarks"].push_back({{"MemAddr", "fF0000"}, {"Vendor", "float"}});
    root["Bookmarks"].push_back({{"MemAddr", "0xH0000"}, {"Size", 15}});
    write_bytes(file, root.dump());
    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir);
    REQUIRE(bookmarks.size() == prefixes.size() + 2);
    const cw::BufferMemory memory({0xA5, 0x12, 0x34, 0x56});
    bookmarks.update(memory, 0);
    for (size_t index = 0; index < prefixes.size(); ++index) {
        CHECK(bookmarks.items()[index].supported());
    }
    CHECK(bookmarks.items()[0].current_value == 1u);
    CHECK(bookmarks.items()[1].current_value == 0u);
    CHECK(bookmarks.items()[8].current_value == 5u);
    CHECK(bookmarks.items()[9].current_value == 10u);
    CHECK(bookmarks.items()[11].current_value == 0x12A5u);
    CHECK(bookmarks.items()[12].current_value == 0x3412A5u);
    CHECK(bookmarks.items()[13].current_value == 0x563412A5u);
    CHECK(bookmarks.items()[14].current_value == 0xA512u);
    CHECK(bookmarks.items()[15].current_value == 0xA51234u);
    CHECK(bookmarks.items()[16].current_value == 0xA5123456u);
    CHECK_FALSE(bookmarks.items()[17].supported());
    CHECK_FALSE(bookmarks.items()[18].supported());
    std::string error;
    REQUIRE(bookmarks.set_description(0, "bit", error));
    const Json saved = Json::parse(read_bytes(file));
    for (size_t index = 0; index < prefixes.size(); ++index) {
        CHECK(saved.at("Bookmarks")[index].at("MemAddr") ==
              root.at("Bookmarks")[index].at("MemAddr"));
    }
    CHECK(saved.at("Bookmarks")[17] == root.at("Bookmarks")[17]);
    CHECK(saved.at("Bookmarks")[18] == root.at("Bookmarks")[18]);
}

TEST_CASE("RACache import and export are explicit and export protects existing files") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    const auto cache_dir = dir.path() / "RACache";
    const auto cache_file = cw::Bookmarks::ra_cache_file_path(cache_dir, 18238);
    write_bytes(cache_file, R"({"Bookmarks":[{"MemAddr":"0xH000a"}]})");

    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir, 0x10000);
    const std::string cache_before = read_bytes(cache_file);
    std::string error;
    REQUIRE(bookmarks.import_file(cache_file, error));
    CHECK(error.empty());
    CHECK(read_bytes(cache_file) == cache_before);
    CHECK(std::filesystem::exists(cw::Bookmarks::user_file_path(data_dir, 18238)));
    REQUIRE(
        bookmarks.add(0x20, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian, {}, error));
    CHECK(read_bytes(cache_file) == cache_before);

    const auto export_file = cw::Bookmarks::ra_cache_file_path(dir.path() / "ExportedCache", 18238);
    REQUIRE(bookmarks.export_file(export_file, false, error));
    CHECK(Json::parse(read_bytes(export_file)).at("Bookmarks")[0].at("MemAddr") == "0xH000a");
    const std::string exported = read_bytes(export_file);
    CHECK_FALSE(bookmarks.export_file(export_file, false, error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(export_file) == exported);
    REQUIRE(bookmarks.export_file(export_file, true, error));
    CHECK(read_bytes(export_file) == exported);
}

TEST_CASE("Changing game IDs switches files without mixing bookmark lists") {
    TempDir dir;
    const auto data_dir = dir.path() / "data";
    cw::Bookmarks bookmarks;
    bookmarks.load(18238, data_dir);
    std::string error;
    REQUIRE(bookmarks.add(0x10, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian,
                          "first game", error));
    bookmarks.load(12345, data_dir);
    CHECK(bookmarks.items().empty());
    REQUIRE(bookmarks.add(0x20, cw::MemoryValueSize::SixteenBit, cw::ByteOrder::LittleEndian,
                          "second game", error));

    cw::Bookmarks first_game;
    first_game.load(18238, data_dir);
    REQUIRE(first_game.items().size() == 1);
    CHECK(first_game.items()[0].address == 0x10u);
    CHECK(first_game.items()[0].description == "first game");
    CHECK(std::filesystem::exists(cw::Bookmarks::user_file_path(data_dir, 12345)));
}

TEST_CASE("Bookmarks retain live values, unavailable state, and change highlights") {
    cw::Bookmarks bookmarks;
    bookmarks.load(std::nullopt, {});
    std::string error;
    REQUIRE(
        bookmarks.add(0, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian, {}, error));
    CHECK(error == "No game ID; bookmarks are not saved.");
    REQUIRE(bookmarks.add(0x100, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian, {},
                          error));

    cw::BufferMemory memory({0x10});
    bookmarks.update(memory, 0);
    CHECK(bookmarks.items()[0].current_value == 0x10u);
    CHECK_FALSE(bookmarks.items()[1].current_value);

    const std::array<uint8_t, 1> changed{0x20};
    REQUIRE(memory.write(0, changed) == cw::MemoryWriteError::None);
    bookmarks.update(memory, 1);
    CHECK(bookmarks.items()[0].current_value == 0x20u);
    CHECK(bookmarks.items()[0].previous_value == 0x10u);
    CHECK(bookmarks.frames_since_change(0, 1) == 0u);
}
