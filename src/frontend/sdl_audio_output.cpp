#include "sdl_audio_output.hpp"

#include <SDL3/SDL.h>

#include <climits>
#include <cmath>
#include <cstdint>

namespace cw {

SdlAudioOutput::~SdlAudioOutput() {
    shutdown();
}

void SdlAudioOutput::shutdown() {
    close_stream();
    if (audio_subsystem_initialized_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio_subsystem_initialized_ = false;
    }
}

bool SdlAudioOutput::initialize() {
    if (audio_subsystem_initialized_) {
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fail("SDL_InitSubSystem(SDL_INIT_AUDIO)");
        return false;
    }
    audio_subsystem_initialized_ = true;
    error_.clear();
    return true;
}

void SdlAudioOutput::set_sample_rate(double sample_rate) {
    if (!audio_subsystem_initialized_) {
        return;
    }
    if (!std::isfinite(sample_rate) || sample_rate < 1.0 || sample_rate > INT_MAX) {
        close_stream();
        sample_rate_ = 0;
        error_ = "Core reported an invalid audio sample rate";
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", error_.c_str());
        return;
    }

    const int requested_rate = static_cast<int>(std::lround(sample_rate));
    if (stream_ && sample_rate_ == requested_rate) {
        return;
    }

    close_stream();
    sample_rate_ = requested_rate;
    SDL_AudioSpec spec{};
    spec.freq = requested_rate;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        fail("SDL_OpenAudioDeviceStream");
        return;
    }

    const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(stream_);
    if (const char* name = SDL_GetAudioDeviceName(device)) {
        device_name_ = name;
    } else {
        device_name_ = "default playback";
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        close_stream();
        fail("SDL_ResumeAudioStreamDevice");
        return;
    }

    error_.clear();
    SDL_Log("Audio output opened: %s at %d Hz", device_name_.c_str(), requested_rate);
}

void SdlAudioOutput::write(const int16_t* stereo_interleaved, size_t frames) {
    if (!stream_ || !stereo_interleaved || frames == 0) {
        return;
    }
    constexpr size_t kBytesPerFrame = 2 * sizeof(int16_t);
    if (frames > static_cast<size_t>(INT_MAX) / kBytesPerFrame) {
        close_stream();
        error_ = "Core emitted an audio batch too large for SDL";
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", error_.c_str());
        return;
    }

    const int bytes = static_cast<int>(frames * kBytesPerFrame);
    if (!SDL_PutAudioStreamData(stream_, stereo_interleaved, bytes)) {
        fail("SDL_PutAudioStreamData");
        close_stream();
    }
}

void SdlAudioOutput::clear() {
    if (stream_ && !SDL_ClearAudioStream(stream_)) {
        fail("SDL_ClearAudioStream");
        close_stream();
    }
}

double SdlAudioOutput::queued_seconds() {
    if (!stream_ || sample_rate_ <= 0) {
        return 0.0;
    }
    const int bytes = SDL_GetAudioStreamQueued(stream_);
    if (bytes < 0) {
        fail("SDL_GetAudioStreamQueued");
        close_stream();
        return 0.0;
    }
    if (bytes == 0) {
        return 0.0;
    }
    constexpr double kBytesPerStereoFrame = 2.0 * sizeof(int16_t);
    return static_cast<double>(bytes) / (sample_rate_ * kBytesPerStereoFrame);
}

std::string SdlAudioOutput::status_text() const {
    if (has_error()) {
        return "Unavailable: " + error_;
    }
    if (stream_) {
        return "Device: " + device_name_;
    }
    if (audio_subsystem_initialized_) {
        return "Waiting for a core audio stream";
    }
    return "Audio subsystem unavailable";
}

void SdlAudioOutput::close_stream() {
    if (!stream_) {
        return;
    }
    SDL_PauseAudioStreamDevice(stream_);
    SDL_ClearAudioStream(stream_);
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    device_name_.clear();
}

void SdlAudioOutput::fail(const char* operation) {
    const char* detail = SDL_GetError();
    error_ = std::string(operation) + " failed";
    if (detail && *detail) {
        error_ += ": ";
        error_ += detail;
    }
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", error_.c_str());
}

} // namespace cw
