# Roadmap issues

Drafts derived from [`docs/roadmap.md`](../roadmap.md), the single source of truth. Statuses and
implementation dependencies match its tables; test-enablement links to 1.2b are called out
separately. No issue has been opened in an external tracker.

Project-wide constraints: write new code from scratch; use synthetic memory, content metadata, and
text fixtures in tests; do not add ROMs, BIOS images, copyrighted game data, or production cores;
do not contact RetroAchievements servers before the approval required by milestone 7. Keep
platform-independent memory and file-format logic in `cw_devtools`.

## Milestone 1 — Minimal libretro front-end

### [1.1] Establish the project skeleton and SDL3/ImGui window — done

- **Objective:** Provide a reproducible C++20 build, test target, and minimal native window.
- **Scope:** CMake targets for the app, `cw_devtools`, and `cw_tests`; vendored dependency targets;
  SDL3/OpenGL window; ImGui docking, menu, status window, and XDG config path.
- **Acceptance criteria:** Configure with `cmake -S . -B build -G Ninja`, build, and pass
  `ctest --test-dir build --output-on-failure`; launching the app shows the menu and status
  window; an absolute `XDG_CONFIG_HOME` stores the ImGui layout under `cheevo-workbench/`; with no
  usable XDG/HOME path, layout persistence is disabled and nothing is written to the working
  directory.
- **Files to study:**
  - `reference/RALibretro/Makefile`, `reference/RALibretro/src/Application.cpp`
  - `third_party/imgui/examples/example_sdl3_opengl3/main.cpp`, `third_party/imgui/imgui.h`,
    `third_party/doctest/doctest/doctest.h`
- **Dependencies:** None.
- **Out of scope:** Loading cores or content, game video/audio/input, save states, and developer
  tools.

### [1.2] Load a core, content, and run frames — done

- **Objective:** Load a user-provided libretro `.so`, load content, and drive the core lifecycle.
- **Scope:** `dlopen`/`dlsym`; API-version check; environment callback for directories, logging,
  pixel format, and core options; video/audio/input callback registration; content load/unload;
  frame execution; command-line core and content paths.
- **Acceptance criteria:** A user-provided compatible core loads from a command-line path and
  accepts a content path; its API version and required symbols are checked; frames advance; unload
  calls the core lifecycle in order; invalid paths or a bad core report an error without crashing.
- **Files to study:**
  - `reference/RALibretro/src/libretro/Core.cpp`, `reference/RALibretro/src/libretro/Core.h`,
    `reference/RALibretro/src/Emulator.cpp`
  - `third_party/libretro-common/include/libretro.h`,
    `third_party/rcheevos/src/rc_libretro.c`
- **Dependencies:** 1.1.
- **Out of scope:** Rendering frames, audio playback/pacing, controller mapping, save-state UI,
  core-options UI, and server access.

### [1.2b] Build a synthetic libretro test core — done

- **Objective:** Provide a deterministic core fixture for automated front-end tests without ROMs or
  BIOS files.
- **Scope:** A small project-authored core shared library in `tests/`; deterministic software video
  output in all three supported pixel formats with padded pitch; synthetic audio; observable input
  reads; deterministic serialization/unserialization.
- **Acceptance criteria:** The test target builds in CI; the front-end can load and unload it; tests
  verify emitted dimensions, pixel format, padded pitch, audio samples, input values, and
  serialize/restore state without any external content file.
- **Files to study:**
  - `reference/RALibretro/src/libretro/Core.cpp`,
    `reference/RALibretro/src/libretro/Core.h`
  - `third_party/libretro-common/include/libretro.h`,
    `third_party/doctest/doctest/doctest.h`
- **Dependencies:** 1.2.
- **Out of scope:** A production emulator core, ROM/BIOS assets, testing RetroAchievements server
  APIs, and UI features beyond what is needed to load the fixture.

### [1.3] Render software video frames — planned

- **Objective:** Display libretro software frames as an OpenGL texture in the dock's central area.
- **Scope:** Support 0RGB1555, XRGB8888, and RGB565; respect source pitch; handle duplicate
  frames; update geometry and aspect ratio; return `false` cleanly for unsupported
  `SET_HW_RENDER` requests.
- **Acceptance criteria:** The synthetic core from 1.2b emits each supported format and the
  expected pixels appear in the viewport; padded rows do not shift pixels; duplicate frames retain
  the previous image; geometry/aspect changes update the displayed image; a hardware-render request
  is rejected without crashing or leaving partial state.
