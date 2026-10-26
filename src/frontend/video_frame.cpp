#include "video_frame.hpp"

#include <cstring>

namespace cw {

namespace {

// Widens a 5- or 6-bit channel to 8 bits by repeating its top bits in the new low bits, so
// that the maximum maps to 255 (0x1F → 0xFF) instead of 248.
uint8_t expand5(unsigned v) {
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}
uint8_t expand6(unsigned v) {
    return static_cast<uint8_t>((v << 2) | (v >> 4));
}

// Pixels are native-endian 16/32-bit values (libretro.h). memcpy reads them from a byte
// pointer without breaking alignment rules.
template <typename T> T load(const uint8_t* p) {
    T value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

} // namespace

unsigned bytes_per_pixel(retro_pixel_format format) {
    switch (format) {
    case RETRO_PIXEL_FORMAT_0RGB1555:
    case RETRO_PIXEL_FORMAT_RGB565:
        return 2;
    case RETRO_PIXEL_FORMAT_XRGB8888:
        return 4;
    default:
        return 0;
    }
}

bool convert_to_rgba(const void* data, unsigned width, unsigned height, size_t pitch,
                     retro_pixel_format format, std::vector<uint8_t>& out) {
    const unsigned bpp = bytes_per_pixel(format);
    if (!data || bpp == 0 || pitch < static_cast<size_t>(width) * bpp) {
        return false;
    }
    out.resize(static_cast<size_t>(width) * height * 4);

    const auto* src = static_cast<const uint8_t*>(data);
    uint8_t* dst = out.data();
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t* row = src + y * pitch; // pitch, not width: skips the row padding
        for (unsigned x = 0; x < width; ++x, dst += 4) {
            const uint8_t* p = row + static_cast<size_t>(x) * bpp;
            switch (format) {
            case RETRO_PIXEL_FORMAT_0RGB1555: { // 0 RRRRR GGGGG BBBBB
                const unsigned v = load<uint16_t>(p);
                dst[0] = expand5((v >> 10) & 0x1F);
                dst[1] = expand5((v >> 5) & 0x1F);
                dst[2] = expand5(v & 0x1F);
                break;
            }
            case RETRO_PIXEL_FORMAT_RGB565: { // RRRRR GGGGGG BBBBB
                const unsigned v = load<uint16_t>(p);
                dst[0] = expand5((v >> 11) & 0x1F);
                dst[1] = expand6((v >> 5) & 0x3F);
                dst[2] = expand5(v & 0x1F);
                break;
            }
            default: { // XRGB8888: the top byte is unused
                const uint32_t v = load<uint32_t>(p);
                dst[0] = static_cast<uint8_t>(v >> 16);
                dst[1] = static_cast<uint8_t>(v >> 8);
                dst[2] = static_cast<uint8_t>(v);
                break;
            }
            }
            dst[3] = 0xFF; // libretro frames are opaque
        }
    }
    return true;
}

} // namespace cw
