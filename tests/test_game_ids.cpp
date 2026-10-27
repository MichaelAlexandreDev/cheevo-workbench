// The game IDs the user enters for content hashes, and their file.

#include "game_ids.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <string>

namespace {

using cw_test::TempDir;

constexpr const char* kHash = "6afb6003b25f3df44cb644563c6da27a";

} // namespace

TEST_CASE("A game ID survives a save and a reload") {
    TempDir dir;
    const auto file = dir.path() / "game-ids.txt";
    std::string error;
    {
        cw::GameIds ids(file);
        ids.load(); // no file yet: empty
        CHECK_FALSE(ids.find(kHash));
        REQUIRE(ids.set(kHash, 18238, error));
    }
    cw::GameIds reloaded(file);
    reloaded.load();
    CHECK(reloaded.find(kHash) == 18238u);

    REQUIRE(reloaded.set(kHash, 0, error)); // 0 removes the entry
    cw::GameIds again(file);
    again.load();
    CHECK_FALSE(again.find(kHash));
}

TEST_CASE("Invalid hashes are refused and broken lines are skipped") {
    TempDir dir;
    const auto file = dir.path() / "game-ids.txt";
    cw::GameIds ids(file);
    std::string error;
    CHECK_FALSE(ids.set("not-a-hash", 1, error));
    CHECK_FALSE(ids.set("6AFB6003B25F3DF44CB644563C6DA27A", 1, error)); // rcheevos uses lowercase
    CHECK_FALSE(error.empty());

    std::ofstream(file) << "# cheevo-workbench game ids v1\n"
                        << kHash << "\t18238\n"
                        << "garbage line\n"
                        << "0123456789abcdef0123456789abcdef\tnot-a-number\n"
                        << "0123456789abcdef0123456789abcdef\t0\n";
    ids.load();
    CHECK(ids.find(kHash) == 18238u);
    CHECK_FALSE(ids.find("0123456789abcdef0123456789abcdef"));
}

TEST_CASE("A file in another format loads as empty instead of being guessed") {
    TempDir dir;
    const auto file = dir.path() / "game-ids.txt";
    std::ofstream(file) << kHash << "\t18238\n"; // no version header
    cw::GameIds ids(file);
    ids.load();
    CHECK_FALSE(ids.find(kHash));
}
