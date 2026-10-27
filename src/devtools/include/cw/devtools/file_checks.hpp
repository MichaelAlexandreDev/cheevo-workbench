#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace cw {

// Why `path` cannot be used as an input file, or nullopt if it is a regular file (symbolic links
// are followed). Distinguishes the cases a user can act on differently:
//   "File not found: <path>"            nothing there (or a broken symlink)
//   "Not a file: <path>"                a directory or another kind of entry
//   "Cannot access <path>: <reason>"    the system refused to look (e.g. permission denied)
// A check made beforehand cannot stop the file from changing right after; it exists to give a
// clear message for the common mistakes, not as a guarantee.
std::optional<std::string> regular_file_problem(const std::filesystem::path& path);

} // namespace cw
