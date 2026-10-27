// Clear messages for unusable input files (issue #34 and its review).

#include "cw/devtools/file_checks.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <fstream>
#include <string>

using cw_test::TempDir;

TEST_CASE("Missing files, directories and broken links get distinct messages") {
    TempDir dir;
    const auto file = dir.path() / "game.cue";
    std::ofstream(file) << "x";
    CHECK_FALSE(cw::regular_file_problem(file));

    CHECK(cw::regular_file_problem(dir.path() / "nope.cue") ==
          "File not found: " + (dir.path() / "nope.cue").string());
    CHECK(cw::regular_file_problem(dir.path() / "nope" / "deeper.cue")
              ->starts_with("File not found"));
    CHECK(cw::regular_file_problem(dir.path()) == "Not a file: " + dir.path().string());

    std::filesystem::create_symlink(dir.path() / "gone.cue", dir.path() / "broken.cue");
    CHECK(cw::regular_file_problem(dir.path() / "broken.cue")->starts_with("File not found"));
    std::filesystem::create_symlink(file, dir.path() / "link.cue");
    CHECK_FALSE(cw::regular_file_problem(dir.path() / "link.cue")); // links are followed
}

TEST_CASE("A permission problem is not reported as a missing file") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root bypasses file permissions");
        return;
    }
    TempDir dir;
    const auto locked = dir.path() / "locked";
    std::filesystem::create_directory(locked);
    std::ofstream(locked / "game.cue") << "x";
    std::filesystem::permissions(locked, std::filesystem::perms::none);

    const auto problem = cw::regular_file_problem(locked / "game.cue");
    std::filesystem::permissions(locked, std::filesystem::perms::owner_all); // let TempDir clean up
    REQUIRE(problem);
    CHECK(problem->starts_with("Cannot access " + (locked / "game.cue").string()));
    CHECK(problem->find("Permission denied") != std::string::npos);
}