- **Files to study:**
  - `reference/RALibretro/src/components/Video.cpp`,
    `reference/RALibretro/src/libretro/Core.cpp`
  - `third_party/libretro-common/include/libretro.h`,
    `third_party/imgui/backends/imgui_impl_opengl3.cpp`
- **Dependencies:** 1.2 (roadmap); 1.2b for automated acceptance.
- **Out of scope:** Hardware rendering, shaders, filters, screenshots, and video recording.

### [1.4] Stream audio and pace frames — planned

- **Objective:** Play core audio and schedule frames from core timing rather than display vsync.
- **Scope:** Queue libretro audio through an SDL3 audio stream; use the core's FPS and audio
  timing for frame pacing; silence audio while the front-end is paused.
- **Acceptance criteria:** The synthetic core from 1.2b produces the expected sample rate, channel
  order, and sample sequence; measured frame timing follows the configured core FPS/audio timing
  rather than monitor refresh; the audio stream is silent while paused; audio-device failure is
  reported without blocking the UI.
- **Files to study:**
  - `reference/RALibretro/src/components/Audio.cpp`,
    `reference/RALibretro/src/libretro/Core.cpp`
  - `third_party/libretro-common/include/libretro.h`
- **Dependencies:** 1.2 (roadmap); 1.2b for automated acceptance.
- **Out of scope:** Pause/resume controls, which belong to 1.7; recording, DSP/effects, and mixer
  settings.

### [1.5] Map keyboard and gamepad input to libretro joypad — planned

- **Objective:** Send keyboard and gamepad input as `RETRO_DEVICE_JOYPAD` state.
- **Scope:** Default keyboard/gamepad bindings; joypad bitmask support including
  `GET_INPUT_BITMASKS`; hot-plug handling; release all buttons when focus is lost or a device is
  disconnected.
- **Acceptance criteria:** The synthetic core from 1.2b observes the expected joypad bits for each
  default binding, including a bitmask read; disconnected devices and focus loss release pressed
  inputs; reconnecting a device restores input without restarting the core.
- **Files to study:**
  - `reference/RALibretro/src/components/Input.cpp`,
    `reference/RALibretro/src/components/Input.h`,
    `reference/RALibretro/src/libretro/Core.cpp`
  - `third_party/libretro-common/include/libretro.h`,
    `third_party/imgui/backends/imgui_impl_sdl3.cpp`
- **Dependencies:** 1.2 (roadmap); 1.2b for automated acceptance.
- **Out of scope:** Analog devices, remapping UI, per-game profiles, controller vibration, and
  non-joypad libretro device types.

### [1.6] Add save-state slots and hotkeys — planned

- **Objective:** Save and restore core state in slots, including cores' serialization quirks.
- **Scope:** Serialize/unserialize into slots under the data directory; query and validate state
  size; honor `SET_SERIALIZATION_QUIRKS`; provide documented F1–F4-style save/load hotkeys.
- **Acceptance criteria:** The synthetic core from 1.2b can be saved after a known frame, advanced,
  and restored to the same core state; wrong-sized or truncated state data is rejected safely;
  serialization quirks are read and applied; hotkeys save and load the documented slots.
- **Files to study:**
  - `reference/RALibretro/src/States.cpp`, `reference/RALibretro/src/States.h`,
    `reference/RALibretro/src/libretro/Core.cpp`
  - `third_party/libretro-common/include/libretro.h`
- **Dependencies:** 1.2 (roadmap); 1.2b for automated acceptance.
- **Out of scope:** Save/load menu entries, which belong to 1.7; rewind, cloud sync, and save-RAM
  management.

### [1.7] Add menus for core/content and session controls — planned

- **Objective:** Make core and session operations available through the app UI.
- **Scope:** SDL3 file dialogs for core/content; recent files; pause/resume; reset; frame advance;
  close content; save/load state menu entries for 1.6 slots.
- **Acceptance criteria:** A manual smoke test opens a core and content, exercises recent files,
  pause/resume, reset, and one-frame advance, closes content, and uses the save/load menu entries;
  canceling a dialog leaves the current session unchanged; invalid selections show an actionable
  error.
- **Files to study:**
  - `reference/RALibretro/src/Application.cpp`, `reference/RALibretro/src/Emulator.cpp`,
    `reference/RALibretro/src/KeyBinds.cpp`
  - `third_party/imgui/examples/example_sdl3_opengl3/main.cpp`,
    `third_party/imgui/backends/imgui_impl_sdl3.cpp`
