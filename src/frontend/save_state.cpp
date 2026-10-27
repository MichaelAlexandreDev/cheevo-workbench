#include "save_state.hpp"

#include <algorithm>
#include <cstring>

namespace cw {

namespace {

constexpr char kMagic[] = "RASTATE"; // 7 characters, followed by the version byte
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 8;      // magic + version
constexpr size_t kBlockHeaderSize = 8; // tag + size

size_t align8(size_t size) {
    return (size + 7) & ~size_t{7};
}

void append_block(std::vector<uint8_t>& out, const char (&tag)[5], std::span<const uint8_t> data) {
    out.insert(out.end(), tag, tag + 4);
    const auto size = static_cast<uint32_t>(data.size());
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<uint8_t>(size >> shift));
    }
    out.insert(out.end(), data.begin(), data.end());
    out.resize(out.size() + (align8(data.size()) - data.size()), 0);
}

uint32_t read_u32_le(const uint8_t* p) {
    return uint32_t{p[0]} | uint32_t{p[1]} << 8 | uint32_t{p[2]} << 16 | uint32_t{p[3]} << 24;
}

} // namespace

std::vector<uint8_t> write_state_file(std::span<const uint8_t> core_data,
                                      std::span<const uint8_t> achievements) {
    std::vector<uint8_t> out(kMagic, kMagic + 7);
    out.push_back(kVersion);
    append_block(out, "MEM ", core_data);
    if (!achievements.empty()) {
        append_block(out, "ACHV", achievements);
    }
    append_block(out, "END ", {});
    return out;
}

std::optional<StateContents> parse_state_file(std::span<const uint8_t> file, std::string& error) {
    if (file.size() < kHeaderSize || std::memcmp(file.data(), kMagic, 7) != 0) {
        if (file.empty()) {
            error = "The state file is empty";
            return std::nullopt;
        }
        return StateContents{.core = {file.begin(), file.end()}, .achievements = std::nullopt};
    }
    if (file[7] != kVersion) {
        error = "Unsupported RASTATE version " + std::to_string(file[7]);
        return std::nullopt;
    }

    StateContents contents;
    bool has_core = false;
    size_t pos = kHeaderSize;
    while (pos < file.size()) {
        if (file.size() - pos < kBlockHeaderSize) {
            error = "Truncated block header";
            return std::nullopt;
        }
        const uint8_t* header = file.data() + pos;
        const size_t size = read_u32_le(header + 4);
        pos += kBlockHeaderSize;
        // Compare against what is left instead of computing pos + size, which could overflow.
        if (size > file.size() - pos) {
            error =
                "Truncated block (" + std::string(reinterpret_cast<const char*>(header), 4) + ")";
            return std::nullopt;
        }
        const std::span<const uint8_t> data = file.subspan(pos, size);

        if (std::memcmp(header, "MEM ", 4) == 0) {
            contents.core.assign(data.begin(), data.end());
            has_core = true;
        } else if (std::memcmp(header, "ACHV", 4) == 0) {
            contents.achievements.emplace(data.begin(), data.end());
        } else if (std::memcmp(header, "END ", 4) == 0) {
            break;
        } // other blocks (e.g. "SCRN") are not needed and skipped

        // The padding of the last block may be missing; never step past the end.
        pos += std::min(align8(size), file.size() - pos);
    }
    if (!has_core) {
        error = "The state file has no core data (MEM block)";
        return std::nullopt;
    }
    return contents;
}

} // namespace cw
