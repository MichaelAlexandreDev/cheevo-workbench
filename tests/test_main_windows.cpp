// Characterization tests: they record how the windows inside src/frontend/main.cpp behave today, so
// moving those windows to their own files cannot change them unnoticed. When a window moves, only
// the WindowsHarness below should need updating; the expectations must stay as they are.

#include "test_support.hpp"

#include <cstring>
#include <doctest/doctest.h>

// main.cpp keeps its windows in an anonymous namespace, so the test compiles it into this file. The
// real entry point comes from test_main.cpp.
#define main cheevo_workbench_main
#include "../src/frontend/main.cpp"
#undef main

namespace {

struct RecordedItem {
    ImGuiID id = 0;
    ImRect rect;
    std::string window;
    std::string label;
};

std::vector<RecordedItem> recorded_items;

} // namespace

// ImGui calls these for every widget when TestEngineHookItems is on.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context, ImGuiID id, const ImRect& rect,
                                 const ImGuiLastItemData*) {
    const ImGuiWindow* window = context->CurrentWindow;
    recorded_items.push_back({id, rect, window ? window->RootWindow->Name : "", ""});
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags) {
    for (auto item = recorded_items.rbegin(); item != recorded_items.rend(); ++item) {
        if (item->id == id) {
            item->label = label ? label : "";
            return;
        }
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) {
    return nullptr;
}

namespace {

constexpr uint32_t kGameId = 18238;

// Every window main.cpp draws around one loaded synthetic game, in the application's order.
struct WindowsHarness {
    cw_test::SyntheticContent content;
    cw_test::TempDir dir;
    cw::LibretroCore core;
    cw::SaveStates save_states{dir.path() / "states"};
    cw::RecentFiles recent{dir.path() / "recent-files.txt"};
    cw::GameIds game_ids{dir.path() / "game-ids.txt"};
    cw::GameView game_view;
    cw::CodeNotes code_notes;
    cw::Bookmarks bookmarks;
    cw::LogicTester logic_tester;
    cw::AuthoringWindow authoring{logic_tester};
    cw::AchievementEditorWindow achievement_editor{logic_tester, code_notes};
    cw::AssistedSearchWindow assisted_search;
    cw::SdlAudioOutput audio_output;
    cw::FramePacer frame_pacer;
    cw::JoypadState joypad_state;
    Gamepads gamepads;
    Notice notice;
    UiState ui;
    MemoryViewerState memory_viewer;
    MemorySearchState memory_search;
    WatchListUiState watch_list;
    CodeNotesUiState code_notes_ui;
    AchievementTesterUi tester_ui;
    bool achievement_editor_open = false;
    std::optional<cw::UiAppearance> appearance;

    WindowsHarness() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.DisplaySize = ImVec2(3600.0f, 1800.0f);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::StyleColorsDark();
        appearance.emplace(dir.path() / "ui-settings.txt", 1.0f, false);
        ImGui::GetCurrentContext()->TestEngineHookItems = true;

        std::string error;
        REQUIRE(core.load(cw_test::test_core_path(), {}, error));
        REQUIRE(core.load_game(content.path(), error));
        ui.game_id = kGameId;
        ++ui.game_session_generation;
        core.run_frame();

        memory_viewer.open = true;
        memory_search.open = true;
        watch_list.open = true;
        code_notes_ui.open = true;
        tester_ui.open = true;
        frame();
        arrange_windows();
        frame();
        frame();
    }

    ~WindowsHarness() {
        core.unload();
        ImGui::DestroyContext();
    }

    WindowsHarness(const WindowsHarness&) = delete;
    WindowsHarness& operator=(const WindowsHarness&) = delete;

    void frame() {
        recorded_items.clear();
        ImGui::NewFrame();
        sync_code_notes(ui, dir.path(), code_notes, code_notes_ui);
        sync_bookmarks(ui, dir.path(), core, bookmarks, watch_list);
        bookmarks.update(core.memory(), core.frame_count());
        draw_main_menu(ui, notice, core, save_states, recent, game_view, dialog_queue(), nullptr,
                       dir.path(), memory_viewer, memory_search, watch_list, code_notes_ui,
                       tester_ui, logic_tester, achievement_editor_open, authoring, assisted_search,
                       *appearance);
        draw_memory_viewer(core, memory_viewer, bookmarks, code_notes, code_notes_ui);
        draw_memory_search(ui.game_session_generation, core, memory_search, memory_viewer,
                           bookmarks, code_notes);
        draw_watch_list(bookmarks, watch_list, memory_viewer, core.memory(), core.frame_count(),
                        code_notes, code_notes_ui.ra_cache_dir);
        draw_code_notes_window(code_notes, code_notes_ui, dir.path(), memory_viewer, core.memory());
        draw_achievement_tester(logic_tester, tester_ui, core, notice);
        draw_status_window(core, ui, audio_output, 0.0, frame_pacer, joypad_state, gamepads);
        achievement_editor.sync_live(ui.game_session_generation, core.is_game_loaded(),
                                     core.memory());
        achievement_editor.draw(dir.path() / "18238-User.txt", "Synthetic game");
        ImGui::Render();
    }

    // Side by side and large enough to show every control, because a covered widget cannot be
    // clicked and a scrolled-out one is not reported.
    void arrange_windows() {
        constexpr float kWidth = 1200.0f;
        constexpr float kHeight = 880.0f;
        int index = 0;
        for (const char* name : {"Memory Viewer", "Memory Search", "Watch List", "Code Notes",
                                 "Achievement Tester", "Status"}) {
            const auto column = static_cast<float>(index % 3);
            const auto row = static_cast<float>(index / 3);
            ImGui::SetWindowPos(name, ImVec2(column * kWidth, 30.0f + row * kHeight));
            ImGui::SetWindowSize(name, ImVec2(kWidth, kHeight));
            ++index;
        }
    }

    const RecordedItem* find(std::string_view window, std::string_view label) const {
        for (const RecordedItem& item : recorded_items) {
            if (item.label == label && (window.empty() || item.window == window))
                return &item;
        }
        return nullptr;
    }

    bool has(std::string_view window, std::string_view label) const {
        return find(window, label) != nullptr;
    }

    // Moves the pointer onto a widget and presses it, across the frames ImGui needs for a click.
    void click(std::string_view window, std::string_view label) {
        const RecordedItem* item = find(window, label);
        REQUIRE_MESSAGE(item != nullptr, "No widget '", std::string(label), "' in '",
                        std::string(window), "'");
        press(item->rect);
    }

    // Tabs report no label to the hooks, so they are found through the ID ImGui derives for them.
    void click_tab(const char* window, const char* tab_bar, const char* label) {
        ImGuiWindow* imgui_window = ImGui::FindWindowByName(window);
        REQUIRE(imgui_window != nullptr);
        const ImGuiID id = ImHashStr(label, 0, imgui_window->GetID(tab_bar));
        for (const RecordedItem& item : recorded_items) {
            if (item.id == id) {
                press(item.rect);
                return;
            }
        }
        FAIL("No tab '", label, "' in '", window, "'");
    }

    void press(const ImRect& rect) {
        const ImVec2 center = rect.GetCenter();
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(center.x, center.y);
        frame();
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    }

    // The labels of a window's controls, in drawing order and without nameless or data rows.
    std::vector<std::string> controls(std::string_view window) const {
        std::vector<std::string> labels;
        for (const RecordedItem& item : recorded_items) {
            if (item.window != window || item.label.empty() || item.label.starts_with("##") ||
                item.label.ends_with("###byte") || item.label.find('/') != std::string::npos)
                continue;
            labels.push_back(item.label);
        }
        return labels;
    }
};

template <size_t N> void set_text(char (&buffer)[N], std::string_view text) {
    REQUIRE(text.size() < N);
    std::memcpy(buffer, text.data(), text.size());
    buffer[text.size()] = '\0';
}

} // namespace

