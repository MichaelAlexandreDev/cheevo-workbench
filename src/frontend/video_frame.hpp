#pragma once

#include <libretro.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cw {

// The last image a core produced, converted to RGBA8: 4 bytes per pixel in memory order
// R, G, B, A, rows tightly packed (no padding). Independent of OpenGL so it can be tested.
struct VideoFrame {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint8_t> rgba;
    // Incremented for every new image; a duplicated frame (data == NULL) keeps the value, so
    // the renderer knows it does not need to upload anything.
    uint64_t serial = 0;
};

// Bytes per pixel of a libretro software format, or 0 if the format is not supported.
unsigned bytes_per_pixel(retro_pixel_format format);

// Converts one libretro software frame to RGBA8 into `out` (resized as needed).
// `pitch` is the length of a source row in BYTES and may be larger than width × bytes per pixel
// (padding at the end of each row). Returns false for an unsupported format or a pitch that is
// too small for `width`; `out` is left untouched in that case.
bool convert_to_rgba(const void* data, unsigned width, unsigned height, size_t pitch,
                     retro_pixel_format format, std::vector<uint8_t>& out);

} // namespace cw
