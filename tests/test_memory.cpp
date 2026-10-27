// cw_devtools memory interface, on synthetic buffers only (no SDL, no core, no game).

#include "cw/devtools/memory.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

TEST_CASE("BufferMemory reads inside its range and stops at the end") {
    cw::BufferMemory memory({0x10, 0x20, 0x30, 0x40});
    CHECK(memory.size() == 4);

    std::array<uint8_t, 2> two{};
    CHECK(memory.read(0, two) == 2);
    CHECK(two == std::array<uint8_t, 2>{0x10, 0x20});

    CHECK(memory.read(3, two) == 1); // only one byte left before the end
    CHECK(two[0] == 0x40);
    CHECK(memory.read(4, two) == 0);          // first address past the end
    CHECK(memory.read(0xFFFFFFFF, two) == 0); // far out of range
}

TEST_CASE("read_le assembles little-endian values and refuses partial reads") {
    cw::BufferMemory memory({0x78, 0x56, 0x34, 0x12, 0xAB});
    CHECK(cw::read_le(memory, 0, 1) == 0x78u);
    CHECK(cw::read_le(memory, 0, 2) == 0x5678u);
    CHECK(cw::read_le(memory, 0, 3) == 0x345678u);
    CHECK(cw::read_le(memory, 0, 4) == 0x12345678u);
    CHECK(cw::read_le(memory, 4, 1) == 0xABu);

    CHECK_FALSE(cw::read_le(memory, 4, 2)); // second byte is past the end
    CHECK_FALSE(cw::read_le(memory, 5, 1));
    CHECK_FALSE(cw::read_le(memory, 0, 0)); // only 1-4 bytes
    CHECK_FALSE(cw::read_le(memory, 0, 5));
}

TEST_CASE("A synthetic 2 MiB PS1 RAM is readable at both ends") {
    std::vector<uint8_t> ram(0x200000, 0);
    ram.front() = 0xA1;
    ram.back() = 0xB2;
    cw::BufferMemory memory(std::move(ram), "System RAM");
    CHECK(cw::read_le(memory, 0x000000, 1) == 0xA1u);
    CHECK(cw::read_le(memory, 0x1FFFFF, 1) == 0xB2u);
    CHECK_FALSE(cw::read_le(memory, 0x200000, 1));
    CHECK(memory.regions() == std::vector<cw::MemoryRegionInfo>{{.start = 0,
                                                                 .end = 0x1FFFFF,
                                                                 .description = "System RAM",
                                                                 .available = true,
                                                                 .writable = true}});
}

TEST_CASE("An empty buffer has no regions and reads nothing") {
    cw::BufferMemory memory({});
    std::array<uint8_t, 1> one{};
    CHECK(memory.size() == 0);
    CHECK(memory.read(0, one) == 0);
    CHECK(memory.regions().empty());
}

TEST_CASE("Memory values use RetroAchievements bit, nibble, and endian semantics") {
    cw::BufferMemory memory({0x81, 0x23, 0x45, 0x67});

    const std::array<cw::MemoryValueSize, 8> bits = {
        cw::MemoryValueSize::Bit0, cw::MemoryValueSize::Bit1, cw::MemoryValueSize::Bit2,
        cw::MemoryValueSize::Bit3, cw::MemoryValueSize::Bit4, cw::MemoryValueSize::Bit5,
        cw::MemoryValueSize::Bit6, cw::MemoryValueSize::Bit7};
    for (size_t i = 0; i < bits.size(); ++i) {
        CHECK(cw::decode_memory_value(memory, 0, bits[i], cw::ByteOrder::LittleEndian) ==
              ((0x81U >> i) & 1U));
    }
    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::LowNibble,
                                  cw::ByteOrder::LittleEndian) == 1U);
    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::HighNibble,
                                  cw::ByteOrder::LittleEndian) == 8U);
    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::EightBit,
                                  cw::ByteOrder::LittleEndian) == 0x81U);

    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::SixteenBit,
                                  cw::ByteOrder::LittleEndian) == 0x2381U);
    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::SixteenBit,
                                  cw::ByteOrder::BigEndian) == 0x8123U);
    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::TwentyFourBit,
                                  cw::ByteOrder::LittleEndian) == 0x452381U);
    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::TwentyFourBit,
                                  cw::ByteOrder::BigEndian) == 0x812345U);
    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::ThirtyTwoBit,
                                  cw::ByteOrder::LittleEndian) == 0x67452381U);
    CHECK(cw::decode_memory_value(memory, 0, cw::MemoryValueSize::ThirtyTwoBit,
                                  cw::ByteOrder::BigEndian) == 0x81234567U);
    CHECK_FALSE(cw::decode_memory_value(memory, 3, cw::MemoryValueSize::SixteenBit,
                                        cw::ByteOrder::LittleEndian));
}