- **Dependencies:** 1.3–1.6.
- **Out of scope:** Playlists, core downloads, saved core-option UI, and automatic core
  configuration.

## Milestone 2 — Memory (start of the dev tools)

### [2.1] Expose core memory through an RA-addressed interface — planned

- **Objective:** Make the active core's memory available to dev tools through a platform-independent
  `cw_devtools` interface.
- **Scope:** Handle `SET_MEMORY_MAPS` and `retro_get_memory_data`/`retro_get_memory_size`; use
  rcheevos `rc_libretro` to map regions to RA addresses; cover PS1 main RAM at `0x000000`–
  `0x1FFFFF`.
- **Acceptance criteria:** Synthetic memory regions map to the expected RA address ranges; PS1's
  synthetic 2 MiB region is readable at both endpoints; unmapped and out-of-range reads fail
  safely; `cw_devtools` tests run without SDL, OpenGL, or a game.
- **Files to study:**
  - `reference/RAIntegration/src/devkit/context/impl/EmulatorMemoryContext.cpp`,
    `reference/RAIntegration/src/devkit/data/models/MemoryRegionsModel.cpp`,
    `reference/RAIntegration/src/devkit/data/MemoryRegion.hh`
  - `third_party/rcheevos/src/rc_libretro.h`, `third_party/rcheevos/src/rc_libretro.c`,
    `third_party/libretro-common/include/libretro.h`
- **Dependencies:** 1.2.
- **Out of scope:** Viewer UI, memory edits, search, and watch lists.

### [2.2] Add the memory viewer and value editing — planned

- **Objective:** Inspect and edit supported values in the live RA-addressed memory map.
- **Scope:** Hex view; bit and low/high 4-bit nibble, 8-, 16-, 24-, and 32-bit values; little- and
  big-endian interpretations for multi-byte values; highlight changes; edit values in writable
  regions.
- **Acceptance criteria:** Synthetic buffers show the expected bytes and values for every listed
  size and endian mode; changed bytes are highlighted after a frame update; edits change only the
  selected writable bytes; read-only, unmapped, and boundary-crossing edits are rejected safely.
- **Files to study:**
  - `reference/RAIntegration/src/devkit/data/Memory.hh`,
    `reference/RAIntegration/src/ui/viewmodels/MemoryViewerViewModel.cpp`,
    `reference/RAIntegration/src/ui/win32/bindings/MemoryViewerControlBinding.cpp`,
    `reference/RAIntegration/src/services/Search/SearchImpl_4bit.hh`,
    `reference/RAIntegration/src/services/Search/SearchImpl_24bit.hh`
  - `third_party/rcheevos/include/rc_runtime_types.h`,
    `third_party/rcheevos/src/rcheevos/operand.c`,
    `third_party/libretro-common/include/libretro.h`
- **Dependencies:** 2.1, 1.3, and 1.5.
- **Out of scope:** Float/MBF display, pointer scanning, notes, and writing to memory regions that
  are not writable.

### [2.3] Search and filter memory snapshots — planned

- **Objective:** Find addresses by comparing memory snapshots or comparing values to a constant.
- **Scope:** Compare to previous snapshot or a value with `=`, `≠`, `<`, `>`, `≤`, and `≥`; support
  the memory sizes defined in 2.2; show filtered results and provide a handoff to open a selected
  address in the viewer.
- **Acceptance criteria:** Unit tests over synthetic buffers return the exact expected addresses
  for every comparison and supported size; byte order and alignment are covered; selecting a result
  exposes its address for the 2.2 viewer; memory-size changes invalidate stale results safely.
- **Files to study:**
  - `reference/RAIntegration/src/services/Search/SearchImpl.cpp`,
    `reference/RAIntegration/src/services/Search/SearchImpl_4bit.hh`,
    `reference/RAIntegration/src/services/Search/SearchImpl_16bit_be.hh`,
    `reference/RAIntegration/src/services/Search/SearchImpl_24bit.hh`,
    `reference/RAIntegration/src/ui/viewmodels/MemorySearchViewModel.cpp`,
    `reference/RAIntegration/tests/ui/viewmodels/MemorySearchViewModel_Tests.cpp`
  - `third_party/rcheevos/include/rc_runtime_types.h`,
    `third_party/libretro-common/include/libretro.h`,
    `third_party/doctest/doctest/doctest.h`
