#include "cw/devtools/watch_list.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

TEST_CASE("Watch list decodes every memory size and byte order") {
    cw::BufferMemory memory({0x89, 0xAB, 0xCD, 0xEF});
    cw::WatchList watches;
    constexpr std::array<uint32_t, 14> little_endian_values = {
        1, 0, 0, 1, 0, 0, 0, 1, 9, 8, 0x89, 0xAB89, 0xCDAB89, 0xEFCDAB89};

    for (unsigned i = 0; i < little_endian_values.size(); ++i) {
        const auto size = static_cast<cw::MemoryValueSize>(i);
        REQUIRE(watches.add(0, size, cw::ByteOrder::LittleEndian));
    }
    REQUIRE(watches.add(0, cw::MemoryValueSize::SixteenBit, cw::ByteOrder::BigEndian));
    REQUIRE(watches.add(0, cw::MemoryValueSize::TwentyFourBit, cw::ByteOrder::BigEndian));
    REQUIRE(watches.add(0, cw::MemoryValueSize::ThirtyTwoBit, cw::ByteOrder::BigEndian));
    REQUIRE(watches.add(0, cw::MemoryValueSize::EightBit, cw::ByteOrder::BigEndian));

    watches.update(memory, 0);
    REQUIRE(watches.size() == 18);
    for (size_t i = 0; i < little_endian_values.size(); ++i) {
        REQUIRE(watches.items()[i].current_value.has_value());
        CHECK(*watches.items()[i].current_value == little_endian_values[i]);
    }
    CHECK(watches.items()[14].current_value == 0x89ABu);
    CHECK(watches.items()[15].current_value == 0x89ABCDu);
    CHECK(watches.items()[16].current_value == 0x89ABCDEFu);
    CHECK(watches.items()[17].current_value == 0x89u);
}

TEST_CASE("Watch list rejects duplicates and supports removal and reordering") {
    cw::WatchList watches;
    REQUIRE(watches.add(0x10, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian, "Area"));
    CHECK_FALSE(
        watches.add(0x10, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian, "Duplicate"));
    REQUIRE(watches.add(0x10, cw::MemoryValueSize::EightBit, cw::ByteOrder::BigEndian));
    REQUIRE(watches.add(0x20, cw::MemoryValueSize::SixteenBit, cw::ByteOrder::LittleEndian));

    CHECK(watches.size() == 3);
    CHECK(watches.move(0, 2));
    CHECK(watches.items()[0].byte_order == cw::ByteOrder::BigEndian);
    CHECK(watches.items()[2].label == "Area");
    CHECK_FALSE(watches.move(0, 3));
    CHECK(watches.remove(2));
    CHECK_FALSE(watches.remove(2));
    CHECK(watches.size() == 2);
}

TEST_CASE("Watch list tracks changes by emulated frame and freezes while paused") {
    cw::BufferMemory memory({0x10});
    cw::WatchList watches;
    REQUIRE(watches.add(0, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian));

    watches.update(memory, 0);
    CHECK(watches.items()[0].current_value == 0x10u);
    CHECK_FALSE(watches.items()[0].previous_value.has_value());
    CHECK_FALSE(watches.frames_since_change(0, 0).has_value());

    const std::array<uint8_t, 1> changed{0x20};
    REQUIRE(memory.write(0, changed) == cw::MemoryWriteError::None);
    watches.update(memory, 1);
    CHECK(watches.items()[0].current_value == 0x20u);
    CHECK(watches.items()[0].previous_value == 0x10u);
    CHECK(watches.frames_since_change(0, 1) == 0u);

    // Repeated UI updates at the same emulated frame cannot age the highlight.
    for (int i = 0; i < 20; ++i) {
        watches.update(memory, 1);
    }
    CHECK(watches.frames_since_change(0, 1) == 0u);
    CHECK(watches.frames_since_change(0, 20) == 19u);
    CHECK_FALSE(watches.frames_since_change(0, 61).has_value());
}

