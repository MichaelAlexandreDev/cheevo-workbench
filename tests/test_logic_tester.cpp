// Live achievement-logic testing with rcheevos' rc_runtime, on synthetic memory.

#include "cw/devtools/logic_tester.hpp"

#include <doctest/doctest.h>
#include <rc_runtime.h>
#include <rc_runtime_types.h>
#include <rhash/md5.h>

#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool has_event(const std::vector<cw::LogicEvent>& events, uint32_t id, cw::LogicEventKind kind) {
    return std::any_of(events.begin(), events.end(),
                       [&](const cw::LogicEvent& e) { return e.id == id && e.kind == kind; });
}

uint32_t read_u32(std::span<const uint8_t> data, size_t offset) {
    return uint32_t{data[offset]} | uint32_t{data[offset + 1]} << 8 |
           uint32_t{data[offset + 2]} << 16 | uint32_t{data[offset + 3]} << 24;
}

void write_u32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        data[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
    }
}

size_t rap_chunk_offset(std::span<const uint8_t> data, std::string_view chunk_id) {
    size_t offset = 4;
    while (offset + 8 <= data.size()) {
        if (chunk_id.size() == 4 &&
            std::memcmp(data.data() + offset, chunk_id.data(), chunk_id.size()) == 0) {
            return offset;
        }

        const size_t chunk_size = read_u32(data, offset + 4);
        if (chunk_size > data.size() - offset - 8) {
            break;
        }
        offset += 8 + chunk_size;
    }
    return data.size();
}

void refresh_rap_checksum(std::vector<uint8_t>& data) {
    md5_state_t checksum;
    md5_init(&checksum);
    md5_append(&checksum, data.data(), static_cast<int>(data.size() - 16));
    md5_finish(&checksum, data.data() + data.size() - 16);
}

struct RawRuntime {
    rc_runtime_t runtime{};

    RawRuntime() { rc_runtime_init(&runtime); }
    ~RawRuntime() { rc_runtime_destroy(&runtime); }

    RawRuntime(const RawRuntime&) = delete;
    RawRuntime& operator=(const RawRuntime&) = delete;
};

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

TEST_CASE("An address disabled in one game is read again after reset in a game that has it") {
    // rcheevos marks the memory reference of an unreadable address as "do not read"; a plain
    // rc_runtime_reset() would keep that mark, and the achievement would sit in Waiting/Active
    // without ever reading the address again (found in review of #41).
    cw::BufferMemory small(std::vector<uint8_t>(0x100, 0));
    cw::BufferMemory large(std::vector<uint8_t>(0x1000, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("High", "0xH0800=7", error);
    REQUIRE(id);
    REQUIRE(tester.validate_addresses(small, 0).size() == 1); // 0x800 is past the small memory
    CHECK(tester.state(*id) == cw::LogicState::Disabled);
    CHECK_FALSE(tester.rearm(*id)); // re-arming cannot fix an unreadable address

    tester.reset(); // a new game is loaded
    CHECK(tester.validate_addresses(large, 0).empty());
    CHECK(tester.state(*id) == cw::LogicState::Waiting);
    tester.do_frame(large, 1);
    large.bytes()[0x800] = 7;
    CHECK(has_event(tester.do_frame(large, 2), *id, cw::LogicEventKind::Triggered));
}

TEST_CASE("Values starting at the last readable byte read the missing bytes as zero") {
    // rcheevos asks for 2 or 4 bytes (24-bit values are 4-byte reads, masked); past the end of
    // memory the missing bytes are zero, as in RAIntegration's EmulatorMemoryContext::ReadMemory.
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id16 = tester.add("16 at end", "0x 00FF=18", error);
    const auto id24 = tester.add("24 at end", "0xW00FF=18", error);
    REQUIRE((id16 && id24));
    CHECK(tester.validate_addresses(memory, 0).empty()); // the first byte is readable
    tester.do_frame(memory, 1);
    memory.bytes()[0xFF] = 18;
    const auto events = tester.do_frame(memory, 2);
    CHECK(has_event(events, *id16, cw::LogicEventKind::Triggered));
    CHECK(has_event(events, *id24, cw::LogicEventKind::Triggered));
}

TEST_CASE("Saved progress restores measured hit counts and rewinds a triggered achievement") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(memory, 1); // false once: Waiting -> Active
    memory.bytes()[0x10] = 1;
    tester.do_frame(memory, 2);
    tester.do_frame(memory, 3); // two of five hits

    const auto before_save = tester.measured(*id);
    REQUIRE(before_save);
    CHECK(before_save->first == 2);
    CHECK(before_save->second == 5);

    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(tester.serialize_progress(progress, error), error);
    REQUIRE_FALSE(progress.empty());

    tester.do_frame(memory, 4);
    tester.do_frame(memory, 5);
    REQUIRE(has_event(tester.do_frame(memory, 6), *id, cw::LogicEventKind::Triggered));
    CHECK(tester.state(*id) == cw::LogicState::Triggered);

    REQUIRE_MESSAGE(tester.deserialize_progress(progress, error), error);
    CHECK(tester.state(*id) == cw::LogicState::Active);
    const auto restored = tester.measured(*id);
    REQUIRE(restored);
    CHECK(restored->first == 2);
    CHECK(restored->second == 5);

    tester.do_frame(memory, 7);
    tester.do_frame(memory, 8);
    CHECK(has_event(tester.do_frame(memory, 9), *id, cw::LogicEventKind::Triggered));
}

TEST_CASE("Saved progress preserves prior and previous-frame memory values") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    memory.bytes()[0x10] = 5;
    memory.bytes()[0x11] = 6;

    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Previous and prior", "d0xH0010=10_p0xH0011=7", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(memory, 1); // initial previous/prior values do not match
    memory.bytes()[0x10] = 10;
    memory.bytes()[0x11] = 7;
    tester.do_frame(memory, 2); // current values differ from the values saved for next frame

    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(tester.serialize_progress(progress, error), error);

    memory.bytes()[0x10] = 12;
    memory.bytes()[0x11] = 8;
    CHECK(has_event(tester.do_frame(memory, 3), *id, cw::LogicEventKind::Triggered));

    REQUIRE_MESSAGE(tester.deserialize_progress(progress, error), error);
    CHECK(tester.state(*id) == cw::LogicState::Active);
    CHECK(has_event(tester.do_frame(memory, 4), *id, cw::LogicEventKind::Triggered));
}

