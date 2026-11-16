// Snapshot search logic on synthetic memory only (no SDL, core, ROM, or game).

#include "cw/memory/memory_search.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

uint32_t expected_value(const std::array<uint8_t, 12>& bytes, uint32_t address,
                        cw::MemoryValueSize size, cw::ByteOrder order) {
    switch (size) {
    case cw::MemoryValueSize::Bit0:
    case cw::MemoryValueSize::Bit1:
    case cw::MemoryValueSize::Bit2:
    case cw::MemoryValueSize::Bit3:
    case cw::MemoryValueSize::Bit4:
    case cw::MemoryValueSize::Bit5:
    case cw::MemoryValueSize::Bit6:
    case cw::MemoryValueSize::Bit7: {
        const unsigned bit =
            static_cast<unsigned>(size) - static_cast<unsigned>(cw::MemoryValueSize::Bit0);
        return (bytes[address] >> bit) & 1U;
    }
    case cw::MemoryValueSize::LowNibble:
        return bytes[address] & 0x0FU;
    case cw::MemoryValueSize::HighNibble:
        return bytes[address] >> 4U;
    case cw::MemoryValueSize::EightBit:
        return bytes[address];
    case cw::MemoryValueSize::SixteenBit:
    case cw::MemoryValueSize::TwentyFourBit:
    case cw::MemoryValueSize::ThirtyTwoBit: {
        const unsigned byte_count = cw::memory_value_byte_count(size);
        uint32_t value = 0;
        for (unsigned i = 0; i < byte_count; ++i) {
            if (order == cw::ByteOrder::LittleEndian) {
                value |= uint32_t{bytes[address + i]} << (i * 8U);
            } else {
                value = (value << 8U) | bytes[address + i];
            }
        }
        return value;
    }
    }
    return 0;
}

bool expected_match(uint32_t left, cw::MemorySearchComparison comparison, uint32_t right) {
    switch (comparison) {
    case cw::MemorySearchComparison::Equal:
        return left == right;
    case cw::MemorySearchComparison::NotEqual:
        return left != right;
    case cw::MemorySearchComparison::Less:
        return left < right;
    case cw::MemorySearchComparison::Greater:
        return left > right;
    case cw::MemorySearchComparison::LessOrEqual:
        return left <= right;
    case cw::MemorySearchComparison::GreaterOrEqual:
        return left >= right;
    }
    return false;
}

std::vector<uint32_t> get_addresses(const cw::MemorySearch& search,
                                    const cw::MemoryReader& memory) {
    std::vector<uint32_t> addresses;
    addresses.reserve(search.candidate_count());
    for (size_t i = 0; i < search.candidate_count(); ++i) {
        const auto result = search.result_at(memory, i);
        REQUIRE(result.has_value());
        addresses.push_back(result->address);
    }
    return addresses;
}

std::vector<std::pair<uint32_t, uint8_t>> get_bit_addresses(const cw::MemorySearch& search,
                                                            const cw::MemoryReader& memory) {
    std::vector<std::pair<uint32_t, uint8_t>> addresses;
    addresses.reserve(search.candidate_count());
    for (size_t i = 0; i < search.candidate_count(); ++i) {
        const auto result = search.result_at(memory, i);
        REQUIRE(result.has_value());
        REQUIRE(result->bit_index.has_value());
        addresses.emplace_back(result->address, *result->bit_index);
    }
    return addresses;
}

} // namespace

