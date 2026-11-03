#pragma once

#include "app_state.hpp"
#include "assisted_search_window.hpp"
#include "authoring_window.hpp"
#include "code_notes_window.hpp"
#include "content_dialogs.hpp"
#include "logic_tester_window.hpp"
#include "memory_search_window.hpp"
#include "memory_viewer_window.hpp"
#include "save_states.hpp"
#include "ui_appearance.hpp"
#include "watch_list_window.hpp"

namespace cw {

// Shift+F1..F4 saves slot 1..4, F1..F4 loads it. RouteGlobal keeps them active in tool windows.
void handle_state_hotkeys(LibretroCore& core, SaveStates& states, UiState& ui, Notice& notice,
                          LogicTester& tester);
void handle_emulation_hotkeys(UiState& ui, Notice& notice, LibretroCore& core, LogicTester& tester,
                              AchievementTesterUi& state);
void draw_main_menu(UiState& ui, Notice& notice, LibretroCore& core, SaveStates& save_states,
                    RecentFiles& recent, GameView& game_view, DialogQueue& dialogs,
                    SDL_Window* window, const std::filesystem::path& config,
                    MemoryViewerState& memory_viewer, MemorySearchState& memory_search,
                    WatchListUiState& watch_list, CodeNotesUiState& code_notes,
                    AchievementTesterUi& achievement_tester, LogicTester& tester,
                    bool& achievement_editor_open, AuthoringWindow& authoring,
                    AssistedSearchWindow& assisted_search, UiAppearance& appearance);

} // namespace cw