using Labels = std::vector<std::string>;

TEST_CASE("Windows keep their controls") {
    WindowsHarness app;
    CHECK(app.controls("Memory Search") ==
          Labels{"Memory Search", "Aligned", "New Search", "Filter", "Undo"});
    CHECK(app.controls("Watch List") ==
          Labels{"Watch List", "Import from RACache", "Export to RACache", "Add"});
    CHECK(app.controls("Code Notes") == Labels{"Code Notes", "Save path"});
    CHECK(app.controls("Achievement Tester") == Labels{"Achievement Tester", "Title", "Logic",
                                                       "Add", "Title", "State", "Measured",
                                                       "Logic"});
    CHECK(app.controls("Status") == Labels{"Status"});

    app.click("Memory Viewer", "Go");
    CHECK(app.controls("Memory Viewer") == Labels{"Memory Viewer", "Address (hex)", "Go", "Watch",
                                                  "Write value", "My note", "Save note",
                                                  "Delete note"});
}

TEST_CASE("The main menu lists and toggles the tool windows") {
    WindowsHarness app;
    constexpr const char* kViewMenu = "View###Menu_00";
    for (const char* menu : {"File", "Emulation", "States", "View"})
        CHECK(app.has("##MainMenuBar", menu));

    app.click("##MainMenuBar", "View");
    for (const char* item :
         {"Memory Viewer", "Memory Search", "Assisted Search", "Watch List", "Code Notes",
          "Achievement Tester", "Achievement Editor", "Rich Presence", "Leaderboards",
          "Dear ImGui demo", "Interface Settings", "Reset Layout"})
        CHECK_MESSAGE(app.has(kViewMenu, item), item);

    REQUIRE(app.watch_list.open);
    app.click(kViewMenu, "Watch List");
    CHECK_FALSE(app.watch_list.open);

    app.click("##MainMenuBar", "View");
    app.click(kViewMenu, "Interface Settings");
    CHECK(app.appearance->window_open());
}

