#include "libretro_core.hpp"

#include "input_state.hpp"

#include <SDL3/SDL_log.h>
#include <dlfcn.h>

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <new>
#include <string_view>
#include <type_traits>

namespace cw {

LibretroCore* LibretroCore::s_active = nullptr;

namespace {

const char* pixel_format_name(retro_pixel_format format) {
    switch (format) {
    case RETRO_PIXEL_FORMAT_0RGB1555:
        return "0RGB1555";
    case RETRO_PIXEL_FORMAT_XRGB8888:
        return "XRGB8888";
    case RETRO_PIXEL_FORMAT_RGB565:
        return "RGB565";
    default:
        return "unknown";
    }
}

SDL_LogPriority to_sdl_priority(retro_log_level level) {
    switch (level) {
    case RETRO_LOG_DEBUG:
        return SDL_LOG_PRIORITY_DEBUG;
    case RETRO_LOG_INFO:
        return SDL_LOG_PRIORITY_INFO;
    case RETRO_LOG_WARN:
        return SDL_LOG_PRIORITY_WARN;
    default:
        return SDL_LOG_PRIORITY_ERROR;
    }
}

} // namespace

LibretroCore::~LibretroCore() {
    unload();
}

bool LibretroCore::load(const std::filesystem::path& core_path, Directories dirs,
                        std::string& error) {
    if (s_active) {
        error = "Another core is already loaded";
        return false;
    }

    // RTLD_LOCAL keeps the core's symbols private, so two cores never clash with each other.
    handle_ = dlopen(core_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        error = dlerror();
        return false;
    }
    if (!load_symbols(error)) {
        unload();
        return false;
    }
    if (api_.api_version() != RETRO_API_VERSION) {
        error = "Core uses libretro API version " + std::to_string(api_.api_version()) +
                ", expected " + std::to_string(RETRO_API_VERSION);
        unload();
        return false;
    }

    s_active = this;
    core_path_ = core_path.string();
    system_dir_ = dirs.system.string();
    save_dir_ = dirs.save.string();

    retro_system_info info{};
    api_.get_system_info(&info); // allowed before retro_init()
    system_info_ = {.name = info.library_name ? info.library_name : "",
                    .version = info.library_version ? info.library_version : "",
                    .valid_extensions = info.valid_extensions ? info.valid_extensions : "",
                    .need_fullpath = info.need_fullpath};

    // The environment callback must be set before retro_init(): cores query directories and
    // the log interface while initializing. The other callbacks only need to be set before
    // the first retro_run().
    api_.set_environment(&LibretroCore::on_environment);
    api_.init();
    initialized_ = true;
    api_.set_video_refresh(&LibretroCore::on_video_refresh);
    api_.set_audio_sample(&LibretroCore::on_audio_sample);
    api_.set_audio_sample_batch(&LibretroCore::on_audio_sample_batch);
    api_.set_input_poll(&LibretroCore::on_input_poll);
    api_.set_input_state(&LibretroCore::on_input_state);

    SDL_Log("Loaded core %s %s (extensions: %s)", system_info_.name.c_str(),
            system_info_.version.c_str(), system_info_.valid_extensions.c_str());
    return true;
}

bool LibretroCore::load_symbols(std::string& error) {
    // dlsym returns void*; each lookup is cast to the exact type of the matching Api field.
    auto lookup = [&](auto& function, const char* name) {
        function =
            reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(handle_, name));
        if (!function && error.empty()) {
            error = std::string("Not a libretro core: missing ") + name;
        }
    };
    lookup(api_.api_version, "retro_api_version");
    lookup(api_.init, "retro_init");
    lookup(api_.deinit, "retro_deinit");
    lookup(api_.get_system_info, "retro_get_system_info");
    lookup(api_.get_system_av_info, "retro_get_system_av_info");
    lookup(api_.set_environment, "retro_set_environment");
    lookup(api_.set_video_refresh, "retro_set_video_refresh");
    lookup(api_.set_audio_sample, "retro_set_audio_sample");
    lookup(api_.set_audio_sample_batch, "retro_set_audio_sample_batch");
    lookup(api_.set_input_poll, "retro_set_input_poll");
    lookup(api_.set_input_state, "retro_set_input_state");
    lookup(api_.set_controller_port_device, "retro_set_controller_port_device");
    lookup(api_.reset, "retro_reset");
    lookup(api_.run, "retro_run");
    lookup(api_.serialize_size, "retro_serialize_size");
    lookup(api_.serialize, "retro_serialize");
    lookup(api_.unserialize, "retro_unserialize");
    lookup(api_.load_game, "retro_load_game");
    lookup(api_.unload_game, "retro_unload_game");
    lookup(api_.get_memory_data, "retro_get_memory_data");
    lookup(api_.get_memory_size, "retro_get_memory_size");
    return error.empty();
}

