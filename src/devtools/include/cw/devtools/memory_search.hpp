#pragma once

#include "cw/devtools/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace cw {

enum class MemorySearchComparison { Equal, NotEqual, Less, Greater, LessOrEqual, GreaterOrEqual };
enum class MemorySearchReference { Previous, Initial, Constant };

struct MemorySearchConfig {
    MemoryValueSize size = MemoryValueSize::EightBit;
    ByteOrder byte_order = ByteOrder::LittleEndian;
    uint32_t range_start = 0;
    uint32_t range_end = 0;
    bool aligned = false;
    bool all_bit_positions = false;

    friend bool operator==(const MemorySearchConfig&, const MemorySearchConfig&) = default;
};

struct MemorySearchResult {
    uint32_t address = 0;
    std::optional<uint8_t> bit_index;
    std::optional<uint32_t> current;
    uint32_t previous = 0;
    uint32_t initial = 0;
};

// Snapshot based search over RA-addressed memory. Candidate presence is stored as a bitset and
// snapshot values use the minimum number of bits required by the selected memory size.
class MemorySearch {
  public:
    static constexpr size_t kMaxUndoSteps = 4;

    // Changing any setting discards results, because old candidates no longer describe the new
    // interpretation of memory. The caller must start another search after this returns true.
    bool configure(const MemorySearchConfig& config);
    void invalidate();

    bool start(const MemoryReader& memory);
    bool filter(const MemoryReader& memory, MemorySearchComparison comparison,
                MemorySearchReference reference, uint32_t constant = 0);
    bool undo();

    bool active() const { return active_; }
    bool invalidated() const { return invalidated_; }
    size_t candidate_count() const { return candidate_count_; }
    size_t undo_count() const { return history_.size(); }
    const MemorySearchConfig& config() const { return config_; }

    // Results are in ascending address order. A missing current value means the address became
    // unreadable after the last filter; its saved previous and initial values remain available.
    std::optional<MemorySearchResult> result_at(const MemoryReader& memory, size_t index) const;

  private:
    struct HistoryEntry {
        std::vector<uint64_t> candidates;
        std::vector<uint8_t> previous_values;
        size_t candidate_count = 0;
    };

    void clear_results();
    size_t packed_value_bits() const;
    std::optional<size_t> candidate_index(size_t rank) const;
    static std::vector<size_t> make_rank_index(const std::vector<uint64_t>& candidates);

    MemorySearchConfig config_{};
    bool configured_ = false;
    bool active_ = false;
    bool invalidated_ = false;
    size_t candidate_count_ = 0;
    std::vector<uint64_t> candidates_;
    std::vector<size_t> rank_index_;
    std::vector<uint8_t> initial_values_;
    std::vector<uint8_t> previous_values_;
    std::deque<HistoryEntry> history_;
};

} // namespace cw
