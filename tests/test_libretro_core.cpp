#include "input_state.hpp"
#include "libretro_core.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using cw_test::SyntheticContent;
using cw_test::test_core_path;

// The color the synthetic core draws at (x, y) on a given frame (see test_core.c).
struct Rgb {
    uint8_t r, g, b;
};
Rgb test_core_color(unsigned x, unsigned y, uint64_t frame) {
    return {static_cast<uint8_t>(x * 3 + frame * 5), static_cast<uint8_t>(y * 7 + frame * 11),
            static_cast<uint8_t>((x + y) * 13 + frame * 17)};
}

// What survives a round trip through a 5- or 6-bit channel and back to 8 bits.
uint8_t through_bits(uint8_t value, unsigned bits) {
    const unsigned v = value >> (8 - bits);
    return static_cast<uint8_t>((v << (8 - bits)) | (v >> (2 * bits - 8)));
}

Rgb expected_color(unsigned x, unsigned y, uint64_t frame, retro_pixel_format format) {
    const Rgb c = test_core_color(x, y, frame);
    switch (format) {
    case RETRO_PIXEL_FORMAT_0RGB1555:
        return {through_bits(c.r, 5), through_bits(c.g, 5), through_bits(c.b, 5)};
    case RETRO_PIXEL_FORMAT_RGB565:
        return {through_bits(c.r, 5), through_bits(c.g, 6), through_bits(c.b, 5)};
    default:
        return c;
    }
}

std::vector<int16_t> expected_audio(uint64_t frame_count) {
    constexpr size_t kFramesPerRun = 735;
    std::vector<int16_t> samples;
    samples.reserve(kFramesPerRun * 2);
    for (size_t index = 0; index < kFramesPerRun; ++index) {
        const uint32_t phase = static_cast<uint32_t>(frame_count * kFramesPerRun + index);
        const int16_t left = static_cast<int16_t>(static_cast<int32_t>(phase % 65535U) - 32767);
        samples.push_back(left);
        samples.push_back(static_cast<int16_t>(-left));
    }
    return samples;
}

class CaptureAudioSink final : public cw::AudioSink {
  public:
    void set_sample_rate(double rate) override {
        sample_rate = rate;
        sample_rates.push_back(rate);
    }

    void write(const int16_t* interleaved, size_t frames) override {
        ++write_calls;
        write_sizes.push_back(frames);
        if (frames > 0) {
            samples.insert(samples.end(), interleaved, interleaved + frames * 2);
        }
    }

    void clear() override { ++clear_calls; }

    double sample_rate = 0.0;
    size_t write_calls = 0;
    size_t clear_calls = 0;
    std::vector<double> sample_rates;
    std::vector<size_t> write_sizes;
    std::vector<int16_t> samples;
};

void check_frame_pixel(const cw::VideoFrame& frame, unsigned x, unsigned y, Rgb expected) {
    CAPTURE(x);
    CAPTURE(y);
    const size_t i = (static_cast<size_t>(y) * frame.width + x) * 4;
    REQUIRE(i + 3 < frame.rgba.size());
    CHECK(frame.rgba[i + 0] == expected.r);
    CHECK(frame.rgba[i + 1] == expected.g);
    CHECK(frame.rgba[i + 2] == expected.b);
    CHECK(frame.rgba[i + 3] == 0xFF);
}

uint32_t core_frame_counter(const cw::LibretroCore& core) {
    REQUIRE(core.system_ram_size() >= 4);
    const uint8_t* ram = core.system_ram_data();
    return uint32_t{ram[0]} | uint32_t{ram[1]} << 8 | uint32_t{ram[2]} << 16 |
           uint32_t{ram[3]} << 24;
}

} // namespace