bool LibretroCore::load_game(const std::filesystem::path& game_path, std::string& error) {
    if (!initialized_ || game_loaded_) {
        error = initialized_ ? "A game is already loaded" : "No core loaded";
        return false;
    }

    game_path_ = std::filesystem::absolute(game_path).string();
    retro_game_info game{.path = game_path_.c_str(), .data = nullptr, .size = 0, .meta = nullptr};

    // Disc-based cores (like PS1) read the files themselves and only want the path. Others
    // expect the whole file in memory; the buffer must stay alive while the game runs.
    if (!system_info_.need_fullpath) {
        std::ifstream file(game_path, std::ios::binary);
        if (!file) {
            error = "Could not open " + game_path_;
            return false;
        }
        game_data_.assign(std::istreambuf_iterator<char>(file), {});
        game.data = game_data_.data();
        game.size = game_data_.size();
    }

    if (!api_.load_game(&game)) {
        error = "The core could not load " + game_path_ + " (see the core log above)";
        game_data_.clear();
        return false;
    }
    game_loaded_ = true;
    frame_count_ = 0;
    clear_frame();

    api_.get_system_av_info(&av_info_);
    if (audio_sink_) {
        audio_sink_->set_sample_rate(av_info_.timing.sample_rate);
    }
    const retro_game_geometry& g = av_info_.geometry;
    SDL_Log("Game loaded: %ux%u (max %ux%u, aspect %.3f), %.3f fps, audio %.0f Hz, pixels %s",
            g.base_width, g.base_height, g.max_width, g.max_height, g.aspect_ratio,
            av_info_.timing.fps, av_info_.timing.sample_rate, pixel_format_name(pixel_format_));
    return true;
}

void LibretroCore::unload() {
    if (audio_sink_) {
        audio_sink_->clear();
    }
    audio_samples_.clear();
    if (game_loaded_) {
        SDL_Log("Unloading game after %llu frames", static_cast<unsigned long long>(frame_count_));
        api_.unload_game();
        game_loaded_ = false;
    }
    if (initialized_) {
        api_.deinit();
        initialized_ = false;
    }
    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
    }
    if (s_active == this) {
        s_active = nullptr;
    }
    api_ = {};
    game_data_.clear();
    options_.clear();
    reported_unsupported_.clear();
    pixel_format_ = RETRO_PIXEL_FORMAT_0RGB1555;
    av_info_ = {};
    serialization_quirks_ = 0;
    clear_frame();
}

void LibretroCore::clear_frame() {
    // The serial keeps counting across games, so a renderer that remembers the last serial it
    // uploaded can never mistake the first frame of a new game for one it already has.
    frame_.width = 0;
    frame_.height = 0;
    frame_.rgba.clear();
    reported_bad_frame_ = false;
}

void LibretroCore::run_frame() {
    if (game_loaded_) {
        audio_samples_.clear();
        audio_buffer_failed_ = false;
        api_.run();
        ++frame_count_;
        if (audio_sink_ && !audio_buffer_failed_) {
            audio_sink_->write(audio_samples_.data(), audio_samples_.size() / 2);
        }
        audio_samples_.clear();
    }
}

void LibretroCore::set_audio_sink(AudioSink* sink) {
    if (audio_sink_ && audio_sink_ != sink) {
        audio_sink_->clear();
    }
    audio_sink_ = sink;
    audio_samples_.clear();
    if (audio_sink_ && game_loaded_) {
        audio_sink_->set_sample_rate(av_info_.timing.sample_rate);
    }
}

void LibretroCore::set_joypad_state(JoypadState* state) {
    joypad_state_ = state;
}

bool LibretroCore::serialize_state(std::vector<uint8_t>& out, std::string& error) {
    if (!game_loaded_) {
        error = "No game loaded";
        return false;
    }
    const bool must_initialize = serialization_quirks_ & RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE;
    const size_t size = api_.serialize_size();
    if (size == 0) {
        error = must_initialize ? "The core is not ready to save yet; try again in a moment"
                                : "This core does not support save states";
        return false;
    }
    out.assign(size, 0);
    if (!api_.serialize(out.data(), size)) {
        error = must_initialize ? "The core is not ready to save yet; try again in a moment"
                                : "The core failed to save its state";
        return false;
    }
    return true;
}