TEST_CASE("Memory values encode and decode round-trip within their size") {
    const std::array<cw::MemoryValueSize, 14> sizes = {
        cw::MemoryValueSize::Bit0,          cw::MemoryValueSize::Bit1,
        cw::MemoryValueSize::Bit2,          cw::MemoryValueSize::Bit3,
        cw::MemoryValueSize::Bit4,          cw::MemoryValueSize::Bit5,
        cw::MemoryValueSize::Bit6,          cw::MemoryValueSize::Bit7,
        cw::MemoryValueSize::LowNibble,     cw::MemoryValueSize::HighNibble,
        cw::MemoryValueSize::EightBit,      cw::MemoryValueSize::SixteenBit,
        cw::MemoryValueSize::TwentyFourBit, cw::MemoryValueSize::ThirtyTwoBit};

    for (const cw::MemoryValueSize size : sizes) {
        for (const cw::ByteOrder order : {cw::ByteOrder::LittleEndian, cw::ByteOrder::BigEndian}) {
            const unsigned count = cw::memory_value_byte_count(size);
            for (const uint32_t value :
                 {cw::memory_value_max(size) / 2U, cw::memory_value_max(size)}) {
                std::array<uint8_t, 4> bytes{0xA5, 0xB6, 0xC7, 0xD8};
                const auto encoded =
                    cw::encode_memory_value(value, size, order, std::span(bytes.data(), count));
                REQUIRE(encoded == cw::MemoryValueEncodeError::None);
                cw::BufferMemory memory(std::vector<uint8_t>(bytes.begin(), bytes.begin() + count));
                CHECK(cw::decode_memory_value(memory, 0, size, order) == value);
            }

            const uint32_t maximum = cw::memory_value_max(size);
            if (maximum != std::numeric_limits<uint32_t>::max()) {
                std::array<uint8_t, 4> bytes{0xA5, 0xB6, 0xC7, 0xD8};
                const auto unchanged = bytes;
                CHECK(cw::encode_memory_value(maximum + 1U, size, order,
                                              std::span(bytes.data(), count)) ==
                      cw::MemoryValueEncodeError::TooLarge);
                CHECK(bytes == unchanged);
            }
        }
    }

    std::array<uint8_t, 1> byte{0xA5};
    CHECK(cw::encode_memory_value(2, cw::MemoryValueSize::LowNibble, cw::ByteOrder::LittleEndian,
                                  byte) == cw::MemoryValueEncodeError::None);
    CHECK(byte[0] == 0xA2);
    CHECK(cw::encode_memory_value(3, cw::MemoryValueSize::HighNibble, cw::ByteOrder::LittleEndian,
                                  byte) == cw::MemoryValueEncodeError::None);
    CHECK(byte[0] == 0x32);
    CHECK(cw::encode_memory_value(0, cw::MemoryValueSize::Bit0, cw::ByteOrder::LittleEndian,
                                  byte) == cw::MemoryValueEncodeError::None);
    CHECK(byte[0] == 0x32);

    std::array<uint8_t, 3> encoded{};
    CHECK(cw::encode_memory_value(0x123456, cw::MemoryValueSize::TwentyFourBit,
                                  cw::ByteOrder::LittleEndian,
                                  encoded) == cw::MemoryValueEncodeError::None);
    CHECK(encoded == std::array<uint8_t, 3>{0x56, 0x34, 0x12});
    CHECK(cw::encode_memory_value(0x123456, cw::MemoryValueSize::TwentyFourBit,
                                  cw::ByteOrder::BigEndian,
                                  encoded) == cw::MemoryValueEncodeError::None);
    CHECK(encoded == std::array<uint8_t, 3>{0x12, 0x34, 0x56});

    const auto original = encoded;
    CHECK(cw::encode_memory_value(0x1000000, cw::MemoryValueSize::TwentyFourBit,
                                  cw::ByteOrder::BigEndian,
                                  encoded) == cw::MemoryValueEncodeError::TooLarge);
    CHECK(encoded == original);
    CHECK(cw::encode_memory_value(1, cw::MemoryValueSize::TwentyFourBit, cw::ByteOrder::BigEndian,
                                  std::span(encoded.data(), encoded.size() - 1)) ==
          cw::MemoryValueEncodeError::InvalidBuffer);
}

