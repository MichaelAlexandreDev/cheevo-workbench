#pragma once

#include "cw/devtools/memory.hpp"

#include <libretro.h>
#include <rc_libretro.h>

#include <cstdint>
#include <filesystem>

namespace cw {

// RetroAchievements console id (rc_consoles.h) for a core, from its file name like RALibretro's
// etc/cores.json does ("mednafen_psx_libretro.so" -> PlayStation). RC_CONSOLE_UNKNOWN (0) when the
// core is not known; its memory is then exposed as a flat block (see LibretroMemory::init).
uint32_t console_for_core(const std::filesystem::path& core_path);

// The loaded core's memory in RetroAchievements addresses, mapped by rcheevos' rc_libretro from
// the core's SET_MEMORY_MAPS descriptors (or retro_get_memory_data when it has none).
//
// The regions point straight into the core's memory, so they are only valid while the game is
// loaded: LibretroCore owns this object and clears it before unloading.
class LibretroMemory final : public MemoryReader, public MemoryWriter {
  public:
    // `map` may be null. Returns false (and exposes nothing) if no region could be mapped.
    bool init(const retro_memory_map* map, rc_libretro_get_core_memory_info_func core_memory,
              uint32_t console_id);
    void clear();

    uint32_t console_id() const { return console_id_; }

    uint32_t size() const override;
    size_t read(uint32_t address, std::span<uint8_t> out) const override;
    std::vector<MemoryRegionInfo> regions() const override;
    MemoryWriteError write(uint32_t address, std::span<const uint8_t> bytes) override;

  private:
    rc_libretro_memory_regions_t regions_{};
    uint32_t console_id_ = 0;
};

} // namespace cw