TEST_CASE("Unavailable watch reads never expose stale current values and recover") {
    cw::BufferMemory available({0x34, 0x12});
    cw::WatchList watches;
    REQUIRE(watches.add(0, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian));
    REQUIRE(watches.add(0, cw::MemoryValueSize::SixteenBit, cw::ByteOrder::LittleEndian));
    REQUIRE(watches.add(1, cw::MemoryValueSize::SixteenBit, cw::ByteOrder::LittleEndian));
    watches.update(available, 0);
    REQUIRE(watches.items()[0].current_value == 0x34u);
    REQUIRE(watches.items()[1].current_value == 0x1234u);
    CHECK_FALSE(watches.items()[2].current_value.has_value()); // crosses the buffer boundary

    cw::BufferMemory unavailable({0xAA, 0xBB}, {{.start = 0,
                                                 .end = 1,
                                                 .description = "Unavailable RAM",
                                                 .available = false,
                                                 .writable = false}});
    watches.update(unavailable, 1);
    CHECK_FALSE(watches.items()[0].current_value.has_value());
    CHECK_FALSE(watches.items()[1].current_value.has_value());
    CHECK(watches.items()[0].previous_value == 0x34u);
    CHECK(watches.items()[1].previous_value == 0x1234u);

    cw::BufferMemory no_memory({});
    watches.update(no_memory, 2);
    CHECK_FALSE(watches.items()[0].current_value.has_value());
    CHECK_FALSE(watches.items()[1].current_value.has_value());

    cw::BufferMemory restored({0x35, 0x12});
    watches.update(restored, 3);
    CHECK(watches.items()[0].current_value == 0x35u);
    CHECK(watches.items()[0].previous_value == 0x34u);
    CHECK(watches.items()[0].last_changed_frame == 3u);
    CHECK(watches.items()[1].current_value == 0x1235u);
    CHECK(watches.items()[1].previous_value == 0x1234u);
    CHECK(watches.items()[1].last_changed_frame == 3u);
}

TEST_CASE("Removing an item stops its updates") {
    cw::BufferMemory memory({0x11});
    cw::WatchList watches;
    REQUIRE(watches.add(0, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian));
    watches.update(memory, 0);
    REQUIRE(watches.remove(0));

    const std::array<uint8_t, 1> changed{0x22};
    REQUIRE(memory.write(0, changed) == cw::MemoryWriteError::None);
    watches.update(memory, 1);
    CHECK(watches.size() == 0);
}

TEST_CASE("Core frame-counter resets clear old highlight timestamps") {
    cw::BufferMemory memory({0x01});
    cw::WatchList watches;
    REQUIRE(watches.add(0, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian));
    watches.update(memory, 100);

    const std::array<uint8_t, 1> changed{0x02};
    REQUIRE(memory.write(0, changed) == cw::MemoryWriteError::None);
    watches.update(memory, 101);
    REQUIRE(watches.frames_since_change(0, 101) == 0u);

    watches.update(memory, 0);
    CHECK_FALSE(watches.frames_since_change(0, 0).has_value());
}

TEST_CASE("An unavailable item does not retain an active change highlight") {
    cw::BufferMemory memory({0x01});
    cw::WatchList watches;
    REQUIRE(watches.add(0, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian));
    watches.update(memory, 0);

    const std::array<uint8_t, 1> changed{0x02};
    REQUIRE(memory.write(0, changed) == cw::MemoryWriteError::None);
    watches.update(memory, 1);
    REQUIRE(watches.frames_since_change(0, 1) == 0u);

    cw::BufferMemory no_memory({});
    watches.update(no_memory, 1);
    CHECK_FALSE(watches.items()[0].current_value.has_value());
    CHECK_FALSE(watches.items()[0].last_changed_frame.has_value());
    CHECK_FALSE(watches.frames_since_change(0, 1).has_value());
}

TEST_CASE("Clearing highlights preserves watched values") {
    cw::BufferMemory memory({0x01});
    cw::WatchList watches;
    REQUIRE(watches.add(0, cw::MemoryValueSize::EightBit, cw::ByteOrder::LittleEndian));
    watches.update(memory, 0);

    const std::array<uint8_t, 1> changed{0x02};
    REQUIRE(memory.write(0, changed) == cw::MemoryWriteError::None);
    watches.update(memory, 1);
    REQUIRE(watches.frames_since_change(0, 1) == 0u);

    watches.clear_highlights();
    CHECK(watches.size() == 1);
    CHECK(watches.items()[0].current_value == 0x02u);
    CHECK(watches.items()[0].previous_value == 0x01u);
    CHECK_FALSE(watches.frames_since_change(0, 1).has_value());
}
