# Implementation review — issues #23, #24, #35 and #40

The user approved the offline 8-bit assisted-search algorithm on 2026-11-01. Three GPT-6.1 Terra
subagents handled the engine, Japanese fonts, and authoring review/tests. The coordinator integrated
the changes, inspected the delivered code and upstream sources, requested fixes, and ran the
combined verification. Existing uncommitted Rich Presence/leaderboard work was preserved.

## Delivered scope

- **#35:** opt-in passive observations after each emulated frame, heuristic flag/area/counter
  candidates, labeled moment intersections, first-mark value matching, a Ctrl+M shortcut, cached
  rankings and navigation to Memory Viewer. Reset, state-load attempts, game changes, changed maps
  and loss of a known readable span invalidate observations. Stopping cancels an unfinished mark.
- **#40:** optional installed Japanese font merged into ImGui's default font. Optional runtime
  Fontconfig discovery accepts TrueType/CFF; common font locations provide a fallback. No font is
  bundled and Fontconfig is not a mandatory linked dependency.
- **#23/#24:** reviewed the existing implementation, corrected UTF-8 preview truncation and added
  headless interaction tests for saving, discarding, canceling, invalid drafts and game changes.
- **#25:** unchanged and blocked pending RA-team approval and user confirmation.

## Findings addressed

| Severity | Finding | Resolution / evidence |
|---|---|---|
| High | Japanese font insertion failure freed a buffer already owned and released by ImGui. | Removed the second free; checked vendored `imgui_draw.cpp` AddFont rollback and `ImFontAtlasFontDestroySourceData`. Independently reproduced a rejected-font path with ASan/UBSan; the default source remained intact. |
| Medium | A fragmented/short-reading memory source could produce millions of region objects. | Coalesce adjacent readable spans, bound fragmentation to 4,096 spans, and test short reads and alternating holes. |
| Medium | Kind filtering after top-N could hide every candidate of the selected kind. | Filter inside the engine before heap selection; regression test plus an intentional mutation that fails when the filter is removed. |
| Medium | Lost readable bytes could leave history from incompatible observations. | Reset the entire observation when a known span cannot be read; regression covers unchanged map metadata. |
| Medium | Saturating counters could eventually classify continuous noise as stable. | Exclude saturated counters from passive results; regression observes 270,000 noisy frames. |
| Low | Same-place filtering compared with the previous occurrence instead of the first one. | Preserve initial marked values; regression revisits the first value after an intervening different value. |
| Low | Stopping during a pending mark left the UI waiting forever. | Cancel only the pending mark and release a newly created unused label; preserve completed marks. |
| Low | Rich Presence truncation removed a complete UTF-8 character at the output boundary. | Keep complete sequences and remove only a cut sequence; both boundaries have regression coverage. |
| Low | Fontconfig could select a bitmap font unsupported by stb_truetype. | Require TrueType/CFF format during discovery. |

## Compatibility checked

- `reference/RAIntegration/src/devkit/data/models/LeaderboardModel.cpp`, Serialize/Deserialize:
  Start, Cancel, Submit, Value, format, title, description and lower-is-better fields.
- `reference/RAIntegration/src/devkit/data/models/GameAssets.cpp`: assignment of local IDs for L0.
- `reference/RAIntegration/src/devkit/data/models/RichPresenceModel.cpp`, SetScript: whitespace-only
  scripts, CR removal and final LF normalization.
- Pinned `third_party/rcheevos/src/rcheevos/runtime.c`: reactivation resets existing leaderboard
  definitions. `richpresence.c`: unknown-macro representation used by validation.
- Vendored ImGui font ownership, dynamic glyph loading and OpenGL renderer texture support.

## Verification

- `cmake -S . -B build && cmake --build build -j`: application and tests build without warnings.
- `ctest --test-dir build --output-on-failure`: both test executables pass.
- `cw_tests`: **263 cases**, including **10 assisted-search cases**; `cw_ui_tests`: **4 cases**.
- Headless authoring tests click the real ImGui Save/Discard/Cancel buttons and inspect temporary
  files; font tests rasterize hiragana, katakana and kanji when a suitable font is installed.
  This machine supplied Droid Sans Japanese. Tests also cover the default-font-only path.
- ASan + UBSan build and both test executables pass with `ASAN_OPTIONS=detect_leaks=0` and
  `UBSAN_OPTIONS=halt_on_error=1`. Leak detection is disabled because of the sandbox/ptrace limitation.
- One mutation check: remove the engine's category filter in a temporary source copy. The
  category-before-limit regression fails as expected; repository source remains unchanged.
- Independent invalid-font harness passes under ASan/UBSan. It disables ImGui's assert-on-error
  setting solely to exercise the rejected-font cleanup path.
- `clang-format --dry-run -Werror` passes for changed C/C++ files; `git diff --check` passes.

## Performance measurement

Release build, synthetic 2 MiB memory, 120 frames per workload. The observation timer excludes
the synthetic writes made by the benchmark. Measurements are examples from this machine, not
guaranteed frame budgets:

| Workload | Observation mean |
|---|---:|
| 512 bytes change every eight frames | 0.113 ms/frame |
| 512 scattered bytes change every frame | 0.215 ms/frame |
| Every byte changes every frame | 6.819 ms/frame |

Creating a mark took 4.1–4.7 ms; the following three observations including mark completion took
6.9–7.8 ms total. Ranking took 2.2–4.4 ms and the UI refreshes it at most twice per second during
ordinary observation. State used approximately 48 MiB after one label; each additional label uses
approximately 4 MiB for a 2 MiB address space, with a maximum of eight labels. These numbers exclude
the emulator and rendering. The approximate 1 ms observation target is met in the sparse cases,
but **not** for dense writes or mark processing.

Reproduce:

```sh
cmake -S . -B /tmp/cheevo-review-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cheevo-review-release --target cw_assisted_search_benchmark -j4
/tmp/cheevo-review-release/tests/cw_assisted_search_benchmark
```

## Remaining limits and manual route

The classifier is heuristic and 8-bit only. Up to four distinct values are retained per address;
the tool records overflow rather than retaining an unbounded history. New readable bytes inside a
previously unreadable hole require a restart unless region metadata changes. Map size is capped at
4 MiB. Rankings and marks can still be expensive in pathological dense or fragmented memory.

File saving detects ordinary external edits using loaded snapshots; it does not lock out another
process writing during the small interval before atomic rename. Font discovery filters unsupported
formats, but malformed installed font data remains subject to ImGui's normal assertions.

Visual validation with the user's emulator/game remains pending; headless tests do not validate
desktop appearance, game-specific candidate usefulness, or sustained audio/frame pacing.

1. Open Japanese Code Notes and confirm readable Japanese plus unchanged Latin text.
2. Open View → Assisted Search, start observation, play through area changes, and inspect suggested
   addresses. Hover values for history; verify each category can display its own top candidates.
3. Reuse one moment label for similar events, try Same place again, and confirm Ctrl+M does not
   mark while typing. Stop during a pending mark and confirm no unfinished wait remains.
4. Pause and advance individual frames; reset or load a save state and confirm observation clears.
5. Open View → Rich Presence, edit/save/reload a local script and start its preview. Check Japanese
   output and that pausing does not advance hit counts.
6. Open View → Leaderboards, test Start/Cancel/Submit/Value, and verify save/reload and dirty-draft
   confirmation. All test results remain local.

No commits, pushes, issue closures or RA server operations were performed.