TEST_CASE("Memory Viewer jumps to an address and rejects invalid ones") {
    WindowsHarness app;
    set_text(app.memory_viewer.address_input, "000010");
    app.click("Memory Viewer", "Go");
    CHECK(app.memory_viewer.has_selection);
    CHECK(app.memory_viewer.selected_address == 0x10);
    CHECK_FALSE(app.memory_viewer.message_is_error);

    set_text(app.memory_viewer.address_input, "FFFFFF");
    app.click("Memory Viewer", "Go");
    CHECK(app.memory_viewer.message == "Address is outside the memory space.");
    CHECK(app.memory_viewer.message_is_error);
    CHECK(app.memory_viewer.selected_address == 0x10);

    set_text(app.memory_viewer.address_input, "");
    app.click("Memory Viewer", "Go");
    CHECK(app.memory_viewer.message == "Enter a valid hexadecimal address.");
}

TEST_CASE("Memory Viewer keeps the address controls inside the window at any width") {
    WindowsHarness app;
    ImGuiWindow* window = ImGui::FindWindowByName("Memory Viewer");
    REQUIRE(window != nullptr);
    app.click("Memory Viewer", "Go");
    for (const float width : {1200.0f, 800.0f, 600.0f, 400.0f, 300.0f}) {
        CAPTURE(width);
        ImGui::SetWindowSize("Memory Viewer", ImVec2(width, 880.0f));
        app.frame();
        app.frame();
        for (const char* label : {"Address (hex)", "Go"}) {
            CAPTURE(label);
            const RecordedItem* item = app.find("Memory Viewer", label);
            REQUIRE(item != nullptr);
            CHECK(item->rect.Max.x <= window->Pos.x + window->Size.x);
        }
    }
}

TEST_CASE("Memory Viewer writes values and adds the selection to the Watch List") {
    WindowsHarness app;
    set_text(app.memory_viewer.address_input, "000010");
    app.click("Memory Viewer", "Go");

    set_text(app.memory_viewer.value_input, "AB");
    app.click("Memory Viewer", "Write value");
    uint8_t written = 0;
    REQUIRE(app.core.memory().read(0x10, std::span<uint8_t>(&written, 1)) == 1);
    CHECK(written == 0xAB);

    app.click("Memory Viewer", "Watch");
    REQUIRE(app.bookmarks.size() == 1);
    CHECK(app.bookmarks.items()[0].address == 0x10);
    CHECK(app.memory_viewer.message == "Address added to Watch List.");
}

TEST_CASE("Memory Viewer saves and deletes code notes") {
    WindowsHarness app;
    set_text(app.memory_viewer.address_input, "000010");
    app.click("Memory Viewer", "Go");

    app.code_notes_ui.note_editor = "Current room";
    app.click("Memory Viewer", "Save note");
    const cw::CodeNote* note = app.code_notes.user_note_at(0x10);
    REQUIRE(note != nullptr);
    CHECK(note->note == "Current room");
    CHECK(app.code_notes.notes_to_publish().size() == 1);
    CHECK(std::filesystem::exists(cw::CodeNotes::user_file_path(app.dir.path(), kGameId)));

    app.click("Memory Viewer", "Delete note");
    CHECK(app.code_notes.user_note_at(0x10) == nullptr);
}

