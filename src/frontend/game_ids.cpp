#include "game_ids.hpp"

#include <charconv>
#include <fstream>
#include <system_error>

namespace cw {

namespace {

constexpr std::string_view kHeader = "# cheevo-workbench game ids v1";

} // namespace

bool GameIds::is_valid_hash(std::string_view hash) {
    if (hash.size() != 32) {
        return false;
    }
    for (const char c : hash) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

void GameIds::load() {
    ids_.clear();
    if (file_.empty()) {
        return;
    }
    std::ifstream file(file_);
    std::string line;
    if (!file || !std::getline(file, line) || line != kHeader) {
        return; // missing, or not our format: start empty rather than guess
    }
    while (std::getline(file, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        const std::string_view hash = std::string_view(line).substr(0, tab);
        const std::string_view number = std::string_view(line).substr(tab + 1);
        uint32_t id = 0;
        const auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), id);
        if (ec != std::errc{} || end != number.data() + number.size() || id == 0 ||
            !is_valid_hash(hash)) {
            continue;
        }
        ids_[std::string(hash)] = id;
    }
}

std::optional<uint32_t> GameIds::find(std::string_view hash) const {
    const auto found = ids_.find(hash);
    if (found == ids_.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool GameIds::set(std::string_view hash, uint32_t game_id, std::string& error) {
    if (!is_valid_hash(hash)) {
        error = "Not a RetroAchievements hash: " + std::string(hash);
        return false;
    }
    if (game_id == 0) {
        ids_.erase(std::string(hash));
    } else {
        ids_[std::string(hash)] = game_id;
    }
    return save(error);
}

bool GameIds::save(std::string& error) const {
    if (file_.empty()) {
        return true; // persistence disabled (no config directory)
    }
    // Write a sibling file and rename it over the old one: a crash never leaves half a file.
    std::filesystem::path temp = file_;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        out << kHeader << '\n';
        for (const auto& [hash, id] : ids_) {
            out << hash << '\t' << id << '\n';
        }
        out.close();
        if (!out) {
            error = "Could not write " + temp.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, file_, ec);
    if (ec) {
        error = "Could not replace " + file_.string() + ": " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace cw
