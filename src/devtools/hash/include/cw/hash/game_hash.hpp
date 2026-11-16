#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace cw {

// The RetroAchievements hash of a content file: 32 lowercase hex digits that identify one
// version of a game. RA links achievement sets to hashes, so this is how a game is recognised.
//
// Computed offline with rcheevos' rc_hash (no server lookup: the game ID is entered by the user).
// For PS1 (`console_id` = RC_CONSOLE_PLAYSTATION) the content is a .cue/.bin (or .iso) disc: the
// hash is MD5(boot executable name + executable), the executable being the one SYSTEM.CNF boots
// (third_party/rcheevos/src/rhash/hash_disc.c, rc_hash_psx).
//
// Returns nullopt and a human-readable `error` (rc_hash's own messages) when the file cannot be
// read, the content is not valid for the console, or the console is not supported.
std::optional<std::string> compute_game_hash(const std::filesystem::path& content,
                                             uint32_t console_id, std::string& error);

} // namespace cw
