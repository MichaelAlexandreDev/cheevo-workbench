#include "frame_pacer.hpp"

#include <algorithm>
#include <cmath>

namespace cw {

FramePacer::FramePacer(double audio_target_latency_seconds, size_t max_frames_per_tick,
                       std::chrono::milliseconds long_stall_threshold)
    : audio_target_latency_seconds_(std::max(0.0, audio_target_latency_seconds)),
      max_frames_per_tick_(std::max<size_t>(1, max_frames_per_tick)),
      long_stall_threshold_(long_stall_threshold) {}

void FramePacer::set_core_fps(double fps, TimePoint now) {
    const double valid_fps = std::isfinite(fps) && fps > 0.0 ? fps : 0.0;
    if (std::abs(core_fps_ - valid_fps) < 1e-9) {
        return;
    }
    core_fps_ = valid_fps;
    reset_clock(now);
    measurement_initialized_ = true;
    measurement_start_ = now;
    measured_frame_count_ = 0;
    measured_fps_ = 0.0;
}

void FramePacer::set_audio_available(bool available, TimePoint now) {
    if (audio_available_ == available) {
        return;
    }
    audio_available_ = available;
    reset_clock(now);
}

bool FramePacer::set_paused(bool paused, TimePoint now) {
    if (paused_ == paused) {
        return false;
    }
    paused_ = paused;
    reset_clock(now);
    // Time spent paused must not drag the next measured-FPS sample down.
    measurement_initialized_ = true;
    measurement_start_ = now;
    measured_frame_count_ = 0;
    return paused;
}

size_t FramePacer::frames_to_run(TimePoint now, double queued_audio_seconds) {
    if (paused_ || core_fps_ <= 0.0) {
        return 0;
    }

    if (audio_available_) {
        if (clock_initialized_) {
            const double elapsed = std::chrono::duration<double>(now - last_clock_tick_).count();
            last_clock_tick_ = now;
            if (elapsed < 0.0 || elapsed > long_stall_threshold_.count()) {
                clock_accumulator_seconds_ = 0.0;
                return 0;
            }
        } else {
            reset_clock(now);
        }

        const double queued =
            std::isfinite(queued_audio_seconds) ? std::max(0.0, queued_audio_seconds) : 0.0;
        const double deficit = audio_target_latency_seconds_ - queued;
        if (deficit <= 0.0) {
            return 0;
        }
        const double needed = std::ceil(deficit * core_fps_);
        if (needed >= static_cast<double>(max_frames_per_tick_)) {
            return max_frames_per_tick_;
        }
        return std::max<size_t>(1, static_cast<size_t>(needed));
    }

    if (!clock_initialized_) {
        reset_clock(now);
        return 0;
    }

    const double elapsed = std::chrono::duration<double>(now - last_clock_tick_).count();
    last_clock_tick_ = now;
    if (elapsed < 0.0 || elapsed > long_stall_threshold_.count()) {
        clock_accumulator_seconds_ = 0.0;
        return 0;
    }

    clock_accumulator_seconds_ += elapsed;
    const double frame_period = 1.0 / core_fps_;
    const double available = std::floor(clock_accumulator_seconds_ / frame_period + 1e-6);
    if (available < 1.0) {
        return 0;
    }

    if (available > static_cast<double>(max_frames_per_tick_)) {
        // Drop the overdue whole frames. Keeping them would create repeated catch-up bursts.
        clock_accumulator_seconds_ = std::fmod(clock_accumulator_seconds_, frame_period);
        return max_frames_per_tick_;
    }

    const size_t due = static_cast<size_t>(available);
    clock_accumulator_seconds_ -= static_cast<double>(due) * frame_period;
    return due;
}

bool FramePacer::record_frames(size_t frames, TimePoint now) {
    if (paused_) {
        return false;
    }
    if (!measurement_initialized_) {
        measurement_initialized_ = true;
        measurement_start_ = now;
    }
    measured_frame_count_ += frames;

    const double elapsed = std::chrono::duration<double>(now - measurement_start_).count();
    if (elapsed >= 5.0) {
        measured_fps_ = static_cast<double>(measured_frame_count_) / elapsed;
        measured_frame_count_ = 0;
        measurement_start_ = now;
        return true;
    }
    return false;
}

void FramePacer::reset_clock(TimePoint now) {
    clock_initialized_ = true;
    last_clock_tick_ = now;
    clock_accumulator_seconds_ = 0.0;
}

} // namespace cw
