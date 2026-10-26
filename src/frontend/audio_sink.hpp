#pragma once

#include <cstddef>
#include <cstdint>

namespace cw {

// Receives interleaved signed 16-bit stereo samples from one core frame at a time.
class AudioSink {
  public:
    virtual ~AudioSink() = default;

    virtual void set_sample_rate(double sample_rate) = 0;
    virtual void write(const int16_t* stereo_interleaved, size_t frames) = 0;
    virtual void clear() = 0;
};

} // namespace cw
