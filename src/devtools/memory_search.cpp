#include "cw/devtools/memory_search.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>

namespace cw {

namespace {

size_t value_width(MemoryValueSize size) {
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
        return 4;
    case MemoryValueSize::EightBit:
        return 8;
    case MemoryValueSize::SixteenBit:
        return 16;
    case MemoryValueSize::TwentyFourBit:
        return 24;
    case MemoryValueSize::ThirtyTwoBit:
        return 32;
    }
    return 0;
}

size_t candidates_per_address(const MemorySearchConfig& config) {
    return config.all_bit_positions ? 8U : 1U;
}

MemoryValueSize candidate_size(const MemorySearchConfig& config, size_t index) {
    if (!config.all_bit_positions) {
        return config.size;
    }
    const unsigned bit = static_cast<unsigned>(index % 8U);
    return static_cast<MemoryValueSize>(static_cast<unsigned>(MemoryValueSize::Bit0) + bit);
}

size_t candidate_address_offset(const MemorySearchConfig& config, size_t index) {
    return config.all_bit_positions ? index / 8U : index;
}

uint32_t read_packed_value(const std::vector<uint8_t>& values, size_t index, size_t width) {
    if (width < 8) {
        const size_t bit_offset = index * width;
        const size_t byte_offset = bit_offset / 8;
        const unsigned shift = static_cast<unsigned>(bit_offset % 8);
        const uint32_t mask = (uint32_t{1} << width) - 1U;
        return (values[byte_offset] >> shift) & mask;
    }

    const size_t byte_offset = index * (width / 8);
    uint32_t value = 0;
    for (size_t i = 0; i < width / 8; ++i) {
        value |= uint32_t{values[byte_offset + i]} << (i * 8U);
    }
    return value;
}

void write_packed_value(std::vector<uint8_t>& values, size_t index, size_t width, uint32_t value) {
    if (width < 8) {
        const size_t bit_offset = index * width;
        const size_t byte_offset = bit_offset / 8;
        const unsigned shift = static_cast<unsigned>(bit_offset % 8);
        const uint32_t mask = (uint32_t{1} << width) - 1U;
        const uint8_t shifted_mask = static_cast<uint8_t>(mask << shift);
        values[byte_offset] =
            static_cast<uint8_t>((values[byte_offset] & static_cast<uint8_t>(~shifted_mask)) |
                                 static_cast<uint8_t>((value & mask) << shift));
        return;
    }

    const size_t byte_offset = index * (width / 8);
    for (size_t i = 0; i < width / 8; ++i) {
        values[byte_offset + i] = static_cast<uint8_t>(value >> (i * 8U));
    }
}

// The search range copied out of memory in one pass, with a per-byte "readable" flag.
// Decoding candidate by candidate through MemoryReader::read would cost one virtual call (and,
// for a running core, one walk of rc_libretro's region list) per candidate: two million calls
// for 2 MiB of 8-bit values, sixteen million in all-bits mode. One bulk copy plus a tight loop
// over plain bytes is several times faster.
class RangeSnapshot {
  public:
    RangeSnapshot(const MemoryReader& memory, uint32_t start, uint32_t end)
        : start_(start), bytes_(static_cast<size_t>(end - start) + 1U),
          readable_(bytes_.size(), 0) {
        size_t offset = 0;
        while (offset < bytes_.size()) {
            const auto address = static_cast<uint32_t>(start + offset);
            const size_t copied = memory.read(address, std::span(bytes_).subspan(offset));
            if (copied == 0) {
                ++offset; // unreadable byte (gap in the memory map): leave it flagged as such
                continue;
            }
            std::fill_n(readable_.begin() + static_cast<std::ptrdiff_t>(offset), copied, 1);
            offset += copied;
        }
    }

    // As decode_memory_value(memory, address, ...) would answer, from the copy.
    std::optional<uint32_t> decode(uint32_t address, MemoryValueSize size, ByteOrder order) const {
        const unsigned byte_count = memory_value_byte_count(size);
        const size_t offset = address - start_;
        if (byte_count == 0 || offset + byte_count > bytes_.size()) {
            return std::nullopt;
        }
        for (unsigned i = 0; i < byte_count; ++i) {
            if (!readable_[offset + i]) {
                return std::nullopt;
            }
        }
        return decode_memory_bytes(std::span(bytes_).subspan(offset, byte_count), size, order);
    }

