// cheevo-workbench entry point: creates the SDL3 window with an OpenGL context, loads the
// libretro core given on the command line and runs the Dear ImGui main loop.

#include "frame_pacer.hpp"
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

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
    return load_session(core, options.core, options.game, options.system_dir);
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
};

void report_message(UiState& ui, Notice& notice, const std::string& text, bool ok) {
    ui.status_message = text;
    ui.status_is_error = !ok;
    show_notice(notice, text, ok);
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

void draw_main_menu(UiState& ui, Notice& notice, cw::LibretroCore& core,
                    cw::SaveStates& save_states, cw::RecentFiles& recent, cw::GameView& game_view,
                    DialogQueue& dialogs, SDL_Window* window, const std::filesystem::path& config) {
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
        ImGui::MenuItem("Dear ImGui demo", nullptr, &ui.show_imgui_demo);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
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
    Notice notice;
    UiState ui;
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
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const bool window_focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        // Only typing into a text field (memory search, notes) takes the keyboard from the game.
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
        draw_main_menu(ui, notice, core, save_states, recent, game_view, dialogs, window, config);
        draw_status_window(core, ui, audio_output, queued_audio_seconds, frame_pacer, joypad_state,
                           gamepads);
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