TEST_CASE("Corrupt and truncated progress is rejected without changing the runtime") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 1;
    tester.do_frame(memory, 2);
    tester.do_frame(memory, 3); // preserve a two-of-five progress point in the live runtime

    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(tester.serialize_progress(progress, error), error);
    REQUIRE_FALSE(progress.empty());

    for (size_t prefix_size = 0; prefix_size < progress.size(); ++prefix_size) {
        error.clear();
        const std::span<const uint8_t> truncated(progress.data(), prefix_size);
        CHECK_FALSE(tester.deserialize_progress(truncated, error));
        CHECK(tester.state(*id) == cw::LogicState::Active);
        const auto measured = tester.measured(*id);
        REQUIRE(measured);
        CHECK(measured->first == 2);
        CHECK(measured->second == 5);
    }

    auto corrupted = progress;
    corrupted.back() ^= 0x01; // invalidate the RAP checksum
    error.clear();
    CHECK_FALSE(tester.deserialize_progress(corrupted, error));
    CHECK(tester.state(*id) == cw::LogicState::Active);
    const auto measured = tester.measured(*id);
    REQUIRE(measured);
    CHECK(measured->first == 2);
    CHECK(measured->second == 5);

    tester.do_frame(memory, 4);
    tester.do_frame(memory, 5);
    CHECK(has_event(tester.do_frame(memory, 6), *id, cw::LogicEventKind::Triggered));
}

