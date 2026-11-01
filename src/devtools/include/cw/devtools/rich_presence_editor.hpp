#pragma once

#include "cw/devtools/text_file_store.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace cw {

inline constexpr size_t kMaxRichPresenceSize = 65535;

// Applies RAIntegration's local Rich Presence normalization: strip an initial UTF-8 BOM, remove
// carriage returns, convert whitespace-only scripts to empty, and ensure non-empty scripts end
// with LF.
std::string normalize_rich_presence(std::string_view script);

// Validates normalized script bytes with the pinned rcheevos parser. Empty scripts are valid and
// clear the preview. Parse errors include rcheevos' 1-based line number.
bool validate_rich_presence(std::string_view script, std::string& error);

std::filesystem::path ra_rich_presence_path(const std::filesystem::path& racache, uint32_t game_id);

// Draft editing for one RACache Data/<GameID>-Rich.txt file. Loading never writes; malformed
// scripts can be corrected in the editor, but only a validated script can be saved.
class RichPresenceEditor {
  public:
    bool load(const std::filesystem::path& path, std::string& error);
    void set_text(std::string text);

    const std::string& text() const { return text_; }
    bool loaded() const { return loaded_; }
    bool dirty() const { return dirty_; }
    const std::filesystem::path& path() const { return store_.path(); }
    const std::string& validation_error() const { return validation_error_; }
    bool can_save() const;
    bool save(std::string& error);
    void discard_changes();
    uint64_t revision() const { return revision_; }

  private:
    void update_validation();

    TextFileStore store_;
    std::string text_;
    std::string saved_text_;
    std::string validation_error_;
    bool loaded_ = false;
    bool dirty_ = false;
    uint64_t revision_ = 0;
};

} // namespace cw
