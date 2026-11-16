#include "cw/formats/bookmarks.hpp"

#include <nlohmann/json.hpp>
#include <rcheevos/rc_internal.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace cw {

struct BookmarkFormatAccess {
    static std::string& original_json(MemoryBookmark& bookmark) { return bookmark.original_json; }
    static const std::string& original_json(const MemoryBookmark& bookmark) {
        return bookmark.original_json;
    }
    static bool description_changed(const MemoryBookmark& bookmark) {
        return bookmark.description_changed;
    }
    static bool decimal_changed(const MemoryBookmark& bookmark) { return bookmark.decimal_changed; }
};

namespace {

using Json = nlohmann::ordered_json;

bool read_file(const std::filesystem::path& path, std::string& contents, bool& missing,
               std::string& error) {
    missing = false;
    std::error_code filesystem_error;
    if (!std::filesystem::exists(path, filesystem_error)) {
        if (filesystem_error) {
            error = "Could not inspect " + path.string() + ": " + filesystem_error.message();
            return false;
        }
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

bool atomic_write(const std::filesystem::path& path, std::string_view contents, std::string& error,
                  bool replace = true) {
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

    if (replace) {
        if (::rename(temporary_path.c_str(), path.c_str()) != 0) {
            error = "Could not replace " + path.string() + ": " + std::strerror(errno);
            ::unlink(temporary_path.c_str());
            return false;
        }
    } else {
        std::filesystem::create_hard_link(temporary_path, path, filesystem_error);
        if (filesystem_error) {
            error = "Could not create " + path.string() + ": " + filesystem_error.message();
            ::unlink(temporary_path.c_str());
            return false;
        }
        ::unlink(temporary_path.c_str());
    }
    error.clear();
    return true;
}

bool json_u32(const Json& value, uint32_t& result) {
    if (!value.is_number_integer()) {
        return false;
    }
    try {
        if (value.is_number_unsigned()) {
            const uint64_t number = value.get<uint64_t>();
            if (number > UINT32_MAX) {
                return false;
            }
            result = static_cast<uint32_t>(number);
            return true;
        }
        const int64_t number = value.get<int64_t>();
        if (number < 0 || static_cast<uint64_t>(number) > UINT32_MAX) {
            return false;
        }
        result = static_cast<uint32_t>(number);
        return true;
    } catch (const Json::exception&) {
        return false;
    }
}

bool json_int(const Json& value, int& result) {
    if (!value.is_number_integer()) {
        return false;
    }
    try {
        const int64_t number = value.get<int64_t>();
        if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
            return false;
        }
        result = static_cast<int>(number);
        return true;
    } catch (const Json::exception&) {
        return false;
    }
}

std::optional<MemoryValueSize> watch_size_for_rcheevos(uint8_t size, ByteOrder& byte_order) {
    byte_order = ByteOrder::LittleEndian;
    switch (size) {
    case RC_MEMSIZE_BIT_0:
        return MemoryValueSize::Bit0;
    case RC_MEMSIZE_BIT_1:
        return MemoryValueSize::Bit1;
    case RC_MEMSIZE_BIT_2:
        return MemoryValueSize::Bit2;
    case RC_MEMSIZE_BIT_3:
        return MemoryValueSize::Bit3;
    case RC_MEMSIZE_BIT_4:
        return MemoryValueSize::Bit4;
    case RC_MEMSIZE_BIT_5:
        return MemoryValueSize::Bit5;
    case RC_MEMSIZE_BIT_6:
        return MemoryValueSize::Bit6;
    case RC_MEMSIZE_BIT_7:
        return MemoryValueSize::Bit7;
    case RC_MEMSIZE_LOW:
        return MemoryValueSize::LowNibble;
    case RC_MEMSIZE_HIGH:
        return MemoryValueSize::HighNibble;
    case RC_MEMSIZE_8_BITS:
        return MemoryValueSize::EightBit;
    case RC_MEMSIZE_16_BITS:
        return MemoryValueSize::SixteenBit;
    case RC_MEMSIZE_24_BITS:
        return MemoryValueSize::TwentyFourBit;
    case RC_MEMSIZE_32_BITS:
        return MemoryValueSize::ThirtyTwoBit;
    case RC_MEMSIZE_16_BITS_BE:
        byte_order = ByteOrder::BigEndian;
        return MemoryValueSize::SixteenBit;
    case RC_MEMSIZE_24_BITS_BE:
        byte_order = ByteOrder::BigEndian;
        return MemoryValueSize::TwentyFourBit;
    case RC_MEMSIZE_32_BITS_BE:
        byte_order = ByteOrder::BigEndian;
        return MemoryValueSize::ThirtyTwoBit;
    default:
        return std::nullopt;
    }
}

std::optional<MemoryValueSize> watch_size_for_legacy_size(int size) {
    switch (size) {
    case 0:
        return MemoryValueSize::Bit0;
    case 1:
        return MemoryValueSize::Bit1;
    case 2:
        return MemoryValueSize::Bit2;
    case 3:
        return MemoryValueSize::Bit3;
    case 4:
        return MemoryValueSize::Bit4;
    case 5:
        return MemoryValueSize::Bit5;
    case 6:
        return MemoryValueSize::Bit6;
    case 7:
        return MemoryValueSize::Bit7;
    case 8:
        return MemoryValueSize::LowNibble;
    case 9:
        return MemoryValueSize::HighNibble;
    case 10:
        return MemoryValueSize::EightBit;
    case 11:
        return MemoryValueSize::SixteenBit;
    case 12:
        return MemoryValueSize::TwentyFourBit;
    case 13:
        return MemoryValueSize::ThirtyTwoBit;
    default:
        return std::nullopt; // 14 = BitCount and 15 = Text in RAIntegration's legacy format.
    }
}

const char* legacy_size_name(int size) {
    if (size == 14) {
        return "BitCount";
    }
    if (size == 15) {
        return "Text";
    }
    return "Unsupported size";
}

std::string format_address(uint32_t address, uint32_t address_space_size) {
    unsigned width = 4;
    if (address_space_size > 0x1000000) {
        width = 8;
    } else if (address_space_size > 0x10000) {
        width = 6;
    }
    if ((address & 0xFFFF0000U) != 0) {
        width = std::max(width, 6U);
    }
    if ((address & 0xFF000000U) != 0) {
        width = 8;
    }

    char buffer[9];
    std::snprintf(buffer, sizeof(buffer), "%0*x", static_cast<int>(width), address);
    return buffer;
}

std::optional<std::string> format_mem_addr(const MemoryBookmark& bookmark,
                                           uint32_t address_space_size) {
    if (!bookmark.address || !bookmark.size) {
        return std::nullopt;
    }

    std::string result = "0x";
    switch (*bookmark.size) {
    case MemoryValueSize::Bit0:
    case MemoryValueSize::Bit1:
    case MemoryValueSize::Bit2:
    case MemoryValueSize::Bit3:
    case MemoryValueSize::Bit4:
    case MemoryValueSize::Bit5:
    case MemoryValueSize::Bit6:
    case MemoryValueSize::Bit7:
        result.push_back(static_cast<char>('M' + static_cast<unsigned>(*bookmark.size)));
        break;
    case MemoryValueSize::LowNibble:
        result.push_back('L');
        break;
    case MemoryValueSize::HighNibble:
        result.push_back('U');
        break;
    case MemoryValueSize::EightBit:
        result.push_back('H');
        break;
    case MemoryValueSize::SixteenBit:
        result.push_back(bookmark.byte_order == ByteOrder::LittleEndian ? ' ' : 'I');
        break;
    case MemoryValueSize::TwentyFourBit:
        result.push_back(bookmark.byte_order == ByteOrder::LittleEndian ? 'W' : 'J');
        break;
    case MemoryValueSize::ThirtyTwoBit:
        result.push_back(bookmark.byte_order == ByteOrder::LittleEndian ? 'X' : 'G');
        break;
    }
    result += format_address(*bookmark.address, address_space_size);
    return result;
}

void mark_unsupported(MemoryBookmark& bookmark, std::string reason) {
    bookmark.size.reset();
    bookmark.unsupported_reason = std::move(reason);
}

bool parse_bookmark(const Json& value, MemoryBookmark& bookmark) {
    if (!value.is_object()) {
        return false;
    }
    BookmarkFormatAccess::original_json(bookmark) = value.dump();

    if (value.contains("Decimal")) {
        if (!value.at("Decimal").is_boolean()) {
            mark_unsupported(bookmark, "Invalid bookmark format");
        } else {
            bookmark.decimal = value.at("Decimal").get<bool>();
        }
    }
    if (value.contains("Description")) {
        if (!value.at("Description").is_string()) {
            mark_unsupported(bookmark, "Invalid bookmark format");
        } else {
            bookmark.description = value.at("Description").get<std::string>();
        }
    }
    if (!bookmark.unsupported_reason.empty()) {
        return true;
    }

    if (value.contains("MemAddr")) {
        if (!value.at("MemAddr").is_string()) {
            mark_unsupported(bookmark, "Invalid MemAddr");
            return true;
        }
        bookmark.mem_addr = value.at("MemAddr").get<std::string>();
        if (bookmark.mem_addr.find('_') != std::string::npos) {
            mark_unsupported(bookmark, "Indirect address");
            return true;
        }

        const char* cursor = bookmark.mem_addr.c_str();
        if (bookmark.mem_addr.starts_with("M:")) {
            cursor += 2; // RAIntegration accepts this prefix for direct memrefs.
        }
        uint8_t rcheevos_size = 0;
        uint32_t address = 0;
        if (rc_parse_memref(&cursor, &rcheevos_size, &address) != RC_OK || *cursor != '\0') {
            mark_unsupported(bookmark, "Indirect or unsupported address");
            return true;
        }

        bookmark.address = address;
        if (value.contains("Size")) {
            int saved_size = 0;
            if (json_int(value.at("Size"), saved_size) && saved_size == 15) {
                mark_unsupported(bookmark, "Text"); // RAIntegration's current text format.
                return true;
            }
        }
        bookmark.size = watch_size_for_rcheevos(rcheevos_size, bookmark.byte_order);
        if (!bookmark.size) {
            mark_unsupported(bookmark, "Unsupported size");
        }
        return true;
    }

    uint32_t address = 0;
    if (!value.contains("Address") || !json_u32(value.at("Address"), address)) {
        mark_unsupported(bookmark, "Invalid or missing address");
        return true;
    }
    bookmark.address = address;

    if (value.contains("Type")) {
        int type = 0;
        if (!json_int(value.at("Type"), type)) {
            mark_unsupported(bookmark, "Invalid Type");
            return true;
        }
        switch (type) {
        case 1:
            bookmark.size = MemoryValueSize::EightBit;
            break;
        case 2:
            bookmark.size = MemoryValueSize::SixteenBit;
            break;
        case 3:
            bookmark.size = MemoryValueSize::ThirtyTwoBit;
            break;
        default:
            mark_unsupported(bookmark, "Unsupported Type");
            break;
        }
        return true;
    }

    int size = 10; // LoadBookmarks defaults the old raw enum representation to 8-bit.
    if (value.contains("Size") && !json_int(value.at("Size"), size)) {
        mark_unsupported(bookmark, "Invalid Size");
        return true;
    }
    bookmark.size = watch_size_for_legacy_size(size);
    if (!bookmark.size) {
        mark_unsupported(bookmark, legacy_size_name(size));
    }
    return true;
}

bool parse_bookmarks(std::string_view contents, std::vector<MemoryBookmark>& bookmarks,
                     std::string& document_json, std::string& error) {
    try {
        const Json root = Json::parse(contents);
        if (!root.is_object() || !root.contains("Bookmarks") || !root.at("Bookmarks").is_array()) {
            error = "expected an object containing a Bookmarks array";
            return false;
        }

        std::vector<MemoryBookmark> parsed;
        parsed.reserve(root.at("Bookmarks").size());
        for (const Json& value : root.at("Bookmarks")) {
            MemoryBookmark bookmark;
            if (!parse_bookmark(value, bookmark)) {
                error = "each bookmark must be a JSON object";
                return false;
            }
            parsed.push_back(std::move(bookmark));
        }
        bookmarks = std::move(parsed);
        document_json = root.dump();
        error.clear();
        return true;
    } catch (const Json::exception& exception) {
        error = exception.what();
        return false;
    }
}

Json serialize_bookmark(const MemoryBookmark& bookmark, uint32_t address_space_size) {
    if (bookmark.supported()) {
        Json value = BookmarkFormatAccess::original_json(bookmark).empty()
                         ? Json::object()
                         : Json::parse(BookmarkFormatAccess::original_json(bookmark));
        // Normalize the known legacy fields while retaining extensions we do not interpret.
        value.erase("Address");
        value.erase("Type");
        value.erase("Size");
        value.erase("Decimal");
        value.erase("Description");
        value["MemAddr"] = *format_mem_addr(bookmark, address_space_size);
        if (bookmark.decimal) {
            value["Decimal"] = true;
        }
        if (!bookmark.description.empty()) {
            value["Description"] = bookmark.description;
        }
        return value;
    }

    Json value = Json::parse(BookmarkFormatAccess::original_json(bookmark));
    if (BookmarkFormatAccess::description_changed(bookmark) && value.is_object()) {
        if (bookmark.description.empty()) {
            value.erase("Description");
        } else {
            value["Description"] = bookmark.description;
        }
    }
    if (BookmarkFormatAccess::decimal_changed(bookmark) && value.is_object()) {
        if (bookmark.decimal) {
            value["Decimal"] = true;
        } else {
            value.erase("Decimal");
        }
    }
    return value;
}

std::string serialize_bookmarks(const std::vector<MemoryBookmark>& bookmarks,
                                uint32_t address_space_size, std::string_view document_json) {
    Json root = Json::parse(document_json);
    root["Bookmarks"] = Json::array();
    for (const MemoryBookmark& bookmark : bookmarks) {
        root["Bookmarks"].push_back(serialize_bookmark(bookmark, address_space_size));
    }
    return root.dump();
}

} // namespace

std::filesystem::path Bookmarks::user_file_path(const std::filesystem::path& data_dir,
                                                uint32_t game_id) {
    if (data_dir.empty() || game_id == 0) {
        return {};
    }
    return data_dir / "bookmarks" / (std::to_string(game_id) + "-Bookmarks.json");
}

std::filesystem::path Bookmarks::ra_cache_file_path(const std::filesystem::path& ra_cache_dir,
                                                    uint32_t game_id) {
    if (ra_cache_dir.empty() || game_id == 0) {
        return {};
    }
    return ra_cache_dir / "Bookmarks" / (std::to_string(game_id) + "-Bookmarks.json");
}

void Bookmarks::load(std::optional<uint32_t> game_id, const std::filesystem::path& data_dir,
                     uint32_t address_space_size) {
    game_id_ = game_id && *game_id != 0 ? game_id : std::nullopt;
    user_file_.clear();
    address_space_size_ = address_space_size;
    last_update_frame_.reset();
    file_valid_ = true;
    document_json_ = "{}";
    next_row_id_ = 1;
    items_.clear();
    error_.clear();
    if (!game_id_) {
        return;
    }

    user_file_ = user_file_path(data_dir, *game_id_);
    if (user_file_.empty()) {
        file_valid_ = false;
        error_ = "Local data directory is unavailable; bookmarks cannot be saved.";
        return;
    }

    std::string contents;
    bool missing = false;
    if (!read_file(user_file_, contents, missing, error_)) {
        file_valid_ = false;
    } else if (!missing && !parse_bookmarks(contents, items_, document_json_, error_)) {
        file_valid_ = false;
    }
    if (!file_valid_) {
        items_.clear();
        error_ = "Could not load " + user_file_.string() + ": " + error_;
    } else {
        for (MemoryBookmark& bookmark : items_) {
            bookmark.row_id = next_row_id_++;
        }
    }
}

bool Bookmarks::save_items(const std::vector<MemoryBookmark>& items, std::string_view document_json,
                           std::string& error) const {
    error.clear();
    if (!game_id_) {
        error = "No game ID; bookmarks are not saved.";
        return true; // Keep this list available for the current session only.
    }
    if (!file_valid_) {
        error = error_;
        return false;
    }
    if (user_file_.empty()) {
        error = "Local data directory is unavailable; bookmarks cannot be saved.";
        return false;
    }

    try {
        return atomic_write(user_file_,
                            serialize_bookmarks(items, address_space_size_, document_json), error);
    } catch (const Json::exception& exception) {
        error = "Could not serialize bookmarks: ";
        error += exception.what();
        return false;
    }
}

bool Bookmarks::commit(std::vector<MemoryBookmark> updated, std::string& error,
                       std::optional<std::string> document_json) {
    if (!save_items(updated, document_json ? *document_json : document_json_, error)) {
        return false;
    }
    items_ = std::move(updated);
    if (document_json) {
        document_json_ = std::move(*document_json);
    }
    if (game_id_) {
        error_.clear();
    }
    return true;
}

bool Bookmarks::add(uint32_t address, MemoryValueSize size, ByteOrder byte_order,
                    std::string description, std::string& error) {
    const auto duplicate = std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.supported() && item.address == address && item.size == size &&
               item.byte_order == byte_order;
    });
    if (duplicate != items_.end()) {
        error = "This address and interpretation are already bookmarked.";
        return false;
    }

    std::vector<MemoryBookmark> updated = items_;
    MemoryBookmark bookmark;
    bookmark.row_id = next_row_id_++;
    bookmark.address = address;
    bookmark.size = size;
    bookmark.byte_order = byte_order;
    bookmark.description = std::move(description);
    updated.push_back(std::move(bookmark));
    return commit(std::move(updated), error);
}

