// cheevo-workbench entry point: creates the SDL3 window with an OpenGL context, loads the
// libretro core given on the command line and runs the Dear ImGui main loop.

#include "achievement_editor_window.hpp"
#include "app_state.hpp"
#include "assisted_search_window.hpp"
#include "authoring_window.hpp"
#include "code_notes_window.hpp"
#include "content_dialogs.hpp"
#include "cw/devtools/bookmarks.hpp"
#include "cw/devtools/code_notes.hpp"
#include "cw/devtools/game_hash.hpp"
#include "cw/devtools/logic_tester.hpp"
#include "cw/devtools/memory_search.hpp"
#include "cw/devtools/rich_presence_editor.hpp"
#include "cw/devtools/user_file.hpp"
#include "frame_pacer.hpp"
#include "game_ids.hpp"
#include "game_view.hpp"
#include "gamepad_input.hpp"
#include "input_state.hpp"
#include "libretro_core.hpp"
#include "logic_tester_window.hpp"
#include "memory_search_window.hpp"
#include "memory_viewer_window.hpp"
#include "paths.hpp"
#include "recent_files.hpp"
#include "save_states.hpp"
#include "sdl_audio_output.hpp"
#include "session_state.hpp"
#include "status_window.hpp"
#include "ui_appearance.hpp"
#include "ui_fonts.hpp"
#include "ui_scale.hpp"
#include "ui_widgets.hpp"
#include "watch_list_window.hpp"

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
#include <deque>
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

using cw::AchievementTesterUi;
using cw::begin_core_dialog;
using cw::begin_game_dialog;
using cw::close_content;
using cw::close_gamepad;
using cw::CodeNotesUiState;
using cw::dialog_queue;
using cw::DialogKind;
using cw::DialogQueue;
using cw::directory_exists;
using cw::draw_achievement_tester;
using cw::draw_code_note_load_errors;
using cw::draw_code_notes_window;
using cw::draw_game_identity;
using cw::draw_memory_search;
using cw::draw_memory_viewer;
using cw::draw_note_preview;
using cw::draw_status_window;
using cw::draw_system_directory;
using cw::draw_watch_list;
using cw::flush_pending_bookmark_descriptions;
using cw::game_filter_pattern;
using cw::gamepad_names;
using cw::Gamepads;
using cw::handle_dialog_results;
using cw::has_byte_order;
using cw::input_text_multiline_string;
using cw::input_text_string;
using cw::jump_to_memory_address;
using cw::kMemoryValueSizes;
using cw::load_session;
using cw::memory_value_size_label;
using cw::MemorySearchState;
using cw::MemoryViewerState;
using cw::Notice;
using cw::open_connected_gamepads;
using cw::open_gamepad;
using cw::open_recent;
using cw::parse_hex_value;
using cw::persist_recent;
using cw::pressed_button_names;
using cw::record_logic_events;
using cw::report_message;
using cw::save_directory_setting;
using cw::set_bookmark_message;
using cw::show_notice;
using cw::sync_achievement_tester;
using cw::UiState;
using cw::update_game_identity;
using cw::update_input;
using cw::WatchListUiState;

constexpr const char* kUsage =
    "Usage: cheevo-workbench [--core <core.so>] [--system <bios dir>] [game]\n"
    "\n"
    "  --core <core.so>     libretro core to load, e.g. "
    "/usr/lib/libretro/mednafen_psx_libretro.so\n"
    "  --system <dir>       BIOS directory; remembered for future launches\n"
    "                       default: saved selection, then the XDG data/system directory\n"
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

