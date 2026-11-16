#pragma once

#include "cw/formats/code_notes.hpp"

namespace cw {

// One explicit note edit, bound to the notes session/address where it was opened. The shared
// CodeNotes service remains the only writer. Failures keep the user's draft available.
class CodeNoteDraft {
  public:
    bool begin(const CodeNotes& notes, uint32_t address, std::string& error);
    bool save(CodeNotes& notes, std::string& error);
    void cancel();
    bool open() const { return open_; }
    bool dirty() const { return open_ && text_ != initial_; }
    std::optional<uint32_t> game_id() const { return game_id_; }
    uint32_t address() const { return address_; }
    std::string& text() { return text_; }
    const std::string& text() const { return text_; }
    const std::optional<CodeNote>& server_note() const { return server_note_; }

  private:
    bool open_ = false;
    const CodeNotes* source_ = nullptr;
    std::optional<uint32_t> game_id_;
    uint64_t session_version_ = 0;
    uint32_t address_ = 0;
    std::optional<std::string> original_user_note_;
    std::optional<CodeNote> server_note_;
    std::string initial_;
    std::string text_;
};

} // namespace cw
