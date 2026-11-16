#pragma once

#include "cw/memory/assisted_search.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cw {

// Collect only after emulated frames; drawing never advances the observation history.
class AssistedSearchWindow {
  public:
    bool open = false;

    void sync(uint64_t session, uint64_t discontinuity, bool game_loaded);
    void observe(const MemoryReader& memory);
    void handle_hotkey(bool allow_keyboard);
    // Returns an address to open in the memory viewer.
    std::optional<uint32_t> draw(const MemoryReader& memory, bool game_loaded);

  private:
    void mark();
    AssistedSearch search_;
    std::vector<AssistedSearchResult> results_;
    uint64_t session_ = 0;
    uint64_t discontinuity_ = 0;
    bool synced_ = false;
    bool collecting_ = false;
    bool filter_marks_ = false;
    bool same_value_ = false;
    int kind_ = 0;
    char label_[65] = "moment";
    std::string message_;
    double next_refresh_ = 0;
    double last_observation_ms_ = 0;
};

} // namespace cw
