#include "ui_fonts.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace cw {
namespace {

// Fontconfig is optional. Resolve it at runtime so installations without it still start.
struct FcPattern;
struct FcObjectSet;
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

// Kept loaded for the whole process: fontconfig builds a large global configuration on first use
// and only frees it through FcFini, which is unsafe if anything else in the process uses it.
// Unloading would leak that configuration and rebuild it on every query.
void* fontconfig_library() {
    static void* const library = dlopen("libfontconfig.so.1", RTLD_LAZY | RTLD_LOCAL);
    return library;
}

FontFile fontconfig_japanese_font() {
    void* library = fontconfig_library();
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

bool has_font_signature(const void* data) {
    constexpr std::array<std::array<char, 4>, 4> signatures{{{{'\0', '\1', '\0', '\0'}},
                                                             {{'O', 'T', 'T', 'O'}},
                                                             {{'t', 'r', 'u', 'e'}},
                                                             {{'t', 't', 'c', 'f'}}}};
    for (const auto& signature : signatures) {
        if (std::memcmp(data, signature.data(), signature.size()) == 0)
            return true;
    }
    return false;
}

bool add_font_file(ImFontAtlas& atlas, const FontFile& file, bool merge) {
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
    if (!input.read(static_cast<char*>(data), length) || !has_font_signature(data)) {
        IM_FREE(data);
        return false;
    }
    ImFontConfig config;
    config.MergeMode = merge;
    config.FontNo = file.index;
    // A user-chosen file may still be damaged: ImGui reports that as a failed load, and without
    // this it would also raise an assert.
    ImGuiIO& io = ImGui::GetIO();
    const bool assert_on_error = io.ConfigErrorRecoveryEnableAssert;
    io.ConfigErrorRecoveryEnableAssert = false;
    // Dynamic ImGui backends do not need glyph ranges; all available glyphs can be loaded.
    // Ownership transfers even if adding the font fails; ImGui rolls back and frees it.
    const bool added =
        atlas.AddFontFromMemoryTTF(data, static_cast<int>(length), 0.0f, &config) != nullptr;
    io.ConfigErrorRecoveryEnableAssert = assert_on_error;
    return added;
}

} // namespace

std::vector<UiFontFile> list_system_fonts() {
    std::vector<UiFontFile> result;
    void* library = fontconfig_library();
    if (!library)
        return result;

    const auto pattern_create =
        reinterpret_cast<FcPattern* (*)()>(dlsym(library, "FcPatternCreate"));
    const auto object_set_build =
        reinterpret_cast<FcObjectSet* (*)(const char*, ...)>(dlsym(library, "FcObjectSetBuild"));
    const auto font_list = reinterpret_cast<FcFontSet* (*)(FcConfig*, FcPattern*, FcObjectSet*)>(
        dlsym(library, "FcFontList"));
    const auto pattern_string =
        reinterpret_cast<int (*)(const FcPattern*, const char*, int, FcChar8**)>(
            dlsym(library, "FcPatternGetString"));
    const auto pattern_integer =
        reinterpret_cast<int (*)(const FcPattern*, const char*, int, int*)>(
            dlsym(library, "FcPatternGetInteger"));
    const auto destroy_object_set =
        reinterpret_cast<void (*)(FcObjectSet*)>(dlsym(library, "FcObjectSetDestroy"));
    const auto destroy_pattern =
        reinterpret_cast<void (*)(FcPattern*)>(dlsym(library, "FcPatternDestroy"));
    const auto destroy_set =
        reinterpret_cast<void (*)(FcFontSet*)>(dlsym(library, "FcFontSetDestroy"));

    if (pattern_create && object_set_build && font_list && pattern_string && pattern_integer &&
        destroy_object_set && destroy_pattern && destroy_set) {
        FcPattern* pattern = pattern_create();
        FcObjectSet* objects = object_set_build("family", "style", "file", "index", "fontformat",
                                                static_cast<const char*>(nullptr));
        if (pattern && objects) {
            if (FcFontSet* fonts = font_list(nullptr, pattern, objects)) {
                for (int i = 0; i < fonts->nfont; ++i) {
                    const FcPattern* font = fonts->fonts[i];
                    FcChar8* family = nullptr;
                    FcChar8* style = nullptr;
                    FcChar8* path = nullptr;
                    FcChar8* format = nullptr;
                    int index = 0;
                    if (pattern_string(font, "family", 0, &family) != 0 || !family ||
                        pattern_string(font, "file", 0, &path) != 0 || !path ||
                        pattern_string(font, "fontformat", 0, &format) != 0 || !format ||
                        (std::strcmp(reinterpret_cast<const char*>(format), "TrueType") != 0 &&
                         std::strcmp(reinterpret_cast<const char*>(format), "CFF") != 0))
                        continue;
                    pattern_integer(font, "index", 0, &index);
                    // High bits encode named instances, unsupported by stb_truetype.
                    if (index < 0 || index > 0xffff)
                        continue;
                    std::string label = reinterpret_cast<const char*>(family);
                    if (pattern_string(font, "style", 0, &style) == 0 && style)
                        label += std::string(" - ") + reinterpret_cast<const char*>(style);
                    result.push_back({std::move(label), reinterpret_cast<const char*>(path),
                                      static_cast<unsigned int>(index)});
                }
                destroy_set(fonts);
            }
        }
        if (objects)
            destroy_object_set(objects);
        if (pattern)
            destroy_pattern(pattern);
    }

    const auto folded = [](const std::string& text) {
        std::string lowered = text;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    };
    std::sort(result.begin(), result.end(), [&](const UiFontFile& a, const UiFontFile& b) {
        return std::pair(folded(a.label), a.path) < std::pair(folded(b.label), b.path);
    });
    result.erase(
        std::unique(result.begin(), result.end(),
                    [](const UiFontFile& a, const UiFontFile& b) { return a.label == b.label; }),
        result.end());
    return result;
}

UiFontResult configure_ui_fonts(ImFontAtlas& atlas, const std::filesystem::path& font_path,
                                unsigned int font_index, bool discover_system_font) {
    UiFontResult result;
    if (font_path.empty()) {
        atlas.AddFontDefault();
    } else if (!add_font_file(atlas, {font_path.string(), font_index}, false)) {
        result.error = "Could not load the font " + font_path.string() + "; using the default.";
        atlas.AddFontDefault();
    }
    if (!discover_system_font)
        return result;
    // Found once: the interface reloads its fonts on every font or size change.
    static const FontFile fontconfig_font = fontconfig_japanese_font();
    static const FontFile known_font = known_japanese_font();
    for (const FontFile* font : {&fontconfig_font, &known_font}) {
        if (!font->path.empty() && add_font_file(atlas, *font, true)) {
            result.japanese_font = font->path;
            break;
        }
    }
    return result;
}

} // namespace cw
