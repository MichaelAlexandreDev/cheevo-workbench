#pragma once

#include "frame_pacer.hpp"

#include <cstddef>

namespace cw {

// SDL-free emulation controls layered over FramePacer. A frame advance is a one-turn state that
// schedules one frame and returns to pause as soon as that frame has run.
class SessionState {
  public:
    enum class Mode { Running, Paused, FrameAdvance };

    void set_paused(bool paused);
    void toggle_pause();
    void request_frame_advance();
    void finish_frame_advance();

    size_t frames_to_run(FramePacer& pacer, FramePacer::TimePoint now,
                         double queued_audio_seconds) const;

    Mode mode() const { return mode_; }
    bool paused() const { return mode_ != Mode::Running; }
    bool frame_advance_pending() const { return mode_ == Mode::FrameAdvance; }

  private:
    Mode mode_ = Mode::Running;
};

} // namespace cw
