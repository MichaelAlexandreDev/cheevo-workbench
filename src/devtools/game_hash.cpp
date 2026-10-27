#include "cw/devtools/game_hash.hpp"

#include "cw/devtools/file_checks.hpp"

#include <rc_hash.h>

#include <utility>

namespace cw {

namespace {

// rc_hash reports problems through a callback; collect them for the caller. The iterator's
// userdata points to the string being filled.
void RC_CCONV collect_error(const char* message, const rc_hash_iterator_t* iterator) {
    auto& errors = *static_cast<std::string*>(iterator->userdata);
    if (!errors.empty()) {
        errors += "; ";
    }
    errors += message;
}

} // namespace

std::optional<std::string> compute_game_hash(const std::filesystem::path& content,
                                             uint32_t console_id, std::string& error) {
    error.clear();
    if (auto problem = regular_file_problem(content)) {
        error = std::move(*problem);
        return std::nullopt;
    }

    rc_hash_iterator_t iterator;
    rc_hash_initialize_iterator(&iterator, content.c_str(), nullptr, 0);
    iterator.userdata = &error;
    iterator.callbacks.error_message = &collect_error;
    iterator.callbacks.verbose_message = nullptr;

    char hash[33] = {};
    const bool ok = rc_hash_generate(hash, console_id, &iterator) != 0;
    rc_hash_destroy_iterator(&iterator);

    if (!ok) {
        if (error.empty()) {
            error = "Could not compute a RetroAchievements hash for this content";
        }
        return std::nullopt;
    }
    error.clear(); // a successful hash may still have logged recoverable problems
    return std::string(hash);
}

} // namespace cw