TEST_CASE("Memory Search starts, filters and undoes") {
    WindowsHarness app;
    CHECK_FALSE(app.memory_search.search.active());
    app.click("Memory Search", "New Search");
    REQUIRE(app.memory_search.search.active());
    const size_t all = app.memory_search.search.candidate_count();
    CHECK(all == app.core.memory().size());

    // No frame ran, so nothing differs from the previous snapshot.
    app.click("Memory Search", "Filter");
    CHECK(app.memory_search.search.candidate_count() == 0);
    CHECK(app.memory_search.search.undo_count() == 1);

    app.click("Memory Search", "Undo");
    CHECK(app.memory_search.search.candidate_count() == all);

    // The synthetic core changes its frame counter on every frame.
    app.core.run_frame();
    app.frame();
    app.click("Memory Search", "Filter");
    const size_t changed = app.memory_search.search.candidate_count();
    CHECK(changed > 0);
    CHECK(changed < all);
    CHECK(app.has("Memory Search", "Value (hex)") == false);
}

TEST_CASE("Memory Search keeps its controls inside the window at any width") {
    WindowsHarness app;
    ImGuiWindow* window = ImGui::FindWindowByName("Memory Search");
    REQUIRE(window != nullptr);
    for (const float width : {1200.0f, 900.0f, 600.0f, 400.0f}) {
        CAPTURE(width);
        ImGui::SetWindowSize("Memory Search", ImVec2(width, 880.0f));
        app.frame();
        app.frame();
        for (const char* label : {"Aligned", "New Search", "Filter", "Undo"}) {
            CAPTURE(label);
            const RecordedItem* item = app.find("Memory Search", label);
            REQUIRE(item != nullptr);
            CHECK(item->rect.Max.x <= window->Pos.x + window->Size.x);
        }
    }
}

TEST_CASE("Watch List adds an address and persists it per game") {
    WindowsHarness app;
    set_text(app.watch_list.address_input, "20");
    set_text(app.watch_list.label_input, "Room");
    app.click("Watch List", "Add");
    REQUIRE(app.bookmarks.size() == 1);
    CHECK(app.bookmarks.items()[0].address == 0x20);
    CHECK(app.bookmarks.items()[0].description == "Room");
    const auto file = cw::Bookmarks::user_file_path(app.dir.path(), kGameId);
    CHECK(std::filesystem::exists(file));

    // A different game has its own list, and coming back restores this one.
    app.ui.game_id = kGameId + 1;
    app.frame();
    CHECK(app.bookmarks.size() == 0);
    app.ui.game_id = kGameId;
    app.frame();
    CHECK(app.bookmarks.size() == 1);
}

TEST_CASE("Watch List exports to RACache, confirms overwriting, and imports") {
    WindowsHarness app;
    const auto racache = app.dir.path() / "RACache";
    std::filesystem::create_directories(racache / "Bookmarks");
    app.code_notes_ui.ra_cache_dir = racache;
    app.frame();

    set_text(app.watch_list.address_input, "20");
    app.click("Watch List", "Add");
    REQUIRE(app.bookmarks.size() == 1);

    const auto exported = cw::Bookmarks::ra_cache_file_path(racache, kGameId);
    app.click("Watch List", "Export to RACache");
    REQUIRE(std::filesystem::exists(exported));
    CHECK(app.watch_list.message == "Exported bookmarks to RACache.");

    set_text(app.watch_list.address_input, "21");
    app.click("Watch List", "Add");
    REQUIRE(app.bookmarks.size() == 2);

    // A file that already exists is only replaced after confirming.
    const auto before = std::filesystem::file_size(exported);
    app.click("Watch List", "Export to RACache");
    CHECK(std::filesystem::file_size(exported) == before);
    app.frame();
    app.click("Replace RACache bookmarks?", "Replace");
    CHECK(std::filesystem::file_size(exported) > before);

    std::string error;
    REQUIRE(app.bookmarks.remove(0, error));
    REQUIRE(app.bookmarks.size() == 1);
    app.click("Watch List", "Import from RACache");
    CHECK(app.bookmarks.size() == 2);
    CHECK(app.watch_list.message == "Imported bookmarks from RACache.");
}

