#include "cw/devtools/memory.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace cw {

std::optional<uint32_t> read_le(const MemoryReader& memory, uint32_t address, unsigned bytes) {
    if (bytes < 1 || bytes > 4) {
        return std::nullopt;
    }
    std::array<uint8_t, 4> buffer{};
    if (memory.read(address, std::span(buffer.data(), bytes)) != bytes) {
        return std::nullopt;
    }
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        value |= uint32_t{buffer[i]} << (8 * i);
    }
    return value;
}

MemoryWriteError validate_memory_write(uint32_t address, size_t byte_count, uint32_t address_space,
                                       std::span<const MemoryRegionInfo> regions) {
    if (byte_count == 0) {
        return MemoryWriteError::EmptyWrite;
    }

    const uint64_t end = static_cast<uint64_t>(address) + byte_count;
    if (address >= address_space || end > address_space) {
        return MemoryWriteError::OutOfAddressSpace;
    }

    const uint64_t last = end - 1;
    const MemoryRegionInfo* start_region = nullptr;
    for (const MemoryRegionInfo& region : regions) {
        if (region.start <= address && address <= region.end) {
            start_region = &region;
            break;
        }
    }
    if (!start_region) {
        return MemoryWriteError::Unavailable;
    }
    if (last > start_region->end) {
        return MemoryWriteError::BoundaryCrossing;
    }
    if (!start_region->available) {
        return MemoryWriteError::Unavailable;
    }
    if (!start_region->writable) {
        return MemoryWriteError::ReadOnly;
    }
    return MemoryWriteError::None;
}

const char* memory_write_error_message(MemoryWriteError error) {
    switch (error) {
    case MemoryWriteError::None:
        return "Write completed";
    case MemoryWriteError::EmptyWrite:
        return "No bytes were selected";
    case MemoryWriteError::OutOfAddressSpace:
        return "Address is outside the memory space";
    case MemoryWriteError::Unavailable:
        return "Memory region is unavailable";
    case MemoryWriteError::ReadOnly:
        return "Memory region is read-only";
    case MemoryWriteError::BoundaryCrossing:
        return "Write crosses a memory region boundary";
    }
    return "Memory write was rejected";
}

unsigned memory_value_byte_count(MemoryValueSize size) {
    switch (size) {
    case MemoryValueSize::Bit0:
    case MemoryValueSize::Bit1:
    case MemoryValueSize::Bit2:
    case MemoryValueSize::Bit3:
    case MemoryValueSize::Bit4:
    case MemoryValueSize::Bit5:
    case MemoryValueSize::Bit6:
    case MemoryValueSize::Bit7:
    case MemoryValueSize::LowNibble:
    case MemoryValueSize::HighNibble:
    case MemoryValueSize::EightBit:
        return 1;
    case MemoryValueSize::SixteenBit:
        return 2;
    case MemoryValueSize::TwentyFourBit:
        return 3;
    case MemoryValueSize::ThirtyTwoBit:
        return 4;
    }
    return 0;
}

uint32_t memory_value_max(MemoryValueSize size) {
    switch (size) {
    case MemoryValueSize::Bit0:
    case MemoryValueSize::Bit1:
    case MemoryValueSize::Bit2:
    case MemoryValueSize::Bit3:
    case MemoryValueSize::Bit4:
    case MemoryValueSize::Bit5:
    case MemoryValueSize::Bit6:
    case MemoryValueSize::Bit7:
        return 1;
    case MemoryValueSize::LowNibble:
    case MemoryValueSize::HighNibble:
        return 0x0F;
    case MemoryValueSize::EightBit:
        return 0xFF;
    case MemoryValueSize::SixteenBit:
        return 0xFFFF;
    case MemoryValueSize::TwentyFourBit:
        return 0xFFFFFF;
    case MemoryValueSize::ThirtyTwoBit:
        return std::numeric_limits<uint32_t>::max();
    }
    return 0;
}

