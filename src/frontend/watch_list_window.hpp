#pragma once

#include "cw/devtools/bookmarks.hpp"
#include "cw/devtools/code_notes.hpp"
#include "cw/devtools/memory.hpp"
#include "memory_viewer_window.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cw {

struct WatchListUiState {
    bool open = false;
    MemoryValueSize size = MemoryValueSize::EightBit;
    ByteOrder byte_order = ByteOrder::LittleEndian;
    char address_input[16] = "";
    char label_input[64] = "";
    std::string message;
    bool message_is_error = false;
    bool bookmarks_loaded = false;
    std::optional<uint32_t> loaded_game_id;
    std::filesystem::path loaded_data_dir;
    std::unordered_map<uint64_t, std::string> description_buffers;
    std::unordered_set<uint64_t> dirty_description_ids;
    bool confirm_export = false;
    std::filesystem::path pending_export_path;
    std::optional<uint32_t> pending_export_game_id;
};

void draw_watch_list(Bookmarks& bookmarks, WatchListUiState& state, MemoryViewerState& viewer,
                     const MemoryReader& memory, uint64_t frame, const CodeNotes& notes,
                     const std::filesystem::path& ra_cache_dir);
// Saves renames still being typed. Returns false, keeping the drafts, if one cannot be saved.
bool flush_pending_bookmark_descriptions(Bookmarks& bookmarks, WatchListUiState& state);

} // namespace cw