TEST_CASE("MemorySearch returns exact addresses for every size, byte order, and comparison") {
    const std::array<uint8_t, 12> fixture{0x80, 0x01, 0x02, 0x04, 0x08, 0x10,
                                          0x20, 0x40, 0x00, 0x7F, 0x55, 0xAA};
    const std::array<cw::MemoryValueSize, 14> sizes{
        cw::MemoryValueSize::Bit0,          cw::MemoryValueSize::Bit1,
        cw::MemoryValueSize::Bit2,          cw::MemoryValueSize::Bit3,
        cw::MemoryValueSize::Bit4,          cw::MemoryValueSize::Bit5,
        cw::MemoryValueSize::Bit6,          cw::MemoryValueSize::Bit7,
        cw::MemoryValueSize::LowNibble,     cw::MemoryValueSize::HighNibble,
        cw::MemoryValueSize::EightBit,      cw::MemoryValueSize::SixteenBit,
        cw::MemoryValueSize::TwentyFourBit, cw::MemoryValueSize::ThirtyTwoBit,
    };
    const std::array<cw::ByteOrder, 2> byte_orders{cw::ByteOrder::LittleEndian,
                                                   cw::ByteOrder::BigEndian};
    const std::array<cw::MemorySearchComparison, 6> comparisons{
        cw::MemorySearchComparison::Equal,       cw::MemorySearchComparison::NotEqual,
        cw::MemorySearchComparison::Less,        cw::MemorySearchComparison::Greater,
        cw::MemorySearchComparison::LessOrEqual, cw::MemorySearchComparison::GreaterOrEqual,
    };

    for (const cw::MemoryValueSize size : sizes) {
        for (const cw::ByteOrder byte_order : byte_orders) {
            uint32_t constant = 0;
            switch (size) {
            case cw::MemoryValueSize::Bit0:
            case cw::MemoryValueSize::Bit1:
            case cw::MemoryValueSize::Bit2:
            case cw::MemoryValueSize::Bit3:
            case cw::MemoryValueSize::Bit4:
            case cw::MemoryValueSize::Bit5:
            case cw::MemoryValueSize::Bit6:
            case cw::MemoryValueSize::Bit7:
                constant = 0;
                break;
            case cw::MemoryValueSize::LowNibble:
                constant = 7;
                break;
            case cw::MemoryValueSize::HighNibble:
                constant = 8;
                break;
            case cw::MemoryValueSize::EightBit:
                constant = 0x40;
                break;
            case cw::MemoryValueSize::SixteenBit:
                constant = 0x4000;
                break;
            case cw::MemoryValueSize::TwentyFourBit:
                constant = 0x400000;
                break;
            case cw::MemoryValueSize::ThirtyTwoBit:
                constant = 0x40000000;
                break;
            }

            for (const cw::MemorySearchComparison comparison : comparisons) {
                cw::BufferMemory memory(std::vector<uint8_t>(fixture.begin(), fixture.end()));
                cw::MemorySearch search;
                REQUIRE(search.configure({.size = size,
                                          .byte_order = byte_order,
                                          .range_start = 0,
                                          .range_end = fixture.size() - 1,
                                          .aligned = false}) == false);
                REQUIRE(search.start(memory));
                REQUIRE(search.filter(memory, comparison, cw::MemorySearchReference::Constant,
                                      constant));

                std::vector<uint32_t> expected;
                const unsigned byte_count = cw::memory_value_byte_count(size);
                const size_t last_start = fixture.size() - byte_count;
                for (uint32_t address = 0; address <= last_start; ++address) {
                    if (expected_match(expected_value(fixture, address, size, byte_order),
                                       comparison, constant)) {
                        expected.push_back(address);
                    }
                }
                CHECK(get_addresses(search, memory) == expected);
            }
        }
    }
}

TEST_CASE("All-bit searches keep each address and bit as a distinct candidate") {
    const std::array<uint8_t, 12> fixture{0x80, 0x01, 0x02, 0x04, 0x08, 0x10,
                                          0x20, 0x40, 0x00, 0x7F, 0x55, 0xAA};
    const std::array<cw::ByteOrder, 2> byte_orders{cw::ByteOrder::LittleEndian,
                                                   cw::ByteOrder::BigEndian};
    const std::array<cw::MemorySearchComparison, 6> comparisons{
        cw::MemorySearchComparison::Equal,       cw::MemorySearchComparison::NotEqual,
        cw::MemorySearchComparison::Less,        cw::MemorySearchComparison::Greater,
        cw::MemorySearchComparison::LessOrEqual, cw::MemorySearchComparison::GreaterOrEqual,
    };

    for (const cw::ByteOrder byte_order : byte_orders) {
        for (const cw::MemorySearchComparison comparison : comparisons) {
            cw::BufferMemory memory(std::vector<uint8_t>(fixture.begin(), fixture.end()));
            cw::MemorySearch search;
            REQUIRE(search.configure({.size = cw::MemoryValueSize::Bit0,
                                      .byte_order = byte_order,
                                      .range_start = 0,
                                      .range_end = fixture.size() - 1,
                                      .aligned = false,
                                      .all_bit_positions = true}) == false);
            REQUIRE(search.start(memory));
            CHECK(search.candidate_count() == fixture.size() * 8U);
            REQUIRE(search.filter(memory, comparison, cw::MemorySearchReference::Constant, 0));

            std::vector<std::pair<uint32_t, uint8_t>> expected;
            for (uint32_t address = 0; address < fixture.size(); ++address) {
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    const auto size = static_cast<cw::MemoryValueSize>(
                        static_cast<unsigned>(cw::MemoryValueSize::Bit0) + bit);
                    if (expected_match(expected_value(fixture, address, size, byte_order),
                                       comparison, 0)) {
                        expected.emplace_back(address, bit);
                    }
                }
            }
            CHECK(get_bit_addresses(search, memory) == expected);
        }
    }
}

