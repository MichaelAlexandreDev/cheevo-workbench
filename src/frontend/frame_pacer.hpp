#pragma once

#include <chrono>
#include <cstddef>

namespace cw {

// SDL-free frame scheduling logic. Callers supply every timestamp and audio queue measurement.
class FramePacer {
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    enum class Mode { Audio, Clock };

    explicit FramePacer(
        double audio_target_latency_seconds = 0.050, size_t max_frames_per_tick = 3,
        std::chrono::milliseconds long_stall_threshold = std::chrono::milliseconds(250));

    void set_core_fps(double fps, TimePoint now);
    void set_audio_available(bool available, TimePoint now);
    // Returns true only when transitioning into pause, so the caller can clear its audio queue.
    bool set_paused(bool paused, TimePoint now);

    size_t frames_to_run(TimePoint now, double queued_audio_seconds);
    bool record_frames(size_t frames, TimePoint now);

    Mode mode() const { return audio_available_ ? Mode::Audio : Mode::Clock; }
    bool paused() const { return paused_; }
    double measured_fps() const { return measured_fps_; }

  private:
    void reset_clock(TimePoint now);

    double core_fps_ = 0.0;
    double audio_target_latency_seconds_;
    size_t max_frames_per_tick_;
    std::chrono::duration<double> long_stall_threshold_;
    bool audio_available_ = false;
    bool paused_ = false;

    bool clock_initialized_ = false;
    TimePoint last_clock_tick_{};
    double clock_accumulator_seconds_ = 0.0;

    bool measurement_initialized_ = false;
    TimePoint measurement_start_{};
    size_t measured_frame_count_ = 0;
    double measured_fps_ = 0.0;
};

} // namespace cw