- **Dependencies:** 2.1 and 2.2.
- **Out of scope:** Pointer search, text search, float/MBF search, and saved search sessions.

### [2.4] Add a live memory watch list — planned

- **Objective:** Keep selected addresses visible and updated while the core is running.
- **Scope:** Add/remove watched addresses from the memory viewer and show each address's selected
  value interpretation and current value on each frame.
- **Acceptance criteria:** A synthetic memory sequence updates watched values after each frame;
  removing an item stops updates; invalidated memory regions show an unavailable state instead of
  reading stale pointers.
- **Files to study:**
  - `reference/RAIntegration/src/ui/viewmodels/MemoryWatchListViewModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/MemoryWatchViewModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/MemoryViewerViewModel.cpp`
  - `third_party/rcheevos/src/rc_libretro.h`,
    `third_party/libretro-common/include/libretro.h`
- **Dependencies:** 2.2.
- **Out of scope:** Persisted watch lists, pointer chains, and note editing.

### [2.5] Store and integrate code notes — planned

- **Objective:** Keep the user's notes in the workbench while showing RAIntegration's cached
  server notes as read-only reference data.
- **Scope:** Store workbench notes in the documented JSON format at
  `$XDG_DATA_HOME/cheevo-workbench/notes/<GameID>.json`; read server notes, without modifying
  them, from the configurable `RACache/Data/<GameID>-Notes.json`; show notes in the memory viewer,
  search results, and watch list; provide a “to publish” list comparing workbench notes with the
  cache for copying into RAIntegration.
- **Acceptance criteria:** Synthetic fixtures round-trip through the documented workbench JSON
  format; the selected GameID determines both paths; viewer, search results, and watch list show
  the expected note at each address; the publish list marks new or changed notes against the cache;
  missing or malformed cache data is reported without changing it; no network request or write to
  `-Notes.json` occurs.
- **Files to study:**
  - `reference/RAIntegration/src/devkit/data/models/MemoryNotesModel.cpp`,
    `reference/RAIntegration/src/devkit/data/models/GameAssets.cpp`,
    `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/AssetUploadViewModel.cpp`,
    `reference/RAIntegration/src/devkit/context/impl/RcClient.cpp`,
    `reference/RAIntegration/src/services/impl/OfflineRcClient.cpp`
  - `third_party/rcheevos/src/rc_libretro.h`,
    `third_party/libretro-common/include/libretro.h`
- **Dependencies:** 2.2, 2.3, 2.4, and 3.1.
- **Out of scope:** Editing notes from the achievement editor (5.1), exporting `N0:` lines into
  `-User.txt`, modifying the RAIntegration cache, and all server requests or uploads.

## Milestone 3 — rcheevos live testing

### [3.1] Calculate a game hash and enter the game ID manually — planned

- **Objective:** Calculate the PS1 content hash with the pinned `rc_hash` implementation without
  looking up a game ID online.
- **Scope:** Hash `.bin`/`.cue` content; for PS1, use `SYSTEM.CNF` and the boot executable as the
  selected algorithm requires; let the user enter the game ID manually. Keep `.chd` for later.
- **Acceptance criteria:** A synthetic disc/file-reader fixture containing generated metadata
  produces a deterministic hash; malformed or unsupported input reports an error; the game ID is
  manually supplied; running the hash path makes no network request; no real game image is stored
  in tests.
- **Files to study:**
  - `reference/RALibretro/src/Hash.cpp`, `reference/RALibretro/Makefile`
  - `third_party/rcheevos/include/rc_hash.h`, `third_party/rcheevos/src/rhash/hash.c`,
    `third_party/rcheevos/src/rhash/hash_disc.c`
- **Dependencies:** 1.2.
- **Out of scope:** Hash lookup, game-ID resolution through the server, account login, and CHD
  support.

### [3.2] Evaluate in-memory achievement logic every frame — planned

- **Objective:** Test achievement conditions against live core memory using `rc_runtime`.
- **Scope:** Activate in-memory definitions without reading `-User.txt`; evaluate every frame;
  surface trigger notifications and measured values; report invalid memory addresses.
- **Acceptance criteria:** Synthetic memory and definitions produce the expected event on the
  expected frame; measured values and invalid-address states match expected results; unload/reload
  resets runtime state; tests run with no network path.
