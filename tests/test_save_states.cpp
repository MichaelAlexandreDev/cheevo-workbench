// Save states end to end: LibretroCore (de)serialization and slot files, using the test core.

#include "libretro_core.hpp"
#include "save_states.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using cw_test::SyntheticContent;
using cw_test::TempDir;
using cw_test::test_core_path;

// The test core keeps its frame counter in the first 4 bytes of system RAM (little-endian).
uint32_t core_frame_counter(const cw::LibretroCore& core) {
    REQUIRE(core.system_ram_size() >= 4);
    const uint8_t* ram = core.system_ram_data();
    return uint32_t{ram[0]} | uint32_t{ram[1]} << 8 | uint32_t{ram[2]} << 16 |
           uint32_t{ram[3]} << 24;
}

std::vector<uint8_t> ram_copy(const cw::LibretroCore& core) {
    const uint8_t* ram = core.system_ram_data();
    return {ram, ram + core.system_ram_size()};
}

void run_frames(cw::LibretroCore& core, int count) {
    for (int i = 0; i < count; ++i) {
        core.run_frame();
    }
}

struct LoadedCore {
    SyntheticContent content;
    cw::LibretroCore core;

    explicit LoadedCore(bool quirks = false) {
        std::string error;
        REQUIRE(core.load(test_core_path(), {}, error));
        if (quirks) {
            REQUIRE(core.set_option("cw_test_serialization_quirks", "enabled"));
        }
        REQUIRE(core.load_game(content.path(), error));
    }
};

} // namespace

TEST_CASE("A state restores the exact core state after the game advanced") {
    LoadedCore t;
    run_frames(t.core, 3);
    std::vector<uint8_t> state;
    std::string error;
    REQUIRE(t.core.serialize_state(state, error));
    const std::vector<uint8_t> ram_at_frame_3 = ram_copy(t.core);

    run_frames(t.core, 5);
    REQUIRE(core_frame_counter(t.core) == 8);

    REQUIRE(t.core.unserialize_state(state, error));
    CHECK(core_frame_counter(t.core) == 3);
    CHECK(ram_copy(t.core) == ram_at_frame_3);

    t.core.run_frame(); // the game continues from the restored point
    CHECK(core_frame_counter(t.core) == 4);
}

TEST_CASE("A state of the wrong size never reaches the core") {
    LoadedCore t;
    run_frames(t.core, 2);
    std::vector<uint8_t> state;
    std::string error;
    REQUIRE(t.core.serialize_state(state, error));
    const std::vector<uint8_t> ram_before = ram_copy(t.core);

    state.pop_back();
    CHECK_FALSE(t.core.unserialize_state(state, error));
    CHECK(error.find("bytes") != std::string::npos);
    CHECK(ram_copy(t.core) == ram_before); // the running game is untouched

    t.core.run_frame();
    CHECK(core_frame_counter(t.core) == 3);
}

TEST_CASE("Unknown serialization quirks are cleared and variable size is acknowledged") {
    LoadedCore plain;
    CHECK(plain.core.serialization_quirks() == 0);
    plain.core.unload();

    LoadedCore t(/*quirks=*/true);
    CHECK(t.core.serialization_quirks() ==
          (RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE | RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION |
           RETRO_SERIALIZATION_QUIRK_FRONT_VARIABLE_SIZE));
}

TEST_CASE("Slots save to disk, load back and report empty or broken files") {
    TempDir dir;
    LoadedCore t;
    cw::SaveStates states(dir.path());
    std::string message;

    const auto path = states.slot_path(t.core, 1);
    CHECK(path.filename() == "synthetic-game-cw-test-core.state1");

    run_frames(t.core, 3);
    REQUIRE(states.save(t.core, 1, message));
    CHECK(message == "Saved slot 1");
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp")); // atomic write cleaned up

    run_frames(t.core, 4);
    REQUIRE(states.load(t.core, 1, message));
    CHECK(core_frame_counter(t.core) == 3);

    CHECK_FALSE(states.load(t.core, 2, message));
    CHECK(message == "Slot 2 is empty");

    SUBCASE("a truncated file on disk is rejected and the game keeps running") {
        const auto size = std::filesystem::file_size(path);
        std::filesystem::resize_file(path, size / 2);
        run_frames(t.core, 2);
        const std::vector<uint8_t> ram_before = ram_copy(t.core);
        CHECK_FALSE(states.load(t.core, 1, message));
        CHECK(message.find("Could not load slot 1") == 0);
        CHECK(ram_copy(t.core) == ram_before);
    }
}

TEST_CASE("Single-session states only load in the session that saved them") {
    TempDir dir;
    LoadedCore t(/*quirks=*/true);
    std::string message;
    {
        cw::SaveStates first_session(dir.path());
        REQUIRE(first_session.save(t.core, 1, message));
        CHECK(first_session.load(t.core, 1, message));
    }
    cw::SaveStates second_session(dir.path()); // e.g. after restarting the program
    CHECK_FALSE(second_session.load(t.core, 1, message));
    CHECK(message.find("session") != std::string::npos);
}

TEST_CASE("Save states are refused without a data directory or a game") {
    cw::SaveStates disabled({});
    LoadedCore t;
    std::string message;
    CHECK_FALSE(disabled.save(t.core, 1, message));
    CHECK(message.find("disabled") != std::string::npos);

    cw::LibretroCore no_game;
    cw::SaveStates states("/nonexistent-dir-for-test");
    CHECK_FALSE(states.save(no_game, 1, message));
    CHECK(message == "No game loaded");
}