TEST_CASE("Structurally malformed RAP chunks are rejected without changing progress") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Five hits", "M:0xH0010=1.5.", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 1;
    tester.do_frame(memory, 2);
    tester.do_frame(memory, 3);

    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(tester.serialize_progress(progress, error), error);
    const size_t done_offset = rap_chunk_offset(progress, "DONE");
    const size_t achievement_offset = rap_chunk_offset(progress, "ACHV");
    const size_t memrefs_offset = rap_chunk_offset(progress, "MREF");
    REQUIRE(done_offset < progress.size());
    REQUIRE(achievement_offset < progress.size());
    REQUIRE(memrefs_offset < progress.size());

    const auto reject_without_mutation = [&](const std::vector<uint8_t>& malformed) {
        error.clear();
        CHECK_FALSE(tester.deserialize_progress(malformed, error));
        CHECK(tester.state(*id) == cw::LogicState::Active);
        const auto measured = tester.measured(*id);
        REQUIRE(measured);
        CHECK(measured->first == 2);
        CHECK(measured->second == 5);

        std::vector<uint8_t> after_failure;
        REQUIRE_MESSAGE(tester.serialize_progress(after_failure, error), error);
        CHECK(after_failure == progress);
    };

    SUBCASE("DONE has no digest payload") {
        auto malformed = progress;
        write_u32(malformed, done_offset + 4, 0);
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("Matching ACHV has no trigger payload") {
        auto malformed = progress;
        write_u32(malformed, achievement_offset + 4, 20); // id and logic checksum only
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("Chunk length overflows the remaining buffer") {
        auto malformed = progress;
        write_u32(malformed, memrefs_offset + 4, 0xFFFFFFFF);
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("Indirect condition flag claims missing operand data") {
        cw::BufferMemory pointer_memory(std::vector<uint8_t>(0x100, 0));
        cw::LogicTester pointer_tester;
        const auto pointer_id =
            pointer_tester.add("Indirect read", "I:0xH0002_0xH0001=3_0xH0004=99", error);
        REQUIRE_MESSAGE(pointer_id, error);
        pointer_tester.do_frame(pointer_memory, 1);

        std::vector<uint8_t> pointer_progress;
        REQUIRE_MESSAGE(pointer_tester.serialize_progress(pointer_progress, error), error);
        const size_t pointer_achievement = rap_chunk_offset(pointer_progress, "ACHV");
        REQUIRE(pointer_achievement < pointer_progress.size());
        const auto original_pointer_progress = pointer_progress;

        // The third condition is a direct memory read. Mark it as indirect without adding the
        // eight serialized value/prior bytes that the flag claims should follow it.
        // The second condition has one indirect operand, so its serialized value and prior add
        // eight bytes before the third condition's hit-count/flags pair.
        const size_t final_condition_flags = pointer_achievement + 8 + 20 + 8 + 4 + 8 + 8 + 8 + 4;
        const uint32_t flags = read_u32(pointer_progress, final_condition_flags);
        CHECK((flags & 0x00010000) == 0);
        write_u32(pointer_progress, final_condition_flags, flags | 0x00010000);
        refresh_rap_checksum(pointer_progress);

        error.clear();
        CHECK_FALSE(pointer_tester.deserialize_progress(pointer_progress, error));
        CHECK(pointer_tester.state(*pointer_id) == cw::LogicState::Active);
        std::vector<uint8_t> after_failure;
        REQUIRE_MESSAGE(pointer_tester.serialize_progress(after_failure, error), error);
        CHECK(after_failure == original_pointer_progress);
    }

    CHECK(tester.state(*id) == cw::LogicState::Active);
    const auto unchanged_measurement = tester.measured(*id);
    REQUIRE(unchanged_measurement);
    CHECK(unchanged_measurement->first == 2);
    CHECK(unchanged_measurement->second == 5);
    tester.do_frame(memory, 4);
    tester.do_frame(memory, 5);
    CHECK(has_event(tester.do_frame(memory, 6), *id, cw::LogicEventKind::Triggered));
}

TEST_CASE("Changed definitions reset and missing definitions are ignored on progress load") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester saved_tester;
    std::string error;
    const auto saved_id = saved_tester.add("Original", "M:0xH0010=1.5.", error);
    const auto removed_id = saved_tester.add("Removed", "0xH0030=9", error);
    REQUIRE((saved_id && removed_id));
    CHECK(*saved_id == 1);
    CHECK(*removed_id == 2);

    saved_tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 1;
    saved_tester.do_frame(memory, 2);
    saved_tester.do_frame(memory, 3); // old definition has two hits

    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(saved_tester.serialize_progress(progress, error), error);

    cw::LogicTester current_tester;
    const auto changed_id = current_tester.add("Changed", "M:0xH0020=1.5.", error);
    REQUIRE(changed_id);
    CHECK(*changed_id == *saved_id); // match the old definition's id, but not its checksum

    REQUIRE_MESSAGE(current_tester.deserialize_progress(progress, error), error);
    CHECK(current_tester.state(*changed_id) == cw::LogicState::Waiting);
    CHECK(current_tester.state(*removed_id) == cw::LogicState::Inactive);
    const auto reset_measurement = current_tester.measured(*changed_id);
    REQUIRE(reset_measurement);
    CHECK(reset_measurement->first == 0);
    CHECK(reset_measurement->second == 5);

    current_tester.do_frame(memory, 4); // false once: Waiting -> Active
    memory.bytes()[0x20] = 1;
    current_tester.do_frame(memory, 5);
    current_tester.do_frame(memory, 6);
    current_tester.do_frame(memory, 7);
    current_tester.do_frame(memory, 8);
    CHECK(
        has_event(current_tester.do_frame(memory, 9), *changed_id, cw::LogicEventKind::Triggered));
}

TEST_CASE("Memory-backed progress loads with empty and constant-only definitions") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester saved_tester;
    std::string error;
    const auto saved_id = saved_tester.add("Memory-backed", "0xH0010=5", error);
    REQUIRE_MESSAGE(saved_id, error);
    saved_tester.do_frame(memory, 1);

    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(saved_tester.serialize_progress(progress, error), error);
    REQUIRE(rap_chunk_offset(progress, "MREF") < progress.size());

    SUBCASE("No current definitions") {
        cw::LogicTester empty_tester;
        REQUIRE_MESSAGE(empty_tester.deserialize_progress(progress, error), error);
        CHECK(empty_tester.state(*saved_id) == cw::LogicState::Inactive);
        CHECK(empty_tester.achievements().empty());
    }

    SUBCASE("Current definition has no memory references") {
        cw::LogicTester constant_tester;
        const auto constant_id = constant_tester.add("Constant false", "0=1", error);
        REQUIRE_MESSAGE(constant_id, error);
        REQUIRE(*constant_id == *saved_id);

        REQUIRE_MESSAGE(constant_tester.deserialize_progress(progress, error), error);
        CHECK(constant_tester.state(*constant_id) == cw::LogicState::Waiting);
        CHECK(constant_tester.achievements().size() == 1);
    }
}

