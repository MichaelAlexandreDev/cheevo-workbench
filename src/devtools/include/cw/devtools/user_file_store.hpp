#pragma once

#include "cw/devtools/text_file_store.hpp"
#include "cw/devtools/user_file.hpp"

#include <filesystem>
#include <string>

namespace cw {

// Explicit local-file access. Loading never creates or writes files. A failed load blocks saves;
// a missing file starts an empty document. Paths are fixed as absolute paths when loaded.
// A save checks for external changes before staging and before rename; this is not a lock
// shared with another application writing at exactly the same time.
class UserFileStore {
  public:
    bool load(const std::filesystem::path& path, std::string& error);
    bool save(const UserFileDocument& document, std::string& error);
    const UserFileDocument& document() const { return document_; }
    const std::filesystem::path& path() const { return text_store_.path(); }
    bool writable() const { return writable_; }
    bool missing() const { return text_store_.missing(); }

  private:
    TextFileStore text_store_;
    UserFileDocument document_;
    bool writable_ = false;
};

} // namespace cw
