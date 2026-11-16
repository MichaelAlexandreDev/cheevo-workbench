#pragma once

#include "cw/devtools/bookmarks.hpp"
#include "cw/devtools/code_notes.hpp"
#include "cw/memory/memory.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cw {

class LibretroCore;
struct CodeNotesUiState;

struct MemoryViewerState {
    bool open = false;
    int selected_region = 0;
    uint32_t selected_address = 0;
    bool has_selection = false;
    std::optional<uint32_t> scroll_to_address;
    char address_input[16] = "000000";
    char value_input[16] = "";
    MemoryValueSize edit_size = MemoryValueSize::EightBit;
    ByteOrder edit_order = ByteOrder::LittleEndian;
    uint32_t edit_address = 0;
    bool edit_value_initialized = false;
    std::optional<uint32_t> pending_jump_address;
    std::string message;
    bool message_is_error = false;
    ByteChangeTracker changes; // highlights recent changes for ~1 s of emulated frames
    std::vector<MemoryRegionInfo> previous_regions;
    std::string previous_game_path;
};

void draw_memory_viewer(LibretroCore& core, MemoryViewerState& viewer, Bookmarks& bookmarks,
                        CodeNotes& notes, CodeNotesUiState& notes_ui);
// Opens the viewer on an address, for windows that link to memory.
void jump_to_memory_address(MemoryViewerState& viewer, const MemoryReader& memory,
                            uint32_t address);

} // namespace cw
