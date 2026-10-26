---
name: linux-packaging
description: Cross-distro build and distribution for the project - CMake setup, vendored dependencies, AppImage, AUR PKGBUILD, Flatpak, XDG paths, CI. Use when touching CMakeLists, install rules, release scripts, CI workflows or file/config locations.
---

# Linux packaging

Goal: the same app works on Arch, Debian/Ubuntu, Fedora and Steam Deck.

## Build

- CMake ≥ 3.20, C++20. Export `compile_commands.json` (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`).
- Vendor rcheevos, Dear ImGui and libretro-common as git submodules in `third_party/`. Use the
  system SDL (`find_package(SDL2 ...)` / `SDL3`) — every distro ships it.
- Provide `install()` rules: binary to `bin/`, `.desktop` to `share/applications/`, icon to
  `share/icons/hicolor/<size>/apps/`. `cmake --install build --prefix /usr` must work — the
  AUR, AppImage and Flatpak builds all rely on it.

## Paths (XDG Base Directory)

| What | Where |
|---|---|
| Config | `$XDG_CONFIG_HOME/cheevo-workbench/` (default `~/.config/...`) |
| Data (code notes, local sets, RACache-compatible files) | `$XDG_DATA_HOME/cheevo-workbench/` |
| Cache | `$XDG_CACHE_HOME/cheevo-workbench/` |

Never hardcode `/home/<user>`. Let the user configure the cores directory; suggest
`~/.config/retroarch/cores/` if it exists.

## Formats (in order of priority)

1. **AUR** — `PKGBUILD` (a `-git` package first). The user runs Arch, so this is tested first.
2. **AppImage** — build on the oldest supported base (e.g. Ubuntu LTS container in CI) so
   glibc is compatible with older distros; use `linuxdeploy`.
3. **Flatpak** — later, for Flathub/Steam Deck. Needs filesystem permissions for ROM and core
   folders (or the file chooser portal).

## Licenses

rcheevos (MIT), Dear ImGui (MIT), SDL (zlib), libretro-common (MIT). Cores are GPL and are
never bundled. Keep a `THIRD_PARTY_LICENSES` file once dependencies are added.

## CI

GitHub Actions: build + test on every push; on tags, produce the AppImage and attach it to
the release. x86_64 first, aarch64 later.
