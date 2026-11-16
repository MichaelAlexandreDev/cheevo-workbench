#pragma once

#include "cw/devtools/code_notes.hpp"
#include "cw/memory/memory.hpp"
#include "memory_viewer_window.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace cw {

// The Memory Viewer also uses this state, to edit the note of the selected address.
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
    float below_note_height = 0.0f; // what the Memory Viewer draws below its note box
};

void draw_code_notes_window(CodeNotes& notes, CodeNotesUiState& state,
                            const std::filesystem::path& config_dir, MemoryViewerState& viewer,
                            const MemoryReader& memory);

} // namespace cw
