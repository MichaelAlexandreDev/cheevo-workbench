#include "ui_settings.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace cw {
namespace {

constexpr std::string_view kHeader = "# cheevo-workbench ui settings v1";

template <typename T> bool parse_number(std::string_view text, T& value) {
    const char* const end = text.data() + text.size();
    const auto [parsed_end, error] = std::from_chars(text.data(), end, value);
    return error == std::errc{} && parsed_end == end;
}

} // namespace

int clamp_font_size(int size) {
    return std::clamp(size, UiSettings::kMinFontSize, UiSettings::kMaxFontSize);
}

UiSettings load_ui_settings(const std::filesystem::path& file) {
    UiSettings settings;
    if (file.empty())
        return settings;
    std::ifstream input(file);
    std::string line;
    if (!input || !std::getline(input, line) || line != kHeader)
        return settings;
    while (std::getline(input, line)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos)
            continue;
        const std::string_view key = std::string_view(line).substr(0, tab);
        const std::string_view value = std::string_view(line).substr(tab + 1);
        if (key == "font_size") {
            int size = 0;
            if (parse_number(value, size))
                settings.font_size = clamp_font_size(size);
        } else if (key == "font_path") {
            const std::filesystem::path path{std::string(value)};
            if (path.is_absolute())
                settings.font_path = path;
        } else if (key == "font_index") {
            unsigned int index = 0;
            if (parse_number(value, index))
                settings.font_index = index;
        }
    }
    return settings;
}

bool save_ui_settings(const std::filesystem::path& file, const UiSettings& settings,
                      std::string& error) {
    if (file.empty()) {
        error = "The configuration directory is unavailable; the settings cannot be saved.";
        return false;
    }
    const std::string path = settings.font_path.string();
    if (path.find_first_of("\r\n") != std::string::npos) {
        error = "The font path cannot contain line breaks.";
        return false;
    }
    // The temporary name is unique per process and call, so instances never share one.
    static std::atomic<unsigned> counter{0};
    std::filesystem::path temp = file;
    temp += ".tmp." + std::to_string(::getpid()) + "." + std::to_string(counter++);
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        output << kHeader << '\n'
               << "font_size\t" << clamp_font_size(settings.font_size) << '\n'
               << "font_path\t" << path << '\n'
               << "font_index\t" << settings.font_index << '\n';
        output.close();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            error = "Could not write " + temp.string();
            return false;
        }
    }
    std::error_code rename_error;
    std::filesystem::rename(temp, file, rename_error);
    if (rename_error) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        error = "Could not write " + file.string() + ": " + rename_error.message();
        return false;
    }
    return true;
}

} // namespace cw