std::optional<uint32_t> decode_memory_value(const MemoryReader& memory, uint32_t address,
                                            MemoryValueSize size, ByteOrder order) {
    const unsigned byte_count = memory_value_byte_count(size);
    if (byte_count == 0) {
        return std::nullopt;
    }
    std::array<uint8_t, 4> bytes{};
    if (memory.read(address, std::span(bytes.data(), byte_count)) != byte_count) {
        return std::nullopt;
    }
    return decode_memory_bytes(std::span(bytes.data(), byte_count), size, order);
}

std::optional<uint32_t> decode_memory_bytes(std::span<const uint8_t> bytes, MemoryValueSize size,
                                            ByteOrder order) {
    const unsigned byte_count = memory_value_byte_count(size);
    if (byte_count == 0 || bytes.size() != byte_count) {
        return std::nullopt;
    }

    uint32_t value = 0;
    if (order == ByteOrder::LittleEndian) {
        for (unsigned i = 0; i < byte_count; ++i) {
            value |= uint32_t{bytes[i]} << (i * 8U);
        }
    } else {
        for (unsigned i = 0; i < byte_count; ++i) {
            value = (value << 8U) | bytes[i];
        }
    }

    switch (size) {
    case MemoryValueSize::Bit0:
    case MemoryValueSize::Bit1:
    case MemoryValueSize::Bit2:
    case MemoryValueSize::Bit3:
    case MemoryValueSize::Bit4:
    case MemoryValueSize::Bit5:
    case MemoryValueSize::Bit6:
    case MemoryValueSize::Bit7: {
        const unsigned bit =
            static_cast<unsigned>(size) - static_cast<unsigned>(MemoryValueSize::Bit0);
        return (value >> bit) & 1U;
    }
    case MemoryValueSize::LowNibble:
        return value & 0x0FU;
    case MemoryValueSize::HighNibble:
        return (value >> 4U) & 0x0FU;
    case MemoryValueSize::EightBit:
    case MemoryValueSize::SixteenBit:
    case MemoryValueSize::TwentyFourBit:
    case MemoryValueSize::ThirtyTwoBit:
        return value;
    }
    return std::nullopt;
}

MemoryValueEncodeError encode_memory_value(uint32_t value, MemoryValueSize size, ByteOrder order,
                                           std::span<uint8_t> bytes) {
    const unsigned byte_count = memory_value_byte_count(size);
    if (byte_count == 0 || bytes.size() != byte_count) {
        return MemoryValueEncodeError::InvalidBuffer;
    }
    if (value > memory_value_max(size)) {
        return MemoryValueEncodeError::TooLarge;
    }

    switch (size) {
    case MemoryValueSize::Bit0:
    case MemoryValueSize::Bit1:
    case MemoryValueSize::Bit2:
    case MemoryValueSize::Bit3:
    case MemoryValueSize::Bit4:
    case MemoryValueSize::Bit5:
    case MemoryValueSize::Bit6:
    case MemoryValueSize::Bit7: {
        const unsigned bit =
            static_cast<unsigned>(size) - static_cast<unsigned>(MemoryValueSize::Bit0);
        const uint8_t mask = static_cast<uint8_t>(1U << bit);
        bytes[0] = static_cast<uint8_t>((bytes[0] & ~mask) | (value << bit));
        return MemoryValueEncodeError::None;
    }
    case MemoryValueSize::LowNibble:
        bytes[0] = static_cast<uint8_t>((bytes[0] & 0xF0U) | value);
        return MemoryValueEncodeError::None;
    case MemoryValueSize::HighNibble:
        bytes[0] = static_cast<uint8_t>((bytes[0] & 0x0FU) | (value << 4U));
        return MemoryValueEncodeError::None;
    case MemoryValueSize::EightBit:
    case MemoryValueSize::SixteenBit:
    case MemoryValueSize::TwentyFourBit:
    case MemoryValueSize::ThirtyTwoBit:
        for (unsigned i = 0; i < byte_count; ++i) {
            const unsigned shift =
                order == ByteOrder::LittleEndian ? i * 8U : (byte_count - i - 1U) * 8U;
            bytes[i] = static_cast<uint8_t>(value >> shift);
        }
        return MemoryValueEncodeError::None;
    }
    return MemoryValueEncodeError::InvalidBuffer;
}

