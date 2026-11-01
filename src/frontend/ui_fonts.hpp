#pragma once

#include <string>

struct ImFontAtlas;

namespace cw {

// Adds ImGui's default font and, when available, merges a system Japanese font.
// Returns the selected font path, or an empty string if no Japanese font was found.
std::string configure_ui_fonts(ImFontAtlas& atlas, bool discover_system_font = true);

} // namespace cw
