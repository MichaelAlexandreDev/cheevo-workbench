# cheevo-workbench

A native Linux workbench for developing [RetroAchievements](https://retroachievements.org) achievement sets.

> **Unofficial community project.** Not affiliated with or endorsed by RetroAchievements.

## Why

Playing with achievements already works on Linux (RetroArch, DuckStation, PCSX2…), but the
**developer tools** (RALibretro + RAIntegration) are Windows-only. Linux developers currently
depend on Wine. This project aims to provide native developer tooling that produces output
**fully compatible** with the official Windows toolkit, so Linux and Windows developers can
work on the same sets.

## Approach

A small libretro front-end with built-in developer tools:

```
C++ + CMake
├── SDL3          → window, input, audio (X11/Wayland/PipeWire)
├── Dear ImGui    → developer UI
├── rcheevos      → achievement evaluation, hashing, Rich Presence
└── libretro API  → loads any libretro core (.so)
Distribution: AppImage + AUR → Flatpak later
```

## Roadmap

| # | Milestone | Status |
|---|---|---|
| 1 | Minimal libretro front-end: load core + content, video, audio, input, save states | 🟡 |
| 2 | Memory viewer + memory search (bits, 4/8/16/24/32-bit, filters), watch list, code notes | ⏳ |
| 3 | rcheevos integration: game hash, live achievement testing | ⏳ |
| 4 | Achievement editor reading/writing RAIntegration local files (`<GameID>-User.txt`) | ⏳ |
| 5 | Code notes in the achievement editor + memory bookmarks | ⏳ |
| 6 | Rich Presence + leaderboards | ⏳ |
| 7 | Server integration (**only with RetroAchievements team approval**) | 🔒 |

Step-by-step breakdown, dependencies and status: [`docs/roadmap.md`](docs/roadmap.md).

After milestone 4, an achievement set can be developed entirely on Linux and uploaded
through the official RAIntegration (via Wine), which reads the same local files.

## Building

Requirements: a C++20 compiler (GCC or Clang), CMake ≥ 3.20, SDL3 and OpenGL headers.
On Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake ninja sdl3 mesa
```

rcheevos, Dear ImGui, doctest and libretro-common are vendored as git submodules:

```sh
git clone --recursive <repo-url>          # or, in an existing clone:
git submodule update --init --recursive

cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/frontend/cheevo-workbench
```

Settings and window layout are stored in `~/.config/cheevo-workbench/`.

## Default controls

All inputs drive player 1's RetroPad (libretro's virtual controller); on PS1, B/A/Y/X are
Cross/Circle/Square/Triangle.

| RetroPad | Keyboard | Gamepad (SDL standard layout) |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| B / A / Y / X | Z / X / A / S | South / East / West / North |
| L / R | Q / W | Shoulders |
| L2 / R2 | E / R | Triggers (past ~50%) |
| L3 / R3 | 1 / 2 | Stick clicks |
| Start / Select | Enter / Right Shift | Start / Back |

Save states: **Shift+F1–F4** saves slot 1–4, **F1–F4** loads it. States are stored in
`~/.local/share/cheevo-workbench/states/` in RALibretro's `RASTATE` format.

Typing into a text field of the tool does not reach the game. Without a working audio server the
app may wait at startup; `SDL_AUDIO_DRIVER=dummy` runs it without sound.

## Non-goals and rules

- No ROMs, BIOS files or copyrighted game data in this repository.
- libretro cores are **not** bundled; users point the app to cores they already have.
- No uploads to RetroAchievements servers without explicit approval from the RA team.
- Not presented as an official RetroAchievements product.

## Author

SrStarrk777 — RetroAchievements Junior Developer.

## License

TBD (MIT or GPL, to be decided before the first release).
