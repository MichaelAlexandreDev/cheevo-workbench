#include "cw/devtools/code_notes.hpp"

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <system_error>

namespace cw {

namespace {

using Json = nlohmann::json;
constexpr std::string_view kFormat = "cheevo-workbench-code-notes";
constexpr int kVersion = 1;

bool read_file(const std::filesystem::path& path, std::string& contents, bool& missing,
               std::string& error) {
    missing = false;
    std::error_code filesystem_error;
    const bool exists = std::filesystem::exists(path, filesystem_error);
    if (filesystem_error) {
        error = "Could not inspect " + path.string() + ": " + filesystem_error.message();
        return false;
    }
    if (!exists) {
        missing = true;
        return true;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not read " + path.string();
        return false;
    }
    contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) {
        error = "Could not finish reading " + path.string();
        return false;
    }
    return true;
}

std::string address_to_string(uint32_t address) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(6) << address;
    return output.str();
}

bool atomic_write(const std::filesystem::path& path, std::string_view contents,
                  std::string& error) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error) {
        error =
            "Could not create " + path.parent_path().string() + ": " + filesystem_error.message();
        return false;
    }

    std::string temporary_name = path.string() + ".tmp.XXXXXX";
    std::vector<char> temporary_buffer(temporary_name.begin(), temporary_name.end());
    temporary_buffer.push_back('\0');
    const int descriptor = ::mkstemp(temporary_buffer.data());
    if (descriptor < 0) {
        error = "Could not create a temporary file next to " + path.string() + ": " +
                std::strerror(errno);
        return false;
    }
    const std::filesystem::path temporary_path(temporary_buffer.data());

    size_t written = 0;
    bool write_ok = true;
    while (written < contents.size()) {
        const size_t remaining = contents.size() - written;
        const size_t chunk =
            std::min(remaining, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        const ssize_t result = ::write(descriptor, contents.data() + written, chunk);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            error = result == 0 ? "Could not finish writing " + temporary_path.string()
                                : "Could not write " + temporary_path.string() + ": " +
                                      std::strerror(errno);
            write_ok = false;
            break;
        }
        written += static_cast<size_t>(result);
    }
    if (write_ok && ::fsync(descriptor) != 0) {
        error = "Could not flush " + temporary_path.string() + ": " + std::strerror(errno);
        write_ok = false;
    }
    if (::close(descriptor) != 0 && write_ok) {
        error = "Could not close " + temporary_path.string() + ": " + std::strerror(errno);
        write_ok = false;
    }
    if (!write_ok) {
        ::unlink(temporary_path.c_str());
        return false;
    }

    if (::rename(temporary_path.c_str(), path.c_str()) != 0) {
        error = "Could not replace " + path.string() + ": " + std::strerror(errno);
        ::unlink(temporary_path.c_str());
        return false;
    }
    return true;
}

bool positive_game_id(const Json& value, uint32_t& game_id) {
    if (!value.is_number_integer()) {
        return false;
    }
    try {
        if (value.is_number_unsigned()) {
            const uint64_t parsed = value.get<uint64_t>();
            if (parsed == 0 || parsed > UINT32_MAX) {
                return false;
            }
            game_id = static_cast<uint32_t>(parsed);
            return true;
        }
        const int64_t parsed = value.get<int64_t>();
        if (parsed <= 0 || parsed > UINT32_MAX) {
            return false;
        }
        game_id = static_cast<uint32_t>(parsed);
        return true;
    } catch (const Json::exception&) {
        return false;
    }
}

