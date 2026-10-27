#include "cw/devtools/file_checks.hpp"

#include <system_error>

namespace cw {

std::optional<std::string> regular_file_problem(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::status(path, ec);
    // A missing file (or a missing directory along the path) is reported as not_found; any other
    // failure to look at the path is an access problem, not an absence.
    if (status.type() == std::filesystem::file_type::not_found ||
        ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) {
        return "File not found: " + path.string();
    }
    if (ec) {
        return "Cannot access " + path.string() + ": " + ec.message();
    }
    if (!std::filesystem::is_regular_file(status)) {
        return "Not a file: " + path.string();
    }
    return std::nullopt;
}

} // namespace cw
