#pragma once

#include "audio_sink.hpp"

#include <SDL3/SDL_audio.h>

#include <string>

namespace cw {

class SdlAudioOutput final : public AudioSink {
  public:
    SdlAudioOutput() = default;
    ~SdlAudioOutput() override;
    SdlAudioOutput(const SdlAudioOutput&) = delete;
    SdlAudioOutput& operator=(const SdlAudioOutput&) = delete;

    // Audio initialization is optional: a failure is retained for the Status window and does
    // not prevent the video front-end from opening.
    bool initialize();
    void shutdown();

    void set_sample_rate(double sample_rate) override;
    void write(const int16_t* stereo_interleaved, size_t frames) override;
    void clear() override;

    bool available() const { return stream_ != nullptr; }
    bool has_error() const { return !error_.empty(); }
    double queued_seconds();
    std::string status_text() const;

  private:
    void close_stream();
    void fail(const char* operation);

    SDL_AudioStream* stream_ = nullptr;
    int sample_rate_ = 0;
    bool audio_subsystem_initialized_ = false;
    std::string device_name_;
    std::string error_;
};

} // namespace cw