bool parse_note_array(const Json& array, std::vector<CodeNote>& notes, bool server,
                      std::string& error) {
    if (!array.is_array()) {
        error = "the notes field is not an array";
        return false;
    }

    const char* address_key = server ? "Address" : "address";
    const char* note_key = server ? "Note" : "note";
    std::map<uint32_t, CodeNote> sorted_notes;
    for (const Json& entry : array) {
        if (!entry.is_object() || !entry.contains(address_key) ||
            !entry.at(address_key).is_string() || !entry.contains(note_key) ||
            !entry.at(note_key).is_string()) {
            error = "a note must contain a hexadecimal address and a text note";
            return false;
        }
        if (server && (!entry.contains("User") || !entry.at("User").is_string())) {
            error = "a server note is missing its User field";
            return false;
        }

        uint32_t address = 0;
        const std::string address_text = entry.at(address_key).get<std::string>();
        if (!CodeNotes::parse_hex_address(address_text, address)) {
            error = "invalid hexadecimal address: " + address_text;
            return false;
        }
        std::string note_text = entry.at(note_key).get<std::string>();
        // RAIntegration's rcheevos response parser treats an empty note as a server deletion.
        if (server && (note_text.empty() || note_text == "''")) {
            continue;
        }
        if (note_text.empty()) {
            error = "a user note cannot be empty; remove it instead";
            return false;
        }
        std::string author;
        if (server) {
            author = entry.at("User").get<std::string>();
        }
        if (!server && sorted_notes.contains(address)) {
            error = "duplicate user note address: " + address_text;
            return false;
        }
        sorted_notes[address] = CodeNote{address, std::move(note_text), std::move(author)};
    }

    notes.clear();
    notes.reserve(sorted_notes.size());
    for (auto& [address, note] : sorted_notes) {
        (void)address;
        notes.push_back(std::move(note));
    }
    return true;
}

} // namespace

std::filesystem::path CodeNotes::user_file_path(const std::filesystem::path& data_dir,
                                                uint32_t game_id) {
    if (data_dir.empty() || game_id == 0) {
        return {};
    }
    return data_dir / "notes" / (std::to_string(game_id) + ".json");
}

std::filesystem::path CodeNotes::server_file_path(const std::filesystem::path& ra_cache_dir,
                                                  uint32_t game_id) {
    if (ra_cache_dir.empty() || game_id == 0) {
        return {};
    }
    return ra_cache_dir / "Data" / (std::to_string(game_id) + "-Notes.json");
}

bool CodeNotes::parse_hex_address(std::string_view text, uint32_t& address) {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return false;
    }

    uint32_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return false;
    }
    address = parsed;
    return true;
}

void CodeNotes::load(std::optional<uint32_t> game_id, const std::filesystem::path& data_dir,
                     const std::filesystem::path& ra_cache_dir) {
    game_id_ = game_id && *game_id != 0 ? game_id : std::nullopt;
    user_file_.clear();
    user_file_valid_ = true;
    user_notes_.clear();
    server_notes_.clear();
    user_error_.clear();
    server_error_.clear();
    if (!game_id_) {
        return;
    }

    user_file_ = user_file_path(data_dir, *game_id_);
    if (user_file_.empty()) {
        user_file_valid_ = false;
        user_error_ = "Local data directory is unavailable; code notes cannot be saved.";
    } else {
        std::string contents;
        bool missing = false;
        if (!read_file(user_file_, contents, missing, user_error_)) {
            user_file_valid_ = false;
        } else if (!missing) {
            try {
                const Json root = Json::parse(contents);
                if (!root.is_object() || !root.contains("format") ||
                    !root.at("format").is_string() || root.at("format") != kFormat ||
                    !root.contains("version") || !root.at("version").is_number_integer() ||
                    root.at("version") != kVersion || !root.contains("game_id") ||
                    !root.contains("notes")) {
                    user_error_ = "unsupported or incomplete code notes JSON";
                    user_file_valid_ = false;
                } else {
                    uint32_t stored_game_id = 0;
                    if (!positive_game_id(root.at("game_id"), stored_game_id) ||
                        stored_game_id != *game_id_) {
                        user_error_ = "the JSON game_id does not match the selected game";
                        user_file_valid_ = false;
                    } else {
                        std::string parse_error;
                        if (!parse_note_array(root.at("notes"), user_notes_, false, parse_error)) {
                            user_error_ = std::move(parse_error);
                            user_file_valid_ = false;
                        }
                    }
                }
            } catch (const Json::exception& exception) {
                user_error_ = exception.what();
                user_file_valid_ = false;
            }
            if (!user_file_valid_) {
                user_notes_.clear();
                user_error_ = "Could not load " + user_file_.string() + ": " + user_error_;
            }
        }
    }

    const std::filesystem::path server_file = server_file_path(ra_cache_dir, *game_id_);
    if (server_file.empty()) {
        return;
    }
    std::string contents;
    bool missing = false;
    if (!read_file(server_file, contents, missing, server_error_)) {
        return;
    }
    if (missing) {
        return;
    }
    try {
        const Json root = Json::parse(contents);
        std::string parse_error;
        if (!parse_note_array(root, server_notes_, true, parse_error)) {
            server_error_ = std::move(parse_error);
        }
    } catch (const Json::exception& exception) {
        server_error_ = exception.what();
    }
    if (!server_error_.empty()) {
        server_notes_.clear();
        server_error_ = "Could not load " + server_file.string() + ": " + server_error_;
    }
}

