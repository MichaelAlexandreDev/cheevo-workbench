// Code notes: each case directory holds data/notes/<id>.json (ours, editable) and/or
// racache/Data/<id>-Notes.json (RAIntegration's server cache, read-only). Both are loaded, every
// parsed note is listed, then one note is set and the last one removed, which rewrites our file.

#include "corpus_support.hpp"

#include "cw/formats/code_notes.hpp"

#include <cstdio>

namespace cw_corpus {
namespace {

std::string describe(const cw::CodeNote& note) {
    char address[16];
    std::snprintf(address, sizeof(address), "0x%06x", note.address);
    std::string text = std::string(address) + " \"" + note.note + "\"";
    if (!note.author.empty()) {
        text += " by " + note.author;
    }
    return text;
}

void list(const cw::CodeNotes& notes, std::string_view name, Results& results) {
    if (!notes.user_error().empty()) {
        results.add("notes", name, "user file error: " + notes.user_error());
    }
    if (!notes.server_error().empty()) {
        results.add("notes", name, "server cache error: " + notes.server_error());
    }
    for (const auto& note : notes.user_notes()) {
        results.add("notes", name, "user " + describe(note));
    }
    for (const auto& note : notes.server_notes()) {
        results.add("notes", name, "server " + describe(note));
    }
    for (const auto& note : notes.notes_to_publish()) {
        results.add("notes", name, "to publish " + describe(note));
    }
}

} // namespace

void run_code_notes(const std::filesystem::path& input, const std::filesystem::path& out,
                    Results& results) {
    for (const auto& source : dirs_in(input / "notes")) {
        const std::string name = source.filename().string();
        const auto dir = out / "notes" / name;
        copy_tree(source, dir);

        cw::CodeNotes notes;
        notes.load(kGameId, dir / "data", dir / "racache");
        list(notes, name, results);

        std::string error;
        if (!notes.set_note(0x10, "Edited by the corpus:\nsecond line", error)) {
            results.add("notes", name, "set refused: " + error);
        }
        if (!notes.user_notes().empty()) {
            const uint32_t last = notes.user_notes().back().address;
            if (!notes.remove_note(last, error)) {
                results.add("notes", name, "remove refused: " + error);
            }
        }
    }
}

} // namespace cw_corpus
