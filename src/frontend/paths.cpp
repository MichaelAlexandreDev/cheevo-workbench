#include "paths.hpp"

#include <SDL3/SDL_log.h>

#include <cstdlib>
#include <system_error>

namespace cw {

namespace {

constexpr const char* kAppDirName = "cheevo-workbench";

bool is_absolute(const char* path) {
    return path && path[0] == '/';
}

// Resolves <XDG base>/cheevo-workbench: the environment variable if it holds an absolute path
// (the spec says relative values must be ignored), otherwise $HOME/<home_fallback>. Empty when
// neither is usable, or when the directory cannot be created.
std::filesystem::path app_dir(const char* env_var, const char* home_fallback) {
    std::filesystem::path base;
    if (const char* value = std::getenv(env_var); is_absolute(value)) {
        base = value;
    } else if (const char* home = std::getenv("HOME"); is_absolute(home)) {
        base = std::filesystem::path(home) / home_fallback;
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Neither %s nor HOME is an absolute path; not saving files", env_var);
        return {};
    }

    const std::filesystem::path dir = base / kAppDirName;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not create %s: %s", dir.c_str(),
                    ec.message().c_str());
        return {};
    }
    return dir;
}

} // namespace

std::filesystem::path config_dir() {
    return app_dir("XDG_CONFIG_HOME", ".config");
}

std::filesystem::path data_dir() {
    return app_dir("XDG_DATA_HOME", ".local/share");
}

} // namespace cw
