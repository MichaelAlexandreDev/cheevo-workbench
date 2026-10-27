#include "frame_pacer.hpp"
#include "session_state.hpp"

#include <doctest/doctest.h>

#include <chrono>

namespace {

using Pacer = cw::FramePacer;

Pacer::TimePoint at_seconds(double seconds) {
    return Pacer::TimePoint{} + std::chrono::duration_cast<Pacer::Clock::duration>(
                                    std::chrono::duration<double>(seconds));
}

} // namespace

TEST_CASE("Paused sessions schedule no frames and frame advance returns to pause") {
    Pacer pacer;
    pacer.set_core_fps(60.0, at_seconds(0.0));
    pacer.set_audio_available(false, at_seconds(0.0));
    cw::SessionState session;

    session.set_paused(true);
    CHECK(pacer.set_paused(session.paused(), at_seconds(0.0)));
    CHECK(session.frames_to_run(pacer, at_seconds(1.0), 0.0) == 0);

    session.request_frame_advance();
    CHECK(session.mode() == cw::SessionState::Mode::FrameAdvance);
    CHECK(session.paused());
    CHECK(session.frames_to_run(pacer, at_seconds(1.0), 0.0) == 1);
    session.finish_frame_advance();
    CHECK(session.mode() == cw::SessionState::Mode::Paused);
    CHECK(session.frames_to_run(pacer, at_seconds(1.0), 0.0) == 0);

    session.set_paused(false);
    CHECK_FALSE(session.paused());
    CHECK_FALSE(pacer.set_paused(session.paused(), at_seconds(2.0)));
    CHECK(session.frames_to_run(pacer, at_seconds(2.0), 0.0) == 0);
    CHECK(session.frames_to_run(pacer, at_seconds(2.0 + 1.0 / 60.0), 0.0) == 1);
}

TEST_CASE("Frame advance requests are ignored while running") {
    Pacer pacer;
    pacer.set_core_fps(60.0, at_seconds(0.0));
    cw::SessionState session;

    session.request_frame_advance();
    CHECK(session.mode() == cw::SessionState::Mode::Running);
    CHECK(session.frames_to_run(pacer, at_seconds(0.0), 0.0) == 0);
}