TEST_CASE("Code Notes saves a RACache path and refuses a missing directory") {
    WindowsHarness app;
    const auto racache = app.dir.path() / "RACache";
    std::filesystem::create_directories(racache);

    app.code_notes_ui.ra_cache_path_input = (app.dir.path() / "missing").string();
    app.click("Code Notes", "Save path");
    CHECK(app.code_notes_ui.ra_cache_dir.empty());
    CHECK(app.code_notes_ui.cache_message_is_error);
    CHECK(app.code_notes_ui.cache_message == "The selected directory does not exist.");

    app.code_notes_ui.ra_cache_path_input = racache.string();
    app.click("Code Notes", "Save path");
    CHECK(app.code_notes_ui.ra_cache_dir == racache);
    CHECK_FALSE(app.code_notes_ui.cache_message_is_error);
    CHECK(std::filesystem::exists(app.dir.path() / "ra-cache-path.txt"));
}

TEST_CASE("Code Notes lists a saved note in the To publish list") {
    WindowsHarness app;
    std::string error;
    REQUIRE(app.code_notes.set_note(0x40, "Flag", error));
    app.frame();
    bool shown = false;
    for (const std::string& label : app.controls("Code Notes"))
        shown = shown || label.find("000040") != std::string::npos;
    CHECK(shown);

    app.click_tab("Code Notes", "CodeNotesTabs", "To publish");
    CHECK(app.has("Code Notes", "Copy"));
}

TEST_CASE("Achievement Tester adds and removes logic") {
    WindowsHarness app;
    app.tester_ui.title = "Bad";
    app.tester_ui.logic = "not logic";
    app.click("Achievement Tester", "Add");
    CHECK_FALSE(app.tester_ui.add_error.empty());
    CHECK(app.tester_ui.title == "Bad"); // the draft survives an error
    CHECK_FALSE(app.has("Achievement Tester", "Remove"));

    app.tester_ui.title = "Good";
    app.tester_ui.logic = "0xH000010=1";
    app.click("Achievement Tester", "Add");
    CHECK(app.tester_ui.add_error.empty());
    CHECK(app.tester_ui.title.empty());
    CHECK(app.tester_ui.logic.empty());
    CHECK(app.has("Achievement Tester", "Remove"));
    CHECK(app.has("Achievement Tester", "Re-arm"));

    app.click("Achievement Tester", "Remove");
    CHECK_FALSE(app.has("Achievement Tester", "Remove"));
}

TEST_CASE("Achievement Editor freezes the live columns only during a test that leaves room") {
    WindowsHarness app;
    app.achievement_editor.open = true;
    app.frame();
    app.frame();
    constexpr const char* kEditor = "Achievement Editor";
    ImGui::SetWindowPos(kEditor, ImVec2(0.0f, 30.0f));
    ImGui::SetWindowSize(kEditor, ImVec2(1800.0f, 1200.0f));
    app.frame();
    app.frame();
    app.click(kEditor, "Current game");
    app.click(kEditor, "New achievement");
    if (!app.has(kEditor, "Remove"))
        app.click(kEditor, "Add condition");

    const auto frozen_columns = [&](float width) {
        ImGui::SetWindowSize(kEditor, ImVec2(width, 1200.0f));
        app.frame();
        app.frame();
        ImGuiContext& context = *ImGui::GetCurrentContext();
        for (int i = 0; i < context.Tables.GetMapSize(); ++i) {
            const ImGuiTable* table = context.Tables.TryGetMapData(i);
            if (table && table->ColumnsCount == 12 &&
                std::string_view(table->OuterWindow->RootWindow->Name) == kEditor)
                return static_cast<int>(table->FreezeColumnsRequest);
        }
        FAIL("The conditions table was not found");
        return -1;
    };

    // Without a test the columns only show "-", so they scroll away at any width.
    CHECK(frozen_columns(1800.0f) == 0);
    CHECK(frozen_columns(600.0f) == 0);

    frozen_columns(1800.0f);
    app.click(kEditor, "Start live test");
    REQUIRE(app.has(kEditor, "Stop live test"));
    CHECK(frozen_columns(1800.0f) == 2);
    CHECK(frozen_columns(600.0f) == 2);
    CHECK(frozen_columns(450.0f) == 0); // they would leave too little room for the other columns
}
