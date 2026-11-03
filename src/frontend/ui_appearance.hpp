#pragma once

#include "ui_fonts.hpp"
#include "ui_settings.hpp"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace cw {

// Clears all saved docking and places every top-level tool window, undocked, inside the viewport.
// Call between frames.
void reset_window_layout();

// Owns the font and size preferences: loads them, applies them to ImGui, and draws the settings
// window. Construct after ImGui::CreateContext() and after the base style colors are set.
class UiAppearance {
  public:
    // `settings_file` may be empty: changes then apply only for this session.
    UiAppearance(std::filesystem::path settings_file, float display_scale,
                 bool discover_system_fonts = true);

    // Applies pending font, size and layout changes. Call before starting each frame, because
    // these changes are not safe while a frame is being built.
    void begin_frame();
    void draw_window();

    void request_layout_reset() { layout_reset_pending_ = true; }
    bool& window_open() { return window_open_; }
    const UiSettings& settings() const { return settings_; }
    const std::string& japanese_font() const { return japanese_font_; }
    const std::string& message() const { return message_; }

    // Takes effect on the next begin_frame(), which also saves the settings.
    void change(const UiSettings& settings);

  private:
    void apply();

    std::filesystem::path settings_file_;
    float display_scale_;
    bool discover_system_fonts_;
    ImGuiStyle base_style_;
    UiSettings settings_;
    UiSettings pending_;
    bool change_pending_ = false;
    bool layout_reset_pending_ = false;
    bool window_open_ = false;
    bool fonts_loaded_ = false;
    std::vector<UiFontFile> fonts_;
    ImGuiTextFilter font_filter_;
    std::string japanese_font_;
    std::string message_;
};

} // namespace cw