  private:
    uint32_t start_;
    std::vector<uint8_t> bytes_;
    std::vector<uint8_t> readable_;
};

bool matches(uint32_t left, MemorySearchComparison comparison, uint32_t right) {
    switch (comparison) {
    case MemorySearchComparison::Equal:
        return left == right;
    case MemorySearchComparison::NotEqual:
        return left != right;
    case MemorySearchComparison::Less:
        return left < right;
    case MemorySearchComparison::Greater:
        return left > right;
    case MemorySearchComparison::LessOrEqual:
        return left <= right;
    case MemorySearchComparison::GreaterOrEqual:
        return left >= right;
    }
    return false;
}

} // namespace

bool MemorySearch::configure(const MemorySearchConfig& config) {
    if (configured_ && config_ == config) {
        return false;
    }

    const bool had_results = active_;
    const bool was_invalidated = invalidated_;
    clear_results();
    config_ = config;
    configured_ = true;
    invalidated_ = had_results || was_invalidated;
    return had_results;
}

void MemorySearch::invalidate() {
    const bool had_results = active_;
    clear_results();
    invalidated_ = invalidated_ || had_results;
}

bool MemorySearch::start(const MemoryReader& memory) {
    clear_results();
    invalidated_ = false;
    if (!configured_ || config_.range_start > config_.range_end || memory.size() == 0 ||
        config_.range_end >= memory.size()) {
        return false;
    }

    const size_t width = packed_value_bits();
    if (width == 0) {
        return false;
    }

    const size_t address_count =
        static_cast<size_t>(static_cast<uint64_t>(config_.range_end) - config_.range_start + 1U);
    const size_t per_address = candidates_per_address(config_);
    if (address_count > std::numeric_limits<size_t>::max() / per_address) {
        return false;
    }
    const size_t total_candidates = address_count * per_address;
    if (total_candidates > (std::numeric_limits<size_t>::max() - 7U) / width) {
        return false;
    }

    try {
        std::vector<uint64_t> candidates(
            total_candidates / 64U + (total_candidates % 64U == 0 ? 0U : 1U), 0);
        std::vector<uint8_t> initial_values((total_candidates * width + 7U) / 8U, 0);
        size_t candidate_count = 0;
        const RangeSnapshot snapshot(memory, config_.range_start, config_.range_end);

        for (size_t index = 0; index < total_candidates; ++index) {
            const uint32_t address =
                config_.range_start +
                static_cast<uint32_t>(candidate_address_offset(config_, index));
            const MemoryValueSize size = candidate_size(config_, index);
            const unsigned byte_count = memory_value_byte_count(size);
            if (config_.aligned && ((byte_count == 2 && address % 2U != 0) ||
                                    (byte_count == 4 && address % 4U != 0))) {
                continue;
            }
            if (static_cast<uint64_t>(address) + byte_count - 1U > config_.range_end) {
                continue;
            }

            const auto value = snapshot.decode(address, size, config_.byte_order);
            if (!value) {
                continue;
            }

            candidates[index / 64U] |= uint64_t{1} << (index % 64U);
            write_packed_value(initial_values, index, width, *value);
            ++candidate_count;
        }

        std::vector<uint8_t> previous_values = initial_values;
        std::vector<size_t> rank_index = make_rank_index(candidates);
        candidates_ = std::move(candidates);
        rank_index_ = std::move(rank_index);
        initial_values_ = std::move(initial_values);
        previous_values_ = std::move(previous_values);
        candidate_count_ = candidate_count;
        active_ = true;
    } catch (const std::bad_alloc&) {
        clear_results();
        return false;
    }
    return true;
}