- **Files to study:**
  - `reference/RAIntegration/src/services/AchievementRuntime.cpp`,
    `reference/RAIntegration/src/devkit/data/models/AchievementModel.cpp`
  - `third_party/rcheevos/include/rc_runtime.h`,
    `third_party/rcheevos/src/rcheevos/trigger.c`,
    `third_party/rcheevos/src/rc_libretro.c`
- **Dependencies:** 2.1 and 3.1.
- **Out of scope:** Reading `-User.txt`, editor UI, unlocking or submitting achievements online,
  and account state.

### [3.3] Save and restore rcheevos runtime progress — planned

- **Objective:** Preserve achievement hit counts and runtime state when a save state is restored.
- **Scope:** Serialize `rc_runtime` progress alongside each 1.6 save-state slot and restore it
  together with the core state using rcheevos' sized progress APIs.
- **Acceptance criteria:** With a synthetic condition partway through its hit target, save then
  mutate the runtime; loading the slot restores the same hit count and trigger state as well as the
  core state; missing, truncated, or incompatible progress data is handled without unsafe reads.
- **Files to study:**
  - `reference/RAIntegration/src/services/AchievementRuntime.cpp`,
    `reference/RAIntegration/tests/services/AchievementRuntime_Tests.cpp`
  - `third_party/rcheevos/include/rc_runtime.h`,
    `third_party/rcheevos/src/rcheevos/runtime_progress.c`
- **Dependencies:** 3.2 and 1.6.
- **Out of scope:** Online unlock state, achievements loaded from `-User.txt`, and progress sync
  between different save-state slots.

## Milestone 4 — Achievement editor and RAIntegration local files

### [4.1] Build an editable achievement-logic model and serializer — planned

- **Objective:** Represent and serialize RA achievement logic without losing its meaning or text
  form.
- **Scope:** Model conditions, flags, memory sizes, operands, comparisons, hit targets, and Core +
  Alt groups; serialize the model back into logic strings; validate parsing with rcheevos.
- **Acceptance criteria:** Tests cover each supported condition/operand type and multiple Alt
  groups; string → model → string round-trips to the identical input; rcheevos parses each
  serialized result; malformed strings return an actionable error.
- **Files to study:**
  - `reference/RAIntegration/src/devkit/data/util/AchievementLogicSerializer.cpp`,
    `reference/RAIntegration/src/devkit/data/util/AchievementLogicSerializer.hh`,
    `reference/RAIntegration/tests/devkit/data/util/AchievementLogicSerializer_Tests.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/TriggerViewModel.cpp`
  - `third_party/rcheevos/include/rc_runtime_types.h`,
    `third_party/rcheevos/src/rcheevos/condition.c`,
    `third_party/rcheevos/src/rcheevos/operand.c`,
    `third_party/rcheevos/src/rcheevos/condset.c`,
    `third_party/rcheevos/src/rcheevos/trigger.c`
- **Dependencies:** 3.2.
- **Out of scope:** `-User.txt` parsing/writing, editor UI, and live per-condition display.

### [4.2] Read and write `RACache/Data/<GameID>-User.txt` — planned

- **Objective:** Read and write local achievements and leaderboards in the RAIntegration-compatible
  text format.
- **Scope:** Parse and emit the version line, game title, achievements, and leaderboards; use the
  4.1 logic model; write atomically; report malformed lines and I/O failures.
- **Acceptance criteria:** Synthetic files covering each supported asset type parse into the
  expected model and serialize back to the documented format; version-line handling matches the
  RAIntegration reader/writer; a failed write leaves the previous file intact; tests contain no
  game-specific definitions or copyrighted data.
- **Files to study:**
  - `reference/RAIntegration/src/devkit/data/models/GameAssets.cpp`,
    `reference/RAIntegration/src/data/context/GameContext.cpp`,
    `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp`
  - `third_party/rcheevos/src/rcheevos/trigger.c`,
    `third_party/rcheevos/include/rc_runtime_types.h`
- **Dependencies:** 4.1.
- **Out of scope:** Editor UI, server synchronization, badges, notes, and Rich Presence files.

### [4.3] Build the achievement editor UI — planned

- **Objective:** Create and edit local achievement assets through ImGui.
- **Scope:** Asset list; condition table for flag, type, size, address, comparison, value, and hits;
  Core/Alt tabs; title, description, and points; save through the 4.2 file model.
