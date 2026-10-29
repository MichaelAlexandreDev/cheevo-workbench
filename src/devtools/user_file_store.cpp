#include "cw/devtools/user_file_store.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cw {
namespace {

struct DiskFile {
    bool missing = false;
    std::string bytes;
    mode_t mode = 0;
};

bool fail_with_errno(const std::filesystem::path& path, std::string_view action, int error_number,
                     std::string& error) {
    error = "Could not " + std::string(action) + " " + path.string() + ": " +
            std::strerror(error_number);
    return false;
}

bool read_regular_file(const std::filesystem::path& path, DiskFile& result, std::string& error) {
    result = {};

    struct stat path_status{};
    if (::lstat(path.c_str(), &path_status) != 0) {
        if (errno == ENOENT) {
            result.missing = true;
            return true;
        }
        return fail_with_errno(path, "inspect", errno, error);
    }

    if (S_ISLNK(path_status.st_mode)) {
        // Never follow a final-component symlink; save replaces the path atomically.
        error = "Refusing to read symbolic link " + path.string();
        return false;
    }
    if (!S_ISREG(path_status.st_mode)) {
        error = "Expected a regular file at " + path.string();
        return false;
    }

    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) {
        if (errno == ENOENT) {
            result.missing = true;
            return true;
        }
        if (errno == ELOOP) {
            error = "Refusing to read symbolic link " + path.string();
            return false;
        }
        return fail_with_errno(path, "open", errno, error);
    }

    struct stat file_status{};
    if (::fstat(descriptor, &file_status) != 0) {
        const int error_number = errno;
        ::close(descriptor);
        return fail_with_errno(path, "inspect", error_number, error);
    }
    if (!S_ISREG(file_status.st_mode)) {
        ::close(descriptor);
        error = "Expected a regular file at " + path.string();
        return false;
    }
    if (file_status.st_size < 0 ||
        static_cast<std::uintmax_t>(file_status.st_size) > kMaxUserFileSize) {
        ::close(descriptor);
        error = "User file exceeds the 16 MiB limit: " + path.string();
        return false;
    }

    result.mode = file_status.st_mode & 0777;
    result.bytes.reserve(static_cast<size_t>(file_status.st_size));

    std::array<char, 8192> buffer{};
    bool read_ok = true;
    while (true) {
        const ssize_t count = ::read(descriptor, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            const int error_number = errno;
            read_ok = false;
            fail_with_errno(path, "read", error_number, error);
            break;
        }
        if (count == 0) {
            break;
        }
        const size_t bytes_read = static_cast<size_t>(count);
        if (bytes_read > kMaxUserFileSize - result.bytes.size()) {
            read_ok = false;
            error = "User file exceeds the 16 MiB limit: " + path.string();
            break;
        }
        result.bytes.append(buffer.data(), bytes_read);
    }

    if (::close(descriptor) != 0 && read_ok) {
        read_ok = fail_with_errno(path, "close after reading", errno, error);
    }
    return read_ok;
}

bool matches_loaded_snapshot(const std::filesystem::path& path,
                             const std::optional<std::string>& loaded_bytes,
                             std::optional<mode_t>& mode, std::string& error) {
    DiskFile current;
    if (!read_regular_file(path, current, error)) {
        return false;
    }

    if (current.missing) {
        if (loaded_bytes) {
            error = "User file was removed after it was loaded; refusing to save: " + path.string();
            return false;
        }
        mode.reset();
        return true;
    }

    if (!loaded_bytes || current.bytes != *loaded_bytes) {
        error = "User file changed after it was loaded; refusing to overwrite external edits: " +
                path.string();
        return false;
    }

    mode = current.mode;
    return true;
}

bool write_all(int descriptor, std::string_view bytes, const std::filesystem::path& temporary_path,
               std::string& error) {
    size_t written = 0;
    while (written < bytes.size()) {
        const size_t remaining = bytes.size() - written;
        const size_t chunk =
            std::min(remaining, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        const ssize_t count = ::write(descriptor, bytes.data() + written, chunk);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            if (count == 0) {
                error = "Could not finish writing " + temporary_path.string();
            } else {
                fail_with_errno(temporary_path, "write", errno, error);
            }
            return false;
        }
        written += static_cast<size_t>(count);
    }
    return true;
}