TEST_CASE("BufferMemory writes only the requested bytes in a writable region") {
    cw::BufferMemory memory({0x10, 0x20, 0x30, 0x40});
    const std::array<uint8_t, 2> replacement{0xAA, 0xBB};

    CHECK(memory.write(1, replacement) == cw::MemoryWriteError::None);
    CHECK(memory.bytes() == std::vector<uint8_t>{0x10, 0xAA, 0xBB, 0x40});
}

TEST_CASE("BufferMemory rejects read-only, unavailable, boundary, and out-of-space writes") {
    const std::vector<uint8_t> original{0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    cw::BufferMemory memory(
        original,
        {{.start = 0, .end = 1, .description = "Read only", .available = true, .writable = false},
         {.start = 2,
          .end = 3,
          .description = "Unavailable",
          .available = false,
          .writable = false},
         {.start = 4, .end = 4, .description = "Writable A", .available = true, .writable = true},
         {.start = 5, .end = 5, .description = "Writable B", .available = true, .writable = true}});
    const std::array<uint8_t, 1> one{0xFF};
    const std::array<uint8_t, 2> two{0xAA, 0xBB};

    CHECK(memory.write(0, one) == cw::MemoryWriteError::ReadOnly);
    CHECK(memory.bytes() == original);
    CHECK(memory.write(2, one) == cw::MemoryWriteError::Unavailable);
    CHECK(memory.bytes() == original);
    CHECK(memory.write(4, two) == cw::MemoryWriteError::BoundaryCrossing);
    CHECK(memory.bytes() == original);
    CHECK(memory.write(5, two) == cw::MemoryWriteError::OutOfAddressSpace);
    CHECK(memory.bytes() == original);
}

TEST_CASE("Visible memory snapshots report changed addresses for the next frame") {
    const std::array<cw::MemoryByteSnapshot, 4> previous = {
        cw::MemoryByteSnapshot{.address = 0, .value = 0x10},
        cw::MemoryByteSnapshot{.address = 1, .value = 0x20},
        cw::MemoryByteSnapshot{.address = 2, .value = 0x30},
        cw::MemoryByteSnapshot{.address = 3, .value = 0x40}};
    const std::array<cw::MemoryByteSnapshot, 4> current = {
        cw::MemoryByteSnapshot{.address = 0, .value = 0x10},
        cw::MemoryByteSnapshot{.address = 1, .value = 0x21},
        cw::MemoryByteSnapshot{.address = 2, .value = 0x30},
        cw::MemoryByteSnapshot{.address = 4, .value = 0x50}};

    CHECK(cw::changed_memory_bytes(previous, current) == std::vector<uint32_t>{1});
    CHECK(cw::changed_memory_bytes(current, current).empty());
}

TEST_CASE("A change stays highlighted for the window, counted in emulated frames") {
    cw::ByteChangeTracker tracker(60);
    const std::vector<cw::MemoryByteSnapshot> before{{0x100, 1}, {0x101, 7}};
    const std::vector<cw::MemoryByteSnapshot> after{{0x100, 2}, {0x101, 7}}; // only 0x100 changed

    tracker.observe(before, 10);
    CHECK_FALSE(tracker.frames_since_change(0x100, 10)); // first sight: remembered, not marked
    tracker.observe(after, 11);
    CHECK(tracker.frames_since_change(0x100, 11) == 0u);
    CHECK_FALSE(tracker.frames_since_change(0x101, 11));

    // Many UI frames without emulated frames (paused, or a 144 Hz display): still lit.
    for (int ui_frame = 0; ui_frame < 500; ++ui_frame) {
        tracker.observe(after, 11);
    }
    CHECK(tracker.frames_since_change(0x100, 11) == 0u);

    // Fades out after 60 emulated frames.
    tracker.observe(after, 70);
    CHECK(tracker.frames_since_change(0x100, 70) == 59u);
    tracker.observe(after, 71);
    CHECK_FALSE(tracker.frames_since_change(0x100, 71));
}

TEST_CASE("Bytes scrolled into view are not reported as changed") {
    cw::ByteChangeTracker tracker;
    tracker.observe(std::vector<cw::MemoryByteSnapshot>{{0x000, 5}}, 1);
    tracker.observe(std::vector<cw::MemoryByteSnapshot>{{0x010, 9}}, 2); // different row
    CHECK_FALSE(tracker.frames_since_change(0x010, 2));
    // Scrolling back: 0x000 was forgotten, so it is "first sight" again even if it changed.
    tracker.observe(std::vector<cw::MemoryByteSnapshot>{{0x000, 6}}, 3);
    CHECK_FALSE(tracker.frames_since_change(0x000, 3));

    tracker.clear();
    tracker.observe(std::vector<cw::MemoryByteSnapshot>{{0x000, 7}}, 4);
    CHECK_FALSE(tracker.frames_since_change(0x000, 4));
}