- **Acceptance criteria:** A synthetic asset can be created and edited in each field, saved, and
  reloaded with the same model; invalid conditions cannot be saved; Core and Alt groups remain
  distinct after editing; deleting or switching assets does not silently change another asset.
- **Files to study:**
  - `reference/RAIntegration/src/ui/viewmodels/AssetEditorViewModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/TriggerViewModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/TriggerConditionViewModel.cpp`,
    `reference/RAIntegration/src/ui/win32/AssetEditorDialog.cpp`
  - `third_party/imgui/imgui.h`, `third_party/rcheevos/include/rc_runtime_types.h`
- **Dependencies:** 4.1 and 4.2.
- **Out of scope:** Live condition truth/hit display (4.4), online uploads, and badge art editing.

### [4.4] Show live condition truth and hit counts — planned

- **Objective:** Make each edited condition's runtime behavior visible while testing an achievement.
- **Scope:** Refresh per-condition true/false state and current hit count every frame using the
  live runtime from 3.2.
- **Acceptance criteria:** For synthetic memory sequences, every condition row shows the expected
  truth state and hit count after each frame; reset/pause/Alt-group changes update the visible
  state; the view does not report stale values after an asset is removed or reloaded.
- **Files to study:**
  - `reference/RAIntegration/src/ui/viewmodels/TriggerConditionViewModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/TriggerViewModel.cpp`,
    `reference/RAIntegration/src/services/AchievementRuntime.cpp`
  - `third_party/rcheevos/include/rc_runtime_types.h`,
    `third_party/rcheevos/include/rc_runtime.h`
- **Dependencies:** 4.3 and 3.2.
- **Out of scope:** Editing logic grammar, modifying the runtime evaluator, and unlocking online.

## Milestone 5 — Notes in the editor + bookmarks

### [5.1] Integrate code notes into the achievement editor — planned

- **Objective:** Let the user view and edit code notes next to memory addresses in the achievement
  editor.
- **Scope:** Show notes from the 2.5 notes model next to addresses in the editor's condition table;
  allow adding and editing notes from the editor; save changes only to the workbench's own notes
  file and update the 2.5 “to publish” list.
- **Acceptance criteria:** A synthetic achievement with a condition at a noted address shows the
  note beside that address; adding or editing a note from the editor persists it in the workbench
  notes file and updates the “to publish” list; adding a note for a previously unnoted condition
  address also appears in that list; no write to `-Notes.json` or `-User.txt` and no network request
  occurs.
- **Files to study:**
  - `reference/RAIntegration/src/devkit/data/models/MemoryNotesModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/TriggerConditionViewModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/TriggerViewModel.cpp`,
    `reference/RAIntegration/src/ui/win32/AssetEditorDialog.cpp`
  - `third_party/imgui/imgui.h`,
    `third_party/rcheevos/include/rc_runtime_types.h`
- **Dependencies:** 2.5 and 4.3.
- **Out of scope:** Integrating notes into the viewer, search results, or watch list (2.5); writing
  to the RAIntegration cache or `-User.txt`; server requests or uploads; exporting `N0:` lines; and
  bookmarks.

### [5.2] Add RAIntegration-compatible bookmarks — planned

- **Objective:** Follow selected memory addresses using the compatible bookmark file and location.
- **Scope:** Add, rename, reorder, remove, and display live bookmarked addresses; use
  `RACache/Bookmarks/<GameID>-Bookmarks.json`, separate from `RACache/Data/`.
- **Acceptance criteria:** Synthetic bookmark data saves and reloads with address, name, order, and
  display settings intact; malformed data and mismatched game identity do not overwrite valid
  data; the exact path and JSON behavior match the inspected RAIntegration storage code.
- **Files to study:**
  - `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/MemoryBookmarksViewModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/MemoryBookmarksViewModel.hh`,
    `reference/RAIntegration/tests/ui/viewmodels/MemoryBookmarksViewModel_Tests.cpp`
  - `third_party/rcheevos/src/rc_libretro.h`,
    `third_party/libretro-common/include/libretro.h`
- **Dependencies:** 2.4 and 3.1.
- **Out of scope:** Server synchronization, pointer-chain authoring, and shared bookmark sets.

## Milestone 6 — Rich Presence and leaderboards

### [6.1] Build a Rich Presence editor and live preview — planned

- **Objective:** Edit a local Rich Presence script and preview its current output against live memory.
- **Scope:** Edit `RACache/Data/<GameID>-Rich.txt`; validate and activate with rcheevos; update a
  live preview as memory changes.