const CodeNote* CodeNotes::user_note_at(uint32_t address) const {
    const auto found =
        std::lower_bound(user_notes_.begin(), user_notes_.end(), address,
                         [](const CodeNote& note, uint32_t value) { return note.address < value; });
    return found != user_notes_.end() && found->address == address ? &*found : nullptr;
}

const CodeNote* CodeNotes::server_note_at(uint32_t address) const {
    const auto found =
        std::lower_bound(server_notes_.begin(), server_notes_.end(), address,
                         [](const CodeNote& note, uint32_t value) { return note.address < value; });
    return found != server_notes_.end() && found->address == address ? &*found : nullptr;
}

const CodeNote* CodeNotes::display_note_at(uint32_t address) const {
    if (const CodeNote* note = user_note_at(address)) {
        return note;
    }
    return server_note_at(address);
}

std::vector<CodeNote> CodeNotes::notes_to_publish() const {
    std::vector<CodeNote> result;
    if (!server_error_.empty() || !user_error_.empty()) {
        return result;
    }
    for (const CodeNote& user_note : user_notes_) {
        const CodeNote* server_note = server_note_at(user_note.address);
        if (!server_note || server_note->note != user_note.note) {
            result.push_back(user_note);
        }
    }
    return result;
}

bool CodeNotes::set_note(uint32_t address, std::string note, std::string& error) {
    error.clear();
    if (!game_id_) {
        error = "Set the game ID in the Status window before saving code notes.";
        return false;
    }
    if (note.empty()) {
        error = "A code note cannot be empty; use Delete to remove it.";
        return false;
    }
    std::vector<CodeNote> updated = user_notes_;
    const auto found = std::lower_bound(
        updated.begin(), updated.end(), address,
        [](const CodeNote& entry, uint32_t value) { return entry.address < value; });
    if (found != updated.end() && found->address == address) {
        if (found->note == note) {
            return true;
        }
        found->note = std::move(note);
    } else {
        updated.insert(found, CodeNote{address, std::move(note), {}});
    }
    if (!save_user_notes(updated, error)) {
        return false;
    }
    user_notes_ = std::move(updated);
    return true;
}

bool CodeNotes::remove_note(uint32_t address, std::string& error) {
    error.clear();
    if (!game_id_) {
        error = "Set the game ID in the Status window before saving code notes.";
        return false;
    }
    std::vector<CodeNote> updated = user_notes_;
    const auto found = std::lower_bound(
        updated.begin(), updated.end(), address,
        [](const CodeNote& entry, uint32_t value) { return entry.address < value; });
    if (found == updated.end() || found->address != address) {
        if (!user_file_valid_) {
            error = user_error_;
            return false;
        }
        return true;
    }
    updated.erase(found);
    if (!save_user_notes(updated, error)) {
        return false;
    }
    user_notes_ = std::move(updated);
    return true;
}

bool CodeNotes::save_user_notes(const std::vector<CodeNote>& notes, std::string& error) {
    if (!user_file_valid_) {
        error = user_error_;
        return false;
    }
    if (user_file_.empty()) {
        error = "Local data directory is unavailable; code notes cannot be saved.";
        return false;
    }

    try {
        Json root = {{"format", kFormat}, {"version", kVersion}, {"game_id", *game_id_}};
        root["notes"] = Json::array();
        for (const CodeNote& note : notes) {
            root["notes"].push_back(
                {{"address", address_to_string(note.address)}, {"note", note.note}});
        }
        const std::string contents = root.dump(2) + "\n";
        if (!atomic_write(user_file_, contents, error)) {
            return false;
        }
    } catch (const Json::exception& exception) {
        error = "Could not serialize code notes: ";
        error += exception.what();
        return false;
    }
    error.clear();
    return true;
}

} // namespace cw
