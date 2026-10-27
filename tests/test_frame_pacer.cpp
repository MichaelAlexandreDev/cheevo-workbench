#include "frame_pacer.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <utility>

namespace {

using Pacer = cw::FramePacer;

Pacer::TimePoint at_seconds(double seconds) {
    return Pacer::TimePoint{} + std::chrono::duration_cast<Pacer::Clock::duration>(
                                    std::chrono::duration<double>(seconds));
}

std::pair<size_t, double> simulate_clock_pacing(double refresh_hz, double core_fps,
                                                double seconds) {
    Pacer pacer;
    pacer.set_core_fps(core_fps, at_seconds(0.0));
    pacer.set_audio_available(false, at_seconds(0.0));

    size_t frames = 0;
    const size_t ticks = static_cast<size_t>(refresh_hz * seconds);
    for (size_t tick = 0; tick <= ticks; ++tick) {
        const double now_seconds = static_cast<double>(tick) / refresh_hz;
        const auto now = at_seconds(now_seconds);
        const size_t due = pacer.frames_to_run(now, 0.0);
        frames += due;
        pacer.record_frames(due, now);
    }
    return {frames, pacer.measured_fps()};
}

} // namespace

TEST_CASE("Clock pacing follows core fps independently of display refresh") {
    constexpr double kSeconds = 10.0;
    const auto [frames_144_60, measured_144_60] = simulate_clock_pacing(144.0, 60.0, kSeconds);
    const auto [frames_60_60, measured_60_60] = simulate_clock_pacing(60.0, 60.0, kSeconds);
    const auto [frames_144_50, measured_144_50] = simulate_clock_pacing(144.0, 50.0, kSeconds);
    const auto [frames_60_50, measured_60_50] = simulate_clock_pacing(60.0, 50.0, kSeconds);

    CHECK(frames_144_60 == static_cast<size_t>(60.0 * kSeconds));
    CHECK(frames_60_60 == static_cast<size_t>(60.0 * kSeconds));
    CHECK(frames_144_50 == static_cast<size_t>(50.0 * kSeconds));
    CHECK(frames_60_50 == static_cast<size_t>(50.0 * kSeconds));
    CHECK(measured_144_60 == doctest::Approx(60.0).epsilon(0.02));
    CHECK(measured_60_60 == doctest::Approx(60.0).epsilon(0.02));
    CHECK(measured_144_50 == doctest::Approx(50.0).epsilon(0.02));
    CHECK(measured_60_50 == doctest::Approx(50.0).epsilon(0.02));
}

TEST_CASE("Audio pacing fills toward the latency target with a per-tick limit") {
    Pacer pacer;
    pacer.set_core_fps(60.0, at_seconds(0.0));
    pacer.set_audio_available(true, at_seconds(0.0));

    CHECK(pacer.mode() == Pacer::Mode::Audio);
    CHECK(pacer.frames_to_run(at_seconds(0.0), 0.0) == 3);
    CHECK(pacer.frames_to_run(at_seconds(0.01), 0.020) == 2);
    CHECK(pacer.frames_to_run(at_seconds(0.02), 0.050) == 0);
}

TEST_CASE("Audio pacing resynchronizes after a long stall") {
    Pacer pacer;
    pacer.set_core_fps(60.0, at_seconds(0.0));
    pacer.set_audio_available(true, at_seconds(0.0));

    CHECK(pacer.frames_to_run(at_seconds(0.0), 0.0) == 3);
    CHECK(pacer.frames_to_run(at_seconds(1.0), 0.0) == 0);
    CHECK(pacer.frames_to_run(at_seconds(1.0 + 1.0 / 60.0), 0.0) == 3);
}

TEST_CASE("Pausing returns zero frames and reports the queue-clear transition") {
    Pacer pacer;
    pacer.set_core_fps(60.0, at_seconds(0.0));
    pacer.set_audio_available(true, at_seconds(0.0));

    CHECK(pacer.set_paused(true, at_seconds(0.1)));
    CHECK(pacer.paused());
    CHECK(pacer.frames_to_run(at_seconds(0.1), 0.0) == 0);
    CHECK_FALSE(pacer.set_paused(true, at_seconds(0.2)));
    CHECK_FALSE(pacer.set_paused(false, at_seconds(0.3)));
    CHECK_FALSE(pacer.paused());
    CHECK(pacer.frames_to_run(at_seconds(0.3), 0.0) == 3);
}

TEST_CASE("A long clock stall drops overdue frames instead of producing a burst") {
    Pacer pacer;
    pacer.set_core_fps(60.0, at_seconds(0.0));
    pacer.set_audio_available(false, at_seconds(0.0));

    CHECK(pacer.frames_to_run(at_seconds(0.0), 0.0) == 0);
    CHECK(pacer.frames_to_run(at_seconds(1.0), 0.0) == 0);
    CHECK(pacer.frames_to_run(at_seconds(1.0 + 1.0 / 60.0), 0.0) == 1);
    CHECK(pacer.frames_to_run(at_seconds(1.0 + 2.0 / 60.0), 0.0) == 1);
}

TEST_CASE("Measured FPS uses a five-second window and ignores paused time") {
    Pacer pacer;
    pacer.set_core_fps(60.0, at_seconds(0.0));

    CHECK_FALSE(pacer.record_frames(30, at_seconds(0.5)));
    CHECK(pacer.measured_fps() == 0.0);
    CHECK(pacer.record_frames(270, at_seconds(5.0)));
    CHECK(pacer.measured_fps() == doctest::Approx(60.0));

    CHECK_FALSE(pacer.record_frames(80, at_seconds(6.0)));
    CHECK(pacer.measured_fps() == doctest::Approx(60.0));
    CHECK(pacer.set_paused(true, at_seconds(7.0)));
    CHECK_FALSE(pacer.record_frames(200, at_seconds(12.0)));
    CHECK(pacer.measured_fps() == doctest::Approx(60.0));

    CHECK_FALSE(pacer.set_paused(false, at_seconds(12.0)));
    CHECK_FALSE(pacer.record_frames(60, at_seconds(13.0)));
    CHECK(pacer.measured_fps() == doctest::Approx(60.0));
}
