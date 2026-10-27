#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cw {

// A single code note. `author` is populated for notes read from the RAIntegration cache.
struct CodeNote {
    uint32_t address = 0;
    std::string note;
    std::string author;

    friend bool operator==(const CodeNote&, const CodeNote&) = default;
};

// Loads our editable notes and RAIntegration's server cache without depending on the frontend.
// The server cache is strictly read-only. The user's file is:
//   {data_dir}/notes/{GameID}.json
// with this versioned format:
//   {"format":"cheevo-workbench-code-notes","version":1,"game_id":18238,
//    "notes":[{"address":"0x0D9C24","note":"free-form UTF-8 text"}]}
class CodeNotes {
  public:
    // Loads both sources for `game_id`. A missing user file or server cache is an empty list.
    // Parse and I/O errors are exposed separately; malformed user data blocks every save so it
    // cannot be silently replaced with an empty file.
    void load(std::optional<uint32_t> game_id, const std::filesystem::path& data_dir,
              const std::filesystem::path& ra_cache_dir);

    bool set_note(uint32_t address, std::string note, std::string& error);
    bool remove_note(uint32_t address, std::string& error);

    std::optional<uint32_t> game_id() const { return game_id_; }
    const std::vector<CodeNote>& user_notes() const { return user_notes_; }
    const std::vector<CodeNote>& server_notes() const { return server_notes_; }
    const std::string& user_error() const { return user_error_; }
    const std::string& server_error() const { return server_error_; }

    const CodeNote* user_note_at(uint32_t address) const;
    const CodeNote* server_note_at(uint32_t address) const;
    // User notes take precedence for compact displays such as search results and the watch list.
    const CodeNote* display_note_at(uint32_t address) const;

    // Only new or changed user notes are included. A server note absent from the user file is
    // not a deletion request and therefore does not appear here.
    std::vector<CodeNote> notes_to_publish() const;

    static std::filesystem::path user_file_path(const std::filesystem::path& data_dir,
                                                uint32_t game_id);
    static std::filesystem::path server_file_path(const std::filesystem::path& ra_cache_dir,
                                                  uint32_t game_id);
    // Accepts hexadecimal digits with or without a 0x prefix, matching RAIntegration's base-16
    // interpretation while rejecting trailing characters and values wider than uint32_t.
    static bool parse_hex_address(std::string_view text, uint32_t& address);

  private:
    bool save_user_notes(const std::vector<CodeNote>& notes, std::string& error);

    std::optional<uint32_t> game_id_;
    std::filesystem::path user_file_;
    bool user_file_valid_ = true;
    std::vector<CodeNote> user_notes_;
    std::vector<CodeNote> server_notes_;
    std::string user_error_;
    std::string server_error_;
};

} // namespace cw
