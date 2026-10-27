// cheevo-workbench entry point: creates the SDL3 window with an OpenGL context, loads the
// libretro core given on the command line and runs the Dear ImGui main loop.

#include "cw/devtools/code_notes.hpp"
#include "cw/devtools/game_hash.hpp"
#include "cw/devtools/memory_search.hpp"
#include "cw/devtools/watch_list.hpp"
#include "frame_pacer.hpp"
#include "game_ids.hpp"
#include "game_view.hpp"
#include "input_state.hpp"
#include "libretro_core.hpp"
#include "paths.hpp"
#include "recent_files.hpp"
#include "save_states.hpp"
#include "sdl_audio_output.hpp"
#include "session_state.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h> // DockBuilderGetCentralNode: where the game is drawn
#include <rc_consoles.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr const char* kUsage =
    "Usage: cheevo-workbench [--core <core.so>] [--system <bios dir>] [game]\n"
    "\n"
    "  --core <core.so>     libretro core to load, e.g. "
    "/usr/lib/libretro/mednafen_psx_libretro.so\n"
    "  --system <dir>       directory with BIOS files (default: "
    "~/.local/share/cheevo-workbench/system)\n"
    "  game                 content to load into the core (needs --core)\n";

struct LaunchOptions {
    std::filesystem::path core;
    std::filesystem::path system_dir;
    std::filesystem::path game;
};

// Returns nullopt when the program should exit (after --help or a usage error).
std::optional<LaunchOptions> parse_args(int argc, char** argv, int& exit_code) {
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            exit_code = 0;
            return std::nullopt;
        } else if (arg == "--core" && has_value) {
            options.core = argv[++i];
        } else if (arg == "--system" && has_value) {
            options.system_dir = argv[++i];
        } else if (!arg.starts_with('-') && options.game.empty()) {
            options.game = argv[i];
        } else {
            std::fprintf(stderr, "Invalid argument: %s\n\n%s", argv[i], kUsage);
            exit_code = 2;
            return std::nullopt;
        }
    }
    if (!options.game.empty() && options.core.empty()) {
        std::fprintf(stderr, "A game needs a core: pass --core <core.so>\n\n%s", kUsage);
        exit_code = 2;
        return std::nullopt;
    }
    return options;
}

cw::LibretroCore::Directories make_core_directories(const std::filesystem::path& system_override) {
    // Default system/save folders live in the data directory, which is empty when there is no
    // usable location; the core is then told "no directory" instead of a random one.
    const std::filesystem::path data = cw::data_dir();
    auto default_dir = [&data](const char* name) -> std::filesystem::path {
        if (data.empty()) {
            return {};
        }
        std::error_code ec;
        std::filesystem::create_directories(data / name, ec);
        return ec ? std::filesystem::path{} : data / name;
    };
    cw::LibretroCore::Directories dirs{.system = system_override.empty()
                                                     ? default_dir("system")
                                                     : std::filesystem::absolute(system_override),
                                       .save = default_dir("saves")};
    return dirs;
}

// Loads a complete session from paths. A rejected game leaves the frontend in an empty state.
std::string load_session(cw::LibretroCore& core, const std::filesystem::path& core_path,
                         const std::filesystem::path& game_path,
                         const std::filesystem::path& system_override) {
    core.unload();
    const cw::LibretroCore::Directories dirs = make_core_directories(system_override);
    SDL_Log("System (BIOS) directory: %s", dirs.system.empty() ? "(none)" : dirs.system.c_str());

    std::string error;
    if (!core.load(core_path, dirs, error)) {
        return "Could not load core " + core_path.string() + ": " + error;
    }
    if (!game_path.empty() && !core.load_game(game_path, error)) {
        core.unload();
        return "Could not load game " + game_path.string() + ": " + error;
    }
    return {};
}

// Loads the core and game from the command line. Returns an error message, empty on success.
std::string load_from_options(cw::LibretroCore& core, const LaunchOptions& options) {
    if (options.core.empty()) {
        return {};
    }
    // Resolved before the core is loaded: a core's retro_init() could change the working
    // directory, and relative paths would then point somewhere else.
    const std::filesystem::path game =
        options.game.empty() ? std::filesystem::path{} : std::filesystem::absolute(options.game);
    return load_session(core, std::filesystem::absolute(options.core), game, options.system_dir);
}

// Short message drawn over the game for a moment (e.g. "Saved slot 1"); also logged.
struct Notice {
    std::string text;
    std::chrono::steady_clock::time_point until{};
};

void show_notice(Notice& notice, const std::string& text, bool ok) {
    notice.text = text;
    notice.until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    if (ok) {
        SDL_Log("%s", text.c_str());
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str());
    }
}

void draw_notice(const Notice& notice, ImDrawList* draw_list, ImVec2 area_min, ImVec2 area_max) {
    if (notice.text.empty() || std::chrono::steady_clock::now() > notice.until) {
        return;
    }
    const ImVec2 pad(8.0f, 4.0f);
    const ImVec2 size = ImGui::CalcTextSize(notice.text.c_str());
    const ImVec2 pos(area_min.x + 12.0f, area_max.y - size.y - 2 * pad.y - 12.0f);
    draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x + 2 * pad.x, pos.y + size.y + 2 * pad.y),
                             IM_COL32(0, 0, 0, 180), 4.0f);
    draw_list->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), IM_COL32_WHITE, notice.text.c_str());
}

void draw_paused_indicator(ImDrawList* draw_list, ImVec2 area_min, ImVec2 area_max) {
    constexpr float kPaddingX = 14.0f;
    constexpr float kPaddingY = 8.0f;
    const ImVec2 text_size = ImGui::CalcTextSize("Paused");
    const ImVec2 size(text_size.x + 2.0f * kPaddingX, text_size.y + 2.0f * kPaddingY);
    const ImVec2 pos((area_min.x + area_max.x - size.x) * 0.5f,
                     (area_min.y + area_max.y - size.y) * 0.5f);
    draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, 190),
                             6.0f);
    draw_list->AddText(ImVec2(pos.x + kPaddingX, pos.y + kPaddingY), IM_COL32_WHITE, "Paused");
}

struct UiState {
    bool quit = false;
    bool show_imgui_demo = false;
    bool dialog_pending = false;
    cw::SessionState session;
    std::filesystem::path last_core_path;
    std::filesystem::path system_dir;
    std::string last_core_extensions;
    std::string status_message;
    bool status_is_error = false;
    uint64_t game_session_generation = 0;
    // Identity of the loaded game: its RetroAchievements hash, computed when the game changes,
    // and the game ID the user entered for that hash (no server lookup, AGENTS.md rule 1).
    std::string identified_game_path;
    std::optional<uint64_t> identified_generation; // game_session_generation it was computed for
    std::string game_hash;
    std::string game_hash_error;
    std::optional<uint32_t> game_id;
    char game_id_input[16] = "";
};

struct MemoryViewerState {
    bool open = false;
    int selected_region = 0;
    uint32_t selected_address = 0;
    bool has_selection = false;
    std::optional<uint32_t> scroll_to_address;
    char address_input[16] = "000000";
    char value_input[16] = "";
    cw::MemoryValueSize edit_size = cw::MemoryValueSize::EightBit;
    cw::ByteOrder edit_order = cw::ByteOrder::LittleEndian;
    uint32_t edit_address = 0;
    bool edit_value_initialized = false;
    std::optional<uint32_t> pending_jump_address;
    std::string message;
    bool message_is_error = false;
    cw::ByteChangeTracker changes; // highlights recent changes for ~1 s of emulated frames
    std::vector<cw::MemoryRegionInfo> previous_regions;
    std::string previous_game_path;
};

struct WatchListUiState {
    bool open = false;
    cw::MemoryValueSize size = cw::MemoryValueSize::EightBit;
    cw::ByteOrder byte_order = cw::ByteOrder::LittleEndian;
    char address_input[16] = "";
    char label_input[64] = "";
    std::string message;
    bool message_is_error = false;
};

struct MemorySearchState {
    bool open = false;
    int selected_region = 0; // zero is All; other values are one-based region indexes
    cw::MemoryValueSize size = cw::MemoryValueSize::EightBit;
    cw::ByteOrder byte_order = cw::ByteOrder::LittleEndian;
    bool aligned = false;
    bool all_bit_positions = false;
    cw::MemorySearchComparison comparison = cw::MemorySearchComparison::NotEqual;
    cw::MemorySearchReference reference = cw::MemorySearchReference::Previous;
    char constant_input[16] = "0";
    cw::MemorySearch search;
    std::vector<cw::MemoryRegionInfo> previous_regions;
    std::string previous_game_path;
    uint64_t previous_session_generation = 0;
    std::string message;
    bool message_is_error = false;
};

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

void report_message(UiState& ui, Notice& notice, const std::string& text, bool ok) {
    ui.status_message = text;
    ui.status_is_error = !ok;
    show_notice(notice, text, ok);
}

bool directory_exists(const std::filesystem::path& path);

int string_input_callback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* value = static_cast<std::string*>(data->UserData);
        value->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = value->data();
    }
    return 0;
}

bool input_text_string(const char* label, std::string& value, ImGuiInputTextFlags flags = 0) {
    value.reserve(value.size() + 1);
    return ImGui::InputText(label, value.data(), value.capacity() + 1,
                            flags | ImGuiInputTextFlags_CallbackResize, string_input_callback,
                            &value);
}

bool input_text_multiline_string(const char* label, std::string& value, const ImVec2& size,
                                 ImGuiInputTextFlags flags = 0) {
    value.reserve(value.size() + 1);
    return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, size,
                                     flags | ImGuiInputTextFlags_CallbackResize,
                                     string_input_callback, &value);
}

std::filesystem::path load_ra_cache_dir(const std::filesystem::path& config_dir) {
    if (config_dir.empty()) {
        return {};
    }
    std::ifstream input(config_dir / "ra-cache-path.txt");
    std::string path;
    if (!input || !std::getline(input, path)) {
        return {};
    }
    if (!path.empty() && path.back() == '\r') {
        path.pop_back();
    }
    return path.empty() ? std::filesystem::path{} : std::filesystem::path(path);
}

bool save_ra_cache_dir(const std::filesystem::path& config_dir, const std::string& path_text,
                       std::filesystem::path& saved_path, std::string& error) {
    if (config_dir.empty()) {
        error = "The configuration directory is unavailable; the RACache path cannot be saved.";
        return false;
    }
    const std::filesystem::path path(path_text);
    if (!path.empty() && !path.is_absolute()) {
        error = "Enter an absolute path to the RACache directory, or leave it empty.";
        return false;
    }
    if (!path.empty() && !directory_exists(path)) {
        error = "The selected RACache directory does not exist.";
        return false;
    }

    const std::filesystem::path file = config_dir / "ra-cache-path.txt";
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not write " + file.string();
        return false;
    }
    output << path_text << '\n';
    output.close();
    if (!output) {
        error = "Could not finish writing " + file.string();
        return false;
    }
    saved_path = path;
    error.clear();
    return true;
}

