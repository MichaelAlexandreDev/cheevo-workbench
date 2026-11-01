#include "ui_fonts.hpp"

#include <imgui.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace cw {
namespace {

// Fontconfig is optional. Resolve it at runtime so installations without it still start.
struct FcPattern;
struct FcCharSet;
struct FcConfig;
struct FcFontSet {
    int nfont;
    int sfont;
    FcPattern** fonts;
};
using FcChar8 = unsigned char;

struct FontFile {
    std::string path;
    unsigned int index = 0;
};

FontFile fontconfig_japanese_font() {
    void* library = dlopen("libfontconfig.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!library)
        return {};

    const auto name_parse =
        reinterpret_cast<FcPattern* (*)(const FcChar8*)>(dlsym(library, "FcNameParse"));
    const auto config_substitute =
        reinterpret_cast<int (*)(FcConfig*, FcPattern*, int)>(dlsym(library, "FcConfigSubstitute"));
    const auto default_substitute =
        reinterpret_cast<void (*)(FcPattern*)>(dlsym(library, "FcDefaultSubstitute"));
    const auto font_sort =
        reinterpret_cast<FcFontSet* (*)(FcConfig*, FcPattern*, int, FcCharSet**, int*)>(
            dlsym(library, "FcFontSort"));
    const auto pattern_string =
        reinterpret_cast<int (*)(const FcPattern*, const char*, int, FcChar8**)>(
            dlsym(library, "FcPatternGetString"));
    const auto pattern_integer =
        reinterpret_cast<int (*)(const FcPattern*, const char*, int, int*)>(
            dlsym(library, "FcPatternGetInteger"));
    const auto pattern_charset =
        reinterpret_cast<int (*)(const FcPattern*, const char*, int, FcCharSet**)>(
            dlsym(library, "FcPatternGetCharSet"));
    const auto has_char = reinterpret_cast<int (*)(const FcCharSet*, unsigned int)>(
        dlsym(library, "FcCharSetHasChar"));
    const auto destroy_pattern =
        reinterpret_cast<void (*)(FcPattern*)>(dlsym(library, "FcPatternDestroy"));
    const auto destroy_set =
        reinterpret_cast<void (*)(FcFontSet*)>(dlsym(library, "FcFontSetDestroy"));

    FontFile result;
    if (name_parse && config_substitute && default_substitute && font_sort && pattern_string &&
        pattern_integer && pattern_charset && has_char && destroy_pattern && destroy_set) {
        FcPattern* pattern = name_parse(reinterpret_cast<const FcChar8*>("sans:lang=ja"));
        if (pattern) {
            config_substitute(nullptr, pattern, 0); // FcMatchPattern
            default_substitute(pattern);
            int status = 0;
            FcFontSet* fonts = font_sort(nullptr, pattern, 0, nullptr, &status);
            if (fonts) {
                for (int i = 0; i < fonts->nfont; ++i) {
                    FcCharSet* charset = nullptr;
                    FcChar8* path = nullptr;
                    FcChar8* format = nullptr;
                    int index = 0;
                    FcPattern* candidate = fonts->fonts[i];
                    if (pattern_charset(candidate, "charset", 0, &charset) != 0 || !charset ||
                        !has_char(charset, 0x3042) || !has_char(charset, 0x30a2) ||
                        !has_char(charset, 0x65e5) ||
                        pattern_string(candidate, "file", 0, &path) != 0 || !path ||
                        pattern_string(candidate, "fontformat", 0, &format) != 0 || !format ||
                        (std::strcmp(reinterpret_cast<const char*>(format), "TrueType") != 0 &&
                         std::strcmp(reinterpret_cast<const char*>(format), "CFF") != 0))
                        continue;
                    pattern_integer(candidate, "index", 0, &index);
                    // High bits encode named instances, unsupported by stb_truetype.
                    if (index < 0 || index > 0xffff)
                        continue;
                    result = {reinterpret_cast<const char*>(path),
                              static_cast<unsigned int>(index)};
                    break;
                }
                destroy_set(fonts);
            }
            destroy_pattern(pattern);
        }
    }
    dlclose(library);
    return result;
}

FontFile known_japanese_font() {
    constexpr std::array paths{
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/droid/DroidSansJapanese.ttf",
        "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
    };
    for (const char* path : paths) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
            return {path, 0};
    }
    return {};
}

bool merge_font(ImFontAtlas& atlas, const FontFile& file) {
    // Read once ourselves so a missing/unreadable file remains a normal fallback case.
    std::ifstream input(file.path, std::ios::binary | std::ios::ate);
    if (!input)
        return false;
    const auto length = input.tellg();
    if (length <= 100 || length > std::numeric_limits<int>::max())
        return false;
    input.seekg(0);
    void* data = IM_ALLOC(static_cast<size_t>(length));
    if (!data)
        return false;
    if (!input.read(static_cast<char*>(data), length)) {
        IM_FREE(data);
        return false;
    }
    ImFontConfig config;
    config.MergeMode = true;
    config.FontNo = file.index;
    // Dynamic ImGui backends do not need glyph ranges; all available glyphs can be loaded.
    // Ownership transfers even if adding the font fails; ImGui rolls back and frees it.
    return atlas.AddFontFromMemoryTTF(data, static_cast<int>(length), 0.0f, &config) != nullptr;
}

} // namespace

std::string configure_ui_fonts(ImFontAtlas& atlas, bool discover_system_font) {
    atlas.AddFontDefault();
    if (!discover_system_font)
        return {};
    FontFile font = fontconfig_japanese_font();
    if (!font.path.empty() && merge_font(atlas, font))
        return font.path;
    font = known_japanese_font();
    if (!font.path.empty() && merge_font(atlas, font))
        return font.path;
    return {};
}

} // namespace cw
