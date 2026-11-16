// RASTATE save states (RALibretro's container, see src/frontend/save_state.hpp). The inputs are
// generated here from synthetic bytes, because state files must never be committed; the written
// files are stored as hex dumps.

#include "corpus_support.hpp"

#include "save_state.hpp"

#include <vector>

namespace cw_corpus {
namespace {

std::vector<uint8_t> pattern(size_t size, uint8_t seed) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<uint8_t>(seed + i * 37);
    }
    return bytes;
}

void parse(std::string_view name, const std::vector<uint8_t>& file, Results& results) {
    std::string error;
    const auto contents = cw::parse_state_file(file, error);
    if (!contents) {
        results.add("state", name, "rejected: " + error);
        return;
    }
    // The pattern check proves the bytes came back, not just the sizes.
    std::string observation = "core " + std::to_string(contents->core.size()) + " bytes";
    observation += contents->core == pattern(contents->core.size(), 1) ? "" : " (changed)";
    observation +=
        contents->achievements
            ? ", achievements " + std::to_string(contents->achievements->size()) + " bytes"
            : ", no achievements block";
    results.add("state", name, observation);
}

void written(std::string_view name, size_t core, size_t achievements,
             const std::filesystem::path& out, Results& results) {
    const auto file = cw::write_state_file(pattern(core, 1), pattern(achievements, 2));
    write_file(out / "state" / (std::string(name) + ".hex"), hex_dump(file));
    parse(name, file, results);
}

std::vector<uint8_t> bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

} // namespace

void run_save_states(const std::filesystem::path& out, Results& results) {
    // Block data is padded to a multiple of 8, so sizes around 8 matter.
    written("core-1", 1, 0, out, results);
    written("core-8", 8, 0, out, results);
    written("core-9-achievements-3", 9, 3, out, results);
    written("core-16-achievements-8", 16, 8, out, results);

    const auto valid = cw::write_state_file(pattern(9, 1), pattern(3, 2));
    parse("raw-state", pattern(24, 1), results);
    parse("truncated-header", {valid.begin(), valid.begin() + 5}, results);
    auto version = valid;
    version[7] = 2;
    parse("unknown-version", version, results);
    parse("truncated-block", {valid.begin(), valid.end() - 4}, results);
    parse("no-mem-block",
          bytes(std::string_view("RASTATE\x01"
                                 "END \0\0\0\0",
                                 16)),
          results);
    parse("no-end-block", {valid.begin(), valid.end() - 8}, results);
}

} // namespace cw_corpus