void sync_code_notes(const UiState& ui, const std::filesystem::path& data_dir, cw::CodeNotes& notes,
                     CodeNotesUiState& state) {
    if (state.notes_loaded && state.loaded_game_id == ui.game_id &&
        state.loaded_data_dir == data_dir && state.loaded_ra_cache_dir == state.ra_cache_dir) {
        return;
    }
    notes.load(ui.game_id, data_dir, state.ra_cache_dir);
    state.notes_loaded = true;
    state.loaded_game_id = ui.game_id;
    state.loaded_data_dir = data_dir;
    state.loaded_ra_cache_dir = state.ra_cache_dir;
    state.note_editor_address.reset();
    state.note_editor.clear();
    state.editor_message.clear();
}

std::string lowercase_ascii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

bool note_matches_filter(const cw::CodeNote& note, std::string_view source,
                         std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    char address[24];
    std::snprintf(address, sizeof(address), "0x%06X", note.address);
    const std::string needle = lowercase_ascii(filter);
    return lowercase_ascii(address).find(needle) != std::string::npos ||
           lowercase_ascii(note.note).find(needle) != std::string::npos ||
           lowercase_ascii(source).find(needle) != std::string::npos ||
           lowercase_ascii(note.author).find(needle) != std::string::npos;
}

void draw_note_preview(const cw::CodeNote* note) {
    if (!note) {
        ImGui::TextDisabled("--");
        return;
    }
    const size_t end = note->note.find('\n');
    ImGui::TextUnformatted(note->note.data(), end == std::string::npos
                                                  ? note->note.data() + note->note.size()
                                                  : note->note.data() + end);
}

void draw_code_note_load_errors(const cw::CodeNotes& notes) {
    const ImVec4 color(1.0f, 0.45f, 0.4f, 1.0f);
    if (!notes.user_error().empty()) {
        ImGui::TextColored(color, "%s", notes.user_error().c_str());
    }
    if (!notes.server_error().empty()) {
        ImGui::TextColored(color, "%s", notes.server_error().c_str());
    }
}

// Shift+F1..F4 saves slot 1..4, F1..F4 loads it. RouteGlobal keeps them active in tool windows.
void handle_state_hotkeys(cw::LibretroCore& core, cw::SaveStates& states, UiState& ui,
                          Notice& notice) {
    if (!core.is_game_loaded()) {
        return;
    }
    for (int slot = 1; slot <= cw::SaveStates::kSlotCount; ++slot) {
        const auto key = static_cast<ImGuiKey>(ImGuiKey_F1 + (slot - 1));
        std::string message;
        if (ImGui::Shortcut(ImGuiMod_Shift | key, ImGuiInputFlags_RouteGlobal)) {
            const bool ok = states.save(core, slot, message);
            report_message(ui, notice, message, ok);
        } else if (ImGui::Shortcut(key, ImGuiInputFlags_RouteGlobal)) {
            const bool ok = states.load(core, slot, message);
            report_message(ui, notice, message, ok);
        }
    }
}

enum class DialogKind { OpenCore, OpenGame };

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
    SDL_DialogFileFilter filter{};
    std::atomic_bool finished = false;
};

struct DialogQueue {
    std::mutex mutex;
    std::vector<DialogResult> results;
    std::vector<std::unique_ptr<DialogRequest>> requests;
};

// SDL owns an asynchronous callback and may invoke it on another thread. The registry has
// process lifetime so a late callback cannot refer to a destroyed stack object during shutdown.
DialogQueue& dialog_queue() {
    static DialogQueue* queue = new DialogQueue;
    return *queue;
}

void SDLCALL on_open_file_dialog(void* userdata, const char* const* filelist, int) {
    auto& request = *static_cast<DialogRequest*>(userdata);
    DialogResult result{request.kind, {}, {}};
    if (!filelist) {
        result.error = "SDL file dialog failed";
    } else if (filelist[0]) {
        result.path = filelist[0];
    }

    {
        std::lock_guard lock(request.queue->mutex);
        request.queue->results.push_back(std::move(result));
    }
    request.finished.store(true, std::memory_order_release);
}

std::vector<DialogResult> take_dialog_results(DialogQueue& queue) {
    std::lock_guard lock(queue.mutex);
    std::vector<DialogResult> results;
    results.swap(queue.results);
    return results;
}

void remove_finished_dialogs(DialogQueue& queue) {
    std::erase_if(queue.requests, [](const auto& request) {
        return request->finished.load(std::memory_order_acquire);
    });
}

void begin_open_dialog(UiState& ui, DialogQueue& queue, SDL_Window* window, DialogKind kind,
                       std::string filter_name, std::string filter_pattern,
                       const std::filesystem::path& initial_location) {
    if (ui.dialog_pending) {
        return;
    }
    auto request = std::make_unique<DialogRequest>();
    request->queue = &queue;
    request->kind = kind;
    request->filter_name = std::move(filter_name);
    request->filter_pattern = std::move(filter_pattern);
    request->initial_location = initial_location.string();
    request->filter = {request->filter_name.c_str(), request->filter_pattern.c_str()};
    DialogRequest* callback_data = request.get();
    queue.requests.push_back(std::move(request));
    ui.dialog_pending = true;
    SDL_ShowOpenFileDialog(
        on_open_file_dialog, callback_data, window, &callback_data->filter, 1,
        callback_data->initial_location.empty() ? nullptr : callback_data->initial_location.c_str(),
        false);
}

bool directory_exists(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_directory(path, error) && !error;
}

std::filesystem::path default_core_directory(const std::filesystem::path& config) {
    const std::filesystem::path system_cores("/usr/lib/libretro");
    if (directory_exists(system_cores)) {
        return system_cores;
    }
    if (!config.empty()) {
        const auto retroarch_cores = config.parent_path() / "retroarch" / "cores";
        if (directory_exists(retroarch_cores)) {
            return retroarch_cores;
        }
    } else if (const char* home = std::getenv("HOME"); home && home[0] == '/') {
        const auto retroarch_cores = std::filesystem::path(home) / ".config/retroarch/cores";
        if (directory_exists(retroarch_cores)) {
            return retroarch_cores;
        }
    }
    return {};
}

std::filesystem::path default_game_directory(const UiState& ui, const cw::LibretroCore& core,
                                             const cw::RecentFiles& recent) {
    if (core.is_game_loaded()) {
        const auto current = std::filesystem::path(core.game_path()).parent_path();
        if (directory_exists(current)) {
            return current;
        }
    }
    for (const cw::RecentFile& entry : recent.entries()) {
        if (!entry.game.empty() && (ui.last_core_path.empty() || entry.core == ui.last_core_path)) {
            const auto directory = entry.game.parent_path();
            if (directory_exists(directory)) {
                return directory;
            }
        }
    }
    return {};
}

std::string game_filter_pattern(const std::string& valid_extensions) {
    std::string pattern;
    size_t start = 0;
    while (start <= valid_extensions.size()) {
        const size_t end = valid_extensions.find('|', start);
        std::string extension = valid_extensions.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        while (!extension.empty() && std::isspace(static_cast<unsigned char>(extension.front()))) {
            extension.erase(extension.begin());
        }
        while (!extension.empty() && std::isspace(static_cast<unsigned char>(extension.back()))) {
            extension.pop_back();
        }
        if (!extension.empty() && extension.front() == '.') {
            extension.erase(extension.begin());
        }
        const bool allowed = !extension.empty() &&
                             std::all_of(extension.begin(), extension.end(), [](unsigned char c) {
                                 return std::isalnum(c) || c == '-' || c == '_' || c == '.';
                             });
        if (allowed) {
            if (!pattern.empty()) {
                pattern += ';';
            }
            pattern += extension;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return pattern.empty() ? "*" : pattern;
}

void begin_core_dialog(UiState& ui, DialogQueue& queue, SDL_Window* window,
                       const std::filesystem::path& config) {
    begin_open_dialog(ui, queue, window, DialogKind::OpenCore, "Libretro cores", "so;so.*",
                      default_core_directory(config));
}

void begin_game_dialog(UiState& ui, DialogQueue& queue, SDL_Window* window,
                       const cw::LibretroCore& core, const cw::RecentFiles& recent) {
    begin_open_dialog(ui, queue, window, DialogKind::OpenGame, "Game content",
                      game_filter_pattern(ui.last_core_extensions),
                      default_game_directory(ui, core, recent));
}

void persist_recent(UiState& ui, cw::RecentFiles& recent, const std::filesystem::path& core_path,
                    const std::filesystem::path& game_path) {
    recent.add(core_path, game_path);
    std::string error;
    if (!recent.save(error)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
        if (!ui.status_message.empty()) {
            ui.status_message += " (recent files were not saved: " + error + ")";
        }
    }
}

void open_core(UiState& ui, Notice& notice, cw::LibretroCore& core, cw::GameView& game_view,
               cw::RecentFiles& recent, const std::filesystem::path& core_path) {
    const std::string error = load_session(core, core_path, {}, ui.system_dir);
    ++ui.game_session_generation;
    game_view.clear();
    ui.session.set_paused(false);
    if (!error.empty()) {
        report_message(ui, notice, error, false);
        return;
    }
    ui.last_core_path = core.core_path();
    ui.last_core_extensions = core.system_info().valid_extensions;
    persist_recent(ui, recent, ui.last_core_path, {});
    report_message(ui, notice, "Loaded core " + ui.last_core_path.filename().string(), true);
}

void open_game(UiState& ui, Notice& notice, cw::LibretroCore& core, cw::GameView& game_view,
               cw::RecentFiles& recent, const std::filesystem::path& game_path) {
    const std::filesystem::path selected_core =
        core.is_loaded() ? std::filesystem::path(core.core_path()) : ui.last_core_path;
    if (selected_core.empty()) {
        report_message(ui, notice, "Open a core before opening game content", false);
        return;
    }
    const std::string error = load_session(core, selected_core, game_path, ui.system_dir);
    ++ui.game_session_generation;
    game_view.clear();
    ui.session.set_paused(false);
    if (!error.empty()) {
        report_message(ui, notice, error, false);
        return;
    }
    ui.last_core_path = core.core_path();
    ui.last_core_extensions = core.system_info().valid_extensions;
    persist_recent(ui, recent, ui.last_core_path, game_path);
    report_message(ui, notice,
                   "Loaded " + std::filesystem::path(core.game_path()).filename().string(), true);
}

void open_recent(UiState& ui, Notice& notice, cw::LibretroCore& core, cw::GameView& game_view,
                 cw::RecentFiles& recent, const cw::RecentFile& entry) {
    const cw::RecentFile selected = entry;
    const std::string error = load_session(core, selected.core, selected.game, ui.system_dir);
    ++ui.game_session_generation;
    game_view.clear();
    ui.session.set_paused(false);
    if (!error.empty()) {
        report_message(ui, notice, error, false);
        return;
    }
    ui.last_core_path = core.core_path();
    ui.last_core_extensions = core.system_info().valid_extensions;
    persist_recent(ui, recent, selected.core, selected.game);
    const std::string label = selected.game.empty() ? selected.core.filename().string()
                                                    : selected.game.filename().string();
    report_message(ui, notice, "Opened recent " + label, true);
}

void close_content(UiState& ui, Notice& notice, cw::LibretroCore& core, cw::GameView& game_view) {
    if (core.is_loaded()) {
        ui.last_core_path = core.core_path();
        ui.last_core_extensions = core.system_info().valid_extensions;
    }
    core.unload();
    ++ui.game_session_generation;
    game_view.clear();
    ui.session.set_paused(false);
    report_message(ui, notice, "Closed content; the core path is remembered", true);
}

void handle_dialog_results(UiState& ui, DialogQueue& queue, Notice& notice, cw::LibretroCore& core,
                           cw::GameView& game_view, cw::RecentFiles& recent) {
    for (DialogResult& result : take_dialog_results(queue)) {
        ui.dialog_pending = false;
        if (!result.error.empty()) {
            report_message(ui, notice, result.error, false);
        } else if (!result.path.empty()) {
            if (result.kind == DialogKind::OpenCore) {
                open_core(ui, notice, core, game_view, recent, result.path);
            } else {
                open_game(ui, notice, core, game_view, recent, result.path);
            }
        }
    }
    remove_finished_dialogs(queue);
}

struct OpenGamepad {
    SDL_Gamepad* handle = nullptr;
    std::string name;
};

using Gamepads = std::unordered_map<SDL_JoystickID, OpenGamepad>;

void open_gamepad(SDL_JoystickID id, Gamepads& gamepads) {
    if (gamepads.contains(id) || !SDL_IsGamepad(id)) {
        return;
    }
    SDL_Gamepad* handle = SDL_OpenGamepad(id);
    if (!handle) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Could not open gamepad %d: %s", id, SDL_GetError());
        return;
    }
    const char* name = SDL_GetGamepadName(handle);
    gamepads.emplace(id, OpenGamepad{handle, name ? name : "Unknown gamepad"});
    SDL_Log("Opened gamepad %s (id %d)", name ? name : "Unknown gamepad", id);
}

void close_gamepad(SDL_JoystickID id, Gamepads& gamepads, cw::JoypadState& input) {
    input.remove_source(cw::gamepad_input_source(id));
    const auto found = gamepads.find(id);
    if (found == gamepads.end()) {
        return;
    }
    SDL_CloseGamepad(found->second.handle);
    SDL_Log("Closed gamepad %s (id %d)", found->second.name.c_str(), id);
    gamepads.erase(found);
}

void open_connected_gamepads(Gamepads& gamepads) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids && count > 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Could not enumerate gamepads: %s", SDL_GetError());
        return;
    }
    for (int i = 0; i < count; ++i) {
        open_gamepad(ids[i], gamepads);
    }
    SDL_free(ids);
}

