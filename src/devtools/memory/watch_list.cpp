#include "cw/memory/watch_list.hpp"

#include <algorithm>
#include <utility>

namespace cw {

bool WatchList::add(uint32_t address, MemoryValueSize size, ByteOrder byte_order,
                    std::string label) {
    const auto duplicate = std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.address == address && item.size == size && item.byte_order == byte_order;
    });
    if (duplicate != items_.end()) {
        return false;
    }

    items_.push_back({.address = address,
                      .size = size,
                      .byte_order = byte_order,
                      .label = std::move(label),
                      .current_value = std::nullopt,
                      .previous_value = std::nullopt,
                      .last_changed_frame = std::nullopt});
    return true;
}

bool WatchList::remove(size_t index) {
    if (index >= items_.size()) {
        return false;
    }
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool WatchList::move(size_t from, size_t to) {
    if (from >= items_.size() || to >= items_.size()) {
        return false;
    }
    if (from == to) {
        return true;
    }

    MemoryWatchItem item = std::move(items_[from]);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(from));
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(to), std::move(item));
    return true;
}

void WatchList::update(const MemoryReader& memory, uint64_t frame) {
    if (last_update_frame_ && frame < *last_update_frame_) {
        // Core unload/reload resets its frame counter. Keep watches, but do not carry a highlight
        // timestamp into the new frame-counter epoch.
        for (MemoryWatchItem& item : items_) {
            item.last_changed_frame.reset();
        }
    }
    last_update_frame_ = frame;

    for (MemoryWatchItem& item : items_) {
        const auto value = decode_memory_value(memory, item.address, item.size, item.byte_order);
        if (!value) {
            if (item.current_value) {
                item.previous_value = item.current_value;
            }
            item.current_value.reset();
            item.last_changed_frame.reset();
            continue;
        }

        if (item.current_value) {
            if (*item.current_value != *value) {
                item.previous_value = item.current_value;
                item.last_changed_frame = frame;
            }
        } else if (item.previous_value && *item.previous_value != *value) {
            // After an unavailable read, compare against the last value that was actually seen.
            item.last_changed_frame = frame;
        }
        item.current_value = value;
    }
}

std::optional<uint64_t> WatchList::frames_since_change(size_t index, uint64_t frame) const {
    if (index >= items_.size() || !items_[index].last_changed_frame) {
        return std::nullopt;
    }

    const uint64_t changed_at = *items_[index].last_changed_frame;
    if (frame < changed_at || frame - changed_at >= highlight_frames_) {
        return std::nullopt;
    }
    return frame - changed_at;
}

void WatchList::clear_highlights() {
    for (MemoryWatchItem& item : items_) {
        item.last_changed_frame.reset();
    }
}

void WatchList::clear() {
    items_.clear();
    last_update_frame_.reset();
}

} // namespace cw
