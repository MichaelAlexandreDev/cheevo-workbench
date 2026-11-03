#include "ui_appearance.hpp"

#include "ui_scale.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <utility>

namespace cw {

void reset_window_layout() {
    ImGuiContext& context = *ImGui::GetCurrentContext();
    ImGui::ClearIniSettings();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr ImGuiWindowFlags kNotAToolWindow =
        ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_DockNodeHost;
    const float step = ImGui::GetStyle().FontSizeBase * ImGui::GetStyle().FontScaleDpi * 2.0f;
    constexpr int kCascadeLength = 8;
    int placed = 0;
    for (ImGuiWindow* window : context.Windows) {
        if (window->Flags & kNotAToolWindow)
            continue;
        const float offset = step * static_cast<float>(placed % kCascadeLength + 1);
        const ImVec2 position(viewport->WorkPos.x + offset, viewport->WorkPos.y + offset);
        const ImVec2 available(std::max(step, viewport->WorkSize.x - offset),
                               std::max(step, viewport->WorkSize.y - offset));
        window->Collapsed = false;
        ImGui::SetWindowPos(window, position);
        ImGui::SetWindowSize(window, ImVec2(std::min(window->SizeFull.x, available.x),
                                            std::min(window->SizeFull.y, available.y)));
        ++placed;
    }
    ImGui::MarkIniSettingsDirty();
}

UiAppearance::UiAppearance(std::filesystem::path settings_file, float display_scale,
                           bool discover_system_fonts)
    : settings_file_(std::move(settings_file)), display_scale_(display_scale),
      discover_system_fonts_(discover_system_fonts), base_style_(ImGui::GetStyle()),
      settings_(load_ui_settings(settings_file_)), pending_(settings_) {
    apply();
}

void UiAppearance::apply() {
    const auto font_size = static_cast<float>(settings_.font_size);
    ImGuiStyle& style = ImGui::GetStyle();
    style = base_style_;
    style.ScaleAllSizes(display_scale_ * font_size / kUiDesignFontSize);
    style.FontScaleDpi = display_scale_;

    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
    atlas.ClearFonts();
    // Set after ClearFonts(), which writes ImGui's previous size back into the style. The default
    // font also picks its bitmap or vector version from this size. Font changes may do the same
    // write, so also use ImGui's next-frame value, which overrides it at the start of the frame.
    style.FontSizeBase = font_size;
    style._NextFrameFontSizeBase = font_size;
    UiFontResult result = configure_ui_fonts(atlas, settings_.font_path, settings_.font_index,
                                             discover_system_fonts_);
    japanese_font_ = std::move(result.japanese_font);
    message_ = std::move(result.error);
}

void UiAppearance::change(const UiSettings& settings) {
    pending_ = settings;
    pending_.font_size = clamp_font_size(pending_.font_size);
    change_pending_ = pending_ != settings_;
}

void UiAppearance::begin_frame() {
    if (change_pending_) {
        change_pending_ = false;
        settings_ = pending_;
        apply();
        if (!settings_file_.empty()) {
            std::string error;
            if (!save_ui_settings(settings_file_, settings_, error) && message_.empty())
                message_ = std::move(error);
        }
    }
    if (layout_reset_pending_) {
        layout_reset_pending_ = false;
        reset_window_layout();
    }
}

void UiAppearance::draw_window() {
    if (!window_open_)
        return;
    ImGui::SetNextWindowSize(ImVec2(ui_px(520.0f), ui_px(460.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Interface Settings", &window_open_)) {
        ImGui::End();
        return;
    }
    if (!fonts_loaded_) {
        fonts_ = list_system_fonts();
        fonts_loaded_ = true;
    }

    UiSettings edited = pending_;
    ImGui::SetNextItemWidth(ui_px(150.0f));
    ImGui::InputInt("Font size", &edited.font_size, 1, 4, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Default size"))
        edited.font_size = UiSettings::kDefaultFontSize;
    ImGui::TextDisabled("From %d to %d. After typing a number, press Enter.",
                        UiSettings::kMinFontSize, UiSettings::kMaxFontSize);

    ImGui::Separator();
    const UiFontFile* current = nullptr;
    for (const UiFontFile& font : fonts_) {
        if (font.path == pending_.font_path && font.index == pending_.font_index)
            current = &font;
    }
    const std::string current_label = pending_.font_path.empty() ? "Built-in"
                                      : current                  ? current->label
                                                                 : pending_.font_path.string();
    ImGui::Text("Font: %s", current_label.c_str());
    font_filter_.Draw("Search fonts", -ui_px(90.0f));

    const float preview_height = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
    if (ImGui::BeginChild("##fonts", ImVec2(0.0f, -preview_height), ImGuiChildFlags_Borders)) {
        if (ImGui::Selectable("Built-in", pending_.font_path.empty())) {
            edited.font_path.clear();
            edited.font_index = 0;
        }
        std::vector<int> visible;
        visible.reserve(fonts_.size());
        for (size_t i = 0; i < fonts_.size(); ++i) {
            if (font_filter_.PassFilter(fonts_[i].label.c_str()))
                visible.push_back(static_cast<int>(i));
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const UiFontFile& font =
                    fonts_[static_cast<size_t>(visible[static_cast<size_t>(row)])];
                ImGui::PushID(row);
                const bool selected =
                    font.path == pending_.font_path && font.index == pending_.font_index;
                if (ImGui::Selectable(font.label.c_str(), selected)) {
                    edited.font_path = font.path;
                    edited.font_index = font.index;
                }
                ImGui::PopID();
            }
        }
        if (fonts_.empty())
            ImGui::TextDisabled("No system fonts were found.");
    }
    ImGui::EndChild();

    ImGui::TextUnformatted("Preview: AaBbCc 0123456789 0x001F00 >= != á ã ç é あ ア 日本");
    if (!message_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", message_.c_str());
    ImGui::End();

    edited.font_size = clamp_font_size(edited.font_size);
    if (edited != pending_)
        change(edited);
}

} // namespace cw
