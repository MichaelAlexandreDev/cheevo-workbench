// The application's own configuration files: recent-files.txt, game-ids.txt and ui-settings.txt.
// Each fixture is copied, loaded, saved unchanged, then edited and saved, so both the passthrough
// path and the path that writes new values are frozen. The paths inside the fixtures are made up.
// The directory settings (system-path.txt, ra-cache-path.txt) have no fixtures: only their writer
// is reachable, because the reader is still inside main.cpp.

#include "corpus_support.hpp"

#include "game_ids.hpp"
#include "recent_files.hpp"
#include "ui_settings.hpp"
#include "ui_widgets.hpp"

namespace cw_corpus {
namespace {

// Hashes the game-ids fixtures use: present, malformed in the file, and absent.
constexpr const char* kProbeHashes[] = {
    "0123456789abcdef0123456789abcdef",
    "fedcba9876543210fedcba9876543210",
    "00000000000000000000000000000000",
};
constexpr const char* kNewHash = "abcdefabcdefabcdefabcdefabcdef01";

void recent_files(const std::filesystem::path& source, const std::filesystem::path& out,
                  Results& results) {
    const std::string name = source.stem().string();
    const auto file = out / "app" / "recent" / source.filename();
    write_file(file, read_file(source));

    cw::RecentFiles recent(file);
    recent.load();
    for (const auto& entry : recent.entries()) {
        results.add("recent", name,
                    "entry " + entry.core.string() + " | " +
                        (entry.game.empty() ? "(no game)" : entry.game.string()));
    }
    recent.add("/cores/new_libretro.so", "/games/New Game (Disc 1).cue");
    recent.add("/cores/only_core_libretro.so");
    std::string error;
    results.add("recent", name, recent.save(error) ? "saved" : "save failed: " + error);
}

void game_ids(const std::filesystem::path& source, const std::filesystem::path& out,
              Results& results) {
    const std::string name = source.stem().string();
    const auto file = out / "app" / "game-ids" / source.filename();
    write_file(file, read_file(source));

    cw::GameIds ids(file);
    ids.load();
    for (const char* hash : kProbeHashes) {
        const auto id = ids.find(hash);
        results.add("game-ids", name,
                    std::string(hash) + " -> " + (id ? std::to_string(*id) : "unknown"));
    }
    std::string error;
    if (!ids.set(kNewHash, 4242, error)) {
        results.add("game-ids", name, "set refused: " + error);
    }
    if (!ids.set(kProbeHashes[0], 0, error)) {
        results.add("game-ids", name, "remove refused: " + error);
    }
    if (ids.set("not-a-hash", 1, error)) {
        results.add("game-ids", name, "an invalid hash was accepted");
    } else {
        results.add("game-ids", name, "invalid hash refused: " + error);
    }
}

void ui_settings(const std::filesystem::path& source, const std::filesystem::path& out,
                 Results& results) {
    const std::string name = source.stem().string();
    const auto settings = cw::load_ui_settings(source);
    results.add("ui-settings", name,
                "font_size " + std::to_string(settings.font_size) + ", font_path \"" +
                    settings.font_path.string() + "\", font_index " +
                    std::to_string(settings.font_index));

    std::string error;
    const auto dir = out / "app" / "ui-settings";
    std::filesystem::create_directories(dir);
    if (!cw::save_ui_settings(dir / source.filename(), settings, error)) {
        results.add("ui-settings", name, "save failed: " + error);
    }
    cw::UiSettings edited = settings;
    edited.font_size = settings.font_size + 3;
    edited.font_path = "/fonts/Noto Sans CJK JP.ttc";
    edited.font_index = 2;
    if (!cw::save_ui_settings(dir / (name + ".edited.txt"), edited, error)) {
        results.add("ui-settings", name, "edited save failed: " + error);
    }
}

// Saves `path_text` as `filename` in a fresh configuration directory named after the case. "/" is
// the only directory every machine has, so it is the one saved path the output can contain.
void directory_setting(const std::filesystem::path& out, const char* name, const char* filename,
                       const std::string& path_text, Results& results,
                       std::string_view previous = {}) {
    const auto config = out / "app" / "directories" / name;
    std::filesystem::create_directories(config);
    if (!previous.empty()) {
        write_file(config / filename, previous);
    }
    std::filesystem::path saved;
    std::string error;
    if (cw::save_directory_setting(config, filename, path_text, saved, error)) {
        results.add("directories", name,
                    std::string(filename) + " saved, path \"" + saved.string() + "\"");
    } else {
        results.add("directories", name, std::string(filename) + " refused: " + error);
    }
}

void directory_settings(const std::filesystem::path& out, Results& results) {
    directory_setting(out, "root", "system-path.txt", "/", results);
    directory_setting(out, "empty", "ra-cache-path.txt", "", results);
    directory_setting(out, "overwrite", "system-path.txt", "/", results,
                      "/a/much/longer/directory/than/the/new/one\n");
    directory_setting(out, "relative", "system-path.txt", "relative/dir", results);
    directory_setting(out, "missing", "ra-cache-path.txt", "/no/such/cheevo-workbench/dir",
                      results);
    directory_setting(out, "line-break", "system-path.txt", "/\n/", results);

    std::filesystem::path saved;
    std::string error;
    if (cw::save_directory_setting({}, "system-path.txt", "/", saved, error)) {
        results.add("directories", "no-config", "saved without a configuration directory");
    } else {
        results.add("directories", "no-config", "refused: " + error);
    }
}

} // namespace

void run_app_files(const std::filesystem::path& input, const std::filesystem::path& out,
                   Results& results) {
    for (const auto& source : files_in(input / "app" / "recent", ".txt")) {
        recent_files(source, out, results);
    }
    for (const auto& source : files_in(input / "app" / "game-ids", ".txt")) {
        game_ids(source, out, results);
    }
    for (const auto& source : files_in(input / "app" / "ui-settings", ".txt")) {
        ui_settings(source, out, results);
    }
    directory_settings(out, results);
}

} // namespace cw_corpus
