#pragma once

// Format corpus (decision D9 in docs/planning/refactor-plan.md): loads every synthetic fixture
// through the public API the application uses, saves it again and writes what came out, so a
// refactoring can prove it left the on-disk bytes alone.
//
// Layout of `input` (see tests/fixtures/corpus/README.md):
//   user/<case>.txt         RACache Data/<id>-User.txt files
//   rich/<case>.txt         RACache Data/<id>-Rich.txt files
//   bookmarks/<case>.json   RACache Bookmarks/<id>-Bookmarks.json files
//   notes/<case>/data/...   our notes/<id>.json, and racache/Data/<id>-Notes.json
//   app/recent/<case>.txt, app/game-ids/<case>.txt, app/ui-settings/<case>.txt
//                           the application's own configuration files
// RASTATE save states are generated in code (state files must not be committed).
//
// Everything is written under `out`, plus `out/results.txt` with one line per observation.
// The output is deterministic: no dates, no absolute paths, cases in name order.

#include <filesystem>
#include <string>

namespace cw_corpus {

// Returns false only when the corpus itself cannot run (unreadable input, unwritable output);
// a fixture the application rejects is a result, not a failure.
bool run(const std::filesystem::path& input, const std::filesystem::path& out, std::string& error);

} // namespace cw_corpus
