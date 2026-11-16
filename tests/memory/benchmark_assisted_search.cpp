// Standalone release benchmark using synthetic 2 MiB RAM.
#include "cw/memory/assisted_search.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

using Clock = std::chrono::steady_clock;
static double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

static bool bench(const char* label, size_t stride, int change_every) {
    cw::BufferMemory memory(std::vector<uint8_t>(2 * 1024 * 1024));
    cw::AssistedSearch search;
    if (!search.start(memory))
        return false;
    constexpr int frames = 120;
    double observe = 0;
    for (int frame = 0; frame < frames; ++frame) {
        if (frame % change_every == change_every - 1) {
            for (size_t i = 0; i < memory.bytes().size(); i += stride)
                ++memory.bytes()[i];
        }
        const auto start = Clock::now();
        if (!search.observe(memory))
            return false;
        observe += elapsed_ms(start);
    }
    auto start = Clock::now();
    if (!search.mark("event"))
        return false;
    const double mark = elapsed_ms(start);
    double pending = 0;
    for (int i = 0; i < 3; ++i) {
        start = Clock::now();
        if (!search.observe(memory))
            return false;
        pending += elapsed_ms(start);
    }
    start = Clock::now();
    const auto result = search.results(100);
    const double ranking = elapsed_ms(start);
    start = Clock::now();
    const auto marked = search.results(100, "event");
    const double marked_ranking = elapsed_ms(start);
    std::printf("%s: observe %.3f ms/frame; mark %.3f ms; pending frames %.3f ms total; "
                "ranking %.3f ms (%zu); marked ranking %.3f ms (%zu); memory %zu bytes\n",
                label, observe / frames, mark, pending, ranking, result.size(), marked_ranking,
                marked.size(), search.memory_usage_bytes());
    return true;
}

int main() {
    return bench("sparse (512 bytes / 8 frames)", 4096, 8) &&
                   bench("scattered (512 bytes / frame)", 4096, 1) &&
                   bench("dense (2 MiB / frame)", 1, 1)
               ? 0
               : 1;
}
