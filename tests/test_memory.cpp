// cw_devtools memory interface, on synthetic buffers only (no SDL, no core, no game).

#include "cw/devtools/memory.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

TEST_CASE("BufferMemory reads inside its range and stops at the end") {
    cw::BufferMemory memory({0x10, 0x20, 0x30, 0x40});
    CHECK(memory.size() == 4);

    std::array<uint8_t, 2> two{};
    CHECK(memory.read(0, two) == 2);
    CHECK(two == std::array<uint8_t, 2>{0x10, 0x20});

    CHECK(memory.read(3, two) == 1); // only one byte left before the end
    CHECK(two[0] == 0x40);
    CHECK(memory.read(4, two) == 0);          // first address past the end
    CHECK(memory.read(0xFFFFFFFF, two) == 0); // far out of range
}

TEST_CASE("read_le assembles little-endian values and refuses partial reads") {
    cw::BufferMemory memory({0x78, 0x56, 0x34, 0x12, 0xAB});
    CHECK(cw::read_le(memory, 0, 1) == 0x78u);
    CHECK(cw::read_le(memory, 0, 2) == 0x5678u);
    CHECK(cw::read_le(memory, 0, 3) == 0x345678u);
    CHECK(cw::read_le(memory, 0, 4) == 0x12345678u);
    CHECK(cw::read_le(memory, 4, 1) == 0xABu);

    CHECK_FALSE(cw::read_le(memory, 4, 2)); // second byte is past the end
    CHECK_FALSE(cw::read_le(memory, 5, 1));
    CHECK_FALSE(cw::read_le(memory, 0, 0)); // only 1-4 bytes
    CHECK_FALSE(cw::read_le(memory, 0, 5));
}

TEST_CASE("A synthetic 2 MiB PS1 RAM is readable at both ends") {
    std::vector<uint8_t> ram(0x200000, 0);
    ram.front() = 0xA1;
    ram.back() = 0xB2;
    cw::BufferMemory memory(std::move(ram), "System RAM");
    CHECK(cw::read_le(memory, 0x000000, 1) == 0xA1u);
    CHECK(cw::read_le(memory, 0x1FFFFF, 1) == 0xB2u);
    CHECK_FALSE(cw::read_le(memory, 0x200000, 1));
    CHECK(memory.regions() ==
          std::vector<cw::MemoryRegionInfo>{
              {.start = 0, .end = 0x1FFFFF, .description = "System RAM", .available = true}});
}

TEST_CASE("An empty buffer has no regions and reads nothing") {
    cw::BufferMemory memory({});
    std::array<uint8_t, 1> one{};
    CHECK(memory.size() == 0);
    CHECK(memory.read(0, one) == 0);
    CHECK(memory.regions().empty());
}