void update_input(cw::JoypadState& input, const Gamepads& gamepads, bool window_focused,
                  bool imgui_wants_keyboard) {
    if (!window_focused) {
        input.release_all();
        return;
    }

    if (cw::keyboard_input_allowed(window_focused, imgui_wants_keyboard)) {
        int key_count = 0;
        const bool* keys = SDL_GetKeyboardState(&key_count);
        uint16_t keyboard_mask = 0;
        if (keys && key_count > 0) {
            const bool control_pressed = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
            keyboard_mask = cw::map_keyboard_state(
                std::span<const bool>(keys, static_cast<size_t>(key_count)), control_pressed);
        }
        input.set_source(cw::kKeyboardInputSource, keyboard_mask);
    } else {
        input.remove_source(cw::kKeyboardInputSource);
    }

    for (const auto& [id, gamepad] : gamepads) {
        cw::GamepadSnapshot snapshot;
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
            snapshot.buttons[static_cast<size_t>(button)] =
                SDL_GetGamepadButton(gamepad.handle, static_cast<SDL_GamepadButton>(button));
        }
        snapshot.left_trigger = SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        snapshot.right_trigger = SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        input.set_source(cw::gamepad_input_source(id), cw::map_gamepad_state(snapshot));
    }
}

std::string pressed_button_names(uint16_t mask) {
    std::string names;
    for (unsigned id = 0; id < 16; ++id) {
        if ((mask & static_cast<uint16_t>(1U << id)) == 0) {
            continue;
        }
        if (!names.empty()) {
            names += ", ";
        }
        names += cw::joypad_button_name(id);
    }
    return names.empty() ? "none" : names;
}

std::string gamepad_names(const Gamepads& gamepads) {
    std::string names;
    for (const auto& [id, gamepad] : gamepads) {
        (void)id;
        if (!names.empty()) {
            names += ", ";
        }
        names += gamepad.name;
    }
    return names.empty() ? "none" : names;
}

void handle_emulation_hotkeys(UiState& ui, Notice& notice, cw::LibretroCore& core) {
    if (!core.is_game_loaded()) {
        return;
    }
    if (ImGui::Shortcut(ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
        ui.session.toggle_pause();
        report_message(ui, notice, ui.session.paused() ? "Paused" : "Resumed", true);
    }
    if (ui.session.paused() && ImGui::Shortcut(ImGuiKey_N, ImGuiInputFlags_RouteGlobal)) {
        ui.session.request_frame_advance();
        report_message(ui, notice, "Advanced one frame", true);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, ImGuiInputFlags_RouteGlobal)) {
        core.reset();
        report_message(ui, notice, "Reset emulation", true);
    }
}

std::string recent_label(const cw::RecentFile& entry) {
    const std::string game = entry.game.empty() ? "Core only" : entry.game.filename().string();
    return game + " — " + entry.core.filename().string();
}

// Console and memory regions as RetroAchievements sees them (the memory viewer comes in 2.2).
void draw_memory_summary(const cw::LibretroCore& core) {
    const cw::MemoryReader& memory = core.memory();
    if (memory.size() == 0) {
        ImGui::TextDisabled("Memory: not mapped");
        return;
    }
    ImGui::Text("Memory: %s, %u bytes", rc_console_name(core.console_id()), memory.size());
    for (const cw::MemoryRegionInfo& region : memory.regions()) {
        ImGui::BulletText("0x%06X-0x%06X %s%s%s", region.start, region.end,
                          region.description.c_str(), region.available ? "" : " (unavailable)",
                          region.available && !region.writable ? " (read-only)" : "");
    }
}

constexpr std::array<std::pair<cw::MemoryValueSize, const char*>, 14> kMemoryValueSizes = {{
    {cw::MemoryValueSize::Bit0, "Bit 0"},
    {cw::MemoryValueSize::Bit1, "Bit 1"},
    {cw::MemoryValueSize::Bit2, "Bit 2"},
    {cw::MemoryValueSize::Bit3, "Bit 3"},
    {cw::MemoryValueSize::Bit4, "Bit 4"},
    {cw::MemoryValueSize::Bit5, "Bit 5"},
    {cw::MemoryValueSize::Bit6, "Bit 6"},
    {cw::MemoryValueSize::Bit7, "Bit 7"},
    {cw::MemoryValueSize::LowNibble, "Low nibble"},
    {cw::MemoryValueSize::HighNibble, "High nibble"},
    {cw::MemoryValueSize::EightBit, "8-bit"},
    {cw::MemoryValueSize::SixteenBit, "16-bit"},
    {cw::MemoryValueSize::TwentyFourBit, "24-bit"},
    {cw::MemoryValueSize::ThirtyTwoBit, "32-bit"},
}};

constexpr bool has_byte_order(cw::MemoryValueSize size) {
    return size == cw::MemoryValueSize::SixteenBit || size == cw::MemoryValueSize::TwentyFourBit ||
           size == cw::MemoryValueSize::ThirtyTwoBit;
}

const char* memory_value_size_label(cw::MemoryValueSize size) {
    const auto found = std::find_if(kMemoryValueSizes.begin(), kMemoryValueSizes.end(),
                                    [size](const auto& entry) { return entry.first == size; });
    return found == kMemoryValueSizes.end() ? "Unknown" : found->second;
}

bool parse_hex_value(std::string_view text, uint32_t& value) {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return error == std::errc{} && end == text.data() + text.size();
}

void set_memory_address_input(MemoryViewerState& viewer, uint32_t address) {
    std::snprintf(viewer.address_input, sizeof(viewer.address_input), "%06X", address);
}

void set_memory_edit_value(MemoryViewerState& viewer, const cw::MemoryReader& memory) {
    if (!viewer.has_selection) {
        viewer.value_input[0] = '\0';
        viewer.edit_value_initialized = false;
        return;
    }

    const auto value = cw::decode_memory_value(memory, viewer.selected_address, viewer.edit_size,
                                               viewer.edit_order);
    if (!value) {
        viewer.value_input[0] = '\0';
    } else {
        std::snprintf(viewer.value_input, sizeof(viewer.value_input), "%X", *value);
    }
    viewer.edit_address = viewer.selected_address;
    viewer.edit_value_initialized = true;
}

void draw_memory_values(const cw::MemoryReader& memory, uint32_t address) {
    const auto show_value = [&memory, address](const char* label, cw::MemoryValueSize size,
                                               cw::ByteOrder order) {
        const auto value = cw::decode_memory_value(memory, address, size, order);
        if (value) {
            ImGui::Text("%-16s 0x%X", label, *value);
        } else {
            ImGui::TextDisabled("%-16s --", label);
        }
    };

    ImGui::Text("All interpretations");
    for (const auto& [size, label] : kMemoryValueSizes) {
        if (size == cw::MemoryValueSize::SixteenBit || size == cw::MemoryValueSize::TwentyFourBit ||
            size == cw::MemoryValueSize::ThirtyTwoBit) {
            char little_label[32];
            char big_label[32];
            std::snprintf(little_label, sizeof(little_label), "%s LE", label);
            std::snprintf(big_label, sizeof(big_label), "%s BE", label);
            show_value(little_label, size, cw::ByteOrder::LittleEndian);
            show_value(big_label, size, cw::ByteOrder::BigEndian);
        } else {
            show_value(label, size, cw::ByteOrder::LittleEndian);
        }
    }
}

