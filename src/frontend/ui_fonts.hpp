#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct ImFontAtlas;

namespace cw {

struct UiFontFile {
    std::string label; // "Family - Style", as shown to the user
    std::filesystem::path path;
    unsigned int index = 0; // face inside a font collection file
};

struct UiFontResult {
    std::string japanese_font; // merged fallback font path; empty if none was found
    std::string error;         // set when the chosen font failed and the default is used instead
};

// Fonts installed on the system, sorted by label. Empty when fontconfig is unavailable.
std::vector<UiFontFile> list_system_fonts();

// Adds the chosen font, or ImGui's default for an empty path or a file that fails to load. When
// available, a system Japanese font is merged as a fallback for glyphs the main font lacks.
UiFontResult configure_ui_fonts(ImFontAtlas& atlas, const std::filesystem::path& font_path = {},
                                unsigned int font_index = 0, bool discover_system_font = true);

} // namespace cw