bool LibretroCore::unserialize_state(std::span<const uint8_t> data, std::string& error) {
    if (!game_loaded_) {
        error = "No game loaded";
        return false;
    }
    const size_t expected = api_.serialize_size();
    const bool variable_size = serialization_quirks_ & RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE;
    if (data.empty() || (!variable_size && data.size() != expected)) {
        error = "The state has " + std::to_string(data.size()) + " bytes, but this core expects " +
                std::to_string(expected) + " (saved by another core or core version?)";
        return false;
    }
    if (!api_.unserialize(data.data(), data.size())) {
        error = "The core rejected the state";
        return false;
    }
    // Audio produced before the load belongs to the old timeline.
    audio_samples_.clear();
    if (audio_sink_) {
        audio_sink_->clear();
    }
    return true;
}

const uint8_t* LibretroCore::system_ram_data() const {
    if (!game_loaded_ || !api_.get_memory_data) {
        return nullptr;
    }
    return static_cast<const uint8_t*>(api_.get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
}

size_t LibretroCore::system_ram_size() const {
    if (!game_loaded_ || !api_.get_memory_size) {
        return 0;
    }
    return api_.get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
}

// --- Environment ------------------------------------------------------------------------------

bool LibretroCore::environment(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = &LibretroCore::on_log;
        return true;

    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;

    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS: {
        // libretro.h: the front-end keeps the flags it supports and clears the others.
        // - INCOMPLETE: only matters for netplay/rerecording, which we do not do.
        // - MUST_INITIALIZE: a failed save/load just reports "not ready yet".
        // - CORE_VARIABLE_SIZE: retro_serialize_size() is queried before every save, and the
        //   size check on load is relaxed; we acknowledge it with FRONT_VARIABLE_SIZE.
        // - SINGLE_SESSION: enforced by SaveStates (only states saved in this session load).
        // - ENDIAN/PLATFORM_DEPENDENT: cleared. We cannot tell whether a state file was copied
        //   from another machine (the file records no host), so we cannot promise it.
        // Unknown bits are cleared: we cannot promise to honor quirks we do not know.
        constexpr uint64_t kSupported =
            RETRO_SERIALIZATION_QUIRK_INCOMPLETE | RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE |
            RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE | RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION;
        auto& quirks = *static_cast<uint64_t*>(data);
        quirks &= kSupported;
        if (quirks & RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE) {
            quirks |= RETRO_SERIALIZATION_QUIRK_FRONT_VARIABLE_SIZE;
        }
        serialization_quirks_ = quirks;
        return true;
    }

    // libretro.h: answering NULL means "no directory defined"; the core then picks its own.
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = system_dir_.empty() ? nullptr : system_dir_.c_str();
        return true;

    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = save_dir_.empty() ? nullptr : save_dir_.c_str();
        return true;

    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        *static_cast<const char**>(data) = core_path_.c_str();
        return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const auto format = *static_cast<const retro_pixel_format*>(data);
        if (format != RETRO_PIXEL_FORMAT_0RGB1555 && format != RETRO_PIXEL_FORMAT_XRGB8888 &&
            format != RETRO_PIXEL_FORMAT_RGB565) {
            return false;
        }
        pixel_format_ = format;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        // "Dupe" = the core may pass NULL to video_refresh to repeat the previous frame.
        *static_cast<bool*>(data) = true;
        return true;

    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        av_info_ = *static_cast<const retro_system_av_info*>(data);
        if (audio_sink_) {
            try {
                audio_sink_->set_sample_rate(av_info_.timing.sample_rate);
            } catch (...) {
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                             "Audio sink threw while applying SET_SYSTEM_AV_INFO");
            }
        }
        return true;

    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        // libretro.h: only base_width, base_height and aspect_ratio may change here; the
        // max_* values are fixed until SET_SYSTEM_AV_INFO.
        const auto& geometry = *static_cast<const retro_game_geometry*>(data);
        av_info_.geometry.base_width = geometry.base_width;
        av_info_.geometry.base_height = geometry.base_height;
        av_info_.geometry.aspect_ratio = geometry.aspect_ratio;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        // Software rendering only (hardware rendering is "Later / optional" in the roadmap).
        // Refusing stores nothing, and the core falls back to software frames.
        SDL_Log("Core asked for hardware rendering; refused (software rendering only)");
        return false;

    case RETRO_ENVIRONMENT_SET_MESSAGE:
        SDL_Log("[core message] %s", static_cast<const retro_message*>(data)->msg);
        return true;

    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        *static_cast<unsigned*>(data) = 1; // SET_MESSAGE_EXT is supported
        return true;

    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        SDL_Log("[core message] %s", static_cast<const retro_message_ext*>(data)->msg);
        return true;

    // Core options -----------------------------------------------------------------------
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 2;
        return true;

    case RETRO_ENVIRONMENT_SET_VARIABLES:
        options_.set_variables(static_cast<const retro_variable*>(data));
        return true;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        options_.set_definitions_v1(static_cast<const retro_core_option_definition*>(data));
        return true;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        options_.set_definitions_v1(static_cast<const retro_core_options_intl*>(data)->us);
        return true;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        options_.set_definitions_v2(static_cast<const retro_core_options_v2*>(data)->definitions);
        return true;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        options_.set_definitions_v2(
            static_cast<const retro_core_options_v2_intl*>(data)->us->definitions);
        return true;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        return true; // options are not shown in the UI yet; hiding them is a no-op

    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* variable = static_cast<retro_variable*>(data);
        variable->value = options_.get(variable->key);
        return variable->value != nullptr;
    }

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = options_.take_updated();
        return true;

    // Accepted, but nothing to do yet -----------------------------------------------------
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: // button labels, used from step 1.5
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        return true;

    default:
        // Unsupported calls must return false; the core then falls back or skips the feature
        // (e.g. SET_HW_RENDER → software rendering, SET_MEMORY_MAPS → handled in step 2.1).
        if (reported_unsupported_.insert(cmd).second) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Unsupported environment call %u%s",
                         cmd & 0xffff,
                         (cmd & RETRO_ENVIRONMENT_EXPERIMENTAL) ? " (experimental)" : "");
        }
        return false;
    }
}

