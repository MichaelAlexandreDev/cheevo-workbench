#include "cw/devtools/rich_presence_editor.hpp"

#include <rc_error.h>
#include <rc_runtime_types.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace cw {
namespace {

constexpr std::string_view kUtf8Bom("\xEF\xBB\xBF", 3);
constexpr size_t kMaxRichPresenceFileSize = 2 * kMaxRichPresenceSize + kUtf8Bom.size();
constexpr uint8_t kUnknownMacroDisplayType =
    103; // Pinned rcheevos' private RC_FORMAT_UNKNOWN_MACRO.
constexpr size_t kMaxUnknownMacroNameInError = 80;

bool whitespace_only(std::string_view text) {
    return std::all_of(text.begin(), text.end(),
                       [](unsigned char character) { return std::isspace(character) != 0; });
}

bool reject_unknown_macros(const rc_richpresence_t& richpresence, std::string& error) {
    for (const rc_richpresence_display_t* display = richpresence.first_display; display;
         display = display->next) {
        for (const rc_richpresence_display_part_t* part = display->display; part;
             part = part->next) {
            if (part->display_type != kUnknownMacroDisplayType) {
                continue;
            }

            std::string_view macro = part->text ? part->text : "";
            if (const size_t arguments = macro.find('('); arguments != std::string_view::npos) {
                macro = macro.substr(0, arguments);
            }

            error = "Unknown Rich Presence macro '@";
            const size_t displayed_size = std::min(macro.size(), kMaxUnknownMacroNameInError);
            error.append(macro.data(), displayed_size);
            if (macro.size() > displayed_size) {
                error.append("...");
            }
            error.append("'.");
            return false;
        }
    }

    return true;
}

} // namespace

std::string normalize_rich_presence(std::string_view script) {
    if (script.starts_with(kUtf8Bom)) {
        script.remove_prefix(kUtf8Bom.size());
    }

    if (whitespace_only(script)) {
        return {};
    }

    std::string normalized;
    normalized.reserve(script.size() + 1);
    for (const char character : script) {
        if (character != '\r') {
            normalized.push_back(character);
        }
    }

    if (!normalized.empty() && normalized.back() != '\n') {
        normalized.push_back('\n');
    }
    return normalized;
}

bool validate_rich_presence(std::string_view script, std::string& error) {
    if (script.find('\0') != std::string_view::npos) {
        error = "Rich Presence script contains an embedded NUL byte.";
        return false;
    }

    const std::string normalized = normalize_rich_presence(script);
    if (normalized.size() > kMaxRichPresenceSize) {
        error = "Rich Presence script exceeds the 65,535-byte limit.";
        return false;
    }
    if (normalized.empty()) {
        error.clear();
        return true;
    }

    int line = 0;
    const int parsed_size = rc_richpresence_size_lines(normalized.c_str(), &line);
    if (parsed_size < 0) {
        error = "Parse error " + std::to_string(parsed_size) + " (line " + std::to_string(line) +
                "): " + rc_error_str(parsed_size);
        return false;
    }

    std::vector<uint8_t> parse_buffer(static_cast<size_t>(parsed_size));
    const rc_richpresence_t* parsed =
        rc_parse_richpresence(parse_buffer.data(), normalized.c_str(), nullptr, 0);
    if (!parsed) {
        error = "Could not parse Rich Presence script.";
        return false;
    }
    if (!reject_unknown_macros(*parsed, error)) {
        return false;
    }

    error.clear();
    return true;
}

std::filesystem::path ra_rich_presence_path(const std::filesystem::path& racache,
                                            uint32_t game_id) {
    if (racache.empty() || game_id == 0) {
        return {};
    }
    return racache / "Data" / (std::to_string(game_id) + "-Rich.txt");
}

bool RichPresenceEditor::load(const std::filesystem::path& path, std::string& error) {
    if (dirty_) {
        error =
            "Rich Presence has unsaved changes; save or discard them before loading another file.";
        return false;
    }

    TextFileStore candidate_store;
    if (!candidate_store.load(path, kMaxRichPresenceFileSize, error)) {
        return false;
    }

    std::string candidate_text = normalize_rich_presence(candidate_store.bytes());
    store_ = std::move(candidate_store);
    text_ = std::move(candidate_text);
    saved_text_ = text_;
    loaded_ = true;
    dirty_ = false;
    update_validation();
    ++revision_;
    error.clear();
    return true;
}

void RichPresenceEditor::set_text(std::string text) {
    if (text == text_) {
        return;
    }

    text_ = std::move(text);
    dirty_ = !loaded_ || text_ != saved_text_;
    update_validation();
    ++revision_;
}

bool RichPresenceEditor::can_save() const {
    return loaded_ && store_.writable() && validation_error_.empty() &&
           (dirty_ || store_.missing());
}

bool RichPresenceEditor::save(std::string& error) {
    if (!loaded_ || !store_.writable()) {
        error = "Rich Presence editor is not writable; load a valid path before saving.";
        return false;
    }

    const std::string normalized = normalize_rich_presence(text_);
    if (!validate_rich_presence(normalized, error)) {
        validation_error_ = error;
        return false;
    }

    if (!store_.save(normalized, error)) {
        return false;
    }

    text_ = normalized;
    saved_text_ = normalized;
    dirty_ = false;
    validation_error_.clear();
    ++revision_;
    error.clear();
    return true;
}

void RichPresenceEditor::discard_changes() {
    text_ = saved_text_;
    dirty_ = false;
    update_validation();
    ++revision_;
}

void RichPresenceEditor::update_validation() {
    validate_rich_presence(text_, validation_error_);
}

} // namespace cw
