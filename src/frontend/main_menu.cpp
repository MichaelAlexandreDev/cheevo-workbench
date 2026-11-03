#include "main_menu.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <string>

namespace cw {
namespace {

std::string recent_label(const RecentFile& entry) {
    const std::string game = entry.game.empty() ? "Core only" : entry.game.filename().string();
    return game + " — " + entry.core.filename().string();
}

} // namespace

// Shift+F1..F4 saves slot 1..4, F1..F4 loads it. RouteGlobal keeps them active in tool windows.
void handle_state_hotkeys(LibretroCore& core, SaveStates& states, UiState& ui, Notice& notice,
                          LogicTester& tester) {
    if (!core.is_game_loaded()) {
        return;
    }
    for (int slot = 1; slot <= SaveStates::kSlotCount; ++slot) {
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

void handle_emulation_hotkeys(UiState& ui, Notice& notice, LibretroCore& core, LogicTester& tester,
                              AchievementTesterUi& state) {
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

void draw_main_menu(UiState& ui, Notice& notice, LibretroCore& core, SaveStates& save_states,
                    RecentFiles& recent, GameView& game_view, DialogQueue& dialogs,
                    SDL_Window* window, const std::filesystem::path& config,
                    MemoryViewerState& memory_viewer, MemorySearchState& memory_search,
                    WatchListUiState& watch_list, CodeNotesUiState& code_notes,
                    AchievementTesterUi& achievement_tester, LogicTester& tester,
                    bool& achievement_editor_open, AuthoringWindow& authoring,
                    AssistedSearchWindow& assisted_search, UiAppearance& appearance) {
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
            const std::vector<RecentFile> recent_entries = recent.entries();
            for (size_t i = 0; i < recent_entries.size(); ++i) {
                const RecentFile entry = recent_entries[i];
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
            for (int slot = 1; slot <= SaveStates::kSlotCount; ++slot) {
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
            for (int slot = 1; slot <= SaveStates::kSlotCount; ++slot) {
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

} // namespace cw