TEST_CASE("LibretroCore loads the synthetic core and runs frames") {
    SyntheticContent content;
    cw::LibretroCore core;
    std::string error;

    REQUIRE(core.load(test_core_path(), {}, error));
    CHECK(core.system_info().name == "cw-test-core");
    CHECK_FALSE(core.system_info().need_fullpath);

    const auto& options = core.options().options();
    REQUIRE(options.size() == 6);
    CHECK(options[0].key == "cw_test_pixel_format");
    CHECK(options[0].value == "0RGB1555");
    REQUIRE(options[0].values.size() == 3);
    CHECK(options[0].values[0] == "0RGB1555");
    CHECK(options[0].values[1] == "XRGB8888");
    CHECK(options[0].values[2] == "RGB565");
    CHECK(options[1].key == "cw_test_resize");
    CHECK(options[1].value == "disabled");
    CHECK(options[2].key == "cw_test_audio_mode");
    CHECK(options[2].value == "batch");
    CHECK(options[3].key == "cw_test_sample_rate_change");
    CHECK(options[3].value == "disabled");
    CHECK(options[4].key == "cw_test_input_mode");
    CHECK(options[4].value == "individual");

    REQUIRE(core.load_game(content.path(), error));
    CHECK(core.is_game_loaded());
    CHECK(core.pixel_format() == RETRO_PIXEL_FORMAT_0RGB1555);

    const auto& av_info = core.av_info();
    CHECK(av_info.geometry.base_width == 64);
    CHECK(av_info.geometry.base_height == 32);
    CHECK(av_info.geometry.max_width == 64);
    CHECK(av_info.geometry.max_height == 32);
    CHECK(av_info.timing.fps == doctest::Approx(60.0));
    CHECK(av_info.timing.sample_rate == doctest::Approx(44100.0));

    constexpr uint64_t frames_to_run = 5;
    for (uint64_t frame = 0; frame < frames_to_run; ++frame)
        core.run_frame();

    CHECK(core.frame_count() == frames_to_run);
    CHECK(core.last_frame_width() == 64);
    CHECK(core.last_frame_height() == 32);
}

TEST_CASE("LibretroCore reset preserves the game and clears queued audio") {
    SyntheticContent content;
    CaptureAudioSink sink;
    cw::LibretroCore core;
    core.set_audio_sink(&sink);
    std::string error;

    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.load_game(content.path(), error));
    for (int i = 0; i < 3; ++i) {
        core.run_frame();
    }
    REQUIRE(core_frame_counter(core) == 3);
    const size_t clear_calls = sink.clear_calls;

    core.reset();
    CHECK(core.is_loaded());
    CHECK(core.is_game_loaded());
    CHECK(core_frame_counter(core) == 0);
    CHECK(sink.clear_calls == clear_calls + 1);

    const size_t writes_before_advance = sink.write_calls;
    core.run_frame(true);
    CHECK(core.frame_count() == 4);
    CHECK(sink.write_calls == writes_before_advance);
    CHECK(core_frame_counter(core) == 1);
}

TEST_CASE("LibretroCore reports core-loading and missing-game errors") {
    cw::LibretroCore core;
    std::string error;

    const auto missing_core =
        std::filesystem::temp_directory_path() / "cw-test-core-file-that-does-not-exist.so";
    CHECK_FALSE(core.load(missing_core, {}, error));
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(core.load("libm.so.6", {}, error));
    CHECK(error.find("Not a libretro core") != std::string::npos);

    error.clear();
    CHECK_FALSE(core.load_game({}, error));
    CHECK(error == "No core loaded");
}

TEST_CASE("LibretroCore can unload and load the synthetic core again") {
    SyntheticContent content;
    cw::LibretroCore core;
    std::string error;

    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.load_game(content.path(), error));
    core.run_frame();
    CHECK(core.frame_count() == 1);

    core.unload();
    CHECK_FALSE(core.is_loaded());
    CHECK_FALSE(core.is_game_loaded());

    error.clear();
    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.load_game(content.path(), error));
    CHECK(core.frame_count() == 0);
    core.run_frame();
    CHECK(core.frame_count() == 1);
}

TEST_CASE("Each pixel format reaches the video frame with the expected pixels") {
    struct Case {
        const char* option;
        retro_pixel_format format;
    };
    for (const Case c : {Case{"0RGB1555", RETRO_PIXEL_FORMAT_0RGB1555},
                         Case{"XRGB8888", RETRO_PIXEL_FORMAT_XRGB8888},
                         Case{"RGB565", RETRO_PIXEL_FORMAT_RGB565}}) {
        CAPTURE(c.option);
        SyntheticContent content;
        cw::LibretroCore core;
        std::string error;
        REQUIRE(core.load(test_core_path(), {}, error));
        REQUIRE(core.set_option("cw_test_pixel_format", c.option));
        REQUIRE(core.load_game(content.path(), error));
        CHECK(core.pixel_format() == c.format);

        core.run_frame();
        const cw::VideoFrame& frame = core.video_frame();
        REQUIRE(frame.width == 64);
        REQUIRE(frame.height == 32);
        // (0, 1) is the first pixel of the second row: wrong if the 16 bytes of row padding
        // were not skipped. The test core also asked for hardware rendering at load; getting
        // software pixels here shows the request was refused.
        for (const auto [x, y] : {std::pair{0u, 0u}, {63u, 0u}, {0u, 1u}, {17u, 9u}, {63u, 31u}}) {
            check_frame_pixel(frame, x, y, expected_color(x, y, 1, c.format));
        }
    }
}

