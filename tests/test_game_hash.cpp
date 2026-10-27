// RetroAchievements hash of PS1 content, on a synthetic disc generated here (no real game data).

#include "cw/devtools/game_hash.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>
#include <rc_consoles.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using cw_test::TempDir;

constexpr size_t kSector = 2048;

void put_u32_le(std::vector<uint8_t>& disc, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        disc[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

// One ISO9660 directory record: extent (first sector), size in bytes and "NAME;1".
void put_record(std::vector<uint8_t>& disc, size_t& offset, uint32_t extent, uint32_t size,
                const std::string& name) {
    const size_t length = (33 + name.size() + 1) & ~size_t{1}; // records have an even length
    disc[offset] = static_cast<uint8_t>(length);
    put_u32_le(disc, offset + 2, extent);
    put_u32_le(disc, offset + 10, size);
    disc[offset + 32] = static_cast<uint8_t>(name.size());
    std::memcpy(&disc[offset + 33], name.data(), name.size());
    offset += length;
}

// A minimal PS1 data disc with 2048-byte sectors:
//   16: primary volume descriptor ("CD001", root directory at sector 18)
//   18: root directory with SYSTEM.CNF and the boot executable SLPS_123.45
//   19: SYSTEM.CNF ("BOOT = cdrom:\SLPS_123.45;1")
//   20-21: PS-X EXE: 2048-byte header declaring 2048 bytes of code, code bytes (i*13+5)&0xFF
// rcheevos hashes MD5("SLPS_123.45" + the 4096 bytes of the executable). The expected value was
// computed independently with Python's hashlib from the same bytes.
std::vector<uint8_t> make_ps1_disc(bool with_system_cnf = true) {
    std::vector<uint8_t> disc(22 * kSector, 0);

    uint8_t* pvd = &disc[16 * kSector];
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    pvd[128] = 0x00; // logical block size 2048, little-endian
    pvd[129] = 0x08;
    pvd[156] = 34; // root directory record
    put_u32_le(disc, 16 * kSector + 156 + 2, 18);
    put_u32_le(disc, 16 * kSector + 156 + 10, kSector);

    const std::string cnf = "BOOT = cdrom:\\SLPS_123.45;1\r\nTCB = 4\r\nEVENT = 10\r\n";
    size_t record = 18 * kSector;
    put_record(disc, record, 18, kSector, std::string(1, '\0')); // "."
    put_record(disc, record, 18, kSector, std::string(1, '\1')); // ".."
    if (with_system_cnf) {
        put_record(disc, record, 19, static_cast<uint32_t>(cnf.size()), "SYSTEM.CNF;1");
    }
    put_record(disc, record, 20, 2 * kSector, "SLPS_123.45;1");

    std::memcpy(&disc[19 * kSector], cnf.data(), cnf.size());

    const size_t exe = 20 * kSector;
    std::memcpy(&disc[exe], "PS-X EXE", 8);
    put_u32_le(disc, exe + 28, kSector); // code size, not counting the header
    for (size_t i = kSector; i < 2 * kSector; ++i) {
        disc[exe + i] = static_cast<uint8_t>((i * 13 + 5) & 0xFF);
    }
    return disc;
}

std::filesystem::path write_disc(const TempDir& dir, const std::vector<uint8_t>& disc) {
    std::ofstream(dir.path() / "disc.bin", std::ios::binary)
        .write(reinterpret_cast<const char*>(disc.data()),
               static_cast<std::streamsize>(disc.size()));
    const auto cue = dir.path() / "disc.cue";
    std::ofstream(cue)
        << "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n";
    return cue;
}

constexpr const char* kExpectedHash = "6afb6003b25f3df44cb644563c6da27a";

} // namespace

TEST_CASE("A synthetic PS1 disc hashes like rcheevos' PlayStation algorithm") {
    TempDir dir;
    const auto cue = write_disc(dir, make_ps1_disc());
    std::string error;

    const auto hash = cw::compute_game_hash(cue, RC_CONSOLE_PLAYSTATION, error);
    REQUIRE_MESSAGE(hash, error);
    CHECK(*hash == kExpectedHash);
    CHECK(error.empty());

    // Deterministic, and the .bin alone (no cue sheet) gives the same hash.
    CHECK(cw::compute_game_hash(cue, RC_CONSOLE_PLAYSTATION, error) == kExpectedHash);
    CHECK(cw::compute_game_hash(dir.path() / "disc.bin", RC_CONSOLE_PLAYSTATION, error) ==
          kExpectedHash);
}

TEST_CASE("Content without a bootable executable is reported, not hashed") {
    TempDir dir;
    const auto cue = write_disc(dir, make_ps1_disc(/*with_system_cnf=*/false));
    std::string error;
    CHECK_FALSE(cw::compute_game_hash(cue, RC_CONSOLE_PLAYSTATION, error));
    CHECK_FALSE(error.empty());
    MESSAGE("rc_hash said: " << error);
}

TEST_CASE("Missing files and unsupported consoles fail with a message") {
    TempDir dir;
    std::string error;
    const auto missing = dir.path() / "nope.cue";
    CHECK_FALSE(cw::compute_game_hash(missing, RC_CONSOLE_PLAYSTATION, error));
    CHECK(error == "File not found: " + missing.string());

    const auto cue = write_disc(dir, make_ps1_disc());
    CHECK_FALSE(cw::compute_game_hash(cue, RC_CONSOLE_UNKNOWN, error));
    CHECK_FALSE(error.empty());
}
