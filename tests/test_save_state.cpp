// Tests for the RASTATE save-state container (no core involved).

#include "save_state.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> bytes(std::initializer_list<int> values) {
    std::vector<uint8_t> out;
    for (int v : values) {
        out.push_back(static_cast<uint8_t>(v));
    }
    return out;
}

void append(std::vector<uint8_t>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

} // namespace

TEST_CASE("A state file has the exact RASTATE layout RALibretro writes") {
    const std::vector<uint8_t> file = cw::write_state_file(bytes({0xAA, 0xBB, 0xCC}));

    std::vector<uint8_t> expected;
    append(expected, "RASTATE");
    expected.push_back(1); // version
    append(expected, "MEM ");
    expected.insert(expected.end(), {3, 0, 0, 0});                      // size, little-endian
    expected.insert(expected.end(), {0xAA, 0xBB, 0xCC, 0, 0, 0, 0, 0}); // padded to 8
    append(expected, "END ");
    expected.insert(expected.end(), {0, 0, 0, 0});
    CHECK(file == expected);
}

TEST_CASE("Core and achievement blocks survive a write/parse round trip") {
    const auto core = bytes({1, 2, 3, 4, 5, 6, 7, 8, 9});
    const auto achv = bytes({0x41, 0x43});
    std::string error;
    const auto parsed = cw::parse_state_file(cw::write_state_file(core, achv), error);
    REQUIRE(parsed);
    CHECK(parsed->core == core);
    REQUIRE(parsed->achievements);
    CHECK(*parsed->achievements == achv);

    const auto without_achv = cw::parse_state_file(cw::write_state_file(core), error);
    REQUIRE(without_achv);
    CHECK_FALSE(without_achv->achievements);
}

TEST_CASE("A file without the RASTATE magic is a raw core state") {
    const auto raw = bytes({9, 8, 7});
    std::string error;
    const auto parsed = cw::parse_state_file(raw, error);
    REQUIRE(parsed);
    CHECK(parsed->core == raw);
    CHECK_FALSE(parsed->achievements);
}

TEST_CASE("Unknown blocks are skipped and a missing final padding is tolerated") {
    std::vector<uint8_t> file;
    append(file, "RASTATE");
    file.push_back(1);
    append(file, "SCRN"); // screenshot block: not needed, skipped
    file.insert(file.end(), {2, 0, 0, 0, 0xEE, 0xEE, 0, 0, 0, 0, 0, 0});
    append(file, "MEM ");
    file.insert(file.end(), {3, 0, 0, 0, 7, 7, 7}); // last block, no padding, no END block
    std::string error;
    const auto parsed = cw::parse_state_file(file, error);
    REQUIRE(parsed);
    CHECK(parsed->core == bytes({7, 7, 7}));
}

TEST_CASE("Truncated, corrupt or incomplete state files are rejected") {
    std::string error;

    SUBCASE("empty file") {
        CHECK_FALSE(cw::parse_state_file({}, error));
    }
    SUBCASE("unknown version") {
        std::vector<uint8_t> file = cw::write_state_file(bytes({1}));
        file[7] = 2;
        CHECK_FALSE(cw::parse_state_file(file, error));
        CHECK(error.find("version") != std::string::npos);
    }
    SUBCASE("block larger than the file (truncated save)") {
        std::vector<uint8_t> file = cw::write_state_file(bytes({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
        file.resize(8 + 8 + 4); // header + MEM header + only 4 of the 10 bytes
        CHECK_FALSE(cw::parse_state_file(file, error));
        CHECK(error.find("Truncated") != std::string::npos);
    }
    SUBCASE("size field near 4 GiB does not overflow the bounds check") {
        std::vector<uint8_t> file;
        append(file, "RASTATE");
        file.push_back(1);
        append(file, "MEM ");
        file.insert(file.end(), {0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3});
        CHECK_FALSE(cw::parse_state_file(file, error));
    }
    SUBCASE("block header cut in the middle") {
        std::vector<uint8_t> file;
        append(file, "RASTATE");
        file.push_back(1);
        append(file, "ME");
        CHECK_FALSE(cw::parse_state_file(file, error));
    }
    SUBCASE("no core data") {
        std::vector<uint8_t> file;
        append(file, "RASTATE");
        file.push_back(1);
        append(file, "END ");
        file.insert(file.end(), {0, 0, 0, 0});
        CHECK_FALSE(cw::parse_state_file(file, error));
        CHECK(error.find("MEM") != std::string::npos);
    }
}