// --- Static trampolines -------------------------------------------------------------------

bool LibretroCore::on_environment(unsigned cmd, void* data) {
    return s_active && s_active->environment(cmd, data);
}

void LibretroCore::on_video_refresh(const void* data, unsigned width, unsigned height,
                                    size_t pitch) {
    // NULL = "dupe": repeat the previous image (we answered GET_CAN_DUPE with true), so the
    // frame and its serial stay as they are. RETRO_HW_FRAME_BUFFER_VALID only comes from
    // hardware-rendered cores, which we refuse in SET_HW_RENDER.
    if (!s_active || !data || data == RETRO_HW_FRAME_BUFFER_VALID) {
        return;
    }
    VideoFrame& frame = s_active->frame_;
    // This runs inside the core's retro_run(), which is C code: an exception escaping here
    // would unwind through frames that know nothing about C++ (undefined behavior). A failed
    // allocation is treated like any other rejected frame.
    bool converted = false;
    try {
        converted =
            convert_to_rgba(data, width, height, pitch, s_active->pixel_format_, frame.rgba);
    } catch (const std::bad_alloc&) {
        converted = false;
    }
    if (!converted) {
        if (!s_active->reported_bad_frame_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Ignoring a %ux%u frame with pitch %zu (pixel format %d)", width, height,
                        pitch, static_cast<int>(s_active->pixel_format_));
            s_active->reported_bad_frame_ = true;
        }
        return;
    }
    frame.width = width;
    frame.height = height;
    ++frame.serial;
}

void LibretroCore::on_audio_sample(int16_t left, int16_t right) {
    if (!s_active || !s_active->audio_sink_ || s_active->audio_buffer_failed_) {
        return;
    }
    try {
        s_active->audio_samples_.push_back(left);
        s_active->audio_samples_.push_back(right);
    } catch (...) {
        s_active->audio_samples_.clear();
        s_active->audio_buffer_failed_ = true;
        if (!s_active->reported_audio_buffer_failure_) {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Could not buffer audio from the core");
            s_active->reported_audio_buffer_failure_ = true;
        }
    }
}

size_t LibretroCore::on_audio_sample_batch(const int16_t* data, size_t frames) {
    if (!s_active || frames == 0 || !s_active->audio_sink_) {
        return frames;
    }
    if (!data || s_active->audio_buffer_failed_ ||
        frames > (s_active->audio_samples_.max_size() - s_active->audio_samples_.size()) / 2) {
        return 0;
    }

    try {
        s_active->audio_samples_.insert(s_active->audio_samples_.end(), data, data + frames * 2);
    } catch (...) {
        s_active->audio_samples_.clear();
        s_active->audio_buffer_failed_ = true;
        if (!s_active->reported_audio_buffer_failure_) {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Could not buffer audio from the core");
            s_active->reported_audio_buffer_failure_ = true;
        }
        return 0;
    }
    return frames;
}

void LibretroCore::on_input_poll() {}

int16_t LibretroCore::on_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    return s_active && s_active->joypad_state_
               ? s_active->joypad_state_->input_state(port, device, index, id)
               : 0;
}

void LibretroCore::on_log(retro_log_level level, const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    // Cores end their lines with '\n'; SDL adds its own.
    std::string_view text = line;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, to_sdl_priority(level), "[core] %.*s",
                   static_cast<int>(text.size()), text.data());
}

} // namespace cw
