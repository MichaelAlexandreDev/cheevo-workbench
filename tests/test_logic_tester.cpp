// Live achievement-logic testing with rcheevos' rc_runtime, on synthetic memory.

#include "cw/devtools/logic_tester.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

bool has_event(const std::vector<cw::LogicEvent>& events, uint32_t id, cw::LogicEventKind kind) {
    return std::any_of(events.begin(), events.end(),
                       [&](const cw::LogicEvent& e) { return e.id == id && e.kind == kind; });
}

} // namespace

TEST_CASE("An achievement triggers on the exact frame its condition becomes true") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Five", "0xH0010=5", error); // 8-bit value at 0x10 equals 5
    REQUIRE_MESSAGE(id, error);
    CHECK(tester.state(*id) == cw::LogicState::Waiting);

    auto events = tester.do_frame(memory, 1); // false: Waiting -> Active
    CHECK(has_event(events, *id, cw::LogicEventKind::Activated));
    CHECK(tester.state(*id) == cw::LogicState::Active);

    events = tester.do_frame(memory, 2);
    CHECK_FALSE(has_event(events, *id, cw::LogicEventKind::Triggered));

    memory.bytes()[0x10] = 5;
    events = tester.do_frame(memory, 3);
    REQUIRE(has_event(events, *id, cw::LogicEventKind::Triggered));
    CHECK(events.back().frame == 3);
    CHECK(tester.state(*id) == cw::LogicState::Triggered);
}

TEST_CASE("A condition already true when added waits until it has been false once") {
    // rcheevos' WAITING state: an achievement must see its condition false before it can fire,
    // so loading a save state where it is already true does not unlock it.
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    memory.bytes()[0x10] = 5;
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Five", "0xH0010=5", error);
    REQUIRE(id);

    for (uint64_t frame = 1; frame <= 3; ++frame) {
        CHECK_FALSE(has_event(tester.do_frame(memory, frame), *id, cw::LogicEventKind::Triggered));
    }
    memory.bytes()[0x10] = 0;
    tester.do_frame(memory, 4);
    memory.bytes()[0x10] = 5;
    CHECK(has_event(tester.do_frame(memory, 5), *id, cw::LogicEventKind::Triggered));
}

TEST_CASE("Hit counts need the condition true on that many frames") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Three frames", "0xH0010=1.3.", error); // true on 3 frames
    REQUIRE(id);
    tester.do_frame(memory, 1);

    memory.bytes()[0x10] = 1;
    CHECK_FALSE(has_event(tester.do_frame(memory, 2), *id, cw::LogicEventKind::Triggered));
    CHECK_FALSE(has_event(tester.do_frame(memory, 3), *id, cw::LogicEventKind::Triggered));
    CHECK(has_event(tester.do_frame(memory, 4), *id, cw::LogicEventKind::Triggered));
}

TEST_CASE("reset() clears hit counts, as when a game is reloaded") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Three frames", "0xH0010=1.3.", error);
    REQUIRE(id);
    tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 1;
    tester.do_frame(memory, 2);
    tester.do_frame(memory, 3); // two hits so far

    tester.reset();
    CHECK(tester.state(*id) == cw::LogicState::Waiting);
    memory.bytes()[0x10] = 0;
    tester.do_frame(memory, 4); // Waiting -> Active
    memory.bytes()[0x10] = 1;
    CHECK_FALSE(has_event(tester.do_frame(memory, 5), *id, cw::LogicEventKind::Triggered));
    CHECK_FALSE(has_event(tester.do_frame(memory, 6), *id, cw::LogicEventKind::Triggered));
    CHECK(has_event(tester.do_frame(memory, 7), *id, cw::LogicEventKind::Triggered));
}

TEST_CASE("Measured values report the current value and the target") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Collect 10", "M:0xH0020>=10", error);
    REQUIRE(id);
    memory.bytes()[0x20] = 4;
    tester.do_frame(memory, 1);
    const auto measured = tester.measured(*id);
    REQUIRE(measured);
    CHECK(measured->first == 4);
    CHECK(measured->second == 10);
    CHECK_FALSE(tester.measured(9999));
}

TEST_CASE("Multi-byte and little-endian reads match the memory") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    // 0x  = 16-bit, 0xX = 32-bit, 0xW = 24-bit, all little-endian like the PS1.
    const auto id16 = tester.add("16", "0x 0030=4660", error);      // 0x1234
    const auto id32 = tester.add("32", "0xX0040=305419896", error); // 0x12345678
    const auto id24 = tester.add("24", "0xW0050=1193046", error);   // 0x123456
    REQUIRE((id16 && id32 && id24));
    tester.do_frame(memory, 1);
    memory.bytes()[0x30] = 0x34;
    memory.bytes()[0x31] = 0x12;
    memory.bytes()[0x40] = 0x78;
    memory.bytes()[0x41] = 0x56;
    memory.bytes()[0x42] = 0x34;
    memory.bytes()[0x43] = 0x12;
    memory.bytes()[0x50] = 0x56;
    memory.bytes()[0x51] = 0x34;
    memory.bytes()[0x52] = 0x12;
    const auto events = tester.do_frame(memory, 2);
    CHECK(has_event(events, *id16, cw::LogicEventKind::Triggered));
    CHECK(has_event(events, *id32, cw::LogicEventKind::Triggered));
    CHECK(has_event(events, *id24, cw::LogicEventKind::Triggered));
}

TEST_CASE("Invalid logic is refused with rcheevos' explanation") {
    cw::LogicTester tester;
    std::string error;
    CHECK_FALSE(tester.add("Broken", "0xH0010=", error));
    CHECK(error.starts_with("Invalid logic: "));
    CHECK(tester.achievements().empty());
}

TEST_CASE("An address the memory cannot provide disables the achievement") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto good = tester.add("Inside", "0xH0010=5", error);
    const auto bad = tester.add("Outside", "0xH9999=1", error);
    REQUIRE((good && bad));

    const auto events = tester.validate_addresses(memory, 0);
    REQUIRE(events.size() == 1);
    CHECK(events[0].id == *bad);
    CHECK(events[0].kind == cw::LogicEventKind::Disabled);
    CHECK(events[0].value == 0x9999);
    CHECK(tester.state(*bad) == cw::LogicState::Disabled);
    CHECK(tester.state(*good) == cw::LogicState::Waiting);
}

TEST_CASE("A triggered achievement can be re-armed and removed") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Five", "0xH0010=5", error);
    REQUIRE(id);
    tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 5;
    REQUIRE(has_event(tester.do_frame(memory, 2), *id, cw::LogicEventKind::Triggered));

    CHECK(tester.rearm(*id));
    CHECK(tester.state(*id) == cw::LogicState::Waiting);
    memory.bytes()[0x10] = 0;
    tester.do_frame(memory, 3);
    memory.bytes()[0x10] = 5;
    CHECK(has_event(tester.do_frame(memory, 4), *id, cw::LogicEventKind::Triggered));

    CHECK(tester.remove(*id));
    CHECK(tester.state(*id) == cw::LogicState::Inactive);
    CHECK_FALSE(tester.rearm(*id));
    CHECK(tester.achievements().empty());
}