TEST_CASE("A duplicated frame keeps the previous image") {
    SyntheticContent content;
    cw::LibretroCore core;
    std::string error;
    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.load_game(content.path(), error));

    for (int i = 0; i < 3; ++i) {
        core.run_frame();
    }
    const uint64_t serial = core.video_frame().serial;
    const std::vector<uint8_t> image = core.video_frame().rgba;

    core.run_frame(); // frame 4: the test core sends NULL (dupe)
    CHECK(core.frame_count() == 4);
    CHECK(core.video_frame().serial == serial);
    CHECK(core.video_frame().rgba == image);

    core.run_frame(); // frame 5: a new image again
    CHECK(core.video_frame().serial == serial + 1);
    CHECK(core.video_frame().rgba != image);
    check_frame_pixel(core.video_frame(), 5, 5,
                      expected_color(5, 5, 5, RETRO_PIXEL_FORMAT_0RGB1555));
}

TEST_CASE("Geometry changes reach the AV info and the video frame") {
    SyntheticContent content;
    cw::LibretroCore core;
    std::string error;
    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.set_option("cw_test_resize", "enabled"));
    REQUIRE(core.load_game(content.path(), error));

    core.run_frame();
    core.run_frame();
    CHECK(core.video_frame().width == 64);
    CHECK(core.av_info().geometry.base_width == 64);

    core.run_frame(); // frame 3: SET_GEOMETRY to 32x16, aspect 3:2
    const retro_game_geometry& geometry = core.av_info().geometry;
    CHECK(geometry.base_width == 32);
    CHECK(geometry.base_height == 16);
    CHECK(geometry.aspect_ratio == doctest::Approx(1.5));
    CHECK(geometry.max_width == 64); // SET_GEOMETRY must not change the maximum
    CHECK(geometry.max_height == 32);
    CHECK(core.video_frame().width == 32);
    CHECK(core.video_frame().height == 16);
    CHECK(core.video_frame().rgba.size() == 32 * 16 * 4);
    check_frame_pixel(core.video_frame(), 31, 15,
                      expected_color(31, 15, 3, RETRO_PIXEL_FORMAT_0RGB1555));
}

TEST_CASE("Core options reject unknown keys and values") {
    cw::LibretroCore core;
    std::string error;
    REQUIRE(core.load(test_core_path(), {}, error));
    CHECK_FALSE(core.set_option("cw_test_pixel_format", "RGBA4444"));
    CHECK_FALSE(core.set_option("no_such_option", "x"));
    CHECK(core.options().get("cw_test_pixel_format") == std::string("0RGB1555"));
}

TEST_CASE("LibretroCore buffers batch audio and writes once per frame") {
    SyntheticContent content;
    CaptureAudioSink sink;
    cw::LibretroCore core;
    core.set_audio_sink(&sink);
    std::string error;

    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.load_game(content.path(), error));
    CHECK(sink.sample_rate == doctest::Approx(44100.0));

    core.run_frame();
    core.run_frame();

    CHECK(sink.write_calls == 2);
    CHECK((sink.write_sizes == std::vector<size_t>{735, 735}));
    CHECK(sink.samples == [&] {
        std::vector<int16_t> expected = expected_audio(1);
        const std::vector<int16_t> second = expected_audio(2);
        expected.insert(expected.end(), second.begin(), second.end());
        return expected;
    }());
    for (size_t frame = 0; frame < sink.samples.size() / 2; ++frame) {
        CHECK(sink.samples[frame * 2 + 1] == -sink.samples[frame * 2]);
    }
}

