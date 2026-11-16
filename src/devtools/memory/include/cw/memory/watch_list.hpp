#pragma once

#include "cw/memory/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cw {

struct MemoryWatchItem {
    uint32_t address = 0;
    MemoryValueSize size = MemoryValueSize::EightBit;
    ByteOrder byte_order = ByteOrder::LittleEndian;
    std::string label;
    std::optional<uint32_t> current_value;
    std::optional<uint32_t> previous_value;
    std::optional<uint64_t> last_changed_frame;
};

// Tracks selected memory interpretations without depending on the frontend or emulator.
class WatchList {
  public:
    static constexpr uint64_t kDefaultHighlightFrames = 60;

    explicit WatchList(uint64_t highlight_frames = kDefaultHighlightFrames)
        : highlight_frames_(highlight_frames) {}

    // Returns false when the same address, size, and byte order are already watched.
    bool add(uint32_t address, MemoryValueSize size, ByteOrder byte_order, std::string label = {});
    bool remove(size_t index);
    // Moves an item to its new zero-based position. Returns false for an invalid position.
    bool move(size_t from, size_t to);

    // Reads every item once. `frame` is the emulated frame counter, not UI time.
    void update(const MemoryReader& memory, uint64_t frame);

    std::optional<uint64_t> frames_since_change(size_t index, uint64_t frame) const;
    const std::vector<MemoryWatchItem>& items() const { return items_; }
    uint64_t highlight_frames() const { return highlight_frames_; }
    size_t size() const { return items_.size(); }
    void clear_highlights();
    void clear();

  private:
    std::vector<MemoryWatchItem> items_;
    uint64_t highlight_frames_;
    std::optional<uint64_t> last_update_frame_;
};

} // namespace cw
