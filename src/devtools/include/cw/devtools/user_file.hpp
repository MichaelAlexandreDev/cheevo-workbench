#pragma once

#include "cw/devtools/achievement_logic.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cw {

// Private token metadata preserves unused fields, future trailing fields, and original quoting.
class UserAssetText {
    std::vector<std::string> tokens_;
    std::vector<std::string> values_;
    std::string tail_;
    friend struct UserFileFormat;
};

struct LocalAchievement {
    uint32_t id = 0; // Zero requests a new local ID when RAIntegration imports the file.
    std::optional<uint32_t> subset_id;
    AchievementLogic logic;
    std::string title;
    std::string description;
    // Empty, missable, progression, win_condition, or a preserved unknown/numeric type.
    std::string type;
    std::string author;
    uint32_t points = 5;
    std::string badge = "00000";

  private:
    UserAssetText original_;
    friend struct UserFileFormat;
};

struct LocalLeaderboard {
    uint32_t id = 0;
    std::optional<uint32_t> subset_id;
    AchievementLogic start;
    AchievementLogic cancel;
    AchievementLogic submit;
    // Value syntax differs from achievement logic; retain text and validate with rc_parse_value.
    std::string value;
    // Kept verbatim: unknown formats fall back to VALUE in RAIntegration/rcheevos.
    std::string format = "VALUE";
    std::string title;
    std::string description;
    bool lower_is_better = false;

  private:
    UserAssetText original_;
    friend struct UserFileFormat;
};

// Notes, regions, BadgeDir, blank lines and unknown records are passthrough, not authored here.
class PreservedUserLine {
  public:
    const std::string& text() const { return text_; }

  private:
    std::string text_;
    friend struct UserFileFormat;
};

struct UserFileEntry {
    std::variant<LocalAchievement, LocalLeaderboard, PreservedUserLine> asset;

  private:
    std::string ending_ = "\n";
    bool has_original_ending_ = false;
    friend struct UserFileFormat;
};

struct UserFileDocument {
    // The first line identifies the writing program, not a schema version; RAIntegration skips it.
    std::string version = "cheevo-workbench 0.1.0";
    std::string title;
    std::vector<UserFileEntry> entries;

  private:
    std::string version_ending_ = "\n";
    std::string title_ending_ = "\n";
    std::string default_ending_ = "\n";
    friend struct UserFileFormat;
};

inline constexpr size_t kMaxUserFileSize = 16 * 1024 * 1024;
// Failures are transactional and include line/field information. Known malformed records are
// rejected; unknown records and unused fields are preserved. Neither function accesses disk.
std::optional<UserFileDocument> parse_user_file(std::string_view text, std::string& error);
std::optional<std::string> serialize_user_file(const UserFileDocument& document,
                                               std::string& error);
// Validates the value grammar with complete consumption. Empty local-file drafts are allowed;
// runtime activation additionally requires a nonempty expression.
bool validate_leaderboard_value(std::string_view value, std::string& error);
std::filesystem::path ra_user_file_path(const std::filesystem::path& racache, uint32_t game_id);

} // namespace cw