bool Bookmarks::set_description(size_t index, std::string description, std::string& error) {
    if (index >= items_.size()) {
        error = "Invalid bookmark index.";
        return false;
    }
    if (items_[index].description == description) {
        error.clear();
        return true;
    }
    std::vector<MemoryBookmark> updated = items_;
    updated[index].description = std::move(description);
    updated[index].description_changed = true;
    return commit(std::move(updated), error);
}

bool Bookmarks::set_decimal(size_t index, bool decimal, std::string& error) {
    if (index >= items_.size()) {
        error = "Invalid bookmark index.";
        return false;
    }
    if (items_[index].decimal == decimal) {
        error.clear();
        return true;
    }
    std::vector<MemoryBookmark> updated = items_;
    updated[index].decimal = decimal;
    updated[index].decimal_changed = true;
    return commit(std::move(updated), error);
}

bool Bookmarks::remove(size_t index, std::string& error) {
    if (index >= items_.size()) {
        error = "Invalid bookmark index.";
        return false;
    }
    std::vector<MemoryBookmark> updated = items_;
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(index));
    return commit(std::move(updated), error);
}

bool Bookmarks::move(size_t from, size_t to, std::string& error) {
    if (from >= items_.size() || to >= items_.size()) {
        error = "Invalid bookmark position.";
        return false;
    }
    if (from == to) {
        error.clear();
        return true;
    }
    std::vector<MemoryBookmark> updated = items_;
    MemoryBookmark bookmark = std::move(updated[from]);
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(from));
    updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(to), std::move(bookmark));
    return commit(std::move(updated), error);
}

