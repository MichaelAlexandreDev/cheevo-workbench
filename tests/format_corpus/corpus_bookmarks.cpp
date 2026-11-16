// Bookmarks: each fixture is used twice.
// - Imported from RACache, edited, and exported back (export.json, edited-export.json), which also
//   writes our per-game file under import/.
// - Placed as our own per-game file and loaded, then edited, which shows that a malformed file
//   blocks writes instead of being replaced (under local/).

#include "corpus_support.hpp"

#include "cw/formats/bookmarks.hpp"
#include "cw/memory/memory.hpp"

namespace cw_corpus {
namespace {

// PlayStation-sized address space, so addresses beyond 2 MiB are reported as unsupported.
constexpr uint32_t kAddressSpace = 0x200000;

void describe(cw::Bookmarks& bookmarks, std::string_view name, Results& results) {
    size_t supported = 0;
    for (const auto& item : bookmarks.items()) {
        supported += item.supported() ? 1 : 0;
        if (!item.unsupported_reason.empty()) {
            results.add("bookmarks", name,
                        "unsupported " + item.mem_addr + ": " + item.unsupported_reason);
        }
    }
    results.add("bookmarks", name,
                std::to_string(bookmarks.size()) + " bookmarks, " + std::to_string(supported) +
                    " supported");
}

// Edits the first bookmark and appends one; reports each refusal.
void edit(cw::Bookmarks& bookmarks, std::string_view name, Results& results) {
    std::string error;
    if (bookmarks.size() > 0) {
        const bool decimal = bookmarks.items()[0].decimal;
        if (!bookmarks.set_description(0, "Edited: description", error) ||
            !bookmarks.set_decimal(0, !decimal, error)) {
            results.add("bookmarks", name, "edit refused: " + error);
        }
    }
    if (!bookmarks.add(0x1234, cw::MemoryValueSize::SixteenBit, cw::ByteOrder::BigEndian, "Added",
                       error)) {
        results.add("bookmarks", name, "add refused: " + error);
    }
}

void imported(const std::filesystem::path& source, const std::filesystem::path& dir,
              std::string_view name, Results& results) {
    cw::Bookmarks bookmarks;
    bookmarks.load(kGameId, dir / "data", kAddressSpace);
    std::string error;
    if (!bookmarks.import_file(source, error)) {
        results.add("bookmarks", name, "import failed: " + error);
        return;
    }
    describe(bookmarks, name, results);
    if (!bookmarks.export_file(dir / "export.json", true, error)) {
        results.add("bookmarks", name, "export failed: " + error);
    }
    edit(bookmarks, name, results);
    if (!bookmarks.export_file(dir / "edited-export.json", true, error)) {
        results.add("bookmarks", name, "edited export failed: " + error);
    }
}

void local(const std::filesystem::path& source, const std::filesystem::path& dir,
           std::string_view name, Results& results) {
    write_file(cw::Bookmarks::user_file_path(dir, kGameId), read_file(source));
    cw::Bookmarks bookmarks;
    bookmarks.load(kGameId, dir, kAddressSpace);
    if (!bookmarks.error().empty()) {
        results.add("bookmarks", name, "local load error: " + bookmarks.error());
    }
    results.add("bookmarks", name,
                bookmarks.writable() ? "local file writable" : "local file read-only");
    edit(bookmarks, name, results);
}

} // namespace

void run_bookmarks(const std::filesystem::path& input, const std::filesystem::path& out,
                   Results& results) {
    for (const auto& source : files_in(input / "bookmarks", ".json")) {
        const std::string name = source.stem().string();
        imported(source, out / "bookmarks" / name / "import", name, results);
        local(source, out / "bookmarks" / name / "local", name, results);
    }
}

} // namespace cw_corpus
