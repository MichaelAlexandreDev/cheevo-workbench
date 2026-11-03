#pragma once

#include <imgui.h>

namespace cw {

// The font size all fixed UI measurements were designed for.
inline constexpr float kUiDesignFontSize = 13.0f;

// Converts a measurement designed for the default font into the current scale, so widgets grow
// with the user's font size and the display scale.
inline float ui_px(float design_size) {
    return design_size * ImGui::GetFontSize() / kUiDesignFontSize;
}

// Keeps the next widget on the current row if it fits, and otherwise lets it start a new row.
// `next_width` is the widget's whole width, including any label.
inline void same_line_if_fits(float next_width) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + next_width <= right)
        ImGui::SameLine();
}

// The whole width of a combo box or input of `width` followed by its label.
inline float labeled_item_width(float width, const char* label) {
    return width + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
}

// How many leading table columns may stay frozen while scrolling sideways. Frozen columns are only
// useful while they leave most of the visible width for the columns that scroll.
inline int frozen_table_columns(int wanted, float frozen_width, float visible_width) {
    constexpr float kMaxFrozenShare = 0.4f;
    return frozen_width <= visible_width * kMaxFrozenShare ? wanted : 0;
}

} // namespace cw
