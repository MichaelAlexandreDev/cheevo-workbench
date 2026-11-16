// Golden test for the format corpus (decision D9): the bytes the application writes for every
// synthetic fixture must match tests/fixtures/corpus/expected exactly. A refactoring never changes
// them; a behavioral PR that does regenerates them on purpose (see tests/format_corpus/main.cpp).

#include "corpus.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>

namespace {

const std::filesystem::path kCorpus = CW_CORPUS_DIR;

class TempDir {
  public:
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("cw-corpus-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

std::set<std::string> files_under(const std::filesystem::path& root) {
    std::set<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.insert(std::filesystem::relative(entry.path(), root).generic_string());
        }
    }
    return files;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// "line N" of the first difference, so a failure points at the changed field.
std::string first_difference(const std::string& expected, const std::string& actual) {
    const auto mismatch =
        std::mismatch(expected.begin(), expected.end(), actual.begin(), actual.end());
    const auto offset = static_cast<size_t>(mismatch.first - expected.begin());
    const auto line = 1 + std::count(expected.begin(), mismatch.first, '\n');
    return "byte " + std::to_string(offset) + ", line " + std::to_string(line);
}

} // namespace

TEST_CASE("The format corpus writes exactly the golden bytes") {
    TempDir out;
    std::string error;
    REQUIRE_MESSAGE(cw_corpus::run(kCorpus / "input", out.path(), error), error);

    const auto expected = files_under(kCorpus / "expected");
    const auto actual = files_under(out.path());
    REQUIRE(!expected.empty());
    for (const auto& file : expected) {
        INFO("file: ", file);
        REQUIRE_MESSAGE(actual.count(file) == 1, "missing from the corpus output");
        const std::string want = read(kCorpus / "expected" / file);
        const std::string got = read(out.path() / file);
        CHECK_MESSAGE(want == got, "differs at ", first_difference(want, got));
    }
    for (const auto& file : actual) {
        CHECK_MESSAGE(expected.count(file) == 1, "not in the golden files: ", file);
    }
}

TEST_CASE("The format corpus is deterministic") {
    TempDir first;
    TempDir second;
    std::string error;
    REQUIRE_MESSAGE(cw_corpus::run(kCorpus / "input", first.path(), error), error);
    REQUIRE_MESSAGE(cw_corpus::run(kCorpus / "input", second.path(), error), error);
    const auto files = files_under(first.path());
    REQUIRE(files == files_under(second.path()));
    for (const auto& file : files) {
        INFO("file: ", file);
        CHECK(read(first.path() / file) == read(second.path() / file));
    }
}
