#include "cw/devtools/assisted_search.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

namespace {
const cw::AssistedSearchResult* find(const std::vector<cw::AssistedSearchResult>& results,
                                     uint32_t address) {
    const auto it = std::find_if(results.begin(), results.end(),
                                 [=](const auto& r) { return r.address == address; });
    return it == results.end() ? nullptr : &*it;
}
void advance(cw::AssistedSearch& search, const cw::MemoryReader& memory, int frames) {
    for (int i = 0; i < frames; ++i)
        REQUIRE(search.observe(memory));
}
} // namespace

TEST_CASE("Assisted search ranks flags, areas and counters while excluding noise") {
    cw::BufferMemory memory(std::vector<uint8_t>(128));
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    for (int frame = 1; frame <= 80; ++frame) {
        memory.bytes()[20] = static_cast<uint8_t>(frame * 37);
        if (frame % 12 == 0) {
            memory.bytes()[1] ^= 4;
            memory.bytes()[2] = static_cast<uint8_t>((memory.bytes()[2] + 2) % 6);
            ++memory.bytes()[3];
        }
        REQUIRE(search.observe(memory));
    }
    const auto results = search.results();
    REQUIRE(find(results, 1));
    CHECK(find(results, 1)->kind == cw::AssistedSearchKind::Flag);
    CHECK(find(results, 1)->bit_index == 2);
    REQUIRE(find(results, 2));
    CHECK(find(results, 2)->kind == cw::AssistedSearchKind::Area);
    REQUIRE(find(results, 3));
    CHECK(find(results, 3)->kind == cw::AssistedSearchKind::Counter);
    CHECK_FALSE(find(results, 20));
    CHECK_FALSE(find(results, 0));
}

TEST_CASE("Repeated moments intersect and optionally compare the marked value") {
    cw::BufferMemory memory(std::vector<uint8_t>(32));
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    advance(search, memory, 5);
    for (int mark = 0; mark < 3; ++mark) {
        memory.bytes()[1] = static_cast<uint8_t>(mark % 2 + 1);
        memory.bytes()[2 + mark] = 9;
        advance(search, memory, 1);
        REQUIRE(search.mark("door"));
        CHECK(search.mark_pending());
        CHECK_FALSE(search.mark("other"));
        advance(search, memory, 3);
        CHECK(search.mark_count("door") == static_cast<size_t>(mark + 1));
        advance(search, memory, 4);
    }
    const auto results = search.results(100, "door");
    REQUIRE(results.size() == 1);
    CHECK(results[0].address == 1);
    memory.bytes()[1] = 7;
    advance(search, memory, 1);
    REQUIRE(search.mark("door", true));
    advance(search, memory, 3);
    CHECK(search.results(100, "door").empty());
}

TEST_CASE("Assisted search handles holes, reset and bounded sessions") {
    cw::BufferMemory memory(
        std::vector<uint8_t>(12),
        {{0, 3, "a", true, false}, {4, 7, "hole", false, false}, {8, 11, "b", true, false}});
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    CHECK(search.tracked_bytes() == 8);
    CHECK(search.memory_usage_bytes() > 0);
    advance(search, memory, 4);
    memory.bytes()[8] = 1;
    advance(search, memory, 1);
    REQUIRE(search.mark("change"));
    advance(search, memory, 3);
    const auto results = search.results(100, "change");
    REQUIRE(results.size() == 1);
    CHECK(results[0].address == 8);
    cw::BufferMemory smaller(std::vector<uint8_t>(3));
    CHECK_FALSE(search.observe(smaller));
    CHECK_FALSE(search.active());
    CHECK_FALSE(search.error().empty());
    REQUIRE(search.start(smaller));
    CHECK(search.frame_count() == 0);
    CHECK(search.mark_count("change") == 0);
    search.reset();
    CHECK_FALSE(search.active());
    cw::BufferMemory empty(std::vector<uint8_t>{});
    CHECK_FALSE(search.start(empty));
    CHECK_FALSE(search.error().empty());
}

TEST_CASE("Moment windows require stability and accept a change after marking") {
    cw::BufferMemory memory(std::vector<uint8_t>(8));
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    advance(search, memory, 5);
    REQUIRE(search.mark("event"));
    memory.bytes()[0] = 1;
    memory.bytes()[1] = 1;
    advance(search, memory, 1);
    memory.bytes()[1] = 2; // a second change breaks pre-change stability
    advance(search, memory, 2);
    auto results = search.results(100, "event");
    REQUIRE(results.size() == 1);
    CHECK(results[0].address == 0);
    advance(search, memory, 5);
    for (size_t i = 1; i < cw::AssistedSearch::kMaxLabels; ++i) {
        REQUIRE(search.mark(std::to_string(i)));
        advance(search, memory, 3);
    }
    CHECK_FALSE(search.mark("one too many"));
    CHECK_FALSE(search.mark(""));
}

