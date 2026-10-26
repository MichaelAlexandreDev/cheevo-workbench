#pragma once

#include <filesystem>

namespace cw {

// Per-user directories following the XDG Base Directory spec. Each one is created if missing.
//
// Both return an EMPTY path when no usable location exists (neither the XDG variable nor $HOME
// is set to an absolute path, or the directory cannot be created). Callers must then skip
// writing files: falling back to the current working directory would scatter files wherever
// the program happened to be started.

// $XDG_CONFIG_HOME/cheevo-workbench, or ~/.config/cheevo-workbench when it is unset.
std::filesystem::path config_dir();

// Data (saves, local sets): $XDG_DATA_HOME/cheevo-workbench, or
// ~/.local/share/cheevo-workbench when it is unset.
std::filesystem::path data_dir();

} // namespace cw