std::filesystem::path load_directory_setting(const std::filesystem::path& config_dir,
                                             const char* filename) {
    if (config_dir.empty()) {
        return {};
    }
    std::ifstream input(config_dir / filename);
    std::string path;
    if (!input || !std::getline(input, path)) {
        return {};
    }
    if (!path.empty() && path.back() == '\r') {
        path.pop_back();
    }
    const std::filesystem::path directory(path);
    return directory.is_absolute() ? directory : std::filesystem::path{};
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

void sync_bookmarks(const UiState& ui, const std::filesystem::path& data_dir,
                    const cw::LibretroCore& core, cw::Bookmarks& bookmarks,
                    WatchListUiState& state) {
    if (state.bookmarks_loaded && state.loaded_game_id == ui.game_id &&
        state.loaded_data_dir == data_dir) {
        return;
    }
    bookmarks.load(ui.game_id, data_dir, core.memory().size());
    state.bookmarks_loaded = true;
    state.loaded_game_id = ui.game_id;
    state.loaded_data_dir = data_dir;
    state.description_buffers.clear();
    state.dirty_description_ids.clear();
    state.message.clear();
    state.message_is_error = false;
}

// Shift+F1..F4 saves slot 1..4, F1..F4 loads it. RouteGlobal keeps them active in tool windows.
void handle_state_hotkeys(cw::LibretroCore& core, cw::SaveStates& states, UiState& ui,
                          Notice& notice, cw::LogicTester& tester) {
    if (!core.is_game_loaded()) {
        return;
    }
    for (int slot = 1; slot <= cw::SaveStates::kSlotCount; ++slot) {
        const auto key = static_cast<ImGuiKey>(ImGuiKey_F1 + (slot - 1));
        std::string message;
        if (ImGui::Shortcut(ImGuiMod_Shift | key, ImGuiInputFlags_RouteGlobal)) {
            const bool ok = states.save(core, slot, message, &tester);
            report_message(ui, notice, message, ok);
        } else if (ImGui::Shortcut(key, ImGuiInputFlags_RouteGlobal)) {
            const bool ok = states.load(core, slot, message, &tester);
            ++ui.memory_history_generation;
            report_message(ui, notice, message, ok);
        }
    }
}

void handle_emulation_hotkeys(UiState& ui, Notice& notice, cw::LibretroCore& core,
                              cw::LogicTester& tester, AchievementTesterUi& state) {
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
        ++ui.memory_history_generation;
        tester.reset();
        record_logic_events(tester.validate_addresses(core.memory(), core.frame_count()), tester,
                            state, notice);
        report_message(ui, notice, "Reset emulation", true);
    }
}

std::string recent_label(const cw::RecentFile& entry) {
    const std::string game = entry.game.empty() ? "Core only" : entry.game.filename().string();
    return game + " — " + entry.core.filename().string();
}

// --- Achievement tester (step 3.2) ----------------------------------------------------------

