#pragma once

#include <filesystem>
#include <string>

namespace cw {

// User preferences for the interface. The on-disk format starts with
// "# cheevo-workbench ui settings v1", followed by one "<key><TAB><value>" per line.
// A missing file, malformed values and unknown keys all fall back to the defaults.
struct UiSettings {
    static constexpr int kDefaultFontSize = 13;
    static constexpr int kMinFontSize = 10;
    static constexpr int kMaxFontSize = 48;

    int font_size = kDefaultFontSize;
    std::filesystem::path font_path; // empty selects the built-in font
    unsigned int font_index = 0;

    friend bool operator==(const UiSettings&, const UiSettings&) = default;
};

int clamp_font_size(int size);

UiSettings load_ui_settings(const std::filesystem::path& file);

// Writes atomically, so a crash never leaves a half-written file. Returns false with `error` when
// the file cannot be written or the font path cannot be stored.
bool save_ui_settings(const std::filesystem::path& file, const UiSettings& settings,
                      std::string& error);

} // namespace cw
