#include "libretro_memory.hpp"

#include <SDL3/SDL_log.h>
#include <rc_consoles.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace cw {

namespace {

// Core file name (without ".so") -> console, for the cores this project is tested with. The
// mapping is the same as in RALibretro's etc/cores.json ("systems": [12] = PlayStation).
constexpr std::array<std::pair<std::string_view, uint32_t>, 4> kKnownCores = {{
    {"mednafen_psx_libretro", RC_CONSOLE_PLAYSTATION},
    {"mednafen_psx_hw_libretro", RC_CONSOLE_PLAYSTATION},
    {"swanstation_libretro", RC_CONSOLE_PLAYSTATION},
    {"pcsx_rearmed_libretro", RC_CONSOLE_PLAYSTATION},
}};

// True if every address of [start, end] is backed by core memory. rc_libretro may put unmapped
// "filler" blocks inside a console region, so checking only `start` is not enough.
bool fully_available(const rc_libretro_memory_regions_t& regions, uint32_t start, uint32_t end) {
    uint64_t address = start;
    while (address <= end) {
        uint32_t avail = 0;
        if (!rc_libretro_memory_find_avail(&regions, static_cast<uint32_t>(address), &avail) ||
            avail == 0) {
            return false;
        }
        address += avail;
    }
    return true;
}

void RC_CCONV log_rc_libretro(const char* message) {
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "[memory] %s", message);
}

} // namespace

uint32_t console_for_core(const std::filesystem::path& core_path) {
    const std::string stem = core_path.stem().string();
    for (const auto& [name, console] : kKnownCores) {
        if (stem == name) {
            return console;
        }
    }
    return RC_CONSOLE_UNKNOWN;
}

bool LibretroMemory::init(const retro_memory_map* map,
                          rc_libretro_get_core_memory_info_func core_memory, uint32_t console_id) {
    // rc_libretro reports regions it could not map through this (global) callback.
    rc_libretro_init_verbose_message_callback(&log_rc_libretro);
    clear();
    console_id_ = console_id;
    if (!rc_libretro_memory_init(&regions_, map, core_memory, console_id)) {
        clear();
        return false;
    }
    return true;
}

void LibretroMemory::clear() {
    rc_libretro_memory_destroy(&regions_);
    regions_ = {};
    console_id_ = 0;
}

uint32_t LibretroMemory::size() const {
    return static_cast<uint32_t>(
        std::min<size_t>(regions_.total_size, std::numeric_limits<uint32_t>::max()));
}

size_t LibretroMemory::read(uint32_t address, std::span<uint8_t> out) const {
    if (address >= size() || out.empty()) {
        return 0;
    }
    const auto count =
        static_cast<uint32_t>(std::min<size_t>(out.size(), std::numeric_limits<uint32_t>::max()));
    return rc_libretro_memory_read(&regions_, address, out.data(), count);
}

std::vector<MemoryRegionInfo> LibretroMemory::regions() const {
    std::vector<MemoryRegionInfo> result;
    const rc_memory_regions_t* console = rc_console_memory_regions(console_id_);
    if (console && console->num_regions > 0) {
        // The console's documented blocks; available = the core exposes every byte of it.
        for (uint32_t i = 0; i < console->num_regions; ++i) {
            const rc_memory_region_t& r = console->region[i];
            if (r.start_address >= size()) {
                break;
            }
            result.push_back(
                {.start = r.start_address,
                 .end = r.end_address,
                 .description = r.description ? r.description : "",
                 .available = r.end_address < size() &&
                              fully_available(regions_, r.start_address, r.end_address),
                 .writable = r.end_address < size() && r.type != RC_MEMORY_TYPE_READONLY &&
                             fully_available(regions_, r.start_address, r.end_address)});
        }
    } else if (size() > 0) {
        // Unknown console: rc_libretro exposed the core's RAM as one flat block.
        result.push_back({.start = 0,
                          .end = size() - 1,
                          .description = "System RAM",
                          .available = fully_available(regions_, 0, size() - 1),
                          .writable = fully_available(regions_, 0, size() - 1)});
    }
    return result;
}

MemoryWriteError LibretroMemory::write(uint32_t address, std::span<const uint8_t> bytes) {
    // The frontend calls this on its main thread after retro_run() has returned, between frames.
    const std::vector<MemoryRegionInfo> memory_regions = regions();
    const MemoryWriteError validation =
        validate_memory_write(address, bytes.size(), size(), memory_regions);
    if (validation != MemoryWriteError::None) {
        return validation;
    }

    uint32_t available = 0;
    uint8_t* destination = rc_libretro_memory_find_avail(&regions_, address, &available);
    if (!destination) {
        return MemoryWriteError::Unavailable;
    }
    if (bytes.size() > available) {
        return MemoryWriteError::BoundaryCrossing;
    }

    std::memmove(destination, bytes.data(), bytes.size());
    return MemoryWriteError::None;
}

} // namespace cw
