#include "content_dialogs.hpp"

#include "paths.hpp"
#include "ui_scale.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <imgui.h>
#include <utility>

namespace cw {
namespace {

LibretroCore::Directories make_core_directories(const std::filesystem::path& system_override) {
    // Default system/save folders live in the data directory, which is empty when there is no
    // usable location; the core is then told "no directory" instead of a random one.
    const std::filesystem::path data = data_dir();
    auto default_dir = [&data](const char* name) -> std::filesystem::path {
        if (data.empty()) {
            return {};
        }
        std::error_code ec;
        std::filesystem::create_directories(data / name, ec);
        return ec ? std::filesystem::path{} : data / name;
    };
    LibretroCore::Directories dirs{.system = system_override.empty()
                                                 ? default_dir("system")
                                                 : std::filesystem::absolute(system_override),
                                   .save = default_dir("saves")};
    return dirs;
}

void SDLCALL on_open_file_dialog(void* userdata, const char* const* filelist, int) {
    auto& request = *static_cast<DialogRequest*>(userdata);
    DialogResult result{request.kind, {}, {}};
    if (!filelist) {
        result.error = "SDL file dialog failed";
        if (const char* detail = SDL_GetError(); detail && *detail) {
            result.error += ": ";
            result.error += detail;
        }
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

} // namespace

// SDL owns an asynchronous callback and may invoke it on another thread. The registry has
// process lifetime so a late callback cannot refer to a destroyed stack object during shutdown.
DialogQueue& dialog_queue() {
    static DialogQueue* queue = new DialogQueue;
    return *queue;
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
    request->filters = {
        {{request->filter_name.c_str(), request->filter_pattern.c_str()}, {"All files", "*"}}};
    DialogRequest* callback_data = request.get();
    queue.requests.push_back(std::move(request));
    ui.dialog_pending = true;
    if (kind == DialogKind::SystemDirectory) {
        SDL_ShowOpenFolderDialog(on_open_file_dialog, callback_data, window,
                                 callback_data->initial_location.empty()
                                     ? nullptr
                                     : callback_data->initial_location.c_str(),
                                 false);
        return;
    }
    SDL_ShowOpenFileDialog(
        on_open_file_dialog, callback_data, window, callback_data->filters.data(),
        static_cast<int>(callback_data->filters.size()),
        callback_data->initial_location.empty() ? nullptr : callback_data->initial_location.c_str(),
        false);
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

std::filesystem::path default_game_directory(const UiState& ui, const LibretroCore& core,
                                             const RecentFiles& recent) {
    if (core.is_game_loaded()) {
        const auto current = std::filesystem::path(core.game_path()).parent_path();
        if (directory_exists(current)) {
            return current;
        }
    }
    for (const RecentFile& entry : recent.entries()) {
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
    begin_open_dialog(ui, queue, window, DialogKind::OpenCore, "Libretro cores", "so",
                      default_core_directory(config));
}

void begin_game_dialog(UiState& ui, DialogQueue& queue, SDL_Window* window,
                       const LibretroCore& core, const RecentFiles& recent) {
    begin_open_dialog(ui, queue, window, DialogKind::OpenGame, "Game content",
                      game_filter_pattern(ui.last_core_extensions),
                      default_game_directory(ui, core, recent));
}

void persist_recent(UiState& ui, RecentFiles& recent, const std::filesystem::path& core_path,
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

// Loads a complete session from paths. A rejected game leaves the frontend in an empty state.
std::string load_session(LibretroCore& core, const std::filesystem::path& core_path,
                         const std::filesystem::path& game_path,
                         const std::filesystem::path& system_override) {
    core.unload();
    const LibretroCore::Directories dirs = make_core_directories(system_override);
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

void open_core(UiState& ui, Notice& notice, LibretroCore& core, GameView& game_view,
               RecentFiles& recent, const std::filesystem::path& core_path) {
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

void open_game(UiState& ui, Notice& notice, LibretroCore& core, GameView& game_view,
               RecentFiles& recent, const std::filesystem::path& game_path) {
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

void open_recent(UiState& ui, Notice& notice, LibretroCore& core, GameView& game_view,
                 RecentFiles& recent, const RecentFile& entry) {
    const RecentFile selected = entry;
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

void close_content(UiState& ui, Notice& notice, LibretroCore& core, GameView& game_view) {
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

void handle_dialog_results(UiState& ui, DialogQueue& queue, Notice& notice, LibretroCore& core,
                           GameView& game_view, RecentFiles& recent) {
    for (DialogResult& result : take_dialog_results(queue)) {
        ui.dialog_pending = false;
        if (!result.error.empty()) {
            report_message(ui, notice, result.error, false);
        } else if (!result.path.empty()) {
            if (result.kind == DialogKind::OpenCore) {
                open_core(ui, notice, core, game_view, recent, result.path);
            } else if (result.kind == DialogKind::OpenGame) {
                open_game(ui, notice, core, game_view, recent, result.path);
            } else {
                ui.system_directory_open = true;
                ui.system_directory_input = result.path;
                ui.system_directory_error.clear();
            }
        }
    }
    remove_finished_dialogs(queue);
}

void draw_system_directory(UiState& ui, Notice& notice, DialogQueue& dialogs, SDL_Window* window,
                           const std::filesystem::path& config, const std::filesystem::path& data) {
    if (!ui.system_directory_open) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(ui_px(520.0f), ui_px(240.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("System (BIOS) Directory", &ui.system_directory_open)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Choose the folder containing your BIOS files. The selection is remembered "
                       "for future launches. Reopen the game after changing this setting.");
    ImGui::TextUnformatted("Directory (empty uses the default)");
    ImGui::SetNextItemWidth(-1.0f);
    input_text_string("##system_directory", ui.system_directory_input);
    ImGui::BeginDisabled(ui.dialog_pending);
    if (ImGui::Button("Browse...")) {
        begin_open_dialog(ui, dialogs, window, DialogKind::SystemDirectory, {}, {},
                          ui.system_directory_input);
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Save directory")) {
        std::filesystem::path saved;
        if (save_directory_setting(config, "system-path.txt", ui.system_directory_input, saved,
                                   ui.system_directory_error)) {
            ui.system_dir = std::move(saved);
            show_notice(notice, "BIOS directory saved. Reopen the game to apply it.", true);
        }
    }
    const auto effective = ui.system_dir.empty()
                               ? (data.empty() ? std::filesystem::path{} : data / "system")
                               : ui.system_dir;
    ImGui::TextWrapped("Next core load: %s",
                       effective.empty() ? "(unavailable)" : effective.c_str());
    if (!ui.system_directory_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", ui.system_directory_error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

} // namespace cw
