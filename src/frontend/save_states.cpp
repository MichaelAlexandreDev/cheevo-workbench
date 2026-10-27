#include "save_states.hpp"

#include "save_state.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

namespace cw {

namespace {

// The largest file a valid save can produce: one block at the limit plus headers, padding and
// a small achievements block. Anything bigger is not a state file and is not read.
constexpr std::uintmax_t kMaxStateFileSize = kMaxStateBlockSize + 1024 * 1024;

// Keeps a name usable as a single path component.
std::string sanitize(std::string name) {
    for (char& c : name) {
        if (c == '/' || static_cast<unsigned char>(c) < 0x20) {
            c = '_';
        }
    }
    return name;
}

// Writes the whole buffer to `fd`, retrying short writes; false on any error.
bool write_all(int fd, const uint8_t* data, size_t size) {
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool write_file_atomically(const std::filesystem::path& path, const std::vector<uint8_t>& data,
                           std::string& error) {
    // Write a temporary file next to the target, then rename() it over the slot: rename
    // replaces the old file in one step, so a crash mid-write never leaves half a state behind.
    // The temporary name is unique per process and call, and O_EXCL guarantees we created it,
    // so two instances saving the same slot never write into the same file.
    static std::atomic<unsigned> counter{0};
    std::filesystem::path temp = path;
    temp += ".tmp." + std::to_string(::getpid()) + "." + std::to_string(counter++);

    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        error = "Could not create " + temp.string() + ": " + std::strerror(errno);
        return false;
    }
    // close() can report a delayed write error, so its result counts too.
    const bool written = write_all(fd, data.data(), data.size());
    const bool closed = ::close(fd) == 0;
    std::error_code ec;
    if (!written || !closed) {
        error = "Could not write " + temp.string() + ": " + std::strerror(errno);
        std::filesystem::remove(temp, ec);
        return false;
    }
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
    if (dir_.empty() || core.game_path().empty() || slot < 1 || slot > kSlotCount) {
        return {};
    }
    const std::string game = std::filesystem::path(core.game_path()).stem().string();
    return dir_ / sanitize(game + "-" + core.system_info().name + ".state" + std::to_string(slot));
}

bool SaveStates::save(LibretroCore& core, int slot, std::string& message) {
    if (slot < 1 || slot > kSlotCount) {
        message = "Invalid slot " + std::to_string(slot);
        return false;
    }
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
    // Same limit as on load, so a save that succeeds can always be loaded back.
    if (core_data.size() > kMaxStateBlockSize) {
        message = "Could not save slot " + std::to_string(slot) + ": the state is too large (" +
                  std::to_string(core_data.size()) + " bytes)";
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
    if (slot < 1 || slot > kSlotCount) {
        message = "Invalid slot " + std::to_string(slot);
        return false;
    }
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
