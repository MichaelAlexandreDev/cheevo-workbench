#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cw {

// Save-state file container. We write RALibretro's "RASTATE" format so state files stay
// interchangeable with the official toolkit (verified against
// reference/RALibretro/src/States.cpp, saveState / loadRAState1):
//
//   "RASTATE" + version byte (1)
//   blocks: 4-char tag, uint32 little-endian size, data padded with zeros to a multiple of 8
//     "MEM "  the core's retro_serialize() data          (required)
//     "ACHV"  rcheevos runtime progress                  (step 3.3)
//     "SCRN"  screenshot (PNG)                           (skipped when reading)
//     "END "  size 0, terminates the list
//
// Files without the "RASTATE" magic are "raw" states: the core data and nothing else (the old
// RALibretro format, also what many front-ends write).

// Largest data accepted in a block: the size field is 32 bits, and a state over 256 MiB is not
// realistic (Beetle PSX: ~3.7 MB). Savers must check this before calling write_state_file().
inline constexpr size_t kMaxStateBlockSize = 256u * 1024 * 1024;

struct StateContents {
    std::vector<uint8_t> core;
    std::optional<std::vector<uint8_t>> achievements;
};

// Precondition: each span is at most kMaxStateBlockSize bytes.
std::vector<uint8_t> write_state_file(std::span<const uint8_t> core_data,
                                      std::span<const uint8_t> achievements = {});

// Parses a state file. Every block size is checked against the file size, so truncated or
// corrupt files are rejected instead of reading past the end. Returns nullopt and fills `error`
// for an unknown version, a truncated header/block, a missing "END " or a missing "MEM " block.
std::optional<StateContents> parse_state_file(std::span<const uint8_t> file, std::string& error);

} // namespace cw
