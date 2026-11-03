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

} // namespace cw
