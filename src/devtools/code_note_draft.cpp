#include "cw/devtools/code_note_draft.hpp"

namespace cw {

bool CodeNoteDraft::begin(const CodeNotes& notes, uint32_t address, std::string& error) {
    if (dirty()) {
        error = "Save or cancel the current note draft before editing another note.";
        return false;
    }
    if (!notes.game_id()) {
        error = "Set the game ID in Status before editing code notes.";
        return false;
    }
    game_id_ = notes.game_id();
    source_ = &notes;
    session_version_ = notes.session_version();
    address_ = address;
    const auto* user = notes.user_note_at(address);
    original_user_note_ = user ? std::optional(user->note) : std::nullopt;
    const auto* server = notes.server_note_at(address);
    server_note_ = server ? std::optional(*server) : std::nullopt;
    initial_ = user ? user->note : server ? server->note : "";
    text_ = initial_;
    open_ = true;
    error.clear();
    return true;
}

bool CodeNoteDraft::save(CodeNotes& notes, std::string& error) {
    if (!open_) {
        error = "Open a note draft before saving.";
        return false;
    }
    if (source_ != &notes || game_id_ != notes.game_id() ||
        session_version_ != notes.session_version()) {
        error = "The code notes game or storage changed. Copy this draft, cancel and reopen it "
                "for the intended game.";
        return false;
    }
    const auto* current = notes.user_note_at(address_);
    const auto current_text = current ? std::optional(current->note) : std::nullopt;
    if (current_text != original_user_note_) {
        error = "This note changed in another tool. Copy this draft, cancel and reopen the note "
                "before replacing it.";
        return false;
    }
    if (!notes.set_note(address_, text_, error))
        return false;
    cancel();
    return true;
}

void CodeNoteDraft::cancel() {
    open_ = false;
    source_ = nullptr;
    game_id_.reset();
    original_user_note_.reset();
    server_note_.reset();
    initial_.clear();
    text_.clear();
}

} // namespace cw