TEST_CASE("LibretroCore buffers single-sample audio callbacks") {
    SyntheticContent content;
    CaptureAudioSink sink;
    cw::LibretroCore core;
    core.set_audio_sink(&sink);
    std::string error;

    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.set_option("cw_test_audio_mode", "single"));
    REQUIRE(core.load_game(content.path(), error));
    core.run_frame();

    CHECK(sink.write_calls == 1);
    CHECK(sink.write_sizes == std::vector<size_t>{735});
    CHECK(sink.samples == expected_audio(1));
    for (size_t frame = 0; frame < sink.samples.size() / 2; ++frame) {
        CHECK(sink.samples[frame * 2 + 1] == -sink.samples[frame * 2]);
    }
}

TEST_CASE("LibretroCore forwards SET_SYSTEM_AV_INFO sample-rate changes to the sink") {
    SyntheticContent content;
    CaptureAudioSink sink;
    cw::LibretroCore core;
    core.set_audio_sink(&sink);
    std::string error;

    REQUIRE(core.load(test_core_path(), {}, error));
    REQUIRE(core.set_option("cw_test_sample_rate_change", "enabled"));
    REQUIRE(core.load_game(content.path(), error));
    CHECK(sink.sample_rate == doctest::Approx(44100.0));

    core.run_frame();
    core.run_frame();
    core.run_frame(); // frame 3 announces 48 kHz through SET_SYSTEM_AV_INFO

    CHECK(core.av_info().timing.sample_rate == doctest::Approx(48000.0));
    REQUIRE(sink.sample_rates.size() == 2);
    CHECK(sink.sample_rates[0] == doctest::Approx(44100.0));
    CHECK(sink.sample_rates[1] == doctest::Approx(48000.0));
}

TEST_CASE("The synthetic core observes every default input mapping in both query modes") {
    SyntheticContent content;
    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    const auto verify_mode = [&](const char* mode) {
        CAPTURE(mode);
        cw::JoypadState joypad;
        cw::LibretroCore core;
        core.set_joypad_state(&joypad);
        std::string error;
        REQUIRE(core.load(test_core_path(), {}, error));
        REQUIRE(core.set_option("cw_test_input_mode", mode));
        REQUIRE(core.load_game(content.path(), error));
        REQUIRE(core.system_ram_size() == 64U * 1024U);

        const auto check_ram_mask = [&](uint16_t expected) {
            core.run_frame();
            const uint8_t* ram = core.system_ram_data();
            REQUIRE(ram != nullptr);
            CHECK(static_cast<uint16_t>(ram[4] | (static_cast<uint16_t>(ram[5]) << 8U)) ==
                  expected);
        };

        for (const cw::KeyboardBinding binding : cw::keyboard_bindings()) {
            CAPTURE(binding.key);
            keys.fill(false);
            keys[static_cast<size_t>(binding.key)] = true;
            const uint16_t mask = cw::map_keyboard_state(keys);
            joypad.set_source(cw::kKeyboardInputSource, mask);
            check_ram_mask(static_cast<uint16_t>(1U << binding.joypad_id));
        }

        joypad.remove_source(cw::kKeyboardInputSource);
        constexpr SDL_JoystickID kTestGamepadId = 42;
        const uint64_t gamepad_source = cw::gamepad_input_source(kTestGamepadId);
        for (const cw::GamepadBinding binding : cw::gamepad_bindings()) {
            CAPTURE(binding.button);
            cw::GamepadSnapshot snapshot;
            snapshot.buttons[static_cast<size_t>(binding.button)] = true;
            const uint16_t mask = cw::map_gamepad_state(snapshot);
            joypad.set_source(gamepad_source, mask);
            check_ram_mask(static_cast<uint16_t>(1U << binding.joypad_id));
        }

        for (const bool left_trigger : {true, false}) {
            cw::GamepadSnapshot snapshot;
            if (left_trigger) {
                snapshot.left_trigger = 16384;
            } else {
                snapshot.right_trigger = 16384;
            }
            joypad.set_source(gamepad_source, cw::map_gamepad_state(snapshot));
            const unsigned joypad_id =
                left_trigger ? RETRO_DEVICE_ID_JOYPAD_L2 : RETRO_DEVICE_ID_JOYPAD_R2;
            check_ram_mask(static_cast<uint16_t>(1U << joypad_id));
        }
    };

    verify_mode("individual");
    verify_mode("bitmask");
}
