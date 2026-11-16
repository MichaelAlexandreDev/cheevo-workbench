#pragma once

#include "cw/formats/bookmarks.hpp"
#include "cw/formats/code_notes.hpp"
#include "cw/memory/memory.hpp"
#include "cw/memory/memory_search.hpp"
#include "memory_viewer_window.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cw {

class LibretroCore;

struct MemorySearchState {
    bool open = false;
    int selected_region = 0; // zero is All; other values are one-based region indexes
    MemoryValueSize size = MemoryValueSize::EightBit;
    ByteOrder byte_order = ByteOrder::LittleEndian;
    bool aligned = false;
    bool all_bit_positions = false;
    MemorySearchComparison comparison = MemorySearchComparison::NotEqual;
    MemorySearchReference reference = MemorySearchReference::Previous;
    char constant_input[16] = "0";
    MemorySearch search;
    std::vector<MemoryRegionInfo> previous_regions;
    std::string previous_game_path;
    uint64_t previous_session_generation = 0;
    std::string message;
    bool message_is_error = false;
};

// `game_session_generation` changes when content is reloaded, which invalidates a running search.
void draw_memory_search(uint64_t game_session_generation, LibretroCore& core,
                        MemorySearchState& state, MemoryViewerState& viewer, Bookmarks& bookmarks,
                        const CodeNotes& notes);

} // namespace cw
