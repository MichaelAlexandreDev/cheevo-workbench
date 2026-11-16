#pragma once

#include "cw/memory/memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cw {

enum class AssistedSearchKind { Flag, Area, Counter };

struct AssistedSearchResult {
    uint32_t address = 0;
    uint8_t value = 0;
    AssistedSearchKind kind = AssistedSearchKind::Area;
    unsigned score = 0;
    uint32_t changes = 0;
    uint32_t last_change_frame = 0;
    std::array<uint8_t, 4> distinct_values{};
    uint8_t distinct_count = 0; // 5 means more than four distinct values have been observed
    std::optional<uint8_t> bit_index;
};

// A bounded, offline heuristic over 8-bit RA addresses. Results are suggestions, not proof of
// meaning. Call observe once after each emulated frame and reset when the memory map changes.
class AssistedSearch {
  public:
    static constexpr uint32_t kMaxAddressSpace = 4 * 1024 * 1024;
    static constexpr size_t kMaxLabels = 8;
    static constexpr size_t kMaxReadableSpans = 4096;
    static constexpr uint32_t kMarkWindowFrames = 3;

    bool start(const MemoryReader& memory);
    void reset();
    // Returns false and resets if the address-space size changes or memory cannot be read.
    bool observe(const MemoryReader& memory);
    // The observation finishes after kMarkWindowFrames further observations. Reusing a label
    // intersects its candidates; same_value also requires the first marked final value. Empty
    // labels, concurrent marks and excess labels are rejected.
    bool mark(std::string label, bool same_value = false);
    // Discard an incomplete observation without altering completed marks.
    void cancel_mark();
    // At most 10,000 results are returned, ordered by score and address.
    std::vector<AssistedSearchResult> results(size_t limit = 100, std::string_view label = {},
                                              std::optional<AssistedSearchKind> kind = {}) const;
    bool active() const { return active_; }
    bool mark_pending() const { return pending_; }
    uint64_t frame_count() const { return frame_; }
    size_t mark_count(std::string_view label) const;
    const std::string& error() const { return error_; }
    size_t tracked_bytes() const { return tracked_bytes_; }
    size_t memory_usage_bytes() const;

  private:
    struct Stats {
        uint32_t last_change = 0;
        uint16_t changes = 0;
        uint16_t increases = 0;
        uint16_t decreases = 0;
        uint8_t value = 0;
        uint8_t changed_bits = 0;
        uint8_t distinct[4]{};
        uint8_t distinct_count = 0;
        bool valid = false;
        bool stable_before = false;
    };
    struct Label {
        std::string name;
        std::vector<uint8_t> candidates;
        std::vector<uint8_t> values;
        size_t count = 0;
    };
    bool read_memory(const MemoryReader& memory, bool initial);
    void finish_mark();
    bool active_ = false;
    bool pending_ = false;
    bool same_value_ = false;
    size_t tracked_bytes_ = 0;
    std::string error_;
    uint64_t frame_ = 0;
    uint64_t mark_frame_ = 0;
    size_t pending_label_ = 0;
    std::vector<Stats> stats_;
    std::vector<uint8_t> buffer_;
    std::vector<uint8_t> pending_candidates_;
    std::vector<MemoryRegionInfo> regions_;
    std::vector<MemoryRegionInfo> map_;
    std::vector<Label> labels_;
};

} // namespace cw