- **Acceptance criteria:** Synthetic script and memory fixtures produce the expected output over
  successive frames; malformed scripts are rejected with useful errors; save/reload preserves the
  script text; preview works offline.
- **Files to study:**
  - `reference/RAIntegration/src/devkit/data/models/RichPresenceModel.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/RichPresenceMonitorViewModel.cpp`,
    `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp`
  - `third_party/rcheevos/src/rcheevos/richpresence.c`,
    `third_party/rcheevos/include/rc_runtime.h`,
    `third_party/rcheevos/include/rc_runtime_types.h`
- **Dependencies:** 3.2.
- **Out of scope:** Sending presence to the RA site, account status, and remote script downloads.

### [6.2] Test local leaderboards live — planned

- **Objective:** Test leaderboard start, cancel, submit, and value logic against live core memory.
- **Scope:** Load definitions stored in `-User.txt`; evaluate them with rcheevos; show state and
  formatted value in the UI.
- **Acceptance criteria:** Synthetic memory sequences produce expected start/cancel/submit events
  and formatted values on expected frames; invalid definitions are reported; definitions reload
  from the local file; no result is sent to a server.
- **Files to study:**
  - `reference/RAIntegration/src/devkit/data/models/LeaderboardModel.cpp`,
    `reference/RAIntegration/src/services/AchievementRuntime.cpp`,
    `reference/RAIntegration/src/ui/viewmodels/OverlayLeaderboardsPageViewModel.cpp`,
    `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp`
  - `third_party/rcheevos/src/rcheevos/lboard.c`,
    `third_party/rcheevos/include/rc_runtime.h`,
    `third_party/rcheevos/include/rc_runtime_types.h`
- **Dependencies:** 4.2 and 3.2.
- **Out of scope:** Online leaderboard submissions, remote definitions, and account authentication.

## Milestone 7 — Server integration — blocked

### [7] Add RetroAchievements server integration — blocked pending approval

- **Status:** **BLOCKED** until the RA team approves and the user confirms that approval, as
  required by hard rule 1.
- **Objective:** Only after unblocking, integrate the specifically approved server operations.
- **Scope:** Keep the current workflow offline. If approval is granted, split this work into 7a
  (read-only: login, hash-to-game-ID resolution, and asset/note fetches) and 7b (writes), as the
  roadmap specifies.
- **Acceptance criteria:** While blocked, no server request path is compiled or invoked. After
  approval, 7a and 7b have separate scopes; every approved operation has mock-transport tests;
  tests do not contact production servers; write actions require explicit user control.
- **Files to study:**
  - `reference/RAIntegration/src/api/impl/ConnectedServer.cpp`,
    `reference/RAIntegration/src/api/ResolveHash.hh`,
    `reference/RAIntegration/src/api/FetchAchievementInfo.hh`,
    `reference/RAIntegration/src/api/FetchLeaderboardInfo.hh`,
    `reference/RAIntegration/src/api/UpdateAchievement.hh`,
    `reference/RAIntegration/src/api/UpdateRichPresence.hh`
  - `third_party/rcheevos/include/rc_api_request.h`,
    `third_party/rcheevos/include/rc_api_user.h`,
    `third_party/rcheevos/include/rc_api_info.h`,
    `third_party/rcheevos/include/rc_api_runtime.h`,
    `third_party/rcheevos/src/rapi/rc_api_user.c`,
    `third_party/rcheevos/src/rapi/rc_api_info.c`,
    `third_party/rcheevos/src/rapi/rc_api_runtime.c`
- **Dependencies:** Written RA-team approval and explicit user confirmation; then the offline
  milestones needed by each approved operation.
- **Out of scope:** Any server communication before both approvals, automatic uploads, credential
  storage, and unapproved operations.

## Later / optional work — not numbered issues yet

- Hardware-rendered cores (`SET_HW_RENDER`, OpenGL); not needed for Beetle PSX's software build.
- CHD hashing (deferred; the roadmap lists `libchdr`; rcheevos v12.5.0 describes CHD support via
  custom file handlers in `third_party/rcheevos/README.md:67`).
- Export code notes from the workbench notes file as `N0:` lines in `<GameID>-User.txt`, so
  RAIntegration can upload them with the assets after 4.2 using its line-preserving writer.
- Core-options UI and persistence.
- AUR `-git` package, AppImage, and CI after milestone 2.
- License decision before release.
