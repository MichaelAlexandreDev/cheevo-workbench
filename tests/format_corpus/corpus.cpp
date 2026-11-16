#include "corpus.hpp"
#include "corpus_support.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace cw_corpus {

void Results::add(std::string_view format, std::string_view name, std::string_view observation) {
    std::string line = std::string(format) + "/" + std::string(name) + ": ";
    const std::string in = in_.string();
    const std::string out = out_.string();
    for (size_t i = 0; i < observation.size();) {
        if (!in.empty() && observation.substr(i, in.size()) == in) {
            line += "<in>";
            i += in.size();
        } else if (!out.empty() && observation.substr(i, out.size()) == out) {
            line += "<out>";
            i += out.size();
        } else if (observation[i] == '\n') {
            line += "\\n";
            ++i;
        } else {
            line += observation[i++];
        }
    }
    text_ += line + "\n";
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not read " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Could not write " + path.string());
    }
}

void copy_tree(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive);
}

namespace {

std::vector<std::filesystem::path> entries(const std::filesystem::path& dir, bool directories,
                                           std::string_view extension) {
    std::vector<std::filesystem::path> found;
    std::error_code error;
    if (!std::filesystem::is_directory(dir, error)) {
        return found;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const bool wanted = directories
                                ? entry.is_directory()
                                : entry.is_regular_file() && entry.path().extension() == extension;
        if (wanted) {
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

} // namespace

std::vector<std::filesystem::path> files_in(const std::filesystem::path& dir,
                                            std::string_view extension) {
    return entries(dir, false, extension);
}

std::vector<std::filesystem::path> dirs_in(const std::filesystem::path& dir) {
    return entries(dir, true, {});
}

std::string hex_dump(std::span<const uint8_t> bytes) {
    std::string text;
    char digits[4];
    for (size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(digits, sizeof(digits), "%02x", bytes[i]);
        text += digits;
        text += (i % 16 == 15 || i + 1 == bytes.size()) ? '\n' : ' ';
    }
    return text;
}

bool run(const std::filesystem::path& input, const std::filesystem::path& out, std::string& error) {
    try {
        if (!std::filesystem::is_directory(input)) {
            throw std::runtime_error("No corpus input directory at " + input.string());
        }
        std::filesystem::create_directories(out);
        const std::filesystem::path absolute_in = std::filesystem::absolute(input);
        const std::filesystem::path absolute_out = std::filesystem::absolute(out);
        Results results(absolute_in, absolute_out);
        run_user_files(absolute_in, absolute_out, results);
        run_rich_presence(absolute_in, absolute_out, results);
        run_bookmarks(absolute_in, absolute_out, results);
        run_code_notes(absolute_in, absolute_out, results);
        run_save_states(absolute_out, results);
        run_app_files(absolute_in, absolute_out, results);
        write_file(absolute_out / "results.txt", results.text());
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace cw_corpus