TEST_CASE("Saved terminal states restore for matching definitions") {
    SUBCASE("Triggered achievement") {
        cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
        cw::LogicTester saved_tester;
        cw::LogicTester current_tester;
        std::string error;
        const auto saved_id = saved_tester.add("Five", "0xH0010=5", error);
        const auto current_id = current_tester.add("Five", "0xH0010=5", error);
        REQUIRE((saved_id && current_id));

        saved_tester.do_frame(memory, 1);
        memory.bytes()[0x10] = 5;
        REQUIRE(
            has_event(saved_tester.do_frame(memory, 2), *saved_id, cw::LogicEventKind::Triggered));

        std::vector<uint8_t> progress;
        REQUIRE_MESSAGE(saved_tester.serialize_progress(progress, error), error);
        const size_t extension_offset = rap_chunk_offset(progress, "CWST");
        REQUIRE(extension_offset < progress.size());
        CHECK(read_u32(progress, extension_offset + 4) == 28);

        RawRuntime standard_runtime;
        REQUIRE(rc_runtime_activate_achievement(&standard_runtime.runtime, *saved_id, "0xH0010=5",
                                                nullptr, 0) == RC_OK);
        CHECK(rc_runtime_deserialize_progress_sized(&standard_runtime.runtime, progress.data(),
                                                    static_cast<uint32_t>(progress.size()),
                                                    nullptr) == RC_OK);

        REQUIRE_MESSAGE(current_tester.deserialize_progress(progress, error), error);
        CHECK(current_tester.state(*current_id) == cw::LogicState::Triggered);
    }

    SUBCASE("Disabled achievement") {
        cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
        cw::LogicTester saved_tester;
        cw::LogicTester current_tester;
        std::string error;
        const auto saved_id = saved_tester.add("Outside", "0xH9999=1", error);
        const auto current_id = current_tester.add("Outside", "0xH9999=1", error);
        REQUIRE((saved_id && current_id));

        const auto disabled_events = saved_tester.validate_addresses(memory, 1);
        REQUIRE(has_event(disabled_events, *saved_id, cw::LogicEventKind::Disabled));
        CHECK(saved_tester.state(*saved_id) == cw::LogicState::Disabled);

        std::vector<uint8_t> progress;
        REQUIRE_MESSAGE(saved_tester.serialize_progress(progress, error), error);
        REQUIRE_MESSAGE(current_tester.deserialize_progress(progress, error), error);
        CHECK(current_tester.state(*current_id) == cw::LogicState::Disabled);
    }
}
