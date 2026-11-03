#pragma once

#include "session_state.hpp"
#include "ui_widgets.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace cw {

struct UiState {
    bool quit = false;
    bool show_imgui_demo = false;
    bool dialog_pending = false;
    SessionState session;
    std::filesystem::path last_core_path;
    std::filesystem::path system_dir;
    bool system_directory_open = false;
    std::string system_directory_input;
    std::string system_directory_error;
    std::string last_core_extensions;
    std::string status_message;
    bool status_is_error = false;
    uint64_t game_session_generation = 0;
    uint64_t memory_history_generation = 0; // reset/load breaks passive observation history
    // Identity of the loaded game: its RetroAchievements hash, computed when the game changes,
    // and the game ID the user entered for that hash (no server lookup, AGENTS.md rule 1).
    std::string identified_game_path;
    std::optional<uint64_t> identified_generation; // game_session_generation it was computed for
    std::string game_hash;
    std::string game_hash_error;
    std::optional<uint32_t> game_id;
    char game_id_input[16] = "";
};

void report_message(UiState& ui, Notice& notice, const std::string& text, bool ok);

} // namespace cw
