// Save states end to end: LibretroCore (de)serialization and slot files, using the test core.

#include "cw/devtools/logic_tester.hpp"
#include "libretro_core.hpp"
#include "save_state.hpp"
#include "save_states.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
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

bool write_file(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool write_logic_byte(cw::LibretroCore& core, uint32_t address, uint8_t value) {
    return core.memory_writer().write(address, std::span<const uint8_t>(&value, 1)) ==
           cw::MemoryWriteError::None;
}

// The synthetic core overwrites RAM every frame. Set one test byte after retro_run(), then
// evaluate that frame just as the frontend does after a real game frame.
bool run_hit_frame(cw::LibretroCore& core, cw::LogicTester& tester) {
    core.run_frame();
    if (!write_logic_byte(core, 0x10, 1)) {
        return false;
    }
    tester.do_frame(core.memory(), core.frame_count());
    return true;
}

void progress_two_hits(cw::LibretroCore& core, cw::LogicTester& tester) {
    tester.do_frame(core.memory(), core.frame_count()); // false once: Waiting -> Active
    REQUIRE(run_hit_frame(core, tester));
    REQUIRE(run_hit_frame(core, tester));
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

    // ENDIAN_DEPENDENT and the unknown bit are cleared: we cannot enforce them.
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

TEST_CASE("Slot numbers outside 1..4 are refused") {
    TempDir dir;
    LoadedCore t;
    cw::SaveStates states(dir.path());
    std::string message;
    for (const int slot : {0, -1, cw::SaveStates::kSlotCount + 1}) {
        CAPTURE(slot);
        CHECK(states.slot_path(t.core, slot).empty());
        CHECK_FALSE(states.save(t.core, slot, message));
        CHECK(message == "Invalid slot " + std::to_string(slot));
        CHECK_FALSE(states.load(t.core, slot, message));
    }
    CHECK(std::filesystem::is_empty(dir.path())); // nothing was written
}

TEST_CASE("Save states restore exact core RAM and measured achievement progress") {
    TempDir dir;
    LoadedCore t;
    cw::SaveStates states(dir.path());
    cw::LogicTester tester;
    std::string error;
    std::string message;
    const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
    REQUIRE_MESSAGE(id, error);

    progress_two_hits(t.core, tester);
    const std::vector<uint8_t> ram_at_save = ram_copy(t.core);
    const auto measured_at_save = tester.measured(*id);
    REQUIRE(measured_at_save);
    CHECK(measured_at_save->first == 2);
    CHECK(measured_at_save->second == 5);

    REQUIRE(states.save(t.core, 1, message, &tester));
    CHECK(message == "Saved slot 1");

    REQUIRE(run_hit_frame(t.core, tester));
    REQUIRE(run_hit_frame(t.core, tester));
    REQUIRE(run_hit_frame(t.core, tester));
    CHECK(tester.state(*id) == cw::LogicState::Triggered);

    REQUIRE(states.load(t.core, 1, message, &tester));
    CHECK(message == "Loaded slot 1");
    CHECK(core_frame_counter(t.core) == 2);
    CHECK(ram_copy(t.core) == ram_at_save);
    CHECK(tester.state(*id) == cw::LogicState::Active);
    const auto restored = tester.measured(*id);
    REQUIRE(restored);
    CHECK(restored->first == 2);
    CHECK(restored->second == 5);

    REQUIRE(run_hit_frame(t.core, tester));
    const auto after_next_frame = tester.measured(*id);
    REQUIRE(after_next_frame);
    CHECK(after_next_frame->first == 3);
    CHECK(tester.state(*id) == cw::LogicState::Active);
    REQUIRE(run_hit_frame(t.core, tester));
    REQUIRE(run_hit_frame(t.core, tester));
    CHECK(tester.state(*id) == cw::LogicState::Triggered);
}

TEST_CASE("Achievement progress is stored independently in each slot") {
    TempDir dir;
    LoadedCore t;
    cw::SaveStates states(dir.path());
    cw::LogicTester tester;
    std::string error;
    std::string message;
    const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(t.core.memory(), t.core.frame_count()); // Waiting -> Active
    REQUIRE(run_hit_frame(t.core, tester));
    const std::vector<uint8_t> slot1_ram = ram_copy(t.core);
    REQUIRE(states.save(t.core, 1, message, &tester));

    REQUIRE(run_hit_frame(t.core, tester));
    const std::vector<uint8_t> slot2_ram = ram_copy(t.core);
    REQUIRE(states.save(t.core, 2, message, &tester));
    CHECK(slot1_ram != slot2_ram);

    REQUIRE(run_hit_frame(t.core, tester)); // live runtime is now at three hits
    REQUIRE(states.load(t.core, 1, message, &tester));
    CHECK(core_frame_counter(t.core) == 1);
    CHECK(ram_copy(t.core) == slot1_ram);
    CHECK(tester.state(*id) == cw::LogicState::Active);
    auto measured = tester.measured(*id);
    REQUIRE(measured);
    CHECK(measured->first == 1);

    REQUIRE(states.load(t.core, 2, message, &tester));
    CHECK(core_frame_counter(t.core) == 2);
    CHECK(ram_copy(t.core) == slot2_ram);
    CHECK(tester.state(*id) == cw::LogicState::Active);
    measured = tester.measured(*id);
    REQUIRE(measured);
    CHECK(measured->first == 2);
}

TEST_CASE("Legacy states without an ACHV block reset runtime progress with a warning") {
    SUBCASE("RASTATE MEM-only file") {
        TempDir dir;
        LoadedCore t;
        cw::SaveStates states(dir.path());
        cw::LogicTester tester;
        std::string error;
        std::string message;
        const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
        REQUIRE_MESSAGE(id, error);
        progress_two_hits(t.core, tester);

        std::vector<uint8_t> core_state;
        REQUIRE(t.core.serialize_state(core_state, error));
        const std::vector<uint8_t> saved_ram = ram_copy(t.core);
        REQUIRE(write_file(states.slot_path(t.core, 1), cw::write_state_file(core_state)));

        REQUIRE(run_hit_frame(t.core, tester));
        REQUIRE(states.load(t.core, 1, message, &tester));
        CHECK(message.find("progress reset") != std::string::npos);
        CHECK(core_frame_counter(t.core) == 2);
        CHECK(ram_copy(t.core) == saved_ram);
        CHECK(tester.state(*id) == cw::LogicState::Waiting);
        const auto measured = tester.measured(*id);
        REQUIRE(measured);
        CHECK(measured->first == 0);
    }

    SUBCASE("raw legacy core state") {
        TempDir dir;
        LoadedCore t;
        cw::SaveStates states(dir.path());
        cw::LogicTester tester;
        std::string error;
        std::string message;
        const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
        REQUIRE_MESSAGE(id, error);
        progress_two_hits(t.core, tester);

        std::vector<uint8_t> core_state;
        REQUIRE(t.core.serialize_state(core_state, error));
        const std::vector<uint8_t> saved_ram = ram_copy(t.core);
        REQUIRE(write_file(states.slot_path(t.core, 1), core_state));

        REQUIRE(run_hit_frame(t.core, tester));
        REQUIRE(states.load(t.core, 1, message, &tester));
        CHECK(message.find("progress reset") != std::string::npos);
        CHECK(core_frame_counter(t.core) == 2);
        CHECK(ram_copy(t.core) == saved_ram);
        CHECK(tester.state(*id) == cw::LogicState::Waiting);
        const auto measured = tester.measured(*id);
        REQUIRE(measured);
        CHECK(measured->first == 0);
    }
}

TEST_CASE("Corrupt ACHV progress resets the tester but still loads a valid core state") {
    TempDir dir;
    LoadedCore t;
    cw::SaveStates states(dir.path());
    cw::LogicTester tester;
    std::string error;
    std::string message;
    const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
    REQUIRE_MESSAGE(id, error);
    progress_two_hits(t.core, tester);
    const std::vector<uint8_t> saved_ram = ram_copy(t.core);
    REQUIRE(states.save(t.core, 1, message, &tester));

    const auto path = states.slot_path(t.core, 1);
    const std::vector<uint8_t> original_file = read_file(path);
    const auto parsed = cw::parse_state_file(original_file, error);
    REQUIRE(parsed);
    REQUIRE(parsed->achievements);
    const std::vector<uint8_t> corrupt_progress{0xDE, 0xAD, 0xBE, 0xEF};
    const std::vector<uint8_t> corrupt_file = cw::write_state_file(parsed->core, corrupt_progress);
    const auto still_valid_container = cw::parse_state_file(corrupt_file, error);
    REQUIRE(still_valid_container);
    REQUIRE(still_valid_container->achievements);
    CHECK(*still_valid_container->achievements == corrupt_progress);
    REQUIRE(write_file(path, corrupt_file));

    REQUIRE(run_hit_frame(t.core, tester));
    REQUIRE(states.load(t.core, 1, message, &tester));
    CHECK(message.find("progress reset") != std::string::npos);
    CHECK(core_frame_counter(t.core) == 2);
    CHECK(ram_copy(t.core) == saved_ram);
    CHECK(tester.state(*id) == cw::LogicState::Waiting);
    const auto measured = tester.measured(*id);
    REQUIRE(measured);
    CHECK(measured->first == 0);
}

TEST_CASE("Malformed containers leave the core and tester untouched") {
    TempDir dir;
    LoadedCore t;
    cw::SaveStates states(dir.path());
    cw::LogicTester tester;
    std::string error;
    std::string message;
    const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
    REQUIRE_MESSAGE(id, error);
    progress_two_hits(t.core, tester);
    REQUIRE(write_file(states.slot_path(t.core, 1),
                       std::vector<uint8_t>{'R', 'A', 'S', 'T', 'A', 'T', 'E'}));

    t.core.run_frame(); // leave the live game beyond the file's attempted load point
    const std::vector<uint8_t> ram_before_load = ram_copy(t.core);
    CHECK_FALSE(states.load(t.core, 1, message, &tester));
    CHECK(message.find("Could not load slot 1") == 0);
    CHECK(core_frame_counter(t.core) == 3);
    CHECK(ram_copy(t.core) == ram_before_load);
    CHECK(tester.state(*id) == cw::LogicState::Active);
    const auto measured = tester.measured(*id);
    REQUIRE(measured);
    CHECK(measured->first == 2);
}

TEST_CASE("A container with an incorrect core size leaves tester progress untouched") {
    TempDir dir;
    LoadedCore t;
    cw::SaveStates states(dir.path());
    cw::LogicTester tester;
    std::string error;
    std::string message;
    const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
    REQUIRE_MESSAGE(id, error);
    progress_two_hits(t.core, tester);

    std::vector<uint8_t> short_core_state;
    REQUIRE(t.core.serialize_state(short_core_state, error));
    short_core_state.pop_back();
    std::vector<uint8_t> progress;
    REQUIRE(tester.serialize_progress(progress, error));
    const std::vector<uint8_t> file = cw::write_state_file(short_core_state, progress);
    const auto parsed = cw::parse_state_file(file, error);
    REQUIRE(parsed);
    REQUIRE(parsed->achievements);
    REQUIRE(write_file(states.slot_path(t.core, 1), file));

    REQUIRE(run_hit_frame(t.core, tester));
    REQUIRE(run_hit_frame(t.core, tester)); // live progress is now four hits; file has two
    const std::vector<uint8_t> ram_before_load = ram_copy(t.core);
    CHECK_FALSE(states.load(t.core, 1, message, &tester));
    CHECK(message.find("Could not load slot 1") == 0);
    CHECK(message.find("bytes") != std::string::npos);
    CHECK(core_frame_counter(t.core) == 4);
    CHECK(ram_copy(t.core) == ram_before_load);
    CHECK(tester.state(*id) == cw::LogicState::Active);
    const auto measured = tester.measured(*id);
    REQUIRE(measured);
    CHECK(measured->first == 4);
}

TEST_CASE("Loaded core memory is revalidated against achievement addresses") {
    TempDir dir;
    LoadedCore t;
    cw::SaveStates states(dir.path());
    cw::LogicTester tester;
    std::string error;
    std::string message;
    const auto id = tester.add("Outside RAM", "0xH10000=1", error);
    REQUIRE_MESSAGE(id, error);
    run_frames(t.core, 1);
    REQUIRE(states.save(t.core, 1, message, &tester));
    run_frames(t.core, 1);

    REQUIRE(states.load(t.core, 1, message, &tester));
    CHECK(message == "Loaded slot 1");
    CHECK(tester.state(*id) == cw::LogicState::Disabled);
}
