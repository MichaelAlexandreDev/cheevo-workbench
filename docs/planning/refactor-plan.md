# Refactoring plan — modules with machine-checked rules

Phase 0 deliverable (read-only), 2026-11-16, measured on `main` = `37beec5`. Nothing in `src/`,
`tests/` or CMake was changed to produce this document. Decisions D1–D14 were accepted on
2026-11-16 (section 3); one inventory question is still open.

Goal: go from three large targets (`cw_devtools`, `cw_frontend`, the executable) to small modules
with a clear owner and machine-checked dependency rules, **without changing behavior or on-disk
formats**. Refactoring PRs run between roadmap steps and never share a PR with a feature.

## 1. Baseline

### 1.1 Build and tests

| Check | Result |
|---|---|
| `cmake --build build -j` (Debug) | OK, 0 warnings |
| `ctest --test-dir build` | 3/3 executables pass |
| doctest cases | **303** = `cw_tests` 263 + `cw_ui_tests` 16 + `cw_window_tests` 24 |
| ASan+UBSan (`build-asan`, `UBSAN_OPTIONS=halt_on_error=1`) | 0 warnings; `cw_tests` and `cw_window_tests` pass; `cw_ui_tests` **fails on LeakSanitizer** (fontconfig, see below) |
| same, with `LSAN_OPTIONS=suppressions=` a file holding `leak:libfontconfig.so` | 3/3 pass |
| `clang-format --dry-run --Werror` on all tracked `src/` and `tests/` C/C++ files | clean |

The fontconfig leak is "known" (`review-workflow.md`) but **no suppression file exists in the
repo yet**; `tools/check-all.sh` has to add one (`tools/lsan.supp`).

### 1.2 Files above 400 lines

Physical lines (`wc -l`), `src/` and `tests/`. 23 files in total.

`src/` (13 — three more than the 2026-11-16 reference list in the brief, marked ★):

| Lines | File |
|---:|---|
| 1135 | `src/frontend/achievement_editor_window.cpp` |
| 1038 | `src/frontend/authoring_window.cpp` |
| 907 | `src/devtools/logic_tester.cpp` |
| 813 | `src/devtools/bookmarks.cpp` |
| 672 | `src/frontend/libretro_core.cpp` |
| 574 | `src/frontend/main.cpp` |
| 563 | `src/frontend/memory_viewer_window.cpp` |
| 552 | `src/devtools/user_file.cpp` |
| 550 | `src/devtools/achievement_logic.cpp` |
| 487 | `src/devtools/leaderboard_editor.cpp` |
| 449 | `src/frontend/watch_list_window.cpp` ★ |
| 441 | `src/devtools/code_notes.cpp` ★ |
| 411 | `src/devtools/memory_search.cpp` ★ |

`tests/` (10):

| Lines | File |
|---:|---|
| 795 | `tests/test_main_windows.cpp` |
| 754 | `tests/test_authoring_runtime.cpp` |
| 633 | `tests/test_core/test_core.c` |
| 601 | `tests/test_logic_tester.cpp` |
| 551 | `tests/test_achievement_logic.cpp` |
| 508 | `tests/test_editor_live_test.cpp` |
| 473 | `tests/test_save_states.cpp` |
| 455 | `tests/test_libretro_core.cpp` |
| 421 | `tests/test_user_file.cpp` |
| 417 | `tests/test_user_file_store.cpp` |

### 1.3 Functions above 80 lines

Tool: `clang-tidy` 23.1.4, check `readability-function-size` with `LineThreshold: 80`, using
`build/compile_commands.json`; it counts lines including blanks and comments. A full run over
`src/` and `tests/` takes **41 s** on 12 cores. **19 functions** (`main` is reported twice
because `cw_window_tests` compiles `main.cpp` with `main` renamed; counted once). No test
function exceeds 80 lines.

