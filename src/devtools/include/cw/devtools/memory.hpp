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
    bool available = false; // false when the emulator does not expose this block

    friend bool operator==(const MemoryRegionInfo&, const MemoryRegionInfo&) = default;
};

// Read access to emulated memory through RetroAchievements addresses: the address space each
// console has on retroachievements.org (PS1: 0x000000-0x2003FF), whatever the way the emulator
// stores it. Dev tools (viewer, search, achievement logic) only ever see this interface, so they
// can be tested with plain byte buffers.
class MemoryReader {
  public:
    virtual ~MemoryReader() = default;

    // Valid addresses are 0 .. size() - 1 (some of them may be unavailable).
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
class BufferMemory final : public MemoryReader {
  public:
    explicit BufferMemory(std::vector<uint8_t> bytes, std::string description = "Memory");

    uint32_t size() const override;
    size_t read(uint32_t address, std::span<uint8_t> out) const override;
    std::vector<MemoryRegionInfo> regions() const override;

    std::vector<uint8_t>& bytes() { return bytes_; }

  private:
    std::vector<uint8_t> bytes_;
    std::string description_;
};

} // namespace cw
