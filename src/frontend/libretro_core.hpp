#pragma once

#include "core_options.hpp"
#include "video_frame.hpp"

#include <libretro.h>

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace cw {

// Hosts one libretro core (.so): loads it with dlopen, answers its environment calls and
// runs it frame by frame.
//
// libretro callbacks are plain C function pointers without a "user data" argument, so they
// cannot reach a C++ object directly. The class therefore routes them through a static pointer
// to the active instance — only one core can be loaded at a time.
class LibretroCore {
  public:
    struct Directories {
        std::filesystem::path system; // BIOS files (the user's own)
        std::filesystem::path save;   // memory cards / SRAM written by the core
    };

    // What the core reports about itself (retro_get_system_info).
    struct SystemInfo {
        std::string name;
        std::string version;
        std::string valid_extensions;
        bool need_fullpath = false;
    };

    LibretroCore() = default;
    ~LibretroCore();
    LibretroCore(const LibretroCore&) = delete;
    LibretroCore& operator=(const LibretroCore&) = delete;

    // Opens the core and calls retro_init(). On failure, returns false and fills `error`.
    bool load(const std::filesystem::path& core_path, Directories dirs, std::string& error);
    // Loads content (game) into the initialized core.
    bool load_game(const std::filesystem::path& game_path, std::string& error);
    // Unloads the game (if any), calls retro_deinit() and closes the .so. Safe to call twice.
    void unload();

    // Emulates one frame (retro_run). Only valid once a game is loaded.
    void run_frame();

    bool is_loaded() const { return handle_ != nullptr; }
    bool is_game_loaded() const { return game_loaded_; }
    const SystemInfo& system_info() const { return system_info_; }
    const retro_system_av_info& av_info() const { return av_info_; }
    retro_pixel_format pixel_format() const { return pixel_format_; }
    const CoreOptions& options() const { return options_; }
    // Changes a core option; the core sees it on its next GET_VARIABLE(_UPDATE). Returns false
    // for an unknown key or a value the core did not declare.
    bool set_option(const std::string& key, const std::string& value) {
        return options_.set(key, value);
    }
    uint64_t frame_count() const { return frame_count_; }
    // Last image the core produced, already converted to RGBA8.
    const VideoFrame& video_frame() const { return frame_; }
    unsigned last_frame_width() const { return frame_.width; }
    unsigned last_frame_height() const { return frame_.height; }

  private:
    // Entry points looked up in the .so. decltype keeps each signature identical to libretro.h.
    struct Api {
        decltype(&retro_api_version) api_version = nullptr;
        decltype(&retro_init) init = nullptr;
        decltype(&retro_deinit) deinit = nullptr;
        decltype(&retro_get_system_info) get_system_info = nullptr;
        decltype(&retro_get_system_av_info) get_system_av_info = nullptr;
        decltype(&retro_set_environment) set_environment = nullptr;
        decltype(&retro_set_video_refresh) set_video_refresh = nullptr;
        decltype(&retro_set_audio_sample) set_audio_sample = nullptr;
        decltype(&retro_set_audio_sample_batch) set_audio_sample_batch = nullptr;
        decltype(&retro_set_input_poll) set_input_poll = nullptr;
        decltype(&retro_set_input_state) set_input_state = nullptr;
        decltype(&retro_set_controller_port_device) set_controller_port_device = nullptr;
        decltype(&retro_reset) reset = nullptr;
        decltype(&retro_run) run = nullptr;
        decltype(&retro_serialize_size) serialize_size = nullptr;
        decltype(&retro_serialize) serialize = nullptr;
        decltype(&retro_unserialize) unserialize = nullptr;
        decltype(&retro_load_game) load_game = nullptr;
        decltype(&retro_unload_game) unload_game = nullptr;
        decltype(&retro_get_memory_data) get_memory_data = nullptr;
        decltype(&retro_get_memory_size) get_memory_size = nullptr;
    };

    bool load_symbols(std::string& error);
    void clear_frame();
    bool environment(unsigned cmd, void* data);

    // Static trampolines handed to the core; they forward to s_active.
    static bool RETRO_CALLCONV on_environment(unsigned cmd, void* data);
    static void RETRO_CALLCONV on_video_refresh(const void* data, unsigned width, unsigned height,
                                                size_t pitch);
    static void RETRO_CALLCONV on_audio_sample(int16_t left, int16_t right);
    static size_t RETRO_CALLCONV on_audio_sample_batch(const int16_t* data, size_t frames);
    static void RETRO_CALLCONV on_input_poll();
    static int16_t RETRO_CALLCONV on_input_state(unsigned port, unsigned device, unsigned index,
                                                 unsigned id);
    static void RETRO_CALLCONV on_log(retro_log_level level, const char* fmt, ...);

    static LibretroCore* s_active;

    void* handle_ = nullptr; // from dlopen
    Api api_{};
    bool initialized_ = false; // retro_init() was called
    bool game_loaded_ = false;

    std::string core_path_; // kept as strings: the core receives c_str() pointers
    std::string system_dir_;
    std::string save_dir_;
    std::string game_path_;
    std::vector<uint8_t> game_data_; // only used when the core does not need a full path

    SystemInfo system_info_;
    retro_system_av_info av_info_{};
    retro_pixel_format pixel_format_ = RETRO_PIXEL_FORMAT_0RGB1555; // libretro's default
    CoreOptions options_;
    std::set<unsigned> reported_unsupported_; // log each unsupported call only once

    uint64_t frame_count_ = 0;
    VideoFrame frame_;
    bool reported_bad_frame_ = false; // log a rejected frame only once
};

} // namespace cw