| Lines | Function | Location |
|---:|---|---|
| 394 | `draw_watch_list` | `src/frontend/watch_list_window.cpp:51` |
| 348 | `main` | `src/frontend/main.cpp:226` |
| 343 | `draw_memory_search` | `src/frontend/memory_search_window.cpp:15` |
| 224 | `draw_memory_viewer` | `src/frontend/memory_viewer_window.cpp:312` |
| 208 | `draw_code_notes_window` | `src/frontend/code_notes_window.cpp:40` |
| 178 | `LibretroCore::environment` | `src/frontend/libretro_core.cpp:389` |
| 174 | `draw_memory_value_editor` | `src/frontend/memory_viewer_window.cpp:64` |
| 153 | `conditions` (lambda/member) | `src/frontend/achievement_editor_window.cpp:696` |
| 128 | `AssistedSearchWindow::draw` | `src/frontend/assisted_search_window.cpp:67` |
| 127 | `contents` | `src/frontend/achievement_editor_window.cpp:943` |
| 122 | `draw_leaderboards` | `src/frontend/authoring_window.cpp:727` |
| 119 | `read_asset` | `src/devtools/user_file.cpp:261` |
| 114 | `draw_presence` | `src/frontend/authoring_window.cpp:442` |
| 114 | `draw_achievement_tester` | `src/frontend/logic_tester_window.cpp:84` |
| 112 | `draw_main_menu` | `src/frontend/main_menu.cpp:60` |
| 101 | `parse_bookmark` | `src/devtools/bookmarks.cpp:330` |
| 90 | `apply` | `src/frontend/achievement_editor_window.cpp:851` |
| 84 | `read_regular_file` | `src/devtools/text_file_store.cpp:35` |
| 82 | `CodeNotes::load` | `src/devtools/code_notes.cpp:237` |

### 1.4 Include graph

Measured from the `#include` lines of the project's own files (139 files in `src/` and `tests/`;
the graphify graph also indexes `third_party/` and is too noisy for this count).

- 249 project-internal include edges; 183 among `src/` files.
- Between areas: frontend→frontend 115, devtools→devtools 37, frontend→devtools 31,
  tests→devtools 27, tests→frontend 23, tests→tests 16. **devtools→frontend: 0.**
- Highest fan-in: `cw/devtools/memory.hpp` 12, `ui_scale.hpp` 11, `cw/devtools/code_notes.hpp` 8,
  `libretro_core.hpp` 8, `ui_widgets.hpp` 8, `cw/devtools/logic_tester.hpp` 6,
  `memory_viewer_window.hpp` 6.
- One file-level cycle: `code_notes_window` ↔ `memory_viewer_window` (both would be in `ui`).
- Third-party includes in `src/devtools`: rcheevos (`achievement_logic`, `bookmarks`,
  `game_hash`, `leaderboard_editor`, `logic_tester`, `rich_presence_editor`, `user_file`),
  nlohmann/json (`bookmarks`, `code_notes`) and POSIX headers. **No SDL, ImGui, OpenGL or
  `libretro.h`** — the current boundary holds.
- OpenGL is used only by `game_view.cpp` and `main.cpp`.

## 2. Inventory of what must not change

### 2.1 RetroAchievements formats

Paths verified in `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp` (lines 14–18
and 66–117, RAIntegration clone at `7ebd497`, 2026-10-17):

| File | RAIntegration location | Our code |
|---|---|---|
| achievements + leaderboards | `RACache/Data/<id>-User.txt` | `user_file.cpp:549`, `user_file_store.cpp`; app copy `<data>/sets/<id>-User.txt` (`main.cpp:497`) |
| Rich Presence | `RACache/Data/<id>-Rich.txt` | `rich_presence_editor.cpp:123`; app copy `<data>/sets/<id>-Rich.txt` (`main.cpp:511`) |
| code notes | `RACache/Data/<id>-Notes.json` (read) | `code_notes.cpp:217`; our own notes `<data>/notes/<id>.json` (`code_notes.cpp:209`) |
| bookmarks | `RACache/Bookmarks/<id>-Bookmarks.json` | `bookmarks.cpp:526`; app copy `<data>/bookmarks/<id>-Bookmarks.json` (`bookmarks.cpp:518`) |
| save states (RASTATE container, `MEM `/`ACHV` blocks) | RALibretro `src/States.cpp` | `save_state.cpp`, `save_states.cpp:92` (`<data>/states/<game>-<core>.state<N>`) |

The contents of these formats were checked against RAIntegration/rcheevos in milestones 4–6
(see `docs/roadmap.md`). **Phase 0 did not re-verify the contents.** D9 freezes the bytes we
write today, which is what a refactor must preserve.

Write rules that are part of the format behavior: unique temporary file + atomic rename; never
overwrite a file that failed to parse; keep unknown lines/fields.

