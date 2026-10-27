#pragma once

#include "libretro_core.hpp"

#include <filesystem>
#include <set>
#include <string>

namespace cw {

// Save-state slots on disk: one RASTATE file per game, core and slot, written atomically.
class SaveStates {
  public:
    static constexpr int kSlotCount = 4;

    // `dir` holds the state files; an empty path disables saving (no usable data directory).
    explicit SaveStates(std::filesystem::path dir) : dir_(std::move(dir)) {}

    // "<game file stem>-<core name>.state<slot>", as RALibretro names its slots
    // (reference/RALibretro/src/States.cpp, getStatePath). Empty if saving is disabled, no game
    // is loaded or `slot` is outside 1..kSlotCount.
    std::filesystem::path slot_path(const LibretroCore& core, int slot) const;

    // Both return false with a user-facing reason in `message`, or true with a confirmation.
    bool save(LibretroCore& core, int slot, std::string& message);
    bool load(LibretroCore& core, int slot, std::string& message);

  private:
    std::filesystem::path dir_;
    // For cores with RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION: states saved by this run.
    std::set<std::filesystem::path> saved_this_session_;
};

} // namespace cw
