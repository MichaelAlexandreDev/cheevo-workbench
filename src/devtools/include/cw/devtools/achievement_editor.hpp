#pragma once

#include "cw/formats/user_file_store.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cw {

// Offline editing session. The document is independent of the running game and never changes
// its target on a game switch. Mutable fields are drafts: call changed() after an edit.
class AchievementEditor {
  public:
    bool load(const std::filesystem::path& path, std::string& error);
    bool save(std::string& error);
    void discard_changes();
    bool loaded() const { return loaded_; }
    bool dirty() const { return dirty_ || !input_error_.empty(); }
    const std::filesystem::path& path() const { return store_.path(); }
    const UserFileDocument& document() const { return document_; }
    UserFileDocument& document() { return document_; }
    std::optional<size_t> selection() const { return selection_; }
    LocalAchievement* selected();
    const LocalAchievement* selected() const;
    bool select(size_t document_index, std::string& error);
    bool add_achievement(std::string& error);
    bool remove_selected(std::string& error);
    // A rejected text edit must block saving and selection, not fall back to the last number.
    void set_input_error(std::string error);
    const std::string& input_error() const { return input_error_; }
    void changed();
    const std::string& validation_error() const { return validation_error_; }
    bool can_save() const;
    // Changes when selection/document structure is replaced, allowing the UI to reset buffers.
    uint64_t revision() const { return revision_; }
    // Identity of the selection. Reload/discard/delete can replace an asset at the same index;
    // saving metadata does not replace its identity for a live test.
    uint64_t selection_version() const { return selection_version_; }
    // Changes on field/input edits. Together with selection_version(), lets readers cache
    // derived logic instead of serializing and parsing it at the UI refresh rate.
    uint64_t edit_version() const { return edit_version_; }

  private:
    void validate();
    void select_first();
    UserFileStore store_;
    UserFileDocument document_;
    std::optional<size_t> selection_;
    bool loaded_ = false;
    bool dirty_ = false;
    std::string input_error_;
    std::string validation_error_;
    uint64_t revision_ = 0;
    uint64_t selection_version_ = 0;
    uint64_t edit_version_ = 0;
};

// Exact direct addresses only. AddAddress makes the following row's memory operands offsets;
// the current notes model does not resolve pointer chains or structured note subranges.
std::optional<uint32_t> achievement_note_address(const LogicGroup& group, size_t row,
                                                 bool right_operand);

} // namespace cw
