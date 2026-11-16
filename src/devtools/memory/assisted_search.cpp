#include "cw/memory/assisted_search.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace cw {
namespace {
void increment(uint16_t& value) {
    if (value != std::numeric_limits<uint16_t>::max())
        ++value;
}
} // namespace

void AssistedSearch::reset() {
    active_ = false;
    pending_ = false;
    same_value_ = false;
    tracked_bytes_ = 0;
    error_.clear();
    frame_ = 0;
    mark_frame_ = 0;
    pending_label_ = 0;
    std::vector<Stats>().swap(stats_);
    std::vector<uint8_t>().swap(buffer_);
    std::vector<uint8_t>().swap(pending_candidates_);
    std::vector<MemoryRegionInfo>().swap(regions_);
    std::vector<MemoryRegionInfo>().swap(map_);
    std::vector<Label>().swap(labels_);
}

bool AssistedSearch::start(const MemoryReader& memory) {
    reset();
    if (memory.size() == 0 || memory.size() > kMaxAddressSpace) {
        error_ = "Address space is empty or exceeds the 4 MiB limit";
        return false;
    }
    map_ = memory.regions();
    stats_.resize(memory.size());
    buffer_.resize(memory.size());
    // Discover actual readable spans, including partially exposed console regions.
    // Zero-length reads advance one address so even unaligned holes are handled correctly.
    size_t pos = 0;
    while (pos < stats_.size()) {
        size_t n = memory.read(static_cast<uint32_t>(pos), std::span(buffer_).subspan(pos));
        n = std::min(n, stats_.size() - pos);
        if (!n) {
            ++pos;
            continue;
        }
        if (!regions_.empty() && uint64_t{regions_.back().end} + 1 == pos) {
            regions_.back().end = static_cast<uint32_t>(pos + n - 1);
        } else {
            if (regions_.size() == kMaxReadableSpans) {
                reset();
                error_ = "Memory has too many separate readable spans";
                return false;
            }
            regions_.push_back(
                {static_cast<uint32_t>(pos), static_cast<uint32_t>(pos + n - 1), {}, true, false});
        }
        tracked_bytes_ += n;
        pos += n;
    }
    if (!read_memory(memory, true)) {
        reset();
        error_ = "No readable memory";
        return false;
    }
    active_ = true;
    return true;
}

bool AssistedSearch::observe(const MemoryReader& memory) {
    if (!active_)
        return false;
    if (memory.size() != stats_.size() || memory.regions() != map_ || frame_ >= UINT32_MAX - 1) {
        reset();
        error_ = "Address space changed or frame limit reached";
        return false;
    }
    ++frame_;
    if (!read_memory(memory, false)) {
        reset();
        error_ = "Memory is no longer readable";
        return false;
    }
    if (pending_ && frame_ - mark_frame_ >= kMarkWindowFrames)
        finish_mark();
    return true;
}

bool AssistedSearch::read_memory(const MemoryReader& memory, bool initial) {
    std::array<uint8_t, 4096> scratch{};
    bool any = false;
    for (const auto& region : regions_) {
        const size_t end = static_cast<size_t>(region.end) + 1;
        for (size_t start = region.start; start < end;) {
            const size_t length = std::min(scratch.size(), end - start);
            size_t copied = 0;
            while (copied < length) {
                const size_t read =
                    memory.read(static_cast<uint32_t>(start + copied),
                                std::span(scratch).subspan(copied, length - copied));
                if (!read || read > length - copied)
                    return false;
                copied += read;
            }
            any = true;
            for (size_t block = 0; block < length; block += 256) {
                const size_t count = std::min<size_t>(256, length - block);
                if (!initial &&
                    std::memcmp(buffer_.data() + start + block, scratch.data() + block, count) == 0)
                    continue;
                for (size_t offset = block; offset < block + count; ++offset) {
                    const size_t i = start + offset;
                    auto& s = stats_[i];
                    const uint8_t value = scratch[offset];
                    if (initial) {
                        s = {};
                        s.valid = true;
                        s.value = value;
                        s.distinct[0] = value;
                        s.distinct_count = 1;
                        continue;
                    }
                    if (s.value == value)
                        continue;
                    const uint32_t age = static_cast<uint32_t>(frame_) - s.last_change;
                    s.stable_before = age >= 2;
                    s.last_change = static_cast<uint32_t>(frame_);
                    increment(s.changes);
                    if (static_cast<int>(value) - static_cast<int>(s.value) == 1)
                        increment(s.increases);
                    if (static_cast<int>(s.value) - static_cast<int>(value) == 1)
                        increment(s.decreases);
                    s.changed_bits |= value ^ s.value;
                    s.value = value;
                    if (s.distinct_count <= 4) {
                        bool found = false;
                        for (unsigned j = 0; j < std::min<unsigned>(s.distinct_count, 4); ++j)
                            found |= s.distinct[j] == value;
                        if (!found) {
                            if (s.distinct_count < 4)
                                s.distinct[s.distinct_count] = value;
                            ++s.distinct_count;
                        }
                    }
                    if (pending_)
                        pending_candidates_[i] = s.stable_before;
                }
                std::memcpy(buffer_.data() + start + block, scratch.data() + block, count);
            }
            start += length;
        }
    }
    return any;
}

