#pragma once

// Helpers shared by the format corpus. Harness problems (a fixture that cannot be read, an output
// that cannot be written) throw std::runtime_error; run() turns them into its error.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cw_corpus {

// The game ID every fixture is loaded as. It is made up; no real game data is involved.
inline constexpr uint32_t kGameId = 1;

// Collects results.txt: "<format>/<case>: <observation>", one per line. The absolute input and
// output paths are written as "<in>" and "<out>", and line breaks inside messages as "\n", so the
// text does not depend on where the corpus runs.
class Results {
  public:
    Results(std::filesystem::path in, std::filesystem::path out)
        : in_(std::move(in)), out_(std::move(out)) {}
    void add(std::string_view format, std::string_view name, std::string_view observation);
    const std::string& text() const { return text_; }

  private:
    std::filesystem::path in_;
    std::filesystem::path out_;
    std::string text_;
};

std::string read_file(const std::filesystem::path& path);
// Creates the parent directories and replaces the file.
void write_file(const std::filesystem::path& path, std::string_view bytes);
// Copies a directory tree; the destination must not exist yet.
void copy_tree(const std::filesystem::path& from, const std::filesystem::path& to);
// Regular files directly in `dir` with `extension`, by name; empty when `dir` does not exist.
std::vector<std::filesystem::path> files_in(const std::filesystem::path& dir,
                                            std::string_view extension);
// Subdirectories of `dir`, by name; empty when `dir` does not exist.
std::vector<std::filesystem::path> dirs_in(const std::filesystem::path& dir);
// 16 bytes per line as lowercase hex, so binary output stays reviewable in a diff.
std::string hex_dump(std::span<const uint8_t> bytes);

void run_user_files(const std::filesystem::path& input, const std::filesystem::path& out,
                    Results& results);
void run_rich_presence(const std::filesystem::path& input, const std::filesystem::path& out,
                       Results& results);
void run_bookmarks(const std::filesystem::path& input, const std::filesystem::path& out,
                   Results& results);
void run_code_notes(const std::filesystem::path& input, const std::filesystem::path& out,
                    Results& results);
void run_save_states(const std::filesystem::path& out, Results& results);
void run_app_files(const std::filesystem::path& input, const std::filesystem::path& out,
                   Results& results);

} // namespace cw_corpus