bool Bookmarks::import_file(const std::filesystem::path& path, std::string& error) {
    if (!game_id_) {
        error = "Set a game ID before importing bookmarks.";
        return false;
    }
    if (!file_valid_) {
        error = error_;
        return false;
    }

    std::string contents;
    bool missing = false;
    if (!read_file(path, contents, missing, error)) {
        return false;
    }
    if (missing) {
        error = "No RACache bookmarks file was found at " + path.string();
        return false;
    }

    std::vector<MemoryBookmark> imported;
    std::string document_json;
    if (!parse_bookmarks(contents, imported, document_json, error)) {
        error = "Could not import " + path.string() + ": " + error;
        return false;
    }
    for (MemoryBookmark& bookmark : imported) {
        bookmark.row_id = next_row_id_++;
    }
    return commit(std::move(imported), error, std::move(document_json));
}

bool Bookmarks::export_file(const std::filesystem::path& path, bool overwrite,
                            std::string& error) const {
    if (!game_id_) {
        error = "Set a game ID before exporting bookmarks.";
        return false;
    }
    if (!file_valid_) {
        error = error_;
        return false;
    }
    if (path.empty()) {
        error = "The RACache path is unavailable.";
        return false;
    }

    std::error_code filesystem_error;
    const bool exists = std::filesystem::exists(path, filesystem_error);
    if (filesystem_error) {
        error = "Could not inspect " + path.string() + ": " + filesystem_error.message();
        return false;
    }
    if (exists && !overwrite) {
        error = "The RACache bookmarks file already exists.";
        return false;
    }

    try {
        return atomic_write(path, serialize_bookmarks(items_, address_space_size_, document_json_),
                            error, overwrite);
    } catch (const Json::exception& exception) {
        error = "Could not serialize bookmarks: ";
        error += exception.what();
        return false;
    }
}

