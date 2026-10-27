#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace cw {

// The RetroAchievements game ID the user entered for each content hash. Without server access
// (AGENTS.md hard rule 1) the hash cannot be resolved online, so the user types the ID once
// (the number in the game's retroachievements.org URL, e.g. 18238) and it is remembered.
//
// File format (one entry per line, after a version header):
//   # cheevo-workbench game ids v1
//   <32 lowercase hex hash><TAB><decimal game id>
// A missing or malformed file loads as empty; malformed lines are skipped.
class GameIds {
  public:
    // `file` is normally config_dir() / "game-ids.txt"; an empty path disables persistence.
    explicit GameIds(std::filesystem::path file) : file_(std::move(file)) {}

    void load();
    std::optional<uint32_t> find(std::string_view hash) const;
    // Stores the ID (0 removes it) and saves the file. Returns false with `error` for an invalid
    // hash or when the file cannot be written; then nothing changes, in the file or in memory.
    // Two running instances editing at once are not coordinated: the last save wins.
    bool set(std::string_view hash, uint32_t game_id, std::string& error);

    static bool is_valid_hash(std::string_view hash);

  private:
    bool save(const std::map<std::string, uint32_t, std::less<>>& ids, std::string& error) const;

    std::filesystem::path file_;
    std::map<std::string, uint32_t, std::less<>> ids_;
};

} // namespace cw
