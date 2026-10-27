#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace cw {

struct RecentFile {
    std::filesystem::path core;
    std::filesystem::path game;

    friend bool operator==(const RecentFile&, const RecentFile&) = default;
};

// A small, SDL-free recent core/content list. The on-disk format starts with
// "# cheevo-workbench recent files v1" and stores one tab-separated pair of hex-encoded native
// paths per line. An empty game path is written as "-". The encoding keeps arbitrary path bytes,
// tabs, and newlines out of the record structure.
class RecentFiles {
  public:
    static constexpr std::size_t kMaxEntries = 10;

    // `file` is normally config_dir() / "recent-files.txt"; an empty path disables persistence.
    explicit RecentFiles(std::filesystem::path file);

    // A missing or malformed file produces an empty list. No I/O error throws.
    void load();
    void add(const std::filesystem::path& core, const std::filesystem::path& game = {});
    bool save(std::string& error) const;

    const std::vector<RecentFile>& entries() const { return entries_; }
    const std::filesystem::path& file_path() const { return file_; }

  private:
    std::filesystem::path file_;
    std::vector<RecentFile> entries_;
};

} // namespace cw
