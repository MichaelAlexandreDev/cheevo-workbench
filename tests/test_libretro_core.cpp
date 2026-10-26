#include "libretro_core.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#ifndef CW_TEST_CORE_PATH
#error "CW_TEST_CORE_PATH must name the built synthetic libretro core"
#endif

namespace {

class SyntheticContent {
  public:
    SyntheticContent() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("cw-test-core-" + std::to_string(stamp) + ".dat");

        constexpr char bytes[] = "synthetic content fixture";
        std::ofstream file(path_, std::ios::binary);
        if (!file || !file.write(bytes, static_cast<std::streamsize>(sizeof(bytes) - 1))) {
            throw std::runtime_error("Could not create temporary synthetic content");
        }
    }

    ~SyntheticContent() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    SyntheticContent(const SyntheticContent&) = delete;
    SyntheticContent& operator=(const SyntheticContent&) = delete;

    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

const std::filesystem::path& test_core_path() {
    static const std::filesystem::path path(CW_TEST_CORE_PATH);
    return path;
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
    REQUIRE(options.size() == 1);
    CHECK(options[0].key == "cw_test_pixel_format");
    CHECK(options[0].value == "0RGB1555");
    REQUIRE(options[0].values.size() == 3);
    CHECK(options[0].values[0] == "0RGB1555");
    CHECK(options[0].values[1] == "XRGB8888");
    CHECK(options[0].values[2] == "RGB565");

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
