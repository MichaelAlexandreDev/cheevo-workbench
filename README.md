# cheevo-workbench

A native Linux workbench for developing [RetroAchievements](https://retroachievements.org)
achievement sets: a small libretro front-end with built-in developer tools whose files are
compatible with the official Windows toolkit (RALibretro + RAIntegration).

> **Unofficial community project.** Not affiliated with or endorsed by RetroAchievements.
> It never talks to the RetroAchievements servers.

**Status:** early development, no release yet. The tools below work and are covered by unit
tests; some of them are still waiting for manual validation. Expect rough edges.

## Why

Playing with achievements already works on Linux (RetroArch, DuckStation, PCSX2…), but the
**developer tools** (RALibretro + RAIntegration) are Windows-only, so Linux developers depend on
Wine. cheevo-workbench lets you do the research and the logic work natively, and writes the same
local files RAIntegration reads, so Linux and Windows developers can work on the same sets.

## Features

- **libretro front-end** — loads any software-rendered libretro core (`.so`) you already have;
  video, audio, keyboard and gamepad input, pause, frame advance, reset, recent files.
- **Save states** — four slots in RALibretro's `RASTATE` format, including achievement progress
  (hit counts).
- **Memory viewer** — hex view in bit, nibble, 8/16/24/32-bit sizes, little- and big-endian,
  change highlighting and value editing.
- **Memory search** — compare against the previous, first or a typed value, per-bit candidates,
  alignment, region filters and undo.
- **Assisted search** *(experimental)* — per-frame statistics that rank flag, area and counter
  candidates; mark events with Ctrl+M.
- **Watch list and bookmarks** — bookmarks use RAIntegration's
  `RACache/Bookmarks/<GameID>-Bookmarks.json`.
- **Code notes** — your own notes stored locally, server notes shown read-only from
  RAIntegration's cache, and a "to publish" list of notes that changed.
- **Live achievement testing** — game hashing and per-frame evaluation with
  [rcheevos](https://github.com/RetroAchievements/rcheevos), with trigger notifications and
  measured values.
- **Achievement editor** — condition table with Core/Alt groups, flags, sizes and hit targets;
  live true/false state and hit counts for each condition. Reads and writes
  `RACache/Data/<GameID>-User.txt`.
- **Rich Presence and leaderboards** — editor with live preview (`<GameID>-Rich.txt`) and
  leaderboard testing (start / cancel / submit / value).
- **Readable UI** — adjustable font size (10–48), system font choice and optional Japanese glyphs
  from an installed font.

## Workflow

```
cheevo-workbench (Linux)                RAIntegration (Windows or Wine)
  research memory, write logic,  ──►     opens the same RACache files
  test achievements live                 and uploads them to the site
```

Uploading stays in the official toolkit on purpose: this project does not include any server
communication.

## Building

Requirements: a C++20 compiler (GCC or Clang), CMake ≥ 3.20, SDL3 and OpenGL headers.
On Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake ninja sdl3 mesa
```

rcheevos, Dear ImGui, nlohmann/json, doctest and libretro-common are vendored as git submodules:

```sh
git clone --recursive https://github.com/MichaelAlexandreDev/cheevo-workbench.git
cd cheevo-workbench

cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/frontend/cheevo-workbench
```

In an existing clone, run `git submodule update --init --recursive` first.

You need a libretro core and a game you own. The tool was developed with Beetle PSX (software
renderer) on PlayStation games; other software-rendered cores should work, but have seen less
testing.

## Files and folders

| What | Where |
|---|---|
| Settings, window layout, recent files | `~/.config/cheevo-workbench/` |
| Save states | `~/.local/share/cheevo-workbench/states/` |
| Your code notes | `~/.local/share/cheevo-workbench/notes/<GameID>.json` |
| Achievements, leaderboards, Rich Presence, bookmarks | your RAIntegration `RACache/` folder (configurable) |

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

| Action | Key |
|---|---|
| Save state slot 1–4 | Shift+F1–F4 |
| Load state slot 1–4 | F1–F4 |
| Pause / resume | P |
| Frame advance (while paused) | N |
| Reset | Ctrl+R |
| Mark event (assisted search) | Ctrl+M |

Typing into a text field of the tool does not reach the game. Without a working audio server the
app may wait at startup; `SDL_AUDIO_DRIVER=dummy` runs it without sound.

## Roadmap

| # | Milestone | Status |
|---|---|---|
| 1 | Minimal libretro front-end | ✅ |
| 2 | Memory viewer, search, watch list, code notes | ✅ (assisted search awaiting validation) |
| 3 | rcheevos integration: hashing, live testing | ✅ |
| 4 | Achievement editor + RAIntegration local files | ✅ |
| 5 | Code notes in the editor + bookmarks | ✅ |
| 6 | Rich Presence + leaderboards | ✅ |
| 7 | Server integration (**only with RetroAchievements team approval**) | 🔒 |

Next up: packaging (AUR `-git`, AppImage), CHD hashing, a core options UI. Step-by-step
breakdown: [`docs/roadmap.md`](docs/roadmap.md).

## Rules this project follows

- No ROMs, BIOS files or copyrighted game data in the repository; tests use synthetic memory
  buffers and a tiny test core.
- libretro cores are **not** bundled; you point the app to cores you already have.
- No uploads to RetroAchievements servers without explicit approval from the RA team.
- Written from scratch: RALibretro and RAIntegration are read only to verify file formats and
  behavior, never copied.

## Author

SrStarrk777 — RetroAchievements Junior Developer.

## License

[MIT](LICENSE). The vendored dependencies under `third_party/` keep their own licenses.
