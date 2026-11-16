#pragma once

#include "cw/formats/user_file_store.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace cw {

// Text fields stay editable while incomplete. Triggers use achievement logic syntax; value is
// parsed independently by rcheevos because its leaderboard-value grammar is different.
struct LeaderboardDraft {
    std::string title;
    std::string description;
    std::string start;
    std::string cancel;
    std::string submit;
    std::string value;
    std::string format;
    bool lower_is_better = false;
};

// Serializes and validates the full rcheevos leaderboard definition in canonical field order.
// The returned text is suitable for rc_parse_lboard/rc_lboard_size.
std::optional<std::string> leaderboard_definition(const LocalLeaderboard& leaderboard,
                                                  std::string& error);

// Offline editor session for leaderboard records in a RAIntegration User.txt file.
class LeaderboardEditor {
  public:
    bool load(const std::filesystem::path& path, std::string& error);
    bool save(std::string& error);
    void discard_changes();
    bool loaded() const { return loaded_; }
    bool dirty() const { return dirty_; }
    bool can_save() const;
    const std::filesystem::path& path() const { return store_.path(); }
    // True until the first Save creates the file on disk.
    bool new_file() const { return store_.missing(); }
    const UserFileDocument& document() const { return document_; }
    std::optional<size_t> selection() const { return selection_; }
    bool select(size_t document_index, std::string& error);
    bool add_leaderboard(std::string& error);
    bool remove_selected(std::string& error);
    LeaderboardDraft& draft() { return draft_; }
    const LeaderboardDraft& draft() const { return draft_; }
    void changed();
    const std::string& validation_error() const { return validation_error_; }
    std::optional<LocalLeaderboard> selected_definition(std::string& error) const;
    uint64_t selection_version() const { return selection_version_; }
    uint64_t edit_version() const { return edit_version_; }

  private:
    std::optional<LocalLeaderboard> make_selected_definition(std::string& error) const;
    bool apply_draft(UserFileDocument& document, std::string& error) const;
    void validate();
    void load_draft_from_selection();
    void select_first();

    UserFileStore store_;
    UserFileDocument document_;
    std::optional<size_t> selection_;
    LeaderboardDraft draft_;
    LeaderboardDraft loaded_draft_;
    std::string original_format_;
    bool loaded_ = false;
    bool dirty_ = false;
    std::string validation_error_;
    uint64_t selection_version_ = 0;
    uint64_t edit_version_ = 0;
};

} // namespace cw
