// Unit tests for the libretro pixel-format conversion (no core, no OpenGL).

#include "video_frame.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Builds a 2x2 frame whose rows are `padding` bytes longer than the pixels, with the padding
// filled with 0xEE so a converter that ignores the pitch picks up garbage.
template <typename Pixel>
std::vector<uint8_t> make_frame(const Pixel (&pixels)[4], size_t padding, size_t& pitch) {
    pitch = 2 * sizeof(Pixel) + padding;
    std::vector<uint8_t> buffer(pitch * 2, 0xEE);
    for (size_t y = 0; y < 2; ++y) {
        std::memcpy(buffer.data() + y * pitch, &pixels[y * 2], 2 * sizeof(Pixel));
    }
    return buffer;
}

void check_pixel(const std::vector<uint8_t>& rgba, size_t index, uint8_t r, uint8_t g, uint8_t b) {
    CAPTURE(index);
    CHECK(rgba[index * 4 + 0] == r);
    CHECK(rgba[index * 4 + 1] == g);
    CHECK(rgba[index * 4 + 2] == b);
    CHECK(rgba[index * 4 + 3] == 0xFF);
}

} // namespace

TEST_CASE("XRGB8888 converts exactly and ignores the unused top byte") {
    const uint32_t pixels[4] = {0xFF102030, 0x00FFFFFF, 0x00000000, 0xAB0000FF};
    size_t pitch = 0;
    const auto frame = make_frame(pixels, 8, pitch);
    std::vector<uint8_t> rgba;
    REQUIRE(cw::convert_to_rgba(frame.data(), 2, 2, pitch, RETRO_PIXEL_FORMAT_XRGB8888, rgba));
    REQUIRE(rgba.size() == 2 * 2 * 4);
    check_pixel(rgba, 0, 0x10, 0x20, 0x30);
    check_pixel(rgba, 1, 0xFF, 0xFF, 0xFF);
    check_pixel(rgba, 2, 0x00, 0x00, 0x00);
    check_pixel(rgba, 3, 0x00, 0x00, 0xFF); // second row: proves the padding was skipped
}

TEST_CASE("RGB565 expands 5/6-bit channels to the full 0-255 range") {
    const uint16_t pixels[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF}; // red, green, blue, white
    size_t pitch = 0;
    const auto frame = make_frame(pixels, 6, pitch);
    std::vector<uint8_t> rgba;
    REQUIRE(cw::convert_to_rgba(frame.data(), 2, 2, pitch, RETRO_PIXEL_FORMAT_RGB565, rgba));
    check_pixel(rgba, 0, 0xFF, 0x00, 0x00);
    check_pixel(rgba, 1, 0x00, 0xFF, 0x00);
    check_pixel(rgba, 2, 0x00, 0x00, 0xFF);
    check_pixel(rgba, 3, 0xFF, 0xFF, 0xFF);
}

TEST_CASE("0RGB1555 uses bits 10-14 for red and ignores bit 15") {
    // 0x4210 = 0b0100'0010'0001'0000: each channel is 0b10000 = 16 → (16 << 3) | (16 >> 2) = 132
    const uint16_t pixels[4] = {0x7C00, 0x03E0, 0x801F, 0x4210};
    size_t pitch = 0;
    const auto frame = make_frame(pixels, 2, pitch);
    std::vector<uint8_t> rgba;
    REQUIRE(cw::convert_to_rgba(frame.data(), 2, 2, pitch, RETRO_PIXEL_FORMAT_0RGB1555, rgba));
    check_pixel(rgba, 0, 0xFF, 0x00, 0x00);
    check_pixel(rgba, 1, 0x00, 0xFF, 0x00);
    check_pixel(rgba, 2, 0x00, 0x00, 0xFF);
    check_pixel(rgba, 3, 132, 132, 132);
}

TEST_CASE("Invalid frames are rejected without touching the output") {
    const uint16_t pixels[4] = {};
    std::vector<uint8_t> rgba(3, 0x42);

    // pitch smaller than width × bytes per pixel
    CHECK_FALSE(cw::convert_to_rgba(pixels, 2, 2, 3, RETRO_PIXEL_FORMAT_RGB565, rgba));
    // unknown pixel format
    CHECK_FALSE(cw::convert_to_rgba(pixels, 2, 2, 4, RETRO_PIXEL_FORMAT_UNKNOWN, rgba));
    // no data
    CHECK_FALSE(cw::convert_to_rgba(nullptr, 2, 2, 4, RETRO_PIXEL_FORMAT_RGB565, rgba));
    CHECK(rgba == std::vector<uint8_t>(3, 0x42));
}

TEST_CASE("Oversized frames are rejected before any size arithmetic can overflow") {
    // 2^31 x 2^31 x 4 bytes = 2^64, which wraps a 64-bit size_t to 0: without the size limit
    // the output would be resized to nothing and the conversion loop would write past it.
    // Rejection happens before any pixel is read, so a 1-pixel buffer is enough here.
    const uint32_t pixel = 0;
    std::vector<uint8_t> rgba(3, 0x42);
    const unsigned huge = 1u << 31;
    CHECK_FALSE(cw::convert_to_rgba(&pixel, huge, huge, static_cast<size_t>(huge) * 4,
                                    RETRO_PIXEL_FORMAT_XRGB8888, rgba));
    CHECK_FALSE(cw::convert_to_rgba(&pixel, cw::kMaxFrameSide + 1, 1,
                                    static_cast<size_t>(cw::kMaxFrameSide + 1) * 4,
                                    RETRO_PIXEL_FORMAT_XRGB8888, rgba));
    CHECK_FALSE(cw::convert_to_rgba(&pixel, 1, cw::kMaxFrameSide + 1, 4,
                                    RETRO_PIXEL_FORMAT_XRGB8888, rgba));
    CHECK(rgba == std::vector<uint8_t>(3, 0x42));
}
