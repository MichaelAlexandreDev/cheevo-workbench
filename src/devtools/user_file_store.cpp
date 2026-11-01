#include "cw/devtools/user_file_store.hpp"

#include "cw/devtools/user_file.hpp"

#include <utility>

namespace cw {

bool UserFileStore::load(const std::filesystem::path& path, std::string& error) {
    document_ = {};
    writable_ = false;

    if (!text_store_.load(path, kMaxUserFileSize, error)) {
        return false;
    }
    if (text_store_.missing()) {
        writable_ = true;
        error.clear();
        return true;
    }

    auto parsed = parse_user_file(text_store_.bytes(), error);
    if (!parsed) {
        return false;
    }

    document_ = std::move(*parsed);
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
    if (!saved_document) {
        return false;
    }

    if (!text_store_.save(*serialized, error)) {
        return false;
    }

    document_ = std::move(*saved_document);
    error.clear();
    return true;
}

} // namespace cw