TEST_CASE("MemorySearch applies optional natural alignment to 16 and 32-bit candidates") {
    cw::BufferMemory memory(std::vector<uint8_t>(12, 0x5A));
    cw::MemorySearch search;

    REQUIRE(search.configure({.size = cw::MemoryValueSize::SixteenBit,
                              .byte_order = cw::ByteOrder::LittleEndian,
                              .range_start = 1,
                              .range_end = 9,
                              .aligned = true}) == false);
    REQUIRE(search.start(memory));
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{2, 4, 6, 8});

    REQUIRE(search.configure({.size = cw::MemoryValueSize::ThirtyTwoBit,
                              .byte_order = cw::ByteOrder::LittleEndian,
                              .range_start = 1,
                              .range_end = 11,
                              .aligned = true}));
    REQUIRE(search.start(memory));
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{4, 8});
}

TEST_CASE("MemorySearch excludes unavailable bytes and values past the selected range") {
    cw::BufferMemory memory(
        {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80},
        {{.start = 0, .end = 2, .description = "First", .available = true, .writable = true},
         {.start = 3, .end = 3, .description = "Missing", .available = false, .writable = false},
         {.start = 4, .end = 7, .description = "Second", .available = true, .writable = true}});
    cw::MemorySearch search;

    REQUIRE(search.configure({.size = cw::MemoryValueSize::SixteenBit,
                              .byte_order = cw::ByteOrder::LittleEndian,
                              .range_start = 0,
                              .range_end = 7,
                              .aligned = false}) == false);
    REQUIRE(search.start(memory));
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{0, 1, 4, 5, 6});

    REQUIRE(search.configure({.size = cw::MemoryValueSize::SixteenBit,
                              .byte_order = cw::ByteOrder::LittleEndian,
                              .range_start = 0,
                              .range_end = 2,
                              .aligned = false}));
    REQUIRE(search.start(memory));
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{0, 1});
}

TEST_CASE("MemorySearch compares against the previous and first snapshots") {
    cw::BufferMemory memory({10, 20, 30, 40, 50});
    cw::MemorySearch search;
    REQUIRE(search.configure({.size = cw::MemoryValueSize::EightBit,
                              .byte_order = cw::ByteOrder::LittleEndian,
                              .range_start = 0,
                              .range_end = 4,
                              .aligned = false}) == false);
    REQUIRE(search.start(memory));

    memory.bytes() = {11, 19, 30, 44, 55};
    REQUIRE(search.filter(memory, cw::MemorySearchComparison::GreaterOrEqual,
                          cw::MemorySearchReference::Previous));
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{0, 2, 3, 4});
    auto first_result = search.result_at(memory, 0);
    REQUIRE(first_result);
    CHECK(first_result->current == 11);
    CHECK(first_result->previous == 11); // The filtered snapshot becomes the next "previous".
    CHECK(first_result->initial == 10);

    memory.bytes() = {12, 19, 29, 40, 54};
    REQUIRE(search.filter(memory, cw::MemorySearchComparison::Equal,
                          cw::MemorySearchReference::Initial));
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{3});

    cw::BufferMemory constant_memory({1, 2, 3, 4, 5});
    cw::MemorySearch constant_search;
    REQUIRE(constant_search.configure({.size = cw::MemoryValueSize::EightBit,
                                       .byte_order = cw::ByteOrder::LittleEndian,
                                       .range_start = 0,
                                       .range_end = 4,
                                       .aligned = false}) == false);
    REQUIRE(constant_search.start(constant_memory));
    constant_memory.bytes() = {5, 4, 3, 2, 1};
    REQUIRE(constant_search.filter(constant_memory, cw::MemorySearchComparison::Greater,
                                   cw::MemorySearchReference::Constant, 3));
    CHECK(get_addresses(constant_search, constant_memory) == std::vector<uint32_t>{0, 1});
}

