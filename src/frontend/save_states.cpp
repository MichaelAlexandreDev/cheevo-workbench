#include "save_states.hpp"

#include "save_state.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

namespace cw {

namespace {

// Real states are a few MB (Beetle PSX: ~4.5 MB); anything this big is not a state file.
constexpr std::uintmax_t kMaxStateFileSize = 256u * 1024 * 1024;

// Keeps a name usable as a single path component.
std::string sanitize(std::string name) {
    for (char& c : name) {
        if (c == '/' || static_cast<unsigned char>(c) < 0x20) {
            c = '_';
        }
    }
    return name;
}

bool write_file_atomically(const std::filesystem::path& path, const std::vector<uint8_t>& data,
                           std::string& error) {
    // Write next to the target, then rename: a crash mid-write never leaves a half state in
    // the slot, because rename() replaces the old file in one step.
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file.write(reinterpret_cast<const char*>(data.data()),
                        static_cast<std::streamsize>(data.size()))) {
            error = "Could not write " + temp.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        error = "Could not replace " + path.string() + ": " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace

std::filesystem::path SaveStates::slot_path(const LibretroCore& core, int slot) const {
    if (dir_.empty() || core.game_path().empty()) {
        return {};
    }
    const std::string game = std::filesystem::path(core.game_path()).stem().string();
    return dir_ / sanitize(game + "-" + core.system_info().name + ".state" + std::to_string(slot));
}

bool SaveStates::save(LibretroCore& core, int slot, std::string& message) {
    const std::filesystem::path path = slot_path(core, slot);
    if (path.empty()) {
        message = core.is_game_loaded() ? "Save states are disabled (no data directory)"
                                        : "No game loaded";
        return false;
    }
    std::vector<uint8_t> core_data;
    std::string error;
    if (!core.serialize_state(core_data, error)) {
        message = "Could not save slot " + std::to_string(slot) + ": " + error;
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (!write_file_atomically(path, write_state_file(core_data), error)) {
        message = "Could not save slot " + std::to_string(slot) + ": " + error;
        return false;
    }
    saved_this_session_.insert(path);
    message = "Saved slot " + std::to_string(slot);
    return true;
}

bool SaveStates::load(LibretroCore& core, int slot, std::string& message) {
    const std::filesystem::path path = slot_path(core, slot);
    const std::string prefix = "Could not load slot " + std::to_string(slot) + ": ";
    if (path.empty()) {
        message = core.is_game_loaded() ? "Save states are disabled (no data directory)"
                                        : "No game loaded";
        return false;
    }
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        message = "Slot " + std::to_string(slot) + " is empty";
        return false;
    }
    if (size > kMaxStateFileSize) {
        message = prefix + "the file is too large to be a save state";
        return false;
    }
    if ((core.serialization_quirks() & RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION) &&
        !saved_this_session_.contains(path)) {
        message = prefix + "this core's states only load in the session that saved them";
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    if (!file.good() && !file.eof()) {
        message = prefix + "read error";
        return false;
    }
    std::string error;
    const std::optional<StateContents> contents = parse_state_file(bytes, error);
    if (!contents) {
        message = prefix + error;
        return false;
    }
    if (!core.unserialize_state(contents->core, error)) {
        message = prefix + error;
        return false;
    }
    message = "Loaded slot " + std::to_string(slot);
    return true;
}

} // namespace cw