std::vector<uint32_t> changed_memory_bytes(std::span<const MemoryByteSnapshot> previous,
                                           std::span<const MemoryByteSnapshot> current) {
    std::unordered_map<uint32_t, uint8_t> previous_values;
    previous_values.reserve(previous.size());
    for (const MemoryByteSnapshot& byte : previous) {
        previous_values[byte.address] = byte.value;
    }

    std::vector<uint32_t> changed;
    for (const MemoryByteSnapshot& byte : current) {
        const auto found = previous_values.find(byte.address);
        if (found != previous_values.end() && found->second != byte.value) {
            changed.push_back(byte.address);
        }
    }
    return changed;
}

BufferMemory::BufferMemory(std::vector<uint8_t> bytes, std::string description)
    : bytes_(std::move(bytes)) {
    // RA addresses are 32-bit; a bigger buffer would have bytes no address can reach.
    if (bytes_.size() > std::numeric_limits<uint32_t>::max()) {
        bytes_.resize(std::numeric_limits<uint32_t>::max());
    }
    if (!bytes_.empty()) {
        regions_.push_back({.start = 0,
                            .end = size() - 1,
                            .description = std::move(description),
                            .available = true,
                            .writable = true});
    }
}

BufferMemory::BufferMemory(std::vector<uint8_t> bytes, std::vector<MemoryRegionInfo> regions)
    : bytes_(std::move(bytes)), regions_(std::move(regions)) {
    if (bytes_.size() > std::numeric_limits<uint32_t>::max()) {
        bytes_.resize(std::numeric_limits<uint32_t>::max());
    }
}

uint32_t BufferMemory::size() const {
    return static_cast<uint32_t>(bytes_.size());
}

size_t BufferMemory::read(uint32_t address, std::span<uint8_t> out) const {
    if (out.empty() || address >= bytes_.size()) {
        return 0;
    }

    size_t copied = 0;
    while (copied < out.size()) {
        const uint32_t current = address + static_cast<uint32_t>(copied);
        if (current >= bytes_.size()) {
            break;
        }

        const auto region =
            std::find_if(regions_.begin(), regions_.end(), [current](const auto& r) {
                return r.start <= current && current <= r.end;
            });
        if (region == regions_.end() || !region->available) {
            break;
        }

        const uint64_t region_bytes = static_cast<uint64_t>(region->end) - current + 1;
        const size_t count = std::min(
            {out.size() - copied, bytes_.size() - current, static_cast<size_t>(region_bytes)});
        std::memcpy(out.data() + copied, bytes_.data() + current, count);
        copied += count;
    }
    return copied;
}

std::vector<MemoryRegionInfo> BufferMemory::regions() const {
    return regions_;
}

MemoryWriteError BufferMemory::write(uint32_t address, std::span<const uint8_t> bytes) {
    const MemoryWriteError validation =
        validate_memory_write(address, bytes.size(), size(), regions_);
    if (validation != MemoryWriteError::None) {
        return validation;
    }

    std::memmove(bytes_.data() + address, bytes.data(), bytes.size());
    return MemoryWriteError::None;
}

void ByteChangeTracker::observe(std::span<const MemoryByteSnapshot> bytes, uint64_t frame) {
    std::unordered_map<uint32_t, uint8_t> seen;
    seen.reserve(bytes.size());
    for (const MemoryByteSnapshot& byte : bytes) {
        const auto previous = last_value_.find(byte.address);
        if (previous != last_value_.end() && previous->second != byte.value) {
            changed_at_[byte.address] = frame;
        }
        seen[byte.address] = byte.value;
    }
    // Keep only what is visible now, so memory use follows the window, not the 2 MiB of RAM.
    last_value_ = std::move(seen);
    std::erase_if(changed_at_, [&](const auto& entry) {
        return frame >= entry.second && frame - entry.second >= window_;
    });
}

std::optional<uint64_t> ByteChangeTracker::frames_since_change(uint32_t address,
                                                               uint64_t frame) const {
    const auto found = changed_at_.find(address);
    if (found == changed_at_.end() || frame < found->second || frame - found->second >= window_) {
        return std::nullopt;
    }
    return frame - found->second;
}

void ByteChangeTracker::clear() {
    last_value_.clear();
    changed_at_.clear();
}

} // namespace cw
