#include "recent_files.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace cw {

namespace {

constexpr std::string_view kHeader = "# cheevo-workbench recent files v1";
constexpr std::uintmax_t kMaxRecentFileSize = 1024 * 1024;

char hex_digit(unsigned value) {
    return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10);
}

std::string encode(std::string_view value) {
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        encoded.push_back(hex_digit(byte >> 4));
        encoded.push_back(hex_digit(byte & 0x0f));
    }
    return encoded;
}

int hex_value(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool decode(std::string_view encoded, std::string& decoded, bool empty_marker_allowed) {
    if (encoded == "-" && empty_marker_allowed) {
        decoded.clear();
        return true;
    }
    if (encoded.empty() || encoded.size() % 2 != 0) {
        return false;
    }
    decoded.clear();
    decoded.reserve(encoded.size() / 2);
    for (size_t i = 0; i < encoded.size(); i += 2) {
        const int high = hex_value(encoded[i]);
        const int low = hex_value(encoded[i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        const char byte = static_cast<char>((high << 4) | low);
        if (byte == '\0') {
            return false;
        }
        decoded.push_back(byte);
    }
    return !decoded.empty();
}

std::filesystem::path normalized(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

} // namespace

RecentFiles::RecentFiles(std::filesystem::path file) : file_(std::move(file)) {}

void RecentFiles::load() {
    entries_.clear();
    if (file_.empty()) {
        return;
    }

    std::error_code size_error;
    const std::uintmax_t size = std::filesystem::file_size(file_, size_error);
    if (size_error || size > kMaxRecentFileSize) {
        return;
    }

    std::ifstream input(file_, std::ios::binary);
    if (!input) {
        return;
    }

    std::string line;
    if (!std::getline(input, line) || line != kHeader) {
        return;
    }

    std::vector<RecentFile> loaded;
    while (std::getline(input, line)) {
        const size_t separator = line.find('\t');
        if (separator == std::string::npos || line.find('\t', separator + 1) != std::string::npos) {
            entries_.clear();
            return;
        }
        std::string core;
        std::string game;
        if (!decode(std::string_view(line).substr(0, separator), core, false) ||
            !decode(std::string_view(line).substr(separator + 1), game, true)) {
            entries_.clear();
            return;
        }
        loaded.push_back({std::filesystem::path(core), std::filesystem::path(game)});
        if (loaded.size() > kMaxEntries) {
            entries_.clear();
            return;
        }
    }
    if (!input.eof()) {
        return;
    }

    // Be tolerant of manually duplicated records while keeping the first (most recent) copy.
    for (const RecentFile& entry : loaded) {
        const RecentFile key{normalized(entry.core), normalized(entry.game)};
        if (std::find(entries_.begin(), entries_.end(), key) == entries_.end()) {
            entries_.push_back(key);
        }
    }
}

void RecentFiles::add(const std::filesystem::path& core, const std::filesystem::path& game) {
    if (core.empty()) {
        return;
    }
    const RecentFile item{normalized(core), normalized(game)};
    entries_.erase(std::remove(entries_.begin(), entries_.end(), item), entries_.end());
    entries_.insert(entries_.begin(), item);
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
    }
}

bool RecentFiles::save(std::string& error) const {
    if (file_.empty()) {
        error = "Recent files are disabled (no config directory)";
        return false;
    }

    std::error_code filesystem_error;
    if (!file_.parent_path().empty()) {
        std::filesystem::create_directories(file_.parent_path(), filesystem_error);
    }
    if (filesystem_error) {
        error =
            "Could not create " + file_.parent_path().string() + ": " + filesystem_error.message();
        return false;
    }

    static std::atomic<unsigned> counter{0};
    std::filesystem::path temporary = file_;
    temporary += ".tmp." + std::to_string(counter++);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not create " + temporary.string();
            return false;
        }
        output << kHeader << '\n';
        for (const RecentFile& entry : entries_) {
            output << encode(entry.core.native()) << '\t';
            output << (entry.game.empty() ? "-" : encode(entry.game.native())) << '\n';
        }
        output.flush();
        if (!output) {
            error = "Could not write " + temporary.string();
            output.close();
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }

    std::filesystem::rename(temporary, file_, filesystem_error);
    if (filesystem_error) {
        error = "Could not replace " + file_.string() + ": " + filesystem_error.message();
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    error.clear();
    return true;
}

} // namespace cw