void Bookmarks::update(const MemoryReader& memory, uint64_t frame) {
    if (memory.size() > 0) {
        address_space_size_ = memory.size();
    }
    if (last_update_frame_ && frame < *last_update_frame_) {
        for (MemoryBookmark& bookmark : items_) {
            bookmark.last_changed_frame.reset();
        }
    }
    last_update_frame_ = frame;

    for (MemoryBookmark& bookmark : items_) {
        if (!bookmark.supported()) {
            bookmark.current_value.reset();
            bookmark.last_changed_frame.reset();
            continue;
        }
        const auto value =
            decode_memory_value(memory, *bookmark.address, *bookmark.size, bookmark.byte_order);
        if (!value) {
            if (bookmark.current_value) {
                bookmark.previous_value = bookmark.current_value;
            }
            bookmark.current_value.reset();
            bookmark.last_changed_frame.reset();
            continue;
        }

        if (bookmark.current_value) {
            if (*bookmark.current_value != *value) {
                bookmark.previous_value = bookmark.current_value;
                bookmark.last_changed_frame = frame;
            }
        } else if (bookmark.previous_value && *bookmark.previous_value != *value) {
            bookmark.last_changed_frame = frame;
        }
        bookmark.current_value = value;
    }
}

std::optional<uint64_t> Bookmarks::frames_since_change(size_t index, uint64_t frame) const {
    if (index >= items_.size() || !items_[index].last_changed_frame) {
        return std::nullopt;
    }

    const uint64_t changed_at = *items_[index].last_changed_frame;
    if (frame < changed_at || frame - changed_at >= highlight_frames_) {
        return std::nullopt;
    }
    return frame - changed_at;
}

void Bookmarks::clear_highlights() {
    for (MemoryBookmark& bookmark : items_) {
        bookmark.last_changed_frame.reset();
    }
}

} // namespace cw
