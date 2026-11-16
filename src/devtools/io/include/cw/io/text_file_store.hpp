#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace cw {

// Explicit, bounded access to one text file. A failed load disables saves; a missing file is
// writable but is not created until save(). Saves compare the original snapshot before staging
// and immediately before replacement. This detects ordinary external edits, but is not a lock
// shared with another application writing at exactly the same time.
class TextFileStore {
  public:
    bool load(const std::filesystem::path& path, size_t max_bytes, std::string& error);
    bool save(std::string_view bytes, std::string& error);

    const std::string& bytes() const { return bytes_; }
    const std::filesystem::path& path() const { return path_; }
    bool writable() const { return writable_; }
    bool missing() const { return missing_; }

  private:
    std::filesystem::path path_;
    std::string bytes_;
    std::optional<std::string> loaded_bytes_;
    size_t max_bytes_ = 0;
    bool missing_ = false;
    bool writable_ = false;
};

} // namespace cw
