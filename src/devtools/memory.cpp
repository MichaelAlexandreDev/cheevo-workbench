#include "cw/devtools/memory.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

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

BufferMemory::BufferMemory(std::vector<uint8_t> bytes, std::string description)
    : bytes_(std::move(bytes)), description_(std::move(description)) {
    // RA addresses are 32-bit; a bigger buffer would have bytes no address can reach.
    if (bytes_.size() > std::numeric_limits<uint32_t>::max()) {
        bytes_.resize(std::numeric_limits<uint32_t>::max());
    }
}

uint32_t BufferMemory::size() const {
    return static_cast<uint32_t>(bytes_.size());
}

size_t BufferMemory::read(uint32_t address, std::span<uint8_t> out) const {
    if (address >= bytes_.size()) {
        return 0;
    }
    const size_t count = std::min(out.size(), bytes_.size() - address);
    std::memcpy(out.data(), bytes_.data() + address, count);
    return count;
}

std::vector<MemoryRegionInfo> BufferMemory::regions() const {
    if (bytes_.empty()) {
        return {};
    }
    return {{.start = 0, .end = size() - 1, .description = description_, .available = true}};
}

} // namespace cw