### 2.2 The app's own files

Config dir = `$XDG_CONFIG_HOME/cheevo-workbench` (`paths.cpp:47`), data dir =
`$XDG_DATA_HOME/cheevo-workbench` (`paths.cpp:51`).

| File | Written by | Round-trip test today |
|---|---|---|
| `<config>/imgui.ini` | ImGui (`main.cpp:282`) — sections are keyed by **window names/IDs** | none |
| `<config>/ui-settings.txt` | `ui_settings.cpp` | yes (`cw_ui_tests`) |
| `<config>/recent-files.txt` | `recent_files.cpp` | load/save tests, no byte comparison |
| `<config>/game-ids.txt` | `game_ids.cpp` | load/save tests, no byte comparison |
| `<config>/system-path.txt` | `content_dialogs.cpp:337` | none found |
| `<config>/ra-cache-path.txt` | `code_notes_window.cpp:73` | window test only |
| `<data>/states/*.state<N>` | `save_states.cpp` | yes (RASTATE) |

**"Session":** `SessionState` (`session_state.hpp`) only holds Running/Paused/FrameAdvance in
memory; **no session file exists**. Nothing to freeze there beyond the behavior.

Consequence of `imgui.ini`: renaming a window's title or `###id` silently drops the user's
layout. Window titles and IDs are frozen like a file format.

### 2.3 Hotkeys and window behavior

| Key | Action | Where |
|---|---|---|
| F1–F4 / Shift+F1–F4 | load / save state slot 1–4 (global route) | `main_menu.cpp:18` |
| P | pause / resume | `main_menu.cpp:42` |
| N (while paused) | advance one frame | `main_menu.cpp:46` |
| Ctrl+R | reset emulation | `main_menu.cpp:50` |
| Ctrl+Q | quit | `main_menu.cpp:68` |
| Ctrl+M | Assisted Search hotkey (global route) | `assisted_search_window.cpp:63` |
| Ctrl+S | save in the Achievement Editor | `achievement_editor_window.cpp:1052` |
| game pad keyboard map | Z/X/A/S, Q/W/E/R, 1/2, arrows, Enter, RShift; ignored while F1–F4 or Ctrl+R/Ctrl+Q are held | `input_state.cpp:13–81` |

Also frozen: window sizes, widget order, menu items, the "game ignores keys while
`WantTextInput`" rule, and every measurement going through `cw::ui_px()`.

### 2.4 The current boundary

`cw_devtools` does not include SDL, OpenGL, ImGui, `libretro.h` or any `src/frontend` header
(section 1.4). This becomes the first rule of the layer test.

## 3. Decisions (to accept)

Numbering follows the brief (D5–D7 and D11 are not in it).

### D1 — modules: the proposal has a cycle

As proposed, `devtools/logic` → `devtools/authoring` → `devtools/formats` → `devtools/logic`:

- `logic_tester.cpp:2-3` includes `leaderboard_editor.hpp` and `rich_presence_editor.hpp`
  (it calls `leaderboard_definition`, `validate_rich_presence`, `normalize_rich_presence`);
- `editor_live_test.hpp:3` includes `achievement_editor.hpp`;
- `user_file.hpp:3` (formats) includes `achievement_logic.hpp` (logic);
- `achievement_editor`/`leaderboard_editor` → `user_file_store`; `rich_presence_editor` →
  `text_file_store`.

Also `game_hash.cpp:3` includes `file_checks.hpp`, so `hash` would depend on `formats` and is not
a leaf.

**Recommendation (D1-A):** keep files whole and change the grouping. Layers, bottom to top:

| Module | Files | Depends on |
|---|---|---|
| `io` | `file_checks`, `text_file_store` | — |
| `hash` | `game_hash` | `io`, rcheevos |
| `memory` | `memory`, `memory_search`, `assisted_search`, `watch_list` | — |
| `logic` | `achievement_logic` (logic string syntax only) | rcheevos |
| `formats` | `user_file`, `user_file_store`, `bookmarks`, `code_notes` | `io`, `logic`, `memory`, json, rcheevos |
| `authoring` | `achievement_editor`, `leaderboard_editor`, `rich_presence_editor`, `code_note_draft` | `formats`, `io`, rcheevos |
| `runtime` | `logic_tester`, `editor_live_test` | `authoring`, `formats`, `memory`, rcheevos |
| `host` | as in the brief (+ `audio_sink.hpp`) | devtools modules, SDL, libretro |
| `ui` | windows, `ui_widgets`, `ui_fonts`, `ui_appearance`, `ui_settings`, `ui_scale` | `host`, devtools, ImGui, SDL |
| `app` | `main`, `main_menu`, `app_state`, `content_dialogs`, `gamepad_input`, `status_window`, `game_view` | everything below, OpenGL |