bool MemorySearch::filter(const MemoryReader& memory, MemorySearchComparison comparison,
                          MemorySearchReference reference, uint32_t constant) {
    if (!active_) {
        return false;
    }
    if (memory.size() == 0 || config_.range_end >= memory.size()) {
        invalidate();
        return false;
    }

    bool history_saved = false;
    try {
        history_.push_back({.candidates = candidates_,
                            .previous_values = previous_values_,
                            .candidate_count = candidate_count_});
        history_saved = true;

        std::vector<uint64_t> next_candidates(candidates_.size(), 0);
        std::vector<uint8_t> next_previous = previous_values_;
        size_t next_count = 0;
        const size_t width = packed_value_bits();
        const RangeSnapshot snapshot(memory, config_.range_start, config_.range_end);

        for (size_t word_index = 0; word_index < candidates_.size(); ++word_index) {
            uint64_t word = candidates_[word_index];
            while (word != 0) {
                const unsigned bit = std::countr_zero(word);
                const size_t index = word_index * 64U + bit;
                word &= word - 1U;

                const uint32_t address =
                    config_.range_start +
                    static_cast<uint32_t>(candidate_address_offset(config_, index));
                const MemoryValueSize size = candidate_size(config_, index);
                const unsigned byte_count = memory_value_byte_count(size);
                if (static_cast<uint64_t>(address) + byte_count - 1U > config_.range_end) {
                    continue;
                }
                const auto current = snapshot.decode(address, size, config_.byte_order);
                if (!current) {
                    continue;
                }

                uint32_t target = constant;
                switch (reference) {
                case MemorySearchReference::Previous:
                    target = read_packed_value(previous_values_, index, width);
                    break;
                case MemorySearchReference::Initial:
                    target = read_packed_value(initial_values_, index, width);
                    break;
                case MemorySearchReference::Constant:
                    break;
                }

                if (matches(*current, comparison, target)) {
                    next_candidates[word_index] |= uint64_t{1} << bit;
                    write_packed_value(next_previous, index, width, *current);
                    ++next_count;
                }
            }
        }

        std::vector<size_t> next_rank_index = make_rank_index(next_candidates);
        if (history_.size() > kMaxUndoSteps) {
            history_.pop_front();
        }
        candidates_ = std::move(next_candidates);
        previous_values_ = std::move(next_previous);
        rank_index_ = std::move(next_rank_index);
        candidate_count_ = next_count;
    } catch (const std::bad_alloc&) {
        if (history_saved) {
            history_.pop_back();
        }
        return false;
    }
    return true;
}

bool MemorySearch::undo() {
    if (!active_ || history_.empty()) {
        return false;
    }

    try {
        std::vector<size_t> rank_index = make_rank_index(history_.back().candidates);
        candidates_ = std::move(history_.back().candidates);
        previous_values_ = std::move(history_.back().previous_values);
        candidate_count_ = history_.back().candidate_count;
        rank_index_ = std::move(rank_index);
        history_.pop_back();
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

std::optional<MemorySearchResult> MemorySearch::result_at(const MemoryReader& memory,
                                                          size_t index) const {
    if (!active_) {
        return std::nullopt;
    }
    const auto candidate = candidate_index(index);
    if (!candidate) {
        return std::nullopt;
    }

    const size_t width = packed_value_bits();
    const uint32_t address =
        config_.range_start + static_cast<uint32_t>(candidate_address_offset(config_, *candidate));
    const MemoryValueSize size = candidate_size(config_, *candidate);
    return MemorySearchResult{
        .address = address,
        .bit_index = config_.all_bit_positions
                         ? std::optional<uint8_t>(static_cast<uint8_t>(*candidate % 8U))
                         : std::nullopt,
        .current = decode_memory_value(memory, address, size, config_.byte_order),
        .previous = read_packed_value(previous_values_, *candidate, width),
        .initial = read_packed_value(initial_values_, *candidate, width),
    };
}

void MemorySearch::clear_results() {
    active_ = false;
    candidate_count_ = 0;
    candidates_.clear();
    rank_index_.clear();
    initial_values_.clear();
    previous_values_.clear();
    history_.clear();
}

size_t MemorySearch::packed_value_bits() const {
    return config_.all_bit_positions ? 1U : value_width(config_.size);
}

std::optional<size_t> MemorySearch::candidate_index(size_t rank) const {
    if (!active_ || rank >= candidate_count_ || rank_index_.empty()) {
        return std::nullopt;
    }

    const auto upper = std::upper_bound(rank_index_.begin(), rank_index_.end(), rank);
    if (upper == rank_index_.begin()) {
        return std::nullopt;
    }
    const size_t word_index = static_cast<size_t>(std::distance(rank_index_.begin(), upper) - 1);
    uint64_t word = candidates_[word_index];
    size_t within_word = rank - rank_index_[word_index];
    while (within_word > 0) {
        word &= word - 1U;
        --within_word;
    }
    if (word == 0) {
        return std::nullopt;
    }
    return word_index * 64U + std::countr_zero(word);
}

std::vector<size_t> MemorySearch::make_rank_index(const std::vector<uint64_t>& candidates) {
    std::vector<size_t> rank(candidates.size() + 1U, 0);
    for (size_t i = 0; i < candidates.size(); ++i) {
        rank[i + 1U] = rank[i] + std::popcount(candidates[i]);
    }
    return rank;
}

} // namespace cw
