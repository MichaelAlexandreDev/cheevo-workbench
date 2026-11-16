#pragma once

// Shared by the window tests in cw_window_tests. ImGui's test engine hooks (window_test_hooks.cpp)
// record every widget a frame draws, with its window, label and rectangle; UiDriver runs frames
// around one drawing function and presses widgets and keys across the frames ImGui needs.

#include <imgui.h>
#include <imgui_internal.h>

#include <doctest/doctest.h>

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cw_test {

struct RecordedItem {
    ImGuiID id = 0;
    ImRect rect;
    std::string window;
    std::string label;
};

// What the hooks recorded since the last clear; a frame clears it before drawing.
std::vector<RecordedItem>& recorded_items();

// Owns an ImGui context with the hooks enabled and runs frames around `draw`.
class UiDriver {
  public:
    explicit UiDriver(std::function<void()> draw, ImVec2 display = ImVec2(1600.0f, 1000.0f)) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.DisplaySize = display;
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::StyleColorsDark();
        ImGui::GetCurrentContext()->TestEngineHookItems = true;
        draw_ = std::move(draw);
    }
    ~UiDriver() { ImGui::DestroyContext(); }
    UiDriver(const UiDriver&) = delete;
    UiDriver& operator=(const UiDriver&) = delete;

    void frame() {
        recorded_items().clear();
        ImGui::NewFrame();
        draw_();
        ImGui::Render();
    }

    const RecordedItem* find(std::string_view window, std::string_view label) const {
        for (const RecordedItem& item : recorded_items()) {
            if (item.label == label && (window.empty() || item.window == window))
                return &item;
        }
        return nullptr;
    }

    bool has(std::string_view window, std::string_view label) const {
        return find(window, label) != nullptr;
    }

    // Moves the pointer onto a widget and presses it.
    void click(std::string_view window, std::string_view label) {
        const RecordedItem* item = find(window, label);
        REQUIRE_MESSAGE(item != nullptr, "No widget '", std::string(label), "' in '",
                        std::string(window), "'");
        const ImVec2 center = item->rect.GetCenter();
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

    // Presses and releases `key` with `mods` held, one frame each, as a user would.
    void key(ImGuiKey key, ImGuiKeyChord mods = ImGuiMod_None) {
        ImGuiIO& io = ImGui::GetIO();
        for (ImGuiKey mod : {ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt}) {
            if (mods & mod)
                io.AddKeyEvent(mod, true);
        }
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        for (ImGuiKey mod : {ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt}) {
            if (mods & mod)
                io.AddKeyEvent(mod, false);
        }
        frame();
    }

    // Replaces the text of the active input field: selects all of it, then types `text`.
    void type(const char* text) {
        key(ImGuiKey_A, ImGuiMod_Ctrl);
        ImGui::GetIO().AddInputCharactersUTF8(text);
        frame();
        frame();
    }

  private:
    std::function<void()> draw_;
};

} // namespace cw_test
