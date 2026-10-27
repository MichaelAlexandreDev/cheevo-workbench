#include "recent_files.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

using cw_test::TempDir;

} // namespace

TEST_CASE("Recent files put a duplicate pair first and keep at most ten entries") {
    TempDir dir;
    cw::RecentFiles recent(dir.path() / "recent-files.txt");

    const auto core_a = dir.path() / "core-a.so";
    const auto core_b = dir.path() / "core-b.so";
    const auto game_a = dir.path() / "game-a.cue";
    const auto game_b = dir.path() / "game-b.cue";
    recent.add(core_a, game_a);
    recent.add(core_b, game_b);
    recent.add(core_a, game_a);

    REQUIRE(recent.entries().size() == 2);
    CHECK((recent.entries()[0] ==
           cw::RecentFile{std::filesystem::absolute(core_a), std::filesystem::absolute(game_a)}));
    CHECK((recent.entries()[1] ==
           cw::RecentFile{std::filesystem::absolute(core_b), std::filesystem::absolute(game_b)}));

    for (size_t i = 0; i < cw::RecentFiles::kMaxEntries + 2; ++i) {
        recent.add(dir.path() / ("core-" + std::to_string(i) + ".so"),
                   dir.path() / ("game-" + std::to_string(i) + ".cue"));
    }
    CHECK(recent.entries().size() == cw::RecentFiles::kMaxEntries);
    CHECK(recent.entries().front().core.filename() == "core-11.so");
}

TEST_CASE("Recent files persist core-only and core/content entries") {
    TempDir dir;
    const auto recent_path = dir.path() / "recent-files.txt";
    const auto core = dir.path() / "core with spaces.so";
    const auto game = dir.path() / "game\twith\ncontrol.cue";

    cw::RecentFiles recent(recent_path);
    recent.add(core, game);
    recent.add(core);
    std::string error;
    REQUIRE(recent.save(error));
    CHECK(error.empty());

    cw::RecentFiles loaded(recent_path);
    loaded.load();
    REQUIRE(loaded.entries().size() == 2);
    CHECK(loaded.entries() == recent.entries());
    CHECK(loaded.entries()[0].game.empty());
    CHECK(loaded.entries()[1].game.filename() == game.filename());
}

TEST_CASE("Missing or corrupt recent files load as an empty list") {
    TempDir dir;
    const auto recent_path = dir.path() / "recent-files.txt";
    cw::RecentFiles recent(recent_path);

    recent.load();
    CHECK(recent.entries().empty());

    {
        std::ofstream file(recent_path, std::ios::binary);
        REQUIRE(file);
        file << "not the recent-files header\n";
        file << "4142\t43\n";
    }
    recent.add(dir.path() / "old.so", dir.path() / "old.cue");
    recent.load();
    CHECK(recent.entries().empty());

    {
        std::ofstream file(recent_path, std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file << "# cheevo-workbench recent files v1\n";
        file << "4142\tnot-hex\n";
    }
    recent.load();
    CHECK(recent.entries().empty());
}
