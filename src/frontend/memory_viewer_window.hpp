#pragma once

#include "cw/devtools/bookmarks.hpp"
#include "cw/devtools/code_notes.hpp"
#include "cw/devtools/memory.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cw {

class LibretroCore;

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

// The Code Notes window owns this state; the Memory Viewer edits the selected address's note.
struct CodeNotesUiState {
    bool open = false;
    std::string filter;
    std::string note_editor;
    std::optional<uint32_t> note_editor_address;
    std::string editor_message;
    bool editor_message_is_error = false;
    std::string ra_cache_path_input;
    std::filesystem::path ra_cache_dir;
    std::string cache_message;
    bool cache_message_is_error = false;
    bool notes_loaded = false;
    std::optional<uint32_t> loaded_game_id;
    std::filesystem::path loaded_data_dir;
    std::filesystem::path loaded_ra_cache_dir;
};

void draw_memory_viewer(LibretroCore& core, MemoryViewerState& viewer, Bookmarks& bookmarks,
                        CodeNotes& notes, CodeNotesUiState& notes_ui);
// Opens the viewer on an address, for windows that link to memory.
void jump_to_memory_address(MemoryViewerState& viewer, const MemoryReader& memory,
                            uint32_t address);

} // namespace cw
