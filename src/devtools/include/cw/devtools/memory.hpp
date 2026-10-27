#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cw {

// One block of the console's memory, in RetroAchievements addresses.
struct MemoryRegionInfo {
    uint32_t start = 0; // first address
    uint32_t end = 0;   // last address (inclusive)
    std::string description;
    // True only if every address from start to end can be read; false when the emulator does
    // not expose the block, or only part of it.
    bool available = false;
    // True only when writes to every address in the region are supported.
    bool writable = false;

    friend bool operator==(const MemoryRegionInfo&, const MemoryRegionInfo&) = default;
};

class MemoryReader;

enum class MemoryWriteError {
    None,
    EmptyWrite,
    OutOfAddressSpace,
    Unavailable,
    ReadOnly,
    BoundaryCrossing,
};

// Separate write capability: readers and achievement evaluation do not need mutation access.
// Implementations must validate the complete range before changing any byte.
class MemoryWriter {
  public:
    virtual ~MemoryWriter() = default;

    virtual MemoryWriteError write(uint32_t address, std::span<const uint8_t> bytes) = 0;
};

// The range must fit in one available, writable MemoryRegionInfo and in the address space.
MemoryWriteError validate_memory_write(uint32_t address, size_t byte_count, uint32_t address_space,
                                       std::span<const MemoryRegionInfo> regions);
const char* memory_write_error_message(MemoryWriteError error);

enum class MemoryValueSize {
    Bit0,
    Bit1,
    Bit2,
    Bit3,
    Bit4,
    Bit5,
    Bit6,
    Bit7,
    LowNibble,
    HighNibble,
    EightBit,
    SixteenBit,
    TwentyFourBit,
    ThirtyTwoBit,
};

enum class ByteOrder { LittleEndian, BigEndian };

unsigned memory_value_byte_count(MemoryValueSize size);
uint32_t memory_value_max(MemoryValueSize size);
std::optional<uint32_t> decode_memory_value(const MemoryReader& memory, uint32_t address,
                                            MemoryValueSize size, ByteOrder order);

enum class MemoryValueEncodeError { None, TooLarge, InvalidBuffer };

// Encodes into an existing byte buffer. Bit and nibble sizes preserve the other bits in byte 0.
MemoryValueEncodeError encode_memory_value(uint32_t value, MemoryValueSize size, ByteOrder order,
                                           std::span<uint8_t> bytes);

struct MemoryByteSnapshot {
    uint32_t address = 0;
    uint8_t value = 0;

    friend bool operator==(const MemoryByteSnapshot&, const MemoryByteSnapshot&) = default;
};

// Returns addresses that have a different value in both visible snapshots.
std::vector<uint32_t> changed_memory_bytes(std::span<const MemoryByteSnapshot> previous,
                                           std::span<const MemoryByteSnapshot> current);

// Read access to emulated memory through RetroAchievements addresses: the address space each
// console has on retroachievements.org (PS1: 0x000000-0x2003FF), whatever the way the emulator
// stores it. Dev tools (viewer, search, achievement logic) only ever see this interface, so they
// can be tested with plain byte buffers.
class MemoryReader {
  public:
    virtual ~MemoryReader() = default;

    // Valid addresses are 0 .. size() - 1 (some of them may be unavailable). RA address spaces
    // are far below 4 GiB (PS1: 0x200400 bytes), so a full 2^32-byte space is not representable;
    // implementations cap size() at UINT32_MAX.
    virtual uint32_t size() const = 0;

    // Copies up to out.size() bytes starting at `address` and returns how many were copied.
    // Copying stops at the end of the address space or at an unavailable block, so 0 means
    // `address` itself cannot be read.
    virtual size_t read(uint32_t address, std::span<uint8_t> out) const = 0;

    virtual std::vector<MemoryRegionInfo> regions() const = 0;
};

// Reads an unsigned little-endian value of 1 to 4 bytes (PS1 is little-endian). Returns nullopt
// if any of its bytes cannot be read or `bytes` is not 1-4.
std::optional<uint32_t> read_le(const MemoryReader& memory, uint32_t address, unsigned bytes);

// A MemoryReader over a plain byte buffer, address 0 = first byte. Used as synthetic console
// memory in tests and as the simplest possible implementation of the interface.
class BufferMemory final : public MemoryReader, public MemoryWriter {
  public:
    explicit BufferMemory(std::vector<uint8_t> bytes, std::string description = "Memory");
    BufferMemory(std::vector<uint8_t> bytes, std::vector<MemoryRegionInfo> regions);

    uint32_t size() const override;
    size_t read(uint32_t address, std::span<uint8_t> out) const override;
    std::vector<MemoryRegionInfo> regions() const override;
    MemoryWriteError write(uint32_t address, std::span<const uint8_t> bytes) override;

    std::vector<uint8_t>& bytes() { return bytes_; }

  private:
    std::vector<uint8_t> bytes_;
    std::vector<MemoryRegionInfo> regions_;
};

} // namespace cw
