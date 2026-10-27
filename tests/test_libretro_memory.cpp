// The core's memory in RetroAchievements addresses (rc_libretro), driven by the test core.

#include "libretro_core.hpp"
#include "libretro_memory.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>
#include <rc_consoles.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

using cw_test::SyntheticContent;
using cw_test::test_core_path;

struct LoadedCore {
    SyntheticContent content;
    cw::LibretroCore core;

    explicit LoadedCore(const char* memory_map = "none") {
        std::string error;
        REQUIRE(core.load(test_core_path(), {}, error));
        REQUIRE(core.set_option("cw_test_memory_map", memory_map));
        REQUIRE(core.load_game(content.path(), error));
    }
};

} // namespace

TEST_CASE("Cores are matched to consoles by file name") {
    CHECK(cw::console_for_core("/usr/lib/libretro/mednafen_psx_libretro.so") ==
          RC_CONSOLE_PLAYSTATION);
    CHECK(cw::console_for_core("swanstation_libretro.so") == RC_CONSOLE_PLAYSTATION);
    CHECK(cw::console_for_core("/x/snes9x_libretro.so") == RC_CONSOLE_UNKNOWN);
    CHECK(cw::console_for_core("") == RC_CONSOLE_UNKNOWN);
}

TEST_CASE("Without a memory map, an unknown core's RAM is one flat block") {
    LoadedCore t;
    REQUIRE(t.core.console_id() == RC_CONSOLE_UNKNOWN); // libcw_test_core.so is not in the table
    const cw::MemoryReader& memory = t.core.memory();
    CHECK(memory.size() == 64 * 1024); // the test core's RETRO_MEMORY_SYSTEM_RAM

    t.core.run_frame();
    t.core.run_frame();
    CHECK(cw::read_le(memory, 0, 4) == 2u); // the test core's frame counter, offset 0
    CHECK_FALSE(cw::read_le(memory, 64 * 1024, 1));

    const auto regions = memory.regions();
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].end == 64 * 1024 - 1);
    CHECK(regions[0].available);
}

TEST_CASE("A PS1 memory map lands on the RetroAchievements PS1 addresses") {
    LoadedCore t("ps1");
    REQUIRE(t.core.rebuild_memory(RC_CONSOLE_PLAYSTATION));
    const cw::MemoryReader& memory = t.core.memory();

    // rcheevos' PS1 layout: Kernel RAM 0x000000-0x00FFFF, System RAM 0x010000-0x1FFFFF (both in
    // the 2 MiB main RAM), Scratchpad 0x200000-0x2003FF (real address 0x1F800000).
    CHECK(memory.size() == 0x200400);
    CHECK(cw::read_le(memory, 0x000000, 1) == 0x11u);
    CHECK(cw::read_le(memory, 0x00FFFF, 1) == 0x22u);
    CHECK(cw::read_le(memory, 0x010000, 1) == 0x33u);
    CHECK(cw::read_le(memory, 0x1FFFFF, 1) == 0x44u);
    CHECK(cw::read_le(memory, 0x200000, 1) == 0x55u);
    CHECK(cw::read_le(memory, 0x2003FF, 1) == 0x66u);

    // A value spanning two regions is assembled from both (main RAM's last byte + scratchpad).
    CHECK(cw::read_le(memory, 0x1FFFFF, 2) == 0x5544u);

    // Past the end: nothing is read, and nothing past the scratchpad is touched.
    std::array<uint8_t, 4> out{0xEE, 0xEE, 0xEE, 0xEE};
    CHECK(memory.read(0x200400, out) == 0);
    CHECK(memory.read(0x2003FE, out) == 2); // stops at the end of the address space
    CHECK(out[0] == 0x00);
    CHECK(out[1] == 0x66);
    CHECK(out[2] == 0xEE);
    CHECK_FALSE(cw::read_le(memory, 0xFFFFFFFF, 1));

    const auto regions = memory.regions();
    REQUIRE(regions.size() == 3);
    CHECK(regions[0].description == "Kernel RAM");
    CHECK(regions[1].start == 0x010000);
    CHECK(regions[1].end == 0x1FFFFF);
    CHECK(regions[2].description == "Scratchpad RAM");
    CHECK(regions[2].available);
    CHECK(regions[0].writable);
    CHECK(regions[1].writable);
    CHECK(regions[2].writable);
}

