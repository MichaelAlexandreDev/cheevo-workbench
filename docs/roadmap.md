# Roadmap

Single source of truth for milestones and steps. Issues (`docs/planning/issues.md`, later GitHub)
are derived from this file; when they disagree, update this file first.

Order of priority: get the user's set (*Twilight Syndrome: Tansaku-hen*, PS1, game 18238) into
memory research as early as possible, then logic testing, then authoring.

Status legend: ✅ done · 🟡 in progress · ⏳ planned · 🔒 blocked

## Milestone 1 — Minimal libretro front-end

| Step | Scope | Depends on | Status |
|---|---|---|---|
| 1.1 | Git + submodules, CMake targets (`cheevo-workbench`, `cw_devtools`, `cw_tests`), SDL3 + OpenGL 3.3 window, ImGui docking, XDG config path | — | ✅ |
| 1.2 | Load a core `.so` (dlopen/dlsym, API version), environment callback (dirs, log, pixel format, core options), load/unload content, run frames; command-line paths | 1.1 | ✅ |
| 1.2b | **Synthetic test core**: a tiny libretro core built in `tests/` that emits deterministic frames (each pixel format, padded pitch), audio, input reads and serialization. Lets 1.3–1.6 be tested without ROMs and in CI | 1.2 | ✅ |
| 1.3 | Video: software frames → GL texture in the dock's central area; 0RGB1555/XRGB8888/RGB565; pitch; dupe frames; geometry/aspect changes | 1.2 | ✅ |
| 1.4 | Audio via SDL3 audio stream; pace frames from the core's fps/audio, not from vsync | 1.2 | ✅ |
| 1.5 | Input: keyboard + gamepad → `RETRO_DEVICE_JOYPAD` (incl. `GET_INPUT_BITMASKS`), hot-plug, release on focus loss | 1.2 | ✅ |
| 1.6 | Save states: serialize/unserialize to slots under the data dir, size checks, `SET_SERIALIZATION_QUIRKS`; hotkeys (F1–F4 style) | 1.2 | ⏳ |
| 1.7 | Menus: open core/content (SDL3 file dialog), recent files, pause/resume, reset, **frame advance**, close content; save/load state menu entries | 1.3–1.6 | ⏳ |

**Unlocks for the set:** the game is playable inside the tool.

## Milestone 2 — Memory (start of the dev tools)

| Step | Scope | Depends on | Status |
|---|---|---|---|
| 2.1 | Memory access: `SET_MEMORY_MAPS` + `retro_get_memory_data`, mapped to RA addresses with rcheevos' `rc_libretro` (PS1: `0x000000`–`0x1FFFFF`); platform-independent memory interface in `cw_devtools` | 1.2 | ⏳ |
| 2.2 | Memory viewer: hex view, sizes bit / 4-bit (low/high nibble) / 8 / 16 / 24 / 32, little- and big-endian, highlight changes, **edit values** | 2.1, 1.3, 1.5 | ⏳ |
| 2.3 | Memory search: snapshot, compare to previous / to value (=, ≠, <, >, ≤, ≥), sizes as in 2.2, filtered result list → open in viewer. Unit-tested on synthetic buffers | 2.1, 2.2 | ⏳ |
| 2.4 | Watch list: addresses to follow live while playing | 2.2 | ⏳ |
| 2.5 | Code notes, own storage: the user's notes in our own file (`$XDG_DATA_HOME/cheevo-workbench/notes/<GameID>.json`, documented format), shown in viewer, search results and watch list. Server notes are shown **read-only** from RAIntegration's cache `RACache/Data/<GameID>-Notes.json` (configurable RACache path). A "to publish" list shows notes that are new/changed versus the cache, to copy into RAIntegration | 2.2, 2.3, 2.4, 3.1 | ⏳ |

**Unlocks for the set:** memory research (current area, event flags, items…) can begin, with
notes kept inside the tool from day one.