bool atomic_write(const std::filesystem::path& path, std::string_view bytes,
                  std::optional<mode_t> mode, const std::optional<std::string>& loaded_bytes,
                  std::string& error) {
    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(parent, filesystem_error);
    if (filesystem_error) {
        error = "Could not create parent directory " + parent.string() + ": " +
                filesystem_error.message();
        return false;
    }

    std::string temporary_name =
        (parent / ("." + path.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> temporary_buffer(temporary_name.begin(), temporary_name.end());
    temporary_buffer.push_back('\0');
    const int descriptor = ::mkstemp(temporary_buffer.data());
    if (descriptor < 0) {
        return fail_with_errno(path, "create a temporary file next to", errno, error);
    }
    const std::filesystem::path temporary_path(temporary_buffer.data());

    bool write_ok = true;
    if (mode && ::fchmod(descriptor, *mode) != 0) {
        write_ok = fail_with_errno(temporary_path, "set permissions on", errno, error);
    }
    if (write_ok) {
        write_ok = write_all(descriptor, bytes, temporary_path, error);
    }
    int sync_result = 0;
    do {
        sync_result = write_ok ? ::fsync(descriptor) : 0;
    } while (write_ok && sync_result != 0 && errno == EINTR);
    if (write_ok && sync_result != 0) {
        write_ok = fail_with_errno(temporary_path, "flush", errno, error);
    }

    if (::close(descriptor) != 0 && write_ok) {
        write_ok = fail_with_errno(temporary_path, "close", errno, error);
    }
    if (!write_ok) {
        ::unlink(temporary_path.c_str());
        return false;
    }

    // Staging a large file may take time: check again immediately before replacement.
    // This detects ordinary external edits, but is not a lock shared with other writers.
    std::optional<mode_t> current_mode;
    if (!matches_loaded_snapshot(path, loaded_bytes, current_mode, error) || current_mode != mode) {
        if (error.empty())
            error = "User file permissions changed while preparing the save";
        ::unlink(temporary_path.c_str());
        return false;
    }
    if (::rename(temporary_path.c_str(), path.c_str()) != 0) {
        fail_with_errno(path, "replace", errno, error);
        ::unlink(temporary_path.c_str());
        return false;
    }

    error.clear();
    return true;
}

} // namespace

bool UserFileStore::load(const std::filesystem::path& path, std::string& error) {
    path_ = path;
    document_ = {};
    loaded_bytes_.reset();
    writable_ = false;

    if (path.empty() || path.native().find('\0') != std::string::npos) {
        error = "User file path is empty or contains an embedded NUL byte.";
        return false;
    }
    std::error_code path_error;
    path_ = std::filesystem::absolute(path, path_error);
    if (path_error) {
        error = "Could not resolve user file path: " + path_error.message();
        return false;
    }

    DiskFile disk_file;
    if (!read_regular_file(path_, disk_file, error)) {
        return false;
    }
    if (disk_file.missing) {
        writable_ = true;
        error.clear();
        return true;
    }

    auto parsed = parse_user_file(disk_file.bytes, error);
    if (!parsed) {
        return false;
    }

    document_ = std::move(*parsed);
    loaded_bytes_ = std::move(disk_file.bytes);
    writable_ = true;
    error.clear();
    return true;
}

bool UserFileStore::save(const UserFileDocument& document, std::string& error) {
    if (!writable_) {
        error = "User file store is not writable; load a valid path before saving.";
        return false;
    }

    auto serialized = serialize_user_file(document, error);
    if (!serialized) {
        return false;
    }

    if (serialized->size() > kMaxUserFileSize) {
        error = "Serialized user file exceeds the 16 MiB limit.";
        return false;
    }

    auto saved_document = parse_user_file(*serialized, error);
    if (!saved_document)
        return false;
    std::optional<std::string> saved_bytes(*serialized);

    std::optional<mode_t> mode;
    if (!matches_loaded_snapshot(path_, loaded_bytes_, mode, error)) {
        return false;
    }

    if (!atomic_write(path_, *serialized, mode, loaded_bytes_, error)) {
        return false;
    }

    document_ = std::move(*saved_document);
    loaded_bytes_ = std::move(saved_bytes);
    error.clear();
    return true;
}

} // namespace cw