Checked against the graph: no cycle at module level; the frontend split as proposed has none
(`ui` never includes `app`; `host` never includes ImGui). `ui` → `host` exists (windows take
`LibretroCore&`) and is allowed.

Alternative (D1-B): keep the brief's names and move `leaderboard_definition` and the RP helpers
down into `formats`. That moves functions between files, so it is less mechanical.

### D2 — public headers

**Recommendation:** per module, `src/<area>/<module>/include/cw/<module>/*.hpp` (the pattern
`cw_devtools` already uses), included as `"cw/<module>/x.hpp"`. The C++ namespace stays `cw::`.
No module has private headers today. One that appears later stays next to its `.cpp` and is not
on any include path exported to other modules.

### D3 — one-way layers

- CMake: one target per module (`cw_io`, `cw_memory`, …, `cw_host`, `cw_ui`, `cw_app`), linked
  `PRIVATE` unless a public header needs the dependency.
- CTest `layers` test: a CMake script (`cmake -P`, no new dependency) reads every `#include` in
  `src/`, maps each file to its module and fails on any edge outside the table in D1, on any
  third-party header a layer may not use (devtools: SDL/ImGui/GL/`libretro.h`; host:
  ImGui/GL; ui: GL), and on `../` includes that cross modules.
- File-level cycles inside a module (`code_notes_window` ↔ `memory_viewer_window`) are not
  checked; they are handled when those windows are split.

### D4 — `cw_ui` library

Checked in `third_party/imgui/imgui_internal.h:1104` and `:4467`: `IMGUI_ENABLE_TEST_ENGINE`
only adds enum values and turns on macros used inside ImGui's own `.cpp` files. It does not
change `ImGuiContext`'s layout. So **one** `cw_ui`, compiled without the define, can link with
either ImGui build, **as long as `cw_ui` does not link an ImGui implementation itself**.

**Recommendation:**
- Add an `imgui_headers` INTERFACE target.
- `cw_ui` and `cw_app` (everything in `app` except `main.cpp`) link only those headers.
- The executable links `imgui`; `cw_window_tests` and `cw_ui_tests` link `imgui_test_hooks` and
  `imgui`.

`cw_window_tests` still `#include`s `main.cpp` (with `main` renamed) until `main()` is split.

### D8 — no aliases

Accepted as written: a move updates every include and CMake in the same PR. No forwarding
headers.

### D9 — frozen formats

The byte comparison needs a tool that does not exist yet: a test-only executable
(`cw_format_corpus`). It reads synthetic fixtures from `tests/fixtures/corpus/`, loads and saves
each format through the public API, and writes the results to an output directory.
`tools/compare-with-main.sh` builds it in a `git worktree` of `main` and in the branch, runs both
and `diff -r`s the outputs. It also compares the union of `--list-test-cases` across the test
executables: the branch list must contain every name on main's list. **Until this tool is on
`main`, only the round-trip tests guard the bytes.**

### D10 — size limits

- **Ratchet:** `tools/size_ratchet.json` lists today's excesses (23 files in section 1.2 and 19
  functions in 1.3). A count may go down; a new entry or a higher count fails.
- **Lines:** physical lines, as measured here.
- **Compaction:** packing lines is caught by `clang-format --Werror` (100 columns) and in review.

**Recommendation:**
- The file check is a CMake script in the default `ctest` (instant).
- The function check runs `clang-tidy` (41 s). It is a CTest test with label `slow`, registered
  only when `clang-tidy` is found. `check-all.sh` runs it; the quick loop is `ctest -LE slow`.
- Tests are inside the limit too. Because the ratchet forbids growth, **new characterization
  tests go into new files**, not into the large existing ones.

### D12 — tests mirror modules

Accepted. `tests/<module>/test_*.cpp`. One executable per area (devtools, host, ui/windows) is
enough; the move itself is mechanical, and the test-name comparison guards it.