TEST_CASE("Writing to LibretroMemory changes the mapped core RAM") {
    LoadedCore t("ps1");
    REQUIRE(t.core.rebuild_memory(RC_CONSOLE_PLAYSTATION));

    const std::array<uint8_t, 3> replacement{0xA1, 0xB2, 0xC3};
    CHECK(t.core.memory_writer().write(0x000100, replacement) == cw::MemoryWriteError::None);

    std::array<uint8_t, 3> observed{};
    CHECK(t.core.memory().read(0x000100, observed) == observed.size());
    CHECK(observed == replacement);

    const std::array<uint8_t, 2> crossing{0xEE, 0xFF};
    CHECK(t.core.memory_writer().write(0x00FFFF, crossing) ==
          cw::MemoryWriteError::BoundaryCrossing);
    CHECK(cw::read_le(t.core.memory(), 0x00FFFF, 1) == 0x22U);
    CHECK(cw::read_le(t.core.memory(), 0x010000, 1) == 0x33U);
}

TEST_CASE("The memory map disappears with the game, so nothing can read freed memory") {
    LoadedCore t("ps1");
    REQUIRE(t.core.rebuild_memory(RC_CONSOLE_PLAYSTATION));
    REQUIRE(t.core.memory().size() > 0);

    t.core.unload();
    std::array<uint8_t, 1> one{};
    CHECK(t.core.memory().size() == 0);
    CHECK(t.core.memory().read(0, one) == 0);
    CHECK(t.core.memory().regions().empty());
    CHECK_FALSE(t.core.rebuild_memory(RC_CONSOLE_PLAYSTATION)); // no game: nothing to map
}

TEST_CASE("A memory map announced by a rejected load is not reused for the next game") {
    SyntheticContent content;
    cw::LibretroCore core;
    std::string error;
    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.set_option("cw_test_memory_map", "ps1"));
    REQUIRE(core.set_option("cw_test_fail_load", "after_map"));
    CHECK_FALSE(core.load_game(content.path(), error)); // the core sent its map, then said no

    REQUIRE(core.set_option("cw_test_memory_map", "none"));
    REQUIRE(core.set_option("cw_test_fail_load", "disabled"));
    REQUIRE(core.load_game(content.path(), error));
    CHECK(core.memory_descriptors().empty());
    CHECK(core.memory().size() == 64 * 1024); // the flat RAM of this game, not the old PS1 map
}

TEST_CASE("An empty memory map falls back to retro_get_memory_data") {
    LoadedCore t("empty"); // SET_MEMORY_MAPS with descriptors = NULL, num_descriptors = 0
    CHECK(t.core.memory_descriptors().empty());
    CHECK(t.core.memory().size() == 64 * 1024);
}

TEST_CASE("Descriptor strings are copied, not borrowed from the core") {
    LoadedCore t("ps1");
    const auto descriptors = t.core.memory_descriptors();
    REQUIRE(descriptors.size() == 2);
    // The test core overwrote its own buffer with "XXXXXXX" right after SET_MEMORY_MAPS.
    REQUIRE(descriptors[0].addrspace != nullptr);
    CHECK(std::string(descriptors[0].addrspace) == "ram");
    CHECK(descriptors[1].addrspace == nullptr);
}

TEST_CASE("A region is available only if all of it is mapped") {
    LoadedCore t("ps1_partial"); // main RAM descriptor covers 1 MiB, no scratchpad
    REQUIRE(t.core.rebuild_memory(RC_CONSOLE_PLAYSTATION));
    const cw::MemoryReader& memory = t.core.memory();

    const auto regions = memory.regions();
    REQUIRE(regions.size() == 3);
    CHECK(regions[0].description == "Kernel RAM");
    CHECK(regions[0].available);       // 0x000000-0x00FFFF: inside the mapped 1 MiB
    CHECK_FALSE(regions[1].available); // 0x010000-0x1FFFFF: only up to 0x0FFFFF is mapped
    CHECK_FALSE(regions[2].available); // scratchpad: not announced at all

    CHECK(cw::read_le(memory, 0x0FFFFF, 1).has_value());
    CHECK_FALSE(cw::read_le(memory, 0x100000, 1));
    CHECK_FALSE(cw::read_le(memory, 0x200000, 1));
    CHECK_FALSE(regions[1].writable);
    CHECK_FALSE(regions[2].writable);
    CHECK(t.core.memory_writer().write(0x010000, std::array<uint8_t, 1>{0x99}) ==
          cw::MemoryWriteError::Unavailable);
}
