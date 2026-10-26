#include "paths.hpp"

#include <SDL3/SDL_log.h>

#include <cstdlib>
#include <system_error>

namespace cw {

namespace {

constexpr const char* kAppDirName = "cheevo-workbench";

// Resolves an XDG base directory: the environment variable if it holds an absolute path (the
// spec says relative values must be ignored), otherwise $HOME/<fallback>.
std::filesystem::path xdg_base_dir(const char* env_var, const char* home_fallback) {
    if (const char* value = std::getenv(env_var); value && value[0] == '/') {
        return value;
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return std::filesystem::path(home) / home_fallback;
    }
    return std::filesystem::current_path();
}

std::filesystem::path ensure_dir(std::filesystem::path dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not create %s: %s", dir.c_str(),
                    ec.message().c_str());
    }
    return dir;
}

} // namespace

std::filesystem::path config_dir() {
    return ensure_dir(xdg_base_dir("XDG_CONFIG_HOME", ".config") / kAppDirName);
}

} // namespace cw
