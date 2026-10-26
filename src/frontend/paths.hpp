#pragma once

#include <filesystem>

namespace cw {

// Per-user configuration directory, following the XDG Base Directory spec:
// $XDG_CONFIG_HOME/cheevo-workbench, or ~/.config/cheevo-workbench when it is unset.
// The directory is created if it does not exist yet.
std::filesystem::path config_dir();

} // namespace cw