### D13 — mechanical or behavioral

Accepted as written. Bugs found during a move get a GitHub issue and their own PR.

### D14 — UI

Accepted as written. Also: window titles/`###` IDs are frozen (`imgui.ini`, section 2.2).

### Questions for the user

**Status (2026-11-16):** the user accepted the recommendations for questions 1–6 ("pode
começar"). Question 7 is still open; it blocks nothing before R5.

1. **D1:** accept D1-A (modules `io`, `hash`, `memory`, `logic`, `formats`, `authoring`, `runtime`)
   instead of the brief's five? *Recommended: yes.*
2. **D2:** headers per module at `src/<area>/<module>/include/cw/<module>/`, namespace unchanged?
   *Recommended: yes.*
3. **D3/D10 scripts in CMake (`cmake -P`), not Python?** *Recommended: CMake, no new
   dependency.*
4. **D10:** do the 400/80 limits also cover `tests/` (10 files today)? Is the function check
   allowed to be a `slow`-labeled CTest test? *Recommended: yes to both.*
5. **D4:** create `cw_app` (all `app` files except `main.cpp`) together with `cw_ui` in step 3,
   so no test compiles `../src/frontend/*.cpp` any more? *Recommended: yes.*
6. **Order vs. pending validations:** #35 (assisted search), #40 (Japanese fonts) and 6.1/6.2
   still await manual validation. *Recommended:* do the safety nets (issues R1–R5) now. Move or
   split a file only after the step that touched it was validated, so a validation failure is
   not mixed with a move.
7. **Inventory:** "session" has no file (only in-memory state). Did you mean something else? Is
   anything missing from section 2?

## 4. Ordered issues

Every refactoring PR also follows the loop in `review-workflow.md` and ends with `graphify
update .`. **"Standard proof"** = `tools/check-all.sh` green (Debug build with 0 warnings, ctest,
ASan/UBSan + ctest with `tools/lsan.supp`, `clang-format --dry-run --Werror`). It also includes
the ratchet and the layer test green with no new exception and, from R4 on,
`tools/compare-with-main.sh` with identical bytes and no lost test names.

### Safety nets (step 1)

**R1 — `tools/check-all.sh` + `tools/lsan.supp`.**
- Scope: a bash script that runs, in order:
  - `clang-format --dry-run --Werror` on tracked C/C++ files (first, because it is instant);
  - a Debug configure/build in `build-check/debug` with a new `CW_WERROR` option, so any warning
    in project code fails (third-party targets are not affected);
  - `ctest`;
  - the ASan/UBSan build in `build-check/asan` + `ctest` (`UBSAN_OPTIONS=halt_on_error=1`, LSan
    suppression for fontconfig only).
- Its own build directories leave the developer's `build/` and `build-asan/` caches untouched.
  No product code.
- Proof:
  - green on `main`;
  - red when a file is misformatted on purpose;
  - red when an UBSan error is planted in a scratch test (reverted);
  - the suppression covers only `libfontconfig.so`.

**R2 — size ratchet.**
- Scope: `tools/size_ratchet.json`, `tools/size_ratchet.cmake` (files), the clang-tidy runner
  (functions, label `slow`), and the CTest registration.
- Proof:
  - green on `main`;
  - adding a line to `bookmarks.cpp` → red;
  - a new 401-line file → red;
  - lowering a count without updating the JSON → the test asks to lower the entry (so the
    ratchet tightens).

**R3 — layer test (current shape).**
- Scope: `tools/check_layers.cmake` + CTest. Rules at this point:
  - `src/devtools` ↛ SDL/ImGui/GL/`libretro.h`/`src/frontend`;
  - host files (`cw_frontend`) ↛ ImGui/GL.
- The table grows with each move PR.
- Proof: green on `main`; one planted forbidden include per rule → red.

### Characterization (step 2)

**R4 — `cw_format_corpus` + `tools/compare-with-main.sh` + RA-format round trips.**
- Scope:
  - synthetic fixtures for `-User.txt` (achievements, leaderboards, unknown lines, comments),
    `-Rich.txt`, bookmarks JSON (both locations, old enum sizes), `-Notes.json` +
    `notes/<id>.json`, and RASTATE;
  - byte-exact load→save tests in **new** test files.
- Proof:
  - the corpus output is identical across two runs;
  - a mutation per format (e.g. field order, line ending, an unknown field dropped) turns
    one test red;
  - the script runs against `main` itself and reports zero diffs.

**R5 — app-file round trips.**
- Scope: byte-exact tests for `recent-files.txt`, `game-ids.txt`, `system-path.txt`,
  `ra-cache-path.txt`, `ui-settings.txt`; added to the corpus.
- Proof: as in R4.

**R6 — window characterization gaps.**
- Scope: from the test names, there are no window tests for:
  - the Achievement Editor's create/edit/save/delete and Ctrl+S;
  - leaderboard and RP editing beyond Open/Create and the row width;
  - the Assisted Search window;
  - the keyboard shortcuts in 2.3 (the menus are tested, the keys are not).
- Add them in new files through the ImGui hooks. Split into R6a (editors) and R6b (search +
  hotkeys) if the diff is large.
- Proof:
  - each new test fails when the behavior is broken on purpose (one mutation per window);
  - no existing expectation changes.

### Libraries (step 3)

**R7 — `imgui_headers`, `cw_ui`, `cw_app` (CMake only, no file moves).**
- Proof:
  - standard;
  - no `../src/` left in `tests/CMakeLists.txt`;
  - `nm` on the executable shows no `ImGuiTestEngineHook_*` symbol;
  - same doctest names.

### Devtools modules (step 4), leaves first, one PR each

**R8 `io`, R9 `hash`, R10 `memory`, R11 `logic`, R12 `formats`, R13 `authoring`, R14
`runtime`.**
- Scope of each:
  - `git mv` into `src/devtools/<module>/` and the headers into `include/cw/<module>/`;
  - one CMake target;
  - all includes updated (D8);
  - the layer test's table extended;
  - `cw_devtools` shrinks until it is removed in R14 (or stays as an INTERFACE target that
    links all modules, if the user prefers fewer CMake edits — decide in R8).
- Proof: standard, plus `git diff -M --stat` shows only renames + include/CMake lines.

### Frontend split (step 5)

**R15 `host`, R16 `ui`, R17 `app`.**
- Same pattern; `cw_frontend` becomes `cw_host`.
- Proof: as in R8–R14, plus a manual route: open game, F1/Shift+F1, P/N, Ctrl+R; each window
  opens with its saved layout (proves `imgui.ini` IDs held).

### Breaking up large files and functions (step 6), one family per PR, design in the PR body

Each one is preceded by R6 coverage for that window. Each lowers its ratchet entries.

- **R18** `achievement_editor_window` (1135; `conditions` 153, `contents` 127, `apply` 90).
- **R19** `authoring_window` (1038; `draw_leaderboards` 122, `draw_presence` 114).
- **R20** `memory_viewer_window` (563; `draw_memory_viewer` 224, `draw_memory_value_editor`
  174) + the `code_notes_window` cycle (`draw_code_notes_window` 208).
- **R21** `watch_list_window` (449; `draw_watch_list` 394).
- **R22** `memory_search_window` (`draw_memory_search` 343) + `assisted_search_window` (`draw`
  128).
- **R23** `logic_tester` (907) + `logic_tester_window` (`draw_achievement_tester` 114).
- **R24** `bookmarks` (813; `parse_bookmark` 101).
- **R25** `user_file` (552; `read_asset` 119) + `achievement_logic` (550).
- **R26** `libretro_core` (672; `environment` 178).
- **R27** `main.cpp` (574; `main` 348) + `main_menu` (`draw_main_menu` 112). Afterwards
  `cw_window_tests` stops including `main.cpp`.
- **R28** the rest: `leaderboard_editor` (487), `code_notes` (441; `load` 82), `memory_search`
  (411), `text_file_store` (`read_regular_file` 84), and the 10 test files.

Proof for each:
- standard;
- the corpus is identical;
- every R6 window test still passes unchanged;
- ratchet entries lowered;
- a short manual route for the touched window in the PR summary.

### Closing (steps 7–8)

- **R29** tests mirror modules (`tests/<module>/`).
  - Proof: standard; the union of test names is identical.
- **R30** `docs/architecture.md` ("where new code goes": the D1 table, the layer rules, how to
  add a module) and a PR checklist in `docs/planning/review-workflow.md`. Docs only.
- **Optional, own issue:** GitHub Actions CI running `tools/check-all.sh`, if the user wants it.
