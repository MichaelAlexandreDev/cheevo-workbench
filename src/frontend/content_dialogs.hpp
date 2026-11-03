#pragma once

#include "app_state.hpp"
#include "game_view.hpp"
#include "libretro_core.hpp"
#include "recent_files.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cw {

enum class DialogKind { OpenCore, OpenGame, SystemDirectory };

struct DialogResult {
    DialogKind kind;
    std::string path;
    std::string error;
};

struct DialogQueue;

struct DialogRequest {
    DialogQueue* queue = nullptr;
    DialogKind kind = DialogKind::OpenCore;
    std::string filter_name;
    std::string filter_pattern;
    std::string initial_location;
    std::array<SDL_DialogFileFilter, 2> filters{};
    std::atomic_bool finished = false;
};

struct DialogQueue {
    std::mutex mutex;
    std::vector<DialogResult> results;
    std::vector<std::unique_ptr<DialogRequest>> requests;
};

// SDL owns an asynchronous callback and may invoke it on another thread. The registry has
// process lifetime so a late callback cannot refer to a destroyed stack object during shutdown.
DialogQueue& dialog_queue();
void begin_open_dialog(UiState& ui, DialogQueue& queue, SDL_Window* window, DialogKind kind,
                       std::string filter_name, std::string filter_pattern,
                       const std::filesystem::path& initial_location);
std::filesystem::path default_core_directory(const std::filesystem::path& config);
std::filesystem::path default_game_directory(const UiState& ui, const LibretroCore& core,
                                             const RecentFiles& recent);
std::string game_filter_pattern(const std::string& valid_extensions);
void begin_core_dialog(UiState& ui, DialogQueue& queue, SDL_Window* window,
                       const std::filesystem::path& config);
void begin_game_dialog(UiState& ui, DialogQueue& queue, SDL_Window* window,
                       const LibretroCore& core, const RecentFiles& recent);
void persist_recent(UiState& ui, RecentFiles& recent, const std::filesystem::path& core_path,
                    const std::filesystem::path& game_path);
// Loads a complete session from paths. A rejected game leaves the frontend in an empty state.
std::string load_session(LibretroCore& core, const std::filesystem::path& core_path,
                         const std::filesystem::path& game_path,
                         const std::filesystem::path& system_override);
void open_core(UiState& ui, Notice& notice, LibretroCore& core, GameView& game_view,
               RecentFiles& recent, const std::filesystem::path& core_path);
void open_game(UiState& ui, Notice& notice, LibretroCore& core, GameView& game_view,
               RecentFiles& recent, const std::filesystem::path& game_path);
void open_recent(UiState& ui, Notice& notice, LibretroCore& core, GameView& game_view,
                 RecentFiles& recent, const RecentFile& entry);
void close_content(UiState& ui, Notice& notice, LibretroCore& core, GameView& game_view);
void handle_dialog_results(UiState& ui, DialogQueue& queue, Notice& notice, LibretroCore& core,
                           GameView& game_view, RecentFiles& recent);
void draw_system_directory(UiState& ui, Notice& notice, DialogQueue& dialogs, SDL_Window* window,
                           const std::filesystem::path& config, const std::filesystem::path& data);

} // namespace cw