TEST_CASE("The room search sequence leaves only the address that returns to its first value") {
    cw::BufferMemory memory({10, 30, 50, 50});
    cw::MemorySearch search;
    REQUIRE(search.configure({.size = cw::MemoryValueSize::EightBit,
                              .byte_order = cw::ByteOrder::LittleEndian,
                              .range_start = 0,
                              .range_end = 3,
                              .aligned = false}) == false);
    REQUIRE(search.start(memory)); // Room A is the first photo.

    memory.bytes() = {20, 40, 50, 50}; // Room B.
    REQUIRE(search.filter(memory, cw::MemorySearchComparison::NotEqual,
                          cw::MemorySearchReference::Previous));
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{0, 1});

    REQUIRE(search.filter(memory, cw::MemorySearchComparison::Equal,
                          cw::MemorySearchReference::Previous)); // Stayed still in room B.
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{0, 1});

    memory.bytes() = {10, 41, 50, 50}; // Return to room A; address 1 did not return.
    REQUIRE(search.filter(memory, cw::MemorySearchComparison::Equal,
                          cw::MemorySearchReference::Initial));
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{0});
}

TEST_CASE("Undo restores the candidate set and snapshots, with a bounded history") {
    cw::BufferMemory memory({1, 2, 3});
    cw::MemorySearch search;
    REQUIRE(search.configure({.size = cw::MemoryValueSize::EightBit,
                              .byte_order = cw::ByteOrder::LittleEndian,
                              .range_start = 0,
                              .range_end = 2,
                              .aligned = false}) == false);
    REQUIRE(search.start(memory));

    memory.bytes() = {1, 9, 3};
    REQUIRE(search.filter(memory, cw::MemorySearchComparison::Equal,
                          cw::MemorySearchReference::Constant, 0));
    CHECK(search.candidate_count() == 0);
    REQUIRE(search.undo());
    CHECK(get_addresses(search, memory) == std::vector<uint32_t>{0, 1, 2});
    const auto restored = search.result_at(memory, 1);
    REQUIRE(restored);
    CHECK(restored->current == 9);
    CHECK(restored->previous == 2);
    CHECK(restored->initial == 2);

    for (size_t i = 0; i < cw::MemorySearch::kMaxUndoSteps + 2; ++i) {
        REQUIRE(search.filter(memory, cw::MemorySearchComparison::GreaterOrEqual,
                              cw::MemorySearchReference::Constant, 0));
    }
    CHECK(search.undo_count() == cw::MemorySearch::kMaxUndoSteps);
    for (size_t i = 0; i < cw::MemorySearch::kMaxUndoSteps; ++i) {
        CHECK(search.undo());
    }
    CHECK_FALSE(search.undo());
}

TEST_CASE("Changing search configuration safely invalidates existing results") {
    cw::BufferMemory memory({0x10, 0x20, 0x30, 0x40});
    cw::MemorySearch search;
    cw::MemorySearchConfig config{.size = cw::MemoryValueSize::EightBit,
                                  .byte_order = cw::ByteOrder::LittleEndian,
                                  .range_start = 0,
                                  .range_end = 3,
                                  .aligned = false};
    CHECK_FALSE(search.configure(config));
    REQUIRE(search.start(memory));
    REQUIRE(search.filter(memory, cw::MemorySearchComparison::NotEqual,
                          cw::MemorySearchReference::Constant, 0));
    CHECK(search.active());

    config.size = cw::MemoryValueSize::SixteenBit;
    CHECK(search.configure(config));
    CHECK_FALSE(search.active());
    CHECK(search.invalidated());
    CHECK(search.candidate_count() == 0);
    CHECK(search.undo_count() == 0);

    config.byte_order = cw::ByteOrder::BigEndian;
    CHECK_FALSE(search.configure(config));
    CHECK(search.invalidated());

    REQUIRE(search.start(memory));
    config.size = cw::MemoryValueSize::TwentyFourBit;
    CHECK(search.configure(config));
    CHECK_FALSE(search.active());
    REQUIRE(search.start(memory));
    config.range_start = 1;
    CHECK(search.configure(config));
    CHECK_FALSE(search.active());
}
