#pragma once

#include "cw/devtools/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cw {

// One RAIntegration bookmark. A missing address or size means the bookmark is preserved from
// disk but cannot be evaluated by the Watch List (for example, a pointer chain or text value).
struct MemoryBookmark {
    uint64_t row_id = 0; // Stable only for this process; never written to the RAIntegration file.
    std::optional<uint32_t> address;
    std::optional<MemoryValueSize> size;
    ByteOrder byte_order = ByteOrder::LittleEndian;
    bool decimal = false;
    std::string description;
    std::string mem_addr; // Original RA MemAddr, used to explain unsupported entries in the UI.
    std::string unsupported_reason;
    std::optional<uint32_t> current_value;
    std::optional<uint32_t> previous_value;
    std::optional<uint64_t> last_changed_frame;

    bool supported() const { return address.has_value() && size.has_value(); }

  private:
    // Kept as JSON text so unknown fields survive edits to any bookmark.
    std::string original_json;
    bool description_changed = false;
    bool decimal_changed = false;

    friend class Bookmarks;
    friend struct BookmarkFormatAccess;
};

// Ordered, per-game bookmarks. The local file is separate from RACache and uses its JSON format.
// Import/export are explicit; ordinary mutations only write the workbench's own file.
class Bookmarks {
  public:
    static constexpr uint64_t kDefaultHighlightFrames = 60;

    explicit Bookmarks(uint64_t highlight_frames = kDefaultHighlightFrames)
        : highlight_frames_(highlight_frames) {}

    // Loads one game's local file. A missing file is an empty list; malformed/unreadable data
    // leaves writes blocked so the original file cannot be replaced by an empty list.
    void load(std::optional<uint32_t> game_id, const std::filesystem::path& data_dir,
              uint32_t address_space_size = 0);

    bool add(uint32_t address, MemoryValueSize size, ByteOrder byte_order, std::string description,
             std::string& error);
    bool set_description(size_t index, std::string description, std::string& error);
    bool set_decimal(size_t index, bool decimal, std::string& error);
    bool remove(size_t index, std::string& error);
    bool move(size_t from, size_t to, std::string& error);

    bool import_file(const std::filesystem::path& path, std::string& error);
    bool export_file(const std::filesystem::path& path, bool overwrite, std::string& error) const;

    // Reads supported entries once per emulated frame and preserves Watch List change highlights.
    void update(const MemoryReader& memory, uint64_t frame);
    std::optional<uint64_t> frames_since_change(size_t index, uint64_t frame) const;
    void clear_highlights();

    std::optional<uint32_t> game_id() const { return game_id_; }
    const std::vector<MemoryBookmark>& items() const { return items_; }
    const std::string& error() const { return error_; }
    bool writable() const { return file_valid_; }
    size_t size() const { return items_.size(); }
    uint64_t highlight_frames() const { return highlight_frames_; }

    static std::filesystem::path user_file_path(const std::filesystem::path& data_dir,
                                                uint32_t game_id);
    static std::filesystem::path ra_cache_file_path(const std::filesystem::path& ra_cache_dir,
                                                    uint32_t game_id);

  private:
    bool save_items(const std::vector<MemoryBookmark>& items, std::string_view document_json,
                    std::string& error) const;
    bool commit(std::vector<MemoryBookmark> updated, std::string& error,
                std::optional<std::string> document_json = std::nullopt);

    std::optional<uint32_t> game_id_;
    std::filesystem::path user_file_;
    uint32_t address_space_size_ = 0;
    uint64_t next_row_id_ = 1;
    uint64_t highlight_frames_;
    std::optional<uint64_t> last_update_frame_;
    bool file_valid_ = true;
    std::string document_json_ = "{}";
    std::vector<MemoryBookmark> items_;
    std::string error_;
};

} // namespace cw