void draw_memory_value_editor(cw::LibretroCore& core, MemoryViewerState& viewer,
                              cw::WatchList& watch_list) {
    const cw::MemoryReader& memory = core.memory();
    if (!viewer.has_selection) {
        ImGui::TextDisabled("Select a byte in the memory grid.");
        return;
    }

    if (!viewer.edit_value_initialized || viewer.edit_address != viewer.selected_address) {
        set_memory_edit_value(viewer, memory);
    }

    ImGui::Text("Selected address: 0x%06X", viewer.selected_address);
    ImGui::SameLine();
    if (ImGui::Button("Watch")) {
        if (watch_list.add(viewer.selected_address, viewer.edit_size, viewer.edit_order)) {
            viewer.message = "Address added to Watch List.";
            viewer.message_is_error = false;
        } else {
            viewer.message = "This address and interpretation are already watched.";
            viewer.message_is_error = false;
        }
    }
    ImGui::Separator();
    draw_memory_values(memory, viewer.selected_address);
    ImGui::Separator();

    ImGui::Text("Edit value (hex)");
    if (ImGui::BeginCombo("Size",
                          kMemoryValueSizes[static_cast<size_t>(viewer.edit_size)].second)) {
        for (const auto& [size, label] : kMemoryValueSizes) {
            const bool selected = viewer.edit_size == size;
            if (ImGui::Selectable(label, selected)) {
                viewer.edit_size = size;
                if (!has_byte_order(size)) {
                    viewer.edit_order = cw::ByteOrder::LittleEndian;
                }
                viewer.edit_value_initialized = false;
                set_memory_edit_value(viewer, memory);
                viewer.message.clear();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (has_byte_order(viewer.edit_size)) {
        const char* order_label =
            viewer.edit_order == cw::ByteOrder::LittleEndian ? "Little-endian" : "Big-endian";
        if (ImGui::BeginCombo("Byte order", order_label)) {
            for (const auto& [order, label] :
                 {std::pair{cw::ByteOrder::LittleEndian, "Little-endian"},
                  std::pair{cw::ByteOrder::BigEndian, "Big-endian"}}) {
                const bool selected = viewer.edit_order == order;
                if (ImGui::Selectable(label, selected)) {
                    viewer.edit_order = order;
                    viewer.edit_value_initialized = false;
                    set_memory_edit_value(viewer, memory);
                    viewer.message.clear();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SetNextItemWidth(-1.0f);
    const bool submitted = ImGui::InputText(
        "##MemoryEditValue", viewer.value_input, sizeof(viewer.value_input),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Write value") || submitted) {
        uint32_t value = 0;
        if (!parse_hex_value(viewer.value_input, value)) {
            viewer.message = "Enter a hexadecimal value.";
            viewer.message_is_error = true;
        } else if (value > cw::memory_value_max(viewer.edit_size)) {
            char maximum[48];
            std::snprintf(maximum, sizeof(maximum), "Value exceeds the selected size (max 0x%X).",
                          cw::memory_value_max(viewer.edit_size));
            viewer.message = maximum;
            viewer.message_is_error = true;
        } else {
            const unsigned count = cw::memory_value_byte_count(viewer.edit_size);
            std::array<uint8_t, 4> bytes{};
            if (memory.read(viewer.selected_address, std::span(bytes.data(), count)) != count) {
                viewer.message = "Selected value is not fully available.";
                viewer.message_is_error = true;
            } else {
                const auto encode = cw::encode_memory_value(
                    value, viewer.edit_size, viewer.edit_order, std::span(bytes.data(), count));
                if (encode == cw::MemoryValueEncodeError::TooLarge) {
                    viewer.message = "Value exceeds the selected size.";
                    viewer.message_is_error = true;
                } else if (encode != cw::MemoryValueEncodeError::None) {
                    viewer.message = "Could not encode the selected value.";
                    viewer.message_is_error = true;
                } else {
                    const cw::MemoryWriteError result = core.memory_writer().write(
                        viewer.selected_address, std::span(bytes.data(), count));
                    viewer.message = cw::memory_write_error_message(result);
                    viewer.message_is_error = result != cw::MemoryWriteError::None;
                    if (result == cw::MemoryWriteError::None) {
                        viewer.edit_value_initialized = false;
                    }
                }
            }
        }
    }

    if (!viewer.message.empty()) {
        const ImVec4 color = viewer.message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                     : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", viewer.message.c_str());
    }
}

void draw_memory_code_note_editor(MemoryViewerState& viewer, CodeNotesUiState& state,
                                  cw::CodeNotes& notes) {
    const uint32_t address = viewer.selected_address;
    if (state.note_editor_address != address) {
        state.note_editor_address = address;
        const cw::CodeNote* note = notes.user_note_at(address);
        state.note_editor = note ? note->note : "";
        state.editor_message.clear();
        state.editor_message_is_error = false;
    }

    ImGui::SeparatorText("Code Note");
    if (!notes.game_id()) {
        ImGui::TextDisabled("Defina o game ID na janela Status.");
        return;
    }
    if (!notes.user_error().empty()) {
        ImGui::TextWrapped("%s", notes.user_error().c_str());
    }
    ImGui::Text("Address 0x%06X", address);
    input_text_multiline_string("My note", state.note_editor, ImVec2(-FLT_MIN, 100.0f));
    if (ImGui::Button("Save note")) {
        std::string error;
        if (notes.set_note(address, state.note_editor, error)) {
            state.editor_message = "Saved local note.";
            state.editor_message_is_error = false;
        } else {
            state.editor_message = std::move(error);
            state.editor_message_is_error = true;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(notes.user_note_at(address) == nullptr);
    if (ImGui::Button("Delete note")) {
        std::string error;
        if (notes.remove_note(address, error)) {
            state.note_editor.clear();
            state.editor_message = "Deleted local note.";
            state.editor_message_is_error = false;
        } else {
            state.editor_message = std::move(error);
            state.editor_message_is_error = true;
        }
    }
    ImGui::EndDisabled();

    if (const cw::CodeNote* server_note = notes.server_note_at(address)) {
        ImGui::SeparatorText("RAIntegration server note (read-only)");
        if (!server_note->author.empty()) {
            ImGui::Text("By %s", server_note->author.c_str());
        }
        ImGui::TextWrapped("%s", server_note->note.c_str());
    }
    if (!notes.server_error().empty()) {
        ImGui::TextWrapped("%s", notes.server_error().c_str());
    }
    if (!state.editor_message.empty()) {
        const ImVec4 color = state.editor_message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                           : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", state.editor_message.c_str());
    }
}

void draw_memory_viewer(cw::LibretroCore& core, MemoryViewerState& viewer,
                        cw::WatchList& watch_list, cw::CodeNotes& notes,
                        CodeNotesUiState& notes_ui) {
    if (!viewer.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(1050.0f, 650.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Viewer", &viewer.open)) {
        ImGui::End();
        return;
    }

    const cw::MemoryReader& memory = core.memory();
    const std::vector<cw::MemoryRegionInfo> regions = memory.regions();
    if (viewer.previous_regions != regions || viewer.previous_game_path != core.game_path()) {
        viewer.changes.clear();
        viewer.previous_regions = regions;
        viewer.previous_game_path = core.game_path();
    }
    if (viewer.selected_region < 0 ||
        static_cast<size_t>(viewer.selected_region) >= regions.size()) {
        viewer.selected_region = 0;
    }

    if (!core.is_game_loaded() || regions.empty()) {
        ImGui::TextDisabled("Load a game with an exposed memory map to use the viewer.");
        viewer.changes.clear();
        ImGui::End();
        return;
    }

    if (viewer.pending_jump_address) {
        const uint32_t address = *viewer.pending_jump_address;
        const auto found = std::find_if(regions.begin(), regions.end(), [address](const auto& r) {
            return r.start <= address && address <= r.end;
        });
        if (found != regions.end()) {
            viewer.selected_region = static_cast<int>(std::distance(regions.begin(), found));
            viewer.selected_address = address;
            set_memory_address_input(viewer, address);
            viewer.has_selection = true;
            viewer.edit_value_initialized = false;
            viewer.scroll_to_address = address;
            viewer.pending_jump_address.reset();
        }
    }

    const auto region_label = [&regions](size_t index) {
        const cw::MemoryRegionInfo& region = regions[index];
        std::string label = region.description.empty() ? "Memory" : region.description;
        char range[64];
        std::snprintf(range, sizeof(range), "  0x%06X-0x%06X", region.start, region.end);
        label += range;
        if (!region.available) {
            label += " (unavailable)";
        } else if (!region.writable) {
            label += " (read-only)";
        }
        return label;
    };

    const std::string selected_label = region_label(static_cast<size_t>(viewer.selected_region));
    if (ImGui::BeginCombo("Region", selected_label.c_str())) {
        for (size_t i = 0; i < regions.size(); ++i) {
            const std::string label = region_label(i);
            const bool selected = static_cast<int>(i) == viewer.selected_region;
            if (ImGui::Selectable(label.c_str(), selected)) {
                viewer.selected_region = static_cast<int>(i);
                viewer.selected_address = regions[i].start;
                set_memory_address_input(viewer, viewer.selected_address);
                viewer.has_selection = true;
                viewer.edit_value_initialized = false;
                viewer.scroll_to_address = regions[i].start;
                viewer.changes.clear();
                viewer.message.clear();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    const bool go_submitted = ImGui::InputText(
        "Address (hex)", viewer.address_input, sizeof(viewer.address_input),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Go") || go_submitted) {
        uint32_t address = 0;
        if (!parse_hex_value(viewer.address_input, address)) {
            viewer.message = "Enter a valid hexadecimal address.";
            viewer.message_is_error = true;
        } else if (address >= memory.size()) {
            viewer.message = "Address is outside the memory space.";
            viewer.message_is_error = true;
        } else {
            const auto found =
                std::find_if(regions.begin(), regions.end(), [address](const auto& r) {
                    return r.start <= address && address <= r.end;
                });
            if (found == regions.end()) {
                viewer.message = "Address is not inside a mapped region.";
                viewer.message_is_error = true;
            } else {
                viewer.selected_region = static_cast<int>(std::distance(regions.begin(), found));
                viewer.selected_address = address;
                set_memory_address_input(viewer, address);
                viewer.has_selection = true;
                viewer.edit_value_initialized = false;
                viewer.scroll_to_address = address;
                viewer.changes.clear();
                viewer.message.clear();
            }
        }
    }

    const cw::MemoryRegionInfo& region = regions[static_cast<size_t>(viewer.selected_region)];
    if (!viewer.has_selection && region.start < memory.size()) {
        viewer.selected_address = region.start;
        set_memory_address_input(viewer, region.start);
        viewer.has_selection = true;
    }
    ImGui::Text("0x%06X-0x%06X  %s  %s", region.start, region.end,
                region.available ? "available" : "unavailable",
                !region.available ? "not writable" : (region.writable ? "writable" : "read-only"));
    if (!viewer.message.empty()) {
        const ImVec4 color = viewer.message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                     : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", viewer.message.c_str());
    }
    ImGui::Separator();

    const uint64_t visible_end =
        memory.size() == 0 ? 0 : std::min<uint64_t>(region.end, memory.size() - 1U);
    const bool has_rows = memory.size() > 0 && region.start <= visible_end;
    const uint64_t byte_count = has_rows ? visible_end - region.start + 1U : 0;
    const uint64_t row_count = (byte_count + 15U) / 16U;
    const float line_height = ImGui::GetFrameHeight();
    const float grid_width = std::max(350.0f, ImGui::GetContentRegionAvail().x * 0.62f);
    if (ImGui::BeginChild("MemoryGrid", ImVec2(grid_width, 0.0f), ImGuiChildFlags_Borders)) {
        if (!has_rows) {
            ImGui::TextDisabled("This region has no addresses in the current memory map.");
        } else {
            if (viewer.scroll_to_address) {
                const uint64_t offset = *viewer.scroll_to_address - region.start;
                ImGui::SetScrollY(static_cast<float>((offset / 16U) * line_height));
                viewer.scroll_to_address.reset();
            }

            std::vector<cw::MemoryByteSnapshot> current_visible;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(row_count), line_height);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const uint64_t row_address64 = region.start + static_cast<uint64_t>(row) * 16U;
                    const uint32_t row_address = static_cast<uint32_t>(row_address64);
                    const size_t row_bytes = static_cast<size_t>(
                        std::min<uint64_t>(16U, visible_end - row_address64 + 1U));
                    std::array<uint8_t, 16> bytes{};
                    const size_t read_count =
                        memory.read(row_address, std::span(bytes.data(), row_bytes));
                    for (size_t i = 0; i < read_count; ++i) {
                        current_visible.push_back(
                            {.address = row_address + static_cast<uint32_t>(i), .value = bytes[i]});
                    }

                    ImGui::PushID(row);
                    ImGui::Text("0x%06X", row_address);
                    for (size_t i = 0; i < row_bytes; ++i) {
                        ImGui::SameLine(0.0f, i == 0 ? 10.0f : (i % 4 == 0 ? 9.0f : 4.0f));
                        if (i >= read_count) {
                            ImGui::TextDisabled("--");
                            continue;
                        }

                        const uint32_t address = row_address + static_cast<uint32_t>(i);
                        // Brightest right after the change, fading over the highlight window.
                        const std::optional<uint64_t> age =
                            viewer.changes.frames_since_change(address, core.frame_count());
                        const bool is_changed = age.has_value();
                        const bool selected =
                            viewer.has_selection && viewer.selected_address == address;
                        char byte_label[4];
                        std::snprintf(byte_label, sizeof(byte_label), "%02X", bytes[i]);
                        if (is_changed) {
                            const float fade =
                                1.0f - static_cast<float>(*age) /
                                           static_cast<float>(viewer.changes.window());
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                                  ImVec4(1.0f, 0.75f, 0.25f, 0.35f + 0.65f * fade));
                        }
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(byte_label, selected, 0, ImVec2(24.0f, 0.0f))) {
                            viewer.selected_address = address;
                            set_memory_address_input(viewer, address);
                            viewer.has_selection = true;
                            viewer.edit_value_initialized = false;
                            viewer.message.clear();
                        }
                        ImGui::PopID();
                        if (is_changed) {
                            ImGui::PopStyleColor();
                        }
                    }
                    ImGui::PopID();
                }
            }
            viewer.changes.observe(current_visible, core.frame_count());
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("SelectedValue", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        draw_memory_value_editor(core, viewer, watch_list);
        draw_memory_code_note_editor(viewer, notes_ui, notes);
    }
    ImGui::EndChild();
    ImGui::End();
}

void jump_to_memory_address(MemoryViewerState& viewer, const cw::MemoryReader& memory,
                            uint32_t address) {
    viewer.open = true;
    viewer.pending_jump_address = address;
    set_memory_address_input(viewer, address);

    const std::vector<cw::MemoryRegionInfo> regions = memory.regions();
    const auto found = std::find_if(regions.begin(), regions.end(), [address](const auto& region) {
        return region.start <= address && address <= region.end;
    });
    if (found == regions.end()) {
        return;
    }

    viewer.selected_region = static_cast<int>(std::distance(regions.begin(), found));
    viewer.selected_address = address;
    set_memory_address_input(viewer, address);
    viewer.has_selection = true;
    viewer.edit_value_initialized = false;
    viewer.scroll_to_address = address;
    viewer.pending_jump_address.reset();
    viewer.message.clear();
}

void draw_memory_search(const UiState& ui, cw::LibretroCore& core, MemorySearchState& state,
                        MemoryViewerState& viewer, cw::WatchList& watch_list,
                        const cw::CodeNotes& notes) {
    if (!state.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Search", &state.open)) {
        ImGui::End();
        return;
    }

    const cw::MemoryReader& memory = core.memory();
    const std::vector<cw::MemoryRegionInfo> regions = memory.regions();
    if (state.previous_regions != regions || state.previous_game_path != core.game_path() ||
        state.previous_session_generation != ui.game_session_generation) {
        state.search.invalidate();
        state.selected_region = 0;
        state.previous_regions = regions;
        state.previous_game_path = core.game_path();
        state.previous_session_generation = ui.game_session_generation;
    }

    if (!core.is_game_loaded() || memory.size() == 0 || regions.empty()) {
        state.search.invalidate();
        ImGui::TextDisabled("Load a game with readable memory to start a search.");
        ImGui::End();
        return;
    }
    if (!notes.game_id()) {
        ImGui::TextDisabled("Defina o game ID na janela Status.");
    }
    draw_code_note_load_errors(notes);

    if (state.selected_region < 0 || static_cast<size_t>(state.selected_region) > regions.size()) {
        state.selected_region = 0;
    }

    std::string selected_range = "All";
    if (state.selected_region > 0) {
        const cw::MemoryRegionInfo& region =
            regions[static_cast<size_t>(state.selected_region - 1)];
        selected_range = region.description.empty() ? "Memory" : region.description;
        char range[64];
        std::snprintf(range, sizeof(range), "  0x%06X-0x%06X", region.start, region.end);
        selected_range += range;
        if (!region.available) {
            selected_range += " (not fully available)";
        }
    }
    if (ImGui::BeginCombo("Range", selected_range.c_str())) {
        const bool all_selected = state.selected_region == 0;
        if (ImGui::Selectable("All", all_selected)) {
            state.selected_region = 0;
        }
        if (all_selected) {
            ImGui::SetItemDefaultFocus();
        }
        for (size_t i = 0; i < regions.size(); ++i) {
            const cw::MemoryRegionInfo& region = regions[i];
            std::string label = region.description.empty() ? "Memory" : region.description;
            char range[64];
            std::snprintf(range, sizeof(range), "  0x%06X-0x%06X", region.start, region.end);
            label += range;
            if (!region.available) {
                label += " (not fully available)";
            }
            const bool selected = state.selected_region == static_cast<int>(i + 1);
            if (ImGui::Selectable(label.c_str(), selected)) {
                state.selected_region = static_cast<int>(i + 1);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const char* selected_size = state.all_bit_positions
                                    ? "All bits (0–7)"
                                    : kMemoryValueSizes[static_cast<size_t>(state.size)].second;
    ImGui::SameLine();
    if (ImGui::BeginCombo("Size", selected_size)) {
        const bool all_bits_selected = state.all_bit_positions;
        if (ImGui::Selectable("All bits (0–7)", all_bits_selected)) {
            state.all_bit_positions = true;
        }
        if (all_bits_selected) {
            ImGui::SetItemDefaultFocus();
        }
        for (const auto& [size, label] : kMemoryValueSizes) {
            const bool selected = !state.all_bit_positions && size == state.size;
            if (ImGui::Selectable(label, selected)) {
                state.size = size;
                state.all_bit_positions = false;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    const char* order_label =
        state.byte_order == cw::ByteOrder::LittleEndian ? "Little-endian" : "Big-endian";
    if (ImGui::BeginCombo("Byte order", order_label)) {
        for (const auto& [order, label] : {std::pair{cw::ByteOrder::LittleEndian, "Little-endian"},
                                           std::pair{cw::ByteOrder::BigEndian, "Big-endian"}}) {
            const bool selected = state.byte_order == order;
            if (ImGui::Selectable(label, selected)) {
                state.byte_order = order;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const bool supports_alignment =
        !state.all_bit_positions && (state.size == cw::MemoryValueSize::SixteenBit ||
                                     state.size == cw::MemoryValueSize::ThirtyTwoBit);
    if (!supports_alignment) {
        state.aligned = false;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!supports_alignment);
    ImGui::Checkbox("Aligned", &state.aligned);
    ImGui::EndDisabled();

    cw::MemorySearchConfig config{
        .size = state.size,
        .byte_order = state.byte_order,
        .range_start = state.selected_region == 0
                           ? 0
                           : regions[static_cast<size_t>(state.selected_region - 1)].start,
        .range_end = state.selected_region == 0
                         ? memory.size() - 1U
                         : std::min(regions[static_cast<size_t>(state.selected_region - 1)].end,
                                    memory.size() - 1U),
        .aligned = state.aligned,
        .all_bit_positions = state.all_bit_positions};
    state.search.configure(config);

    constexpr std::array<std::pair<cw::MemorySearchComparison, const char*>, 6> kSearchComparisons =
        {{{cw::MemorySearchComparison::Equal, "="},
          {cw::MemorySearchComparison::NotEqual, "!="},
          {cw::MemorySearchComparison::Less, "<"},
          {cw::MemorySearchComparison::Greater, ">"},
          {cw::MemorySearchComparison::LessOrEqual, "<="},
          {cw::MemorySearchComparison::GreaterOrEqual, ">="}}};
    constexpr std::array<std::pair<cw::MemorySearchReference, const char*>, 3> kSearchReferences = {
        {{cw::MemorySearchReference::Previous, "Previous snapshot"},
         {cw::MemorySearchReference::Initial, "First snapshot"},
         {cw::MemorySearchReference::Constant, "Value"}}};

    if (ImGui::Button("New Search")) {
        if (state.search.start(memory)) {
            state.message.clear();
            state.message_is_error = false;
        } else {
            state.message = "Could not snapshot this memory range.";
            state.message_is_error = true;
        }
    }
    ImGui::SameLine();
    const char* comparison_label = "!=";
    for (const auto& [comparison, label] : kSearchComparisons) {
        if (comparison == state.comparison) {
            comparison_label = label;
            break;
        }
    }
    if (ImGui::BeginCombo("Compare", comparison_label)) {
        for (const auto& [comparison, label] : kSearchComparisons) {
            const bool selected = state.comparison == comparison;
            if (ImGui::Selectable(label, selected)) {
                state.comparison = comparison;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const char* reference_label = "Previous snapshot";
    for (const auto& [reference, label] : kSearchReferences) {
        if (reference == state.reference) {
            reference_label = label;
            break;
        }
    }
    if (ImGui::BeginCombo("Compare with", reference_label)) {
        for (const auto& [reference, label] : kSearchReferences) {
            const bool selected = state.reference == reference;
            if (ImGui::Selectable(label, selected)) {
                state.reference = reference;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (state.reference == cw::MemorySearchReference::Constant) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputText("Value (hex)", state.constant_input, sizeof(state.constant_input),
                         ImGuiInputTextFlags_CharsHexadecimal);
    }

    const bool has_search = state.search.active();
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_search);
    if (ImGui::Button("Filter")) {
        uint32_t constant = 0;
        bool valid = true;
        if (state.reference == cw::MemorySearchReference::Constant &&
            (!parse_hex_value(state.constant_input, constant) ||
             constant > (state.all_bit_positions ? 1U : cw::memory_value_max(state.size)))) {
            state.message = "Enter a valid hexadecimal value for the selected size.";
            state.message_is_error = true;
            valid = false;
        }
        if (valid) {
            if (state.search.filter(memory, state.comparison, state.reference, constant)) {
                state.message.clear();
                state.message_is_error = false;
            } else {
                state.message = "Could not apply this filter.";
                state.message_is_error = true;
            }
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_search || state.search.undo_count() == 0);
    if (ImGui::Button("Undo")) {
        if (state.search.undo()) {
            state.message.clear();
            state.message_is_error = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (state.search.invalidated()) {
        ImGui::TextDisabled("Search settings changed; start a new search.");
    }
    if (state.search.active()) {
        ImGui::Text("Candidates: %zu  (up to %zu filters can be undone)",
                    state.search.candidate_count(), cw::MemorySearch::kMaxUndoSteps);
    } else {
        ImGui::TextDisabled("No active search.");
    }
    if (!state.message.empty()) {
        const ImVec4 color = state.message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                    : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", state.message.c_str());
    }

    if (state.search.active()) {
        if (state.search.candidate_count() == 0) {
            ImGui::TextDisabled("No candidates remain. Undo a filter or start a new search.");
        } else if (ImGui::BeginTable("MemorySearchResults", 6,
                                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                     ImVec2(0.0f, 0.0f))) {
            ImGui::TableSetupColumn("Address");
            ImGui::TableSetupColumn("Current");
            ImGui::TableSetupColumn("Previous");
            ImGui::TableSetupColumn("First snapshot");
            ImGui::TableSetupColumn("Note");
            ImGui::TableSetupColumn("Watch", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            const size_t visible_count =
                std::min<size_t>(state.search.candidate_count(),
                                 static_cast<size_t>(std::numeric_limits<int>::max()));
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible_count));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto result = state.search.result_at(memory, static_cast<size_t>(row));
                    if (!result) {
                        continue;
                    }
                    ImGui::PushID(row);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char address[24];
                    if (result->bit_index) {
                        std::snprintf(address, sizeof(address), "0x%06X.Bit%u", result->address,
                                      static_cast<unsigned>(*result->bit_index));
                    } else {
                        std::snprintf(address, sizeof(address), "0x%06X", result->address);
                    }
                    if (ImGui::Selectable(address) && result->current) {
                        jump_to_memory_address(viewer, memory, result->address);
                    }
                    ImGui::TableSetColumnIndex(1);
                    if (result->current) {
                        ImGui::Text("0x%X", *result->current);
                    } else {
                        ImGui::TextDisabled("--");
                    }
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("0x%X", result->previous);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("0x%X", result->initial);
                    ImGui::TableSetColumnIndex(4);
                    draw_note_preview(notes.display_note_at(result->address));
                    ImGui::TableSetColumnIndex(5);
                    if (ImGui::SmallButton("Watch")) {
                        cw::MemoryValueSize watch_size = state.size;
                        cw::ByteOrder watch_order = state.byte_order;
                        if (result->bit_index) {
                            watch_size = static_cast<cw::MemoryValueSize>(
                                static_cast<unsigned>(cw::MemoryValueSize::Bit0) +
                                *result->bit_index);
                            watch_order = cw::ByteOrder::LittleEndian;
                        } else if (!has_byte_order(watch_size)) {
                            watch_order = cw::ByteOrder::LittleEndian;
                        }
                        if (watch_list.add(result->address, watch_size, watch_order)) {
                            state.message = "Address added to Watch List.";
                            state.message_is_error = false;
                        } else {
                            state.message = "This address and interpretation are already watched.";
                            state.message_is_error = false;
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}

void draw_watch_list(cw::WatchList& watch_list, WatchListUiState& state, MemoryViewerState& viewer,
                     const cw::MemoryReader& memory, uint64_t frame, const cw::CodeNotes& notes) {
    if (!state.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(900.0f, 460.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Watch List", &state.open)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(150.0f);
    const bool submitted = ImGui::InputText(
        "Address (hex)", state.address_input, sizeof(state.address_input),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const char* selected_size = memory_value_size_label(state.size);
    if (ImGui::BeginCombo("Size", selected_size)) {
        for (const auto& [size, label] : kMemoryValueSizes) {
            const bool selected = size == state.size;
            if (ImGui::Selectable(label, selected)) {
                state.size = size;
                if (!has_byte_order(size)) {
                    state.byte_order = cw::ByteOrder::LittleEndian;
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (has_byte_order(state.size)) {
        ImGui::SameLine();
        const char* order_label =
            state.byte_order == cw::ByteOrder::LittleEndian ? "Little-endian" : "Big-endian";
        if (ImGui::BeginCombo("Byte order", order_label)) {
            for (const auto& [order, label] :
                 {std::pair{cw::ByteOrder::LittleEndian, "Little-endian"},
                  std::pair{cw::ByteOrder::BigEndian, "Big-endian"}}) {
                const bool selected = order == state.byte_order;
                if (ImGui::Selectable(label, selected)) {
                    state.byte_order = order;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("Label (optional)", state.label_input, sizeof(state.label_input));
    ImGui::SameLine();
    if (ImGui::Button("Add") || submitted) {
        uint32_t address = 0;
        if (!parse_hex_value(state.address_input, address)) {
            state.message = "Enter a valid hexadecimal address.";
            state.message_is_error = true;
        } else {
            const cw::ByteOrder byte_order =
                has_byte_order(state.size) ? state.byte_order : cw::ByteOrder::LittleEndian;
            if (watch_list.add(address, state.size, byte_order, state.label_input)) {
                state.message = "Address added to the Watch List.";
                state.message_is_error = false;
                state.address_input[0] = '\0';
                state.label_input[0] = '\0';
            } else {
                state.message = "This address and interpretation are already watched.";
                state.message_is_error = false;
            }
        }
    }

    if (!state.message.empty()) {
        const ImVec4 color = state.message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                    : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", state.message.c_str());
    }
    if (!notes.game_id()) {
        ImGui::TextDisabled("Defina o game ID na janela Status.");
    }
    draw_code_note_load_errors(notes);

    if (watch_list.size() == 0) {
        ImGui::TextDisabled("No watched addresses yet.");
    } else if (ImGui::BeginTable("WatchListItems", 9,
                                 ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                 ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 112.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Current (hex)", ImGuiTableColumnFlags_WidthFixed, 112.0f);
        ImGui::TableSetupColumn("Current (decimal)", ImGuiTableColumnFlags_WidthFixed, 132.0f);
        ImGui::TableSetupColumn("Previous", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Label");
        ImGui::TableSetupColumn("Note");
        ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        std::optional<std::pair<size_t, size_t>> move_request;
        std::optional<size_t> remove_request;
        const auto& items = watch_list.items();
        for (size_t i = 0; i < items.size(); ++i) {
            const cw::MemoryWatchItem& item = items[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            if (const auto age = watch_list.frames_since_change(i, frame); age) {
                const float fade = 1.0f - static_cast<float>(*age) /
                                              static_cast<float>(watch_list.highlight_frames());
                const int alpha = static_cast<int>(45.0f + 105.0f * fade);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(175, 108, 20, alpha));
            }
            ImGui::TableSetColumnIndex(0);
            if (i == 0) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Up") && i > 0) {
                move_request = std::pair{i, i - 1};
            }
            if (i == 0) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (i + 1 == items.size()) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Down") && i + 1 < items.size()) {
                move_request = std::pair{i, i + 1};
            }
            if (i + 1 == items.size()) {
                ImGui::EndDisabled();
            }

            ImGui::TableSetColumnIndex(1);
            char address[24];
            std::snprintf(address, sizeof(address), "0x%06X", item.address);
            if (ImGui::Selectable(address)) {
                jump_to_memory_address(viewer, memory, item.address);
            }
            ImGui::TableSetColumnIndex(2);
            std::string interpretation = memory_value_size_label(item.size);
            if (has_byte_order(item.size)) {
                interpretation += item.byte_order == cw::ByteOrder::LittleEndian ? " LE" : " BE";
            }
            ImGui::TextUnformatted(interpretation.c_str());

            ImGui::TableSetColumnIndex(3);
            if (item.current_value) {
                ImGui::Text("0x%X", *item.current_value);
            } else {
                ImGui::TextDisabled("Unavailable");
            }
            ImGui::TableSetColumnIndex(4);
            if (item.current_value) {
                ImGui::Text("%u", *item.current_value);
            } else {
                ImGui::TextDisabled("Unavailable");
            }
            ImGui::TableSetColumnIndex(5);
            if (item.previous_value) {
                ImGui::Text("0x%X", *item.previous_value);
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableSetColumnIndex(6);
            if (item.label.empty()) {
                ImGui::TextDisabled("--");
            } else {
                ImGui::TextUnformatted(item.label.c_str());
            }
            ImGui::TableSetColumnIndex(7);
            draw_note_preview(notes.display_note_at(item.address));
            ImGui::TableSetColumnIndex(8);
            if (ImGui::SmallButton("Remove")) {
                remove_request = i;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (move_request) {
            watch_list.move(move_request->first, move_request->second);
        }
        if (remove_request) {
            watch_list.remove(*remove_request);
        }
    }

    ImGui::End();
}

void draw_code_notes_window(cw::CodeNotes& notes, CodeNotesUiState& state,
                            const std::filesystem::path& config_dir, MemoryViewerState& viewer,
                            const cw::MemoryReader& memory) {
    if (!state.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(900.0f, 580.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Code Notes", &state.open)) {
        ImGui::End();
        return;
    }

    input_text_string("RACache directory", state.ra_cache_path_input);
    ImGui::SameLine();
    if (ImGui::Button("Save path")) {
        std::filesystem::path saved_path;
        std::string error;
        if (save_ra_cache_dir(config_dir, state.ra_cache_path_input, saved_path, error)) {
            state.ra_cache_dir = std::move(saved_path);
            state.cache_message = state.ra_cache_dir.empty()
                                      ? "Server notes disabled."
                                      : "RACache path saved; server notes reloaded.";
            state.cache_message_is_error = false;
            state.notes_loaded = false;
        } else {
            state.cache_message = std::move(error);
            state.cache_message_is_error = true;
        }
    }
    ImGui::TextDisabled(
        "Use the RACache folder that contains Data/; leave empty to disable server notes.");
    if (!state.cache_message.empty()) {
        const ImVec4 color = state.cache_message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                          : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", state.cache_message.c_str());
    }
    if (!notes.game_id()) {
        ImGui::TextDisabled("Defina o game ID na janela Status.");
        ImGui::End();
        return;
    }
    ImGui::Text("Game ID: %u", *notes.game_id());
    draw_code_note_load_errors(notes);
    input_text_string("Filter", state.filter);

    const auto open_note = [&viewer, &memory](const cw::CodeNote& note) {
        jump_to_memory_address(viewer, memory, note.address);
    };
    if (ImGui::BeginTabBar("CodeNotesTabs")) {
        if (ImGui::BeginTabItem("All notes")) {
            struct Row {
                const cw::CodeNote* note = nullptr;
                bool server = false;
            };
            std::vector<Row> rows;
            rows.reserve(notes.user_notes().size() + notes.server_notes().size());
            for (const cw::CodeNote& note : notes.user_notes()) {
                rows.push_back({&note, false});
            }
            for (const cw::CodeNote& note : notes.server_notes()) {
                rows.push_back({&note, true});
            }
            std::stable_sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
                if (left.note->address != right.note->address) {
                    return left.note->address < right.note->address;
                }
                return left.server < right.server;
            });

            if (rows.empty()) {
                ImGui::TextDisabled("No code notes for this game yet.");
            } else if (ImGui::BeginTable("AllCodeNotes", 3,
                                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                         ImVec2(0.0f, 0.0f))) {
                ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Note");
                ImGui::TableSetupColumn("Origin", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < rows.size(); ++i) {
                    const Row& row = rows[i];
                    const std::string origin = row.server ? "Server" : "Mine";
                    if (!note_matches_filter(*row.note, origin, state.filter)) {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char address[24];
                    std::snprintf(address, sizeof(address), "0x%06X", row.note->address);
                    if (ImGui::Selectable(address)) {
                        open_note(*row.note);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", row.note->note.c_str());
                    ImGui::TableSetColumnIndex(2);
                    if (row.server && !row.note->author.empty()) {
                        ImGui::Text("Server (%s)", row.note->author.c_str());
                    } else {
                        ImGui::TextUnformatted(origin.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("To publish")) {
            const std::vector<cw::CodeNote> notes_to_publish = notes.notes_to_publish();
            ImGui::TextDisabled(
                "Copy each new or changed note into RAIntegration. This tool never uploads notes.");
            if (!notes.server_error().empty()) {
                ImGui::TextDisabled("Fix the server cache JSON before comparing notes to publish.");
            } else if (!notes.user_error().empty()) {
                ImGui::TextDisabled(
                    "Fix the workbench notes JSON before comparing notes to publish.");
            } else if (notes_to_publish.empty()) {
                ImGui::TextDisabled("There are no new or changed notes to publish.");
            } else if (ImGui::BeginTable("PublishCodeNotes", 4,
                                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                         ImVec2(0.0f, 0.0f))) {
                ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Note");
                ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Copy", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < notes_to_publish.size(); ++i) {
                    const cw::CodeNote& note = notes_to_publish[i];
                    if (!note_matches_filter(note, "Mine", state.filter)) {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char address[24];
                    std::snprintf(address, sizeof(address), "0x%06X", note.address);
                    if (ImGui::Selectable(address)) {
                        open_note(note);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", note.note.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(notes.server_note_at(note.address) ? "Changed" : "New");
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton("Copy")) {
                        ImGui::SetClipboardText(note.note.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void draw_main_menu(UiState& ui, Notice& notice, cw::LibretroCore& core,
                    cw::SaveStates& save_states, cw::RecentFiles& recent, cw::GameView& game_view,
                    DialogQueue& dialogs, SDL_Window* window, const std::filesystem::path& config,
                    MemoryViewerState& memory_viewer, MemorySearchState& memory_search,
                    WatchListUiState& watch_list, CodeNotesUiState& code_notes) {
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) {
        ui.quit = true;
    }
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Core...", nullptr, false, !ui.dialog_pending)) {
            begin_core_dialog(ui, dialogs, window, config);
        }
        const bool can_open_game =
            !ui.dialog_pending && (core.is_loaded() || !ui.last_core_path.empty());
        if (ImGui::MenuItem("Open Game...", nullptr, false, can_open_game)) {
            begin_game_dialog(ui, dialogs, window, core, recent);
        }
        if (ImGui::BeginMenu("Recent", !ui.dialog_pending && !recent.entries().empty())) {
            const std::vector<cw::RecentFile> recent_entries = recent.entries();
            for (size_t i = 0; i < recent_entries.size(); ++i) {
                const cw::RecentFile entry = recent_entries[i];
                const std::string label = recent_label(entry);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::MenuItem(label.c_str())) {
                    open_recent(ui, notice, core, game_view, recent, entry);
                }
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Close Content", nullptr, false, core.is_loaded())) {
            close_content(ui, notice, core, game_view);
        }
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            ui.quit = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Emulation", core.is_game_loaded())) {
        if (ImGui::MenuItem(ui.session.paused() ? "Resume" : "Pause", "P")) {
            ui.session.toggle_pause();
            report_message(ui, notice, ui.session.paused() ? "Paused" : "Resumed", true);
        }
        if (ImGui::MenuItem("Reset", "Ctrl+R")) {
            core.reset();
            report_message(ui, notice, "Reset emulation", true);
        }
        if (ImGui::MenuItem("Frame Advance", "N", false, ui.session.paused())) {
            ui.session.request_frame_advance();
            report_message(ui, notice, "Advanced one frame", true);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("States", core.is_game_loaded())) {
        if (ImGui::BeginMenu("Save")) {
            for (int slot = 1; slot <= cw::SaveStates::kSlotCount; ++slot) {
                const std::string label = "Slot " + std::to_string(slot);
                const std::string shortcut = "Shift+F" + std::to_string(slot);
                if (ImGui::MenuItem(label.c_str(), shortcut.c_str())) {
                    std::string message;
                    const bool ok = save_states.save(core, slot, message);
                    report_message(ui, notice, message, ok);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Load")) {
            for (int slot = 1; slot <= cw::SaveStates::kSlotCount; ++slot) {
                const std::string label = "Slot " + std::to_string(slot);
                const std::string shortcut = "F" + std::to_string(slot);
                const auto path = save_states.slot_path(core, slot);
                std::error_code error;
                const bool exists =
                    !path.empty() && std::filesystem::is_regular_file(path, error) && !error;
                if (ImGui::MenuItem(label.c_str(), shortcut.c_str(), false, exists)) {
                    std::string message;
                    const bool ok = save_states.load(core, slot, message);
                    report_message(ui, notice, message, ok);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Memory Viewer", nullptr, &memory_viewer.open);
        ImGui::MenuItem("Memory Search", nullptr, &memory_search.open);
        ImGui::MenuItem("Watch List", nullptr, &watch_list.open);
        ImGui::MenuItem("Code Notes", nullptr, &code_notes.open);
        ImGui::MenuItem("Dear ImGui demo", nullptr, &ui.show_imgui_demo);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// Recomputes the hash when another game is loaded (0.3 ms for a PS1 disc), and looks up the
// game ID stored for it.
void update_game_identity(UiState& ui, const cw::LibretroCore& core, const cw::GameIds& ids) {
    // Keyed on the session too, not just the path: reopening a file that was replaced under the
    // same name must hash the new content.
    if (core.game_path() == ui.identified_game_path &&
        ui.identified_generation == ui.game_session_generation) {
        return;
    }
    ui.identified_game_path = core.game_path();
    ui.identified_generation = ui.game_session_generation;
    ui.game_hash.clear();
    ui.game_hash_error.clear();
    ui.game_id.reset();
    ui.game_id_input[0] = '\0';
    if (ui.identified_game_path.empty()) {
        return;
    }
    if (core.console_id() == RC_CONSOLE_UNKNOWN) {
        ui.game_hash_error = "Unknown console for this core: no RetroAchievements hash";
        return;
    }
    if (const auto hash =
            cw::compute_game_hash(ui.identified_game_path, core.console_id(), ui.game_hash_error)) {
        ui.game_hash = *hash;
        ui.game_id = ids.find(*hash);
        if (ui.game_id) {
            std::snprintf(ui.game_id_input, sizeof(ui.game_id_input), "%u", *ui.game_id);
        }
        SDL_Log("Game hash %s, game ID %s", hash->c_str(),
                ui.game_id ? std::to_string(*ui.game_id).c_str() : "not set");
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Game hash: %s", ui.game_hash_error.c_str());
    }
}

// Appended to the Status window: the hash, and the game ID the user types once per hash.
void draw_game_identity(UiState& ui, cw::GameIds& ids, Notice& notice) {
    if (ui.identified_game_path.empty()) {
        return;
    }
    ImGui::Begin("Status");
    ImGui::Separator();
    if (ui.game_hash.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Hash: %s", ui.game_hash_error.c_str());
        ImGui::End();
        return;
    }
    ImGui::Text("Hash: %s", ui.game_hash.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(ui.game_hash.c_str());
    }
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputText("##game_id", ui.game_id_input, sizeof(ui.game_id_input),
                     ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button("Save game ID")) {
        const unsigned long value = std::strtoul(ui.game_id_input, nullptr, 10);
        std::string error;
        if (value > UINT32_MAX) {
            show_notice(notice, "Game ID too large", false);
        } else if (ids.set(ui.game_hash, static_cast<uint32_t>(value), error)) {
            ui.game_id = value == 0 ? std::nullopt : std::optional<uint32_t>(value);
            show_notice(notice, value == 0 ? "Game ID cleared" : "Game ID saved", true);
        } else {
            show_notice(notice, error, false);
        }
    }
    if (ui.game_id) {
        ImGui::Text("Game ID: %u", *ui.game_id);
    } else {
        ImGui::TextDisabled("Game ID not set: type the number from the game's page on");
        ImGui::TextDisabled("retroachievements.org (e.g. 18238) and save it once.");
    }
    ImGui::End();
}

void draw_status_window(const cw::LibretroCore& core, const UiState& ui,
                        const cw::SdlAudioOutput& audio, double queued_audio_seconds,
                        const cw::FramePacer& pacer, const cw::JoypadState& input,
                        const Gamepads& gamepads) {
    ImGui::Begin("Status");
    ImGui::Text("cheevo-workbench %s", CW_VERSION);
    ImGui::Separator();

    if (!ui.status_message.empty()) {
        const ImVec4 color =
            ui.status_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", ui.status_message.c_str());
    }
    if (!core.is_loaded()) {
        ImGui::TextDisabled("No core loaded.");
    } else {
        const auto& info = core.system_info();
        ImGui::Text("Core: %s %s", info.name.c_str(), info.version.c_str());
        ImGui::Text("Core options: %zu", core.options().options().size());
        if (!core.is_game_loaded()) {
            ImGui::TextDisabled("No game loaded.");
        } else {
            const retro_system_av_info& av = core.av_info();
            ImGui::Text("Geometry: %ux%u (max %ux%u)", av.geometry.base_width,
                        av.geometry.base_height, av.geometry.max_width, av.geometry.max_height);
            ImGui::Text("Last frame: %ux%u, aspect %.3f", core.last_frame_width(),
                        core.last_frame_height(), av.geometry.aspect_ratio);
            ImGui::Text("Images received: %llu",
                        static_cast<unsigned long long>(core.video_frame().serial));
            ImGui::Text("Core timing: %.3f fps, audio %.0f Hz", av.timing.fps,
                        av.timing.sample_rate);
            ImGui::Text("Frames run: %llu", static_cast<unsigned long long>(core.frame_count()));
            ImGui::Text("Measured FPS: %.2f", pacer.measured_fps());
            ImGui::Text("Session: %s", ui.session.paused() ? "Paused" : "Running");
            draw_memory_summary(core);
        }
    }

    ImGui::Separator();
    const std::string audio_status = audio.status_text();
    if (audio.has_error()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Audio: %s", audio_status.c_str());
    } else {
        ImGui::Text("Audio: %s", audio_status.c_str());
    }
    ImGui::Text("Audio queue: %.1f ms", queued_audio_seconds * 1000.0);
    ImGui::Text("Frame pacing: %s",
                pacer.mode() == cw::FramePacer::Mode::Audio ? "audio" : "clock");
    const std::string connected_gamepads = gamepad_names(gamepads);
    const std::string pressed_buttons = pressed_button_names(input.buttons());
    ImGui::Text("Gamepads: %s", connected_gamepads.c_str());
    ImGui::Text("Pressed: %s", pressed_buttons.c_str());
    ImGui::Text("UI: %.1f FPS", ImGui::GetIO().Framerate);
    ImGui::TextDisabled("P pause, N frame advance, Ctrl+R reset");
    ImGui::TextDisabled("Shift+F1-F4 save, F1-F4 load states");
    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    int exit_code = 0;
    const std::optional<LaunchOptions> launch = parse_args(argc, argv, exit_code);
    if (!launch) {
        return exit_code;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // OpenGL 3.3 core: supported by every Linux desktop driver (Mesa, NVIDIA) and enough for
    // libretro cores that request a hardware-rendered GL context later on.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_Window* window = SDL_CreateWindow(
        "cheevo-workbench", static_cast<int>(1280 * scale), static_cast<int>(800 * scale),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GL_CreateContext failed: %s",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Synchronize UI presentation; the core has its own frame pacer.

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // No ImGuiConfigFlags_NavEnableKeyboard: with keyboard navigation on, ImGui claims the
    // keyboard (WantCaptureKeyboard) whenever any of its windows has focus, so the game would
    // stop receiving keys after a click on a tool window, and the arrows would move the UI.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Window layout is saved next to the rest of the config instead of the working directory;
    // with no usable config directory, it is simply not saved. Must outlive the ImGui context.
    const std::filesystem::path config = cw::config_dir();
    const std::string ini_path = config.empty() ? "" : (config / "imgui.ini").string();
    io.IniFilename = ini_path.empty() ? nullptr : ini_path.c_str();
    cw::RecentFiles recent(config.empty() ? std::filesystem::path{} : config / "recent-files.txt");
    recent.load();
    cw::GameIds game_ids(config.empty() ? std::filesystem::path{} : config / "game-ids.txt");
    game_ids.load();

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    cw::SdlAudioOutput audio_output;
    audio_output.initialize(); // A missing audio device must not prevent the UI from opening.

    // Loaded after the GL context exists: hardware-rendered cores will need it.
    cw::LibretroCore core;
    core.set_audio_sink(&audio_output);
    cw::JoypadState joypad_state;
    core.set_joypad_state(&joypad_state);
    Gamepads gamepads;
    open_connected_gamepads(gamepads);
    cw::GameView game_view;
    const std::filesystem::path data = cw::data_dir();
    cw::SaveStates save_states(data.empty() ? std::filesystem::path{} : data / "states");
    cw::CodeNotes code_notes;
    Notice notice;
    UiState ui;
    MemoryViewerState memory_viewer;
    MemorySearchState memory_search;
    WatchListUiState watch_list_ui;
    CodeNotesUiState code_notes_ui;
    code_notes_ui.ra_cache_dir = load_ra_cache_dir(config);
    code_notes_ui.ra_cache_path_input = code_notes_ui.ra_cache_dir.string();
    cw::WatchList watch_list;
    ui.system_dir = launch->system_dir;
    const std::string load_error = load_from_options(core, *launch);
    ui.status_message = load_error;
    ui.status_is_error = !load_error.empty();
    if (!load_error.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", load_error.c_str());
    } else if (core.is_loaded()) {
        ui.last_core_path = core.core_path();
        ui.last_core_extensions = core.system_info().valid_extensions;
        persist_recent(ui, recent, ui.last_core_path,
                       core.is_game_loaded() ? std::filesystem::path(core.game_path())
                                             : std::filesystem::path{});
    }

    DialogQueue& dialogs = dialog_queue();
    cw::FramePacer frame_pacer;
    uint64_t watch_session_generation = ui.game_session_generation;
    while (!ui.quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                ui.quit = true;
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                open_gamepad(event.gdevice.which, gamepads);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                close_gamepad(event.gdevice.which, gamepads, joypad_state);
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                joypad_state.release_all();
            }
        }
        handle_dialog_results(ui, dialogs, notice, core, game_view, recent);
        update_game_identity(ui, core, game_ids);
        sync_code_notes(ui, data, code_notes, code_notes_ui);
        if (watch_session_generation != ui.game_session_generation) {
            watch_list.clear_highlights();
            watch_session_generation = ui.game_session_generation;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const bool window_focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        // Typing into an active text field (e.g. the memory viewer) takes the keyboard from the
        // game.
        update_input(joypad_state, gamepads, window_focused, io.WantTextInput);
        handle_emulation_hotkeys(ui, notice, core);
        handle_state_hotkeys(core, save_states, ui, notice);

        const auto now = cw::FramePacer::Clock::now();
        frame_pacer.set_core_fps(core.is_game_loaded() ? core.av_info().timing.fps : 0.0, now);
        frame_pacer.set_audio_available(audio_output.available(), now);
        if (frame_pacer.set_paused(ui.session.paused(), now)) {
            audio_output.clear();
        }

        // SDL's audio queue is sampled once; the same measurement drives pacing and Status.
        const double queued_audio_seconds = audio_output.queued_seconds();
        size_t frames_run = 0;
        if (core.is_game_loaded()) {
            const size_t frames_due =
                ui.session.frames_to_run(frame_pacer, now, queued_audio_seconds);
            const bool discard_audio = ui.session.frame_advance_pending();
            for (size_t i = 0; i < frames_due; ++i) {
                core.run_frame(discard_audio);
                ++frames_run;
                if (!audio_output.available() &&
                    frame_pacer.mode() == cw::FramePacer::Mode::Audio) {
                    frame_pacer.set_audio_available(false, cw::FramePacer::Clock::now());
                    break;
                }
            }
            if (discard_audio && frames_run == 1) {
                ui.session.finish_frame_advance();
            }
        }
        if (frames_run > 0) {
            game_view.update(core.video_frame());
        }
        if (frame_pacer.record_frames(frames_run, cw::FramePacer::Clock::now())) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Core measured %.2f FPS",
                         frame_pacer.measured_fps());
        }
        watch_list.update(core.memory(), core.frame_count());

        // Passthru keeps the central area of the dock see-through. The game is drawn there on
        // the background draw list, i.e. behind every ImGui window, so docked tool windows
        // around it never cover the image.
        const ImGuiID dockspace =
            ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace)) {
            const ImVec2 max(central->Pos.x + central->Size.x, central->Pos.y + central->Size.y);
            ImDrawList* background = ImGui::GetBackgroundDrawList();
            game_view.draw(background, central->Pos, max, core.av_info().geometry.aspect_ratio);
            if (core.is_game_loaded() && ui.session.paused()) {
                draw_paused_indicator(background, central->Pos, max);
            }
            draw_notice(notice, background, central->Pos, max);
        }
        draw_main_menu(ui, notice, core, save_states, recent, game_view, dialogs, window, config,
                       memory_viewer, memory_search, watch_list_ui, code_notes_ui);
        draw_memory_viewer(core, memory_viewer, watch_list, code_notes, code_notes_ui);
        draw_memory_search(ui, core, memory_search, memory_viewer, watch_list, code_notes);
        draw_watch_list(watch_list, watch_list_ui, memory_viewer, core.memory(), core.frame_count(),
                        code_notes);
        draw_code_notes_window(code_notes, code_notes_ui, config, memory_viewer, core.memory());
        draw_status_window(core, ui, audio_output, queued_audio_seconds, frame_pacer, joypad_state,
                           gamepads);
        draw_game_identity(ui, game_ids, notice);
        if (ui.show_imgui_demo) {
            ImGui::ShowDemoWindow(&ui.show_imgui_demo);
        }

        ImGui::Render();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                   static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    core.unload(); // before the GL context goes away
    for (const auto& [id, gamepad] : gamepads) {
        (void)id;
        SDL_CloseGamepad(gamepad.handle);
    }
    audio_output.shutdown();
    game_view.release();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