> **Why notes are not written to `-Notes.json`:** that file is RAIntegration's cache of the
> server's notes, not a local-notes file. RAIntegration writes the server response into it
> (`reference/RAIntegration/src/devkit/context/impl/RcClient.cpp`, `CacheCodeNotesResponse`) and
> only reads it in offline mode (`src/services/impl/OfflineRcClient.cpp`), so anything we wrote
> there would be overwritten on the next download and never uploaded.
>
> RAIntegration does keep *unpublished* note edits locally: as `N0:<address>:"<note>"` lines in
> `<GameID>-User.txt` (`src/devkit/data/models/MemoryNotesModel.cpp`, `Serialize`/`Deserialize`;
> line type `N` in `GameAssets.cpp`), uploaded later with the assets
> (`src/ui/viewmodels/AssetUploadViewModel.cpp`, `QueueMemoryNotes`). The user chose to keep notes
> in our own file anyway (2026-10-26); exporting them as `N0:` lines is listed under
> "Later / optional".
>
> 3.1 (hash + manual game ID) only depends on 1.2, so it can be done before 2.5, which needs the
> game ID to pick the notes file.

## Milestone 3 — rcheevos live testing

| Step | Scope | Depends on | Status |
|---|---|---|---|
| 3.1 | Game hash with `rc_hash` (PS1 = executable name + contents via `SYSTEM.CNF`); game ID entered manually (no server lookup, hard rule 1). `.bin/.cue` only; CHD is a later option | 1.2 | ⏳ |
| 3.2 | Evaluate logic every frame with `rc_runtime` using in-memory definitions (no `-User.txt` yet); trigger notification; measured values; invalid-address reporting | 2.1, 3.1 | ⏳ |
| 3.3 | Store/restore runtime progress (hit counts) together with save states (`rc_runtime_progress`) | 3.2, 1.6 | ⏳ |

**Unlocks for the set:** write a condition and see whether it fires at the right moment.

## Milestone 4 — Achievement editor + RAIntegration local files

| Step | Scope | Depends on | Status |
|---|---|---|---|
| 4.1 | Editable logic model (conditions, flags, sizes, operands, hit targets, Core + Alt groups) and serializer; round-trip tests string → model → identical string, checked against rcheevos parsing | 3.2 | ⏳ |
| 4.2 | Read/write `RACache/Data/<GameID>-User.txt` (version line, achievements, leaderboards), verified against RAIntegration's reader/writer; atomic writes | 4.1 | ⏳ |
| 4.3 | Editor UI: asset list; condition table (flag, type, size, address, comparison, value, hits); Core/Alt tabs; title, description, points | 4.1, 4.2 | ⏳ |
| 4.4 | Live view: per-condition true/false and current hit counts every frame (as RAIntegration shows) | 4.3, 3.2 | ⏳ |

**Unlocks for the set:** the whole set can be developed on Linux; the file opens in the official
RAIntegration for upload.

## Milestone 5 — Notes in the editor + bookmarks

| Step | Scope | Depends on | Status |
|---|---|---|---|
| 5.1 | Code notes (from 2.5) shown in the achievement editor next to each address, and editable from there | 2.5, 4.3 | ⏳ |
| 5.2 | Bookmarks: `RACache/Bookmarks/<GameID>-Bookmarks.json` (separate folder, see RAIntegration `FileLocalStorage.cpp`) | 2.4, 3.1 | ⏳ |

## Milestone 6 — Rich Presence + leaderboards

| Step | Scope | Depends on | Status |
|---|---|---|---|
| 6.1 | Rich Presence editor + live preview, `RACache/Data/<GameID>-Rich.txt` | 3.2 | ⏳ |
| 6.2 | Leaderboards (start/cancel/submit/value) with live testing, stored in `-User.txt` | 4.2, 3.2 | ⏳ |

## Milestone 7 — Server integration 🔒

Blocked until the RA team approves and the user confirms (hard rule 1). Until then: develop here →
open the official RAIntegration (Wine) → it reads the same files → upload from there. If approved,
split into 7a (read-only: login, hash → game ID, fetch assets/notes) and 7b (writes).

## Later / optional

- Hardware-rendered cores (`SET_HW_RENDER`, OpenGL) — not needed for Beetle PSX's software build.
- CHD content for hashing (needs `libchdr`; rcheevos v12.5.0 has no CHD reader).
- Export code notes from our notes file as `N0:` lines in `<GameID>-User.txt`, so RAIntegration
  uploads them with the assets instead of the user copying them by hand (after 4.2, reusing its
  line-preserving writer).
- Core options UI and persisted option values.
- Packaging: AUR `-git` package, AppImage, CI (after milestone 2); license choice before release.