TEST_CASE("Kind filtering happens before the result limit") {
    cw::BufferMemory memory(std::vector<uint8_t>(10));
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    for (int frame = 1; frame <= 40; ++frame) {
        if (frame % 5 == 0) {
            memory.bytes()[0] ^= 1;
            ++memory.bytes()[1];
        }
        REQUIRE(search.observe(memory));
    }
    auto results = search.results(1, {}, cw::AssistedSearchKind::Counter);
    REQUIRE(results.size() == 1);
    CHECK(results[0].address == 1);
}

TEST_CASE("Matching a value compares against the first marked occurrence") {
    cw::BufferMemory memory(std::vector<uint8_t>(1));
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    advance(search, memory, 4);
    for (auto value : {1, 2, 1}) {
        memory.bytes()[0] = static_cast<uint8_t>(value);
        advance(search, memory, 1);
        REQUIRE(search.mark("place", value == 1));
        advance(search, memory, 7);
    }
    CHECK(search.results(10, "place").size() == 1);
}

TEST_CASE("Saturated counters never make constant noise appear stable") {
    cw::BufferMemory memory(std::vector<uint8_t>(1));
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    for (int frame = 0; frame < 270000; ++frame) {
        memory.bytes()[0] ^= 1;
        REQUIRE(search.observe(memory));
    }
    CHECK(search.results().empty());
}

TEST_CASE("Losing a readable byte invalidates the session even without map metadata changes") {
    struct Reader final : cw::MemoryReader {
        bool readable = true;
        uint32_t size() const override { return 2; }
        size_t read(uint32_t address, std::span<uint8_t> out) const override {
            if (address >= 2 || out.empty())
                return 0;
            size_t n =
                std::min<size_t>(out.size(), readable ? 2 - address : 1 - std::min(address, 1U));
            std::fill_n(out.begin(), n, 0);
            return n;
        }
        std::vector<cw::MemoryRegionInfo> regions() const override {
            return {{0, 1, "ram", true, false}};
        }
    } memory;
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    memory.readable = false;
    CHECK_FALSE(search.observe(memory));
    CHECK_FALSE(search.active());
    CHECK_FALSE(search.error().empty());
}

TEST_CASE("Short reads are combined and fragmentation is bounded") {
    struct Reader final : cw::MemoryReader {
        uint32_t length = 0;
        bool holes = false;
        uint8_t value = 0;
        uint32_t size() const override { return length; }
        size_t read(uint32_t address, std::span<uint8_t> out) const override {
            if (address >= length || out.empty() || (holes && address % 2))
                return 0;
            size_t n = holes ? 1 : std::min<size_t>(3, length - address);
            n = std::min(n, out.size());
            std::fill_n(out.begin(), n, value);
            return n;
        }
        std::vector<cw::MemoryRegionInfo> regions() const override {
            return {{0, length - 1, "partial", false, false}};
        }
    } memory;
    memory.length = 20;
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    CHECK(search.tracked_bytes() == 20);
    advance(search, memory, 4);
    memory.value = 1;
    REQUIRE(search.observe(memory));
    CHECK(search.results(1).size() == 1);
    memory.holes = true;
    CHECK_FALSE(search.observe(memory));
    memory.length = 2 * (cw::AssistedSearch::kMaxReadableSpans + 1);
    CHECK_FALSE(search.start(memory));
    CHECK(search.error().find("spans") != std::string::npos);
    CHECK_FALSE(search.active());
}

TEST_CASE("Cancelling a moment keeps completed marks and frees unused labels") {
    cw::BufferMemory memory(std::vector<uint8_t>(2));
    cw::AssistedSearch search;
    REQUIRE(search.start(memory));
    advance(search, memory, 4);
    memory.bytes()[0] = 1;
    advance(search, memory, 1);
    REQUIRE(search.mark("saved"));
    advance(search, memory, 3);
    REQUIRE(search.results(10, "saved").size() == 1);
    REQUIRE(search.mark("saved"));
    search.cancel_mark();
    CHECK_FALSE(search.mark_pending());
    CHECK(search.mark_count("saved") == 1);
    CHECK(search.results(10, "saved").size() == 1);
    for (size_t i = 0; i < 20; ++i) {
        REQUIRE(search.mark(std::to_string(i)));
        CHECK_FALSE(search.mark("concurrent"));
        search.cancel_mark();
    }
    search.cancel_mark();
    CHECK(search.mark_count("saved") == 1);
}
