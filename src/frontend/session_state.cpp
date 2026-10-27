#include "session_state.hpp"

namespace cw {

void SessionState::set_paused(bool paused) {
    mode_ = paused ? Mode::Paused : Mode::Running;
}

void SessionState::toggle_pause() {
    set_paused(!paused());
}

void SessionState::request_frame_advance() {
    if (mode_ == Mode::Paused) {
        mode_ = Mode::FrameAdvance;
    }
}

void SessionState::finish_frame_advance() {
    if (mode_ == Mode::FrameAdvance) {
        mode_ = Mode::Paused;
    }
}

size_t SessionState::frames_to_run(FramePacer& pacer, FramePacer::TimePoint now,
                                   double queued_audio_seconds) const {
    if (mode_ == Mode::Paused) {
        return 0;
    }
    if (mode_ == Mode::FrameAdvance) {
        return 1;
    }
    return pacer.frames_to_run(now, queued_audio_seconds);
}

} // namespace cw
