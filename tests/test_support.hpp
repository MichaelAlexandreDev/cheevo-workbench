#pragma once

// Helpers shared by tests that drive the synthetic libretro core (tests/test_core/test_core.c).

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#ifndef CW_TEST_CORE_PATH
#error "CW_TEST_CORE_PATH must name the built synthetic libretro core"
#endif

namespace cw_test {

inline const std::filesystem::path& test_core_path() {
    static const std::filesystem::path path(CW_TEST_CORE_PATH);
    return path;
}

// A unique, initially empty directory under the system temp dir, removed with its contents.
class TempDir {
  public:
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("cw-test-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

// A small synthetic content file the test core accepts (it needs non-empty data, nothing more).
class SyntheticContent {
  public:
    SyntheticContent() : path_(dir_.path() / "synthetic-game.dat") {
        constexpr char bytes[] = "synthetic content fixture";
        std::ofstream file(path_, std::ios::binary);
        if (!file || !file.write(bytes, static_cast<std::streamsize>(sizeof(bytes) - 1))) {
            throw std::runtime_error("Could not create temporary synthetic content");
        }
    }

    const std::filesystem::path& path() const { return path_; }

  private:
    TempDir dir_;
    std::filesystem::path path_;
};

} // namespace cw_test