void draw_main_menu(UiState& ui, Notice& notice, cw::LibretroCore& core,
                    cw::SaveStates& save_states, cw::RecentFiles& recent, cw::GameView& game_view,
                    DialogQueue& dialogs, SDL_Window* window, const std::filesystem::path& config,
                    MemoryViewerState& memory_viewer, MemorySearchState& memory_search,
                    WatchListUiState& watch_list, CodeNotesUiState& code_notes,
                    AchievementTesterUi& achievement_tester, cw::LogicTester& tester,
                    bool& achievement_editor_open, cw::AuthoringWindow& authoring,
                    cw::AssistedSearchWindow& assisted_search, cw::UiAppearance& appearance) {
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) {
        ui.quit = true;
    }
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("System (BIOS) Directory...")) {
            ui.system_directory_input = ui.system_dir.string();
            ui.system_directory_error.clear();
            ui.system_directory_open = true;
        }
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
            ++ui.memory_history_generation;
            tester.reset();
            record_logic_events(tester.validate_addresses(core.memory(), core.frame_count()),
                                tester, achievement_tester, notice);
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
                    const bool ok = save_states.save(core, slot, message, &tester);
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
                    const bool ok = save_states.load(core, slot, message, &tester);
                    ++ui.memory_history_generation;
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
        ImGui::MenuItem("Assisted Search", nullptr, &assisted_search.open);
        ImGui::MenuItem("Watch List", nullptr, &watch_list.open);
        ImGui::MenuItem("Code Notes", nullptr, &code_notes.open);
        ImGui::MenuItem("Achievement Tester", nullptr, &achievement_tester.open);
        ImGui::MenuItem("Achievement Editor", nullptr, &achievement_editor_open);
        ImGui::MenuItem("Rich Presence", nullptr, &authoring.rich_presence_open);
        ImGui::MenuItem("Leaderboards", nullptr, &authoring.leaderboards_open);
        ImGui::MenuItem("Dear ImGui demo", nullptr, &ui.show_imgui_demo);
        ImGui::Separator();
        ImGui::MenuItem("Interface Settings", nullptr, &appearance.window_open());
        if (ImGui::MenuItem("Reset Layout")) {
            appearance.request_layout_reset();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
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
    // A compositor may throttle swaps for covered windows. Emulation shares this thread, so
    // only wait for VSync while focused; use a bounded clock cadence otherwise.
    bool synchronized_swaps = SDL_GL_SetSwapInterval(1);
    int applied_swap_interval = synchronized_swaps ? 1 : -1;
    auto next_swap_attempt = cw::FramePacer::Clock::time_point{};
    if (!synchronized_swaps) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "VSync unavailable: %s", SDL_GetError());
    }

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
    cw::UiAppearance appearance(
        config.empty() ? std::filesystem::path{} : config / "ui-settings.txt", scale);
    if (!appearance.japanese_font().empty())
        SDL_Log("Japanese UI font: %s", appearance.japanese_font().c_str());

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
    cw::AssistedSearchWindow assisted_search;
    WatchListUiState watch_list_ui;
    CodeNotesUiState code_notes_ui;
    code_notes_ui.ra_cache_dir = load_directory_setting(config, "ra-cache-path.txt");
    code_notes_ui.ra_cache_path_input = code_notes_ui.ra_cache_dir.string();
    cw::Bookmarks bookmarks;
    cw::LogicTester logic_tester;
    AchievementTesterUi achievement_tester_ui;
    cw::AchievementEditorWindow achievement_editor(logic_tester, code_notes);
    cw::AuthoringWindow authoring(logic_tester);
    ui.system_dir = launch->system_dir.empty() ? load_directory_setting(config, "system-path.txt")
                                               : std::filesystem::absolute(launch->system_dir);
    ui.system_directory_input = ui.system_dir.string();
    if (!launch->system_dir.empty()) {
        std::filesystem::path saved;
        std::string error;
        if (!save_directory_setting(config, "system-path.txt", ui.system_directory_input, saved,
                                    error)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "BIOS directory was not remembered: %s",
                        error.c_str());
        }
    }
    LaunchOptions configured_launch = *launch;
    configured_launch.system_dir = ui.system_dir;
    const std::string load_error = load_from_options(core, configured_launch);
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
        const auto iteration_start = cw::FramePacer::Clock::now();
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
        sync_bookmarks(ui, data, core, bookmarks, watch_list_ui);
        // Synchronize before save/load hotkeys even while paused, so an initial session reset
        // cannot erase the runtime progress just restored from a slot.
        achievement_editor.sync_live(ui.game_session_generation, core.is_game_loaded(),
                                     core.memory());
        authoring.sync_live(ui.game_session_generation, core.is_game_loaded(), core.memory(),
                            core.frame_count());
        sync_achievement_tester(logic_tester, achievement_tester_ui, ui.game_session_generation,
                                core, notice);
        if (watch_session_generation != ui.game_session_generation) {
            bookmarks.clear_highlights();
            watch_session_generation = ui.game_session_generation;
        }
        const auto window_flags = SDL_GetWindowFlags(window);
        const bool minimized = (window_flags & SDL_WINDOW_MINIMIZED) != 0;
        const bool window_focused = !minimized && (window_flags & SDL_WINDOW_INPUT_FOCUS) != 0;
        const int desired_swap_interval = window_focused ? 1 : 0;
        if (desired_swap_interval != applied_swap_interval &&
            iteration_start >= next_swap_attempt) {
            if (SDL_GL_SetSwapInterval(desired_swap_interval)) {
                applied_swap_interval = desired_swap_interval;
                synchronized_swaps = desired_swap_interval != 0;
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not change VSync: %s", SDL_GetError());
                next_swap_attempt = iteration_start + std::chrono::seconds(1);
            }
        }
        // If a driver refuses immediate swaps, keep audio running and defer background drawing
        // rather than calling a swap that can still block on an occluded window.
        const bool can_present = !minimized && (window_focused || applied_swap_interval == 0);

        appearance.begin_frame(); // font and layout changes are only safe between frames
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Typing into an active text field (e.g. the memory viewer) takes the keyboard from the
        // game.
        update_input(joypad_state, gamepads, window_focused, io.WantTextInput);
        if (!io.WantTextInput) {
            handle_emulation_hotkeys(ui, notice, core, logic_tester, achievement_tester_ui);
            handle_state_hotkeys(core, save_states, ui, notice, logic_tester);
        }
        assisted_search.sync(ui.game_session_generation, ui.memory_history_generation,
                             core.is_game_loaded());
        assisted_search.handle_hotkey(window_focused && !io.WantTextInput);

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
                assisted_search.observe(core.memory());
                ++frames_run;
                // Evaluated once per emulated frame, right after it ran, as RetroAchievements does.
                const auto logic_events = logic_tester.do_frame(core.memory(), core.frame_count());
                record_logic_events(logic_events, logic_tester, achievement_tester_ui, notice);
                authoring.record_events(logic_events);
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
        if (frames_run > 0 && can_present) {
            game_view.update(core.video_frame());
        }
        if (frame_pacer.record_frames(frames_run, cw::FramePacer::Clock::now())) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Core measured %.2f FPS",
                         frame_pacer.measured_fps());
        }
        bookmarks.update(core.memory(), core.frame_count());

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
                       memory_viewer, memory_search, watch_list_ui, code_notes_ui,
                       achievement_tester_ui, logic_tester, achievement_editor.open, authoring,
                       assisted_search, appearance);
        appearance.draw_window();
        assisted_search.sync(ui.game_session_generation, ui.memory_history_generation,
                             core.is_game_loaded());
        draw_system_directory(ui, notice, dialogs, window, config, data);
        draw_memory_viewer(core, memory_viewer, bookmarks, code_notes, code_notes_ui);
        draw_memory_search(ui.game_session_generation, core, memory_search, memory_viewer,
                           bookmarks, code_notes);
        if (const auto address = assisted_search.draw(core.memory(), core.is_game_loaded()))
            jump_to_memory_address(memory_viewer, core.memory(), *address);
        draw_watch_list(bookmarks, watch_list_ui, memory_viewer, core.memory(), core.frame_count(),
                        code_notes, code_notes_ui.ra_cache_dir);
        draw_code_notes_window(code_notes, code_notes_ui, config, memory_viewer, core.memory());
        draw_achievement_tester(logic_tester, achievement_tester_ui, core, notice);
        const auto editor_file =
            ui.game_id ? (code_notes_ui.ra_cache_dir.empty()
                              ? (data.empty()
                                     ? std::filesystem::path{}
                                     : data / "sets" / (std::to_string(*ui.game_id) + "-User.txt"))
                              : cw::ra_user_file_path(code_notes_ui.ra_cache_dir, *ui.game_id))
                       : std::filesystem::path{};
        // A menu may have replaced content after this iteration's emulation frames.
        achievement_editor.sync_live(ui.game_session_generation, core.is_game_loaded(),
                                     core.memory());
        achievement_editor.draw(editor_file,
                                core.is_game_loaded()
                                    ? std::filesystem::path(core.game_path()).stem().string()
                                    : std::string{});
        const auto presence_file =
            ui.game_id ? (code_notes_ui.ra_cache_dir.empty()
                              ? (data.empty()
                                     ? std::filesystem::path{}
                                     : data / "sets" / (std::to_string(*ui.game_id) + "-Rich.txt"))
                              : cw::ra_rich_presence_path(code_notes_ui.ra_cache_dir, *ui.game_id))
                       : std::filesystem::path{};
        authoring.sync_live(ui.game_session_generation, core.is_game_loaded(), core.memory(),
                            core.frame_count());
        authoring.draw(presence_file, editor_file);
        draw_status_window(core, ui, audio_output, queued_audio_seconds, frame_pacer, joypad_state,
                           gamepads);
        draw_game_identity(ui, game_ids, notice);
        if (ui.show_imgui_demo) {
            ImGui::ShowDemoWindow(&ui.show_imgui_demo);
        }

        if (achievement_editor.take_quit_request())
            ui.quit = true;
        if (authoring.take_quit_request())
            ui.quit = true;
        if (ui.quit && !achievement_editor.allow_quit())
            ui.quit = false;
        if (ui.quit && !authoring.allow_quit())
            ui.quit = false;
        // Save active bookmark edits before leaving the main loop.
        if (ui.quit && !flush_pending_bookmark_descriptions(bookmarks, watch_list_ui)) {
            ui.quit = false;
            show_notice(notice, watch_list_ui.message, false);
        }

        ImGui::Render();
        if (can_present) {
            glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                       static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
            glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(window);
        }
        if (!synchronized_swaps || !can_present) {
            // Keep the event/emulation loop responsive without spinning when the audio queue
            // is full. FramePacer still controls the core's speed from audio or its own clock.
            constexpr auto kUiPeriod = std::chrono::nanoseconds(1'000'000'000 / 120);
            const auto remaining = kUiPeriod - (cw::FramePacer::Clock::now() - iteration_start);
            if (remaining > decltype(remaining)::zero()) {
                const auto nanoseconds =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(remaining);
                SDL_DelayNS(static_cast<Uint64>(nanoseconds.count()));
            }
        }
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