bool AssistedSearch::mark(std::string label, bool same_value) {
    error_.clear();
    if (!active_) {
        error_ = "Start collecting before marking a moment";
        return false;
    }
    if (pending_) {
        error_ = "A moment is already being observed";
        return false;
    }
    if (label.empty() || label.size() > 128) {
        error_ = "Label must contain 1 to 128 bytes";
        return false;
    }
    auto it = std::find_if(labels_.begin(), labels_.end(),
                           [&](const Label& item) { return item.name == label; });
    if (it == labels_.end()) {
        if (labels_.size() >= kMaxLabels) {
            error_ = "Maximum number of labels reached";
            return false;
        }
        labels_.push_back({std::move(label), {}, {}, 0});
        pending_label_ = labels_.size() - 1;
    } else {
        pending_label_ = static_cast<size_t>(it - labels_.begin());
    }
    pending_candidates_.assign(stats_.size(), 0);
    for (size_t i = 0; i < stats_.size(); ++i) {
        const auto& s = stats_[i];
        if (s.valid && s.changes && s.stable_before && frame_ - s.last_change <= kMarkWindowFrames)
            pending_candidates_[i] = 1;
    }
    same_value_ = same_value;
    pending_ = true;
    mark_frame_ = frame_;
    return true;
}

void AssistedSearch::cancel_mark() {
    if (!pending_)
        return;
    if (labels_[pending_label_].count == 0)
        labels_.erase(labels_.begin() + pending_label_);
    pending_ = false;
    pending_candidates_.clear();
    error_.clear();
}

void AssistedSearch::finish_mark() {
    auto& label = labels_[pending_label_];
    if (label.count == 0) {
        label.candidates.assign(stats_.size(), 1);
        label.values.resize(stats_.size());
    }
    for (size_t i = 0; i < stats_.size(); ++i) {
        const auto& s = stats_[i];
        label.candidates[i] &= pending_candidates_[i] && s.valid && frame_ - s.last_change >= 2 &&
                               (!same_value_ || label.count == 0 || label.values[i] == s.value);
        if (label.count == 0)
            label.values[i] = s.value;
    }
    ++label.count;
    pending_ = false;
    pending_candidates_.clear();
}

size_t AssistedSearch::memory_usage_bytes() const {
    size_t total = stats_.capacity() * sizeof(Stats) + buffer_.capacity() +
                   pending_candidates_.capacity() +
                   (regions_.capacity() + map_.capacity()) * sizeof(MemoryRegionInfo);
    for (const auto& label : labels_)
        total += label.candidates.capacity() + label.values.capacity() + label.name.capacity();
    return total;
}

size_t AssistedSearch::mark_count(std::string_view label) const {
    for (const auto& item : labels_)
        if (item.name == label)
            return item.count;
    return 0;
}

std::vector<AssistedSearchResult>
AssistedSearch::results(size_t limit, std::string_view label,
                        std::optional<AssistedSearchKind> kind) const {
    const Label* selected = nullptr;
    if (!label.empty()) {
        for (const auto& item : labels_)
            if (item.name == label && item.count)
                selected = &item;
        if (!selected)
            return {};
    }
    std::vector<AssistedSearchResult> result;
    if (!limit)
        return result;
    // Keep only the requested best results instead of allocating one object per address.
    limit = std::min<size_t>(limit, 10000);
    const auto order = [](const auto& a, const auto& b) {
        return a.score != b.score ? a.score > b.score : a.address < b.address;
    };
    for (size_t i = 0; i < stats_.size(); ++i) {
        const auto& s = stats_[i];
        if (!s.valid || !s.changes || (selected && !selected->candidates[i]))
            continue;
        // Rapidly changing bytes are usually animation, timers or other noise.
        if (!selected &&
            (frame_ < 4 || s.changes == UINT16_MAX || uint64_t{s.changes} * 4 > frame_))
            continue;
        AssistedSearchResult r;
        r.address = static_cast<uint32_t>(i);
        r.value = s.value;
        r.changes = s.changes;
        r.last_change_frame = s.last_change;
        r.distinct_count = s.distinct_count;
        std::copy(std::begin(s.distinct), std::end(s.distinct), r.distinct_values.begin());
        if (std::has_single_bit(s.changed_bits)) {
            r.kind = AssistedSearchKind::Flag;
            r.bit_index =
                static_cast<uint8_t>(std::countr_zero(static_cast<unsigned>(s.changed_bits)));
            r.score = 90;
        } else if (s.changes >= 3 && std::max(s.increases, s.decreases) * 4 >= s.changes * 3) {
            r.kind = AssistedSearchKind::Counter;
            r.score = 80;
        } else if (s.distinct_count <= 4) {
            r.kind = AssistedSearchKind::Area;
            r.score = 70;
        } else if (selected) {
            r.kind = AssistedSearchKind::Area;
            r.score = 40;
        } else {
            continue;
        }
        if (kind && r.kind != *kind)
            continue;
        if (selected)
            r.score += static_cast<unsigned>(std::min<size_t>(selected->count, 9));
        if (result.size() < limit) {
            result.push_back(r);
            std::push_heap(result.begin(), result.end(), order);
        } else if (order(r, result.front())) {
            std::pop_heap(result.begin(), result.end(), order);
            result.back() = r;
            std::push_heap(result.begin(), result.end(), order);
        }
    }
    std::sort(result.begin(), result.end(), order);
    return result;
}
} // namespace cw
