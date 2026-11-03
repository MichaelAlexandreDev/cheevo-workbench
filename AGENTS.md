# Agent guide — cheevo-workbench

Shared instructions for every coding agent (Codex reads this file directly; Claude Code
imports it from `CLAUDE.md`). Keep this file as the single source of truth.

## Project

Native Linux developer tooling for RetroAchievements (RA): a libretro front-end with built-in
dev tools (memory search/viewer, achievement editor, code notes, Rich Presence), compatible
with the official Windows toolkit (RALibretro + RAIntegration). The step-by-step roadmap is
`docs/roadmap.md` (single source of truth for steps, dependencies and status).

**Status:** 2.1–2.5, 3.1–3.3 and 5.2 done and manually validated;
4.1–4.4 and 5.1 implemented and unit-tested;
4.3–4.4 and 5.1 approved for merge after partial manual testing (latest notes layout fix awaits
manual confirmation); 6.1–6.2 validated on screen; achievements, Rich Presence, a leaderboard's
data and bookmarks checked against RAIntegration 1.4.2;
2.6 implemented after algorithm approval (issue #35), awaiting manual UI validation;
Japanese UI font fallback implemented (issue #40), with headless glyph tests;
interface font and layout settings implemented and tested on screen

**Approach:** written from scratch (not a port of RALibretro/RAIntegration). Shallow clones of
`RALibretro` (GPL-3.0), `RAIntegration` (MIT) and `rcheevos` (MIT) live in `reference/`
(gitignored) — read them to verify formats and behavior, but do not copy code from them
(RALibretro's GPL would bind this project's license).

`reference/rcheevos` is upstream `master`, whose source tree was reorganized after the release
we build against. To check rcheevos behavior, read the pinned submodule in
`third_party/rcheevos` (currently v12.5.0) — that is the code actually compiled.

## Layout

- `src/frontend/` — the executable: SDL3 window, OpenGL, ImGui UI, libretro core host.
- `src/devtools/` — `cw_devtools` library: platform-independent dev-tools logic (memory search,
  logic editing, RA file formats). Must not depend on SDL/OpenGL/front-end, so it can be tested
  with synthetic memory buffers.
- `tests/` — doctest unit tests (`cw_tests`).
- `docs/study/` — study notes for the user, **in Brazilian Portuguese** (exception to the
  English-docs rule). After finishing each roadmap step, add or update its section following the
  format in `docs/study/README.md`; when a whole milestone is done, also write its "Resumo da
  etapa" at the top of that milestone's file (format in the same README).
- `cmake/ThirdParty.cmake` — targets for the vendored deps. rcheevos is built **without**
  `rapi/` and `rc_client*` (server communication, hard rule 1).

## The user

- SrStarrk777, RetroAchievements Junior Developer. Runs **Arch Linux**.
- Speaks **Brazilian Portuguese**: reply in PT-BR. Write code, comments, commit messages and
  docs in **English**.
- Is learning as they go: explain non-obvious decisions briefly; propose a plan before large
  changes; prefer small, reviewable steps.
- Their first RA set is *Twilight Syndrome: Tansaku-hen* (PlayStation, RA game ID 18238). It
  is the primary real-world test case for the tool (PS1, 2 MB RAM).

## Stack (decided)

- C++20, CMake (≥ 3.20), out-of-source builds in `build/`.
- SDL3 (window/input/audio), Dear ImGui (UI), rcheevos (evaluation/hash/RP), libretro
  API (cores as `.so`).
- Third-party libraries vendored as git submodules under `third_party/` — do not depend on
  distro packages for rcheevos/ImGui.
- Formatting: `.clang-format` at the repo root (LLVM-based, 4-space indent, 100 cols).

## Hard rules

1. **Never upload or write to RetroAchievements servers** (`dorequest.php` and similar
   endpoints) or build code that does, unless the user confirms the RA team approved it. Reading
   public docs is fine.
2. **No ROMs, BIOS images or copyrighted game data** in the repo, in tests or in fixtures. Tests
   use synthetic memory buffers.
3. **Do not bundle libretro cores**; load cores the user already has.
4. **Do not present the project as official RetroAchievements software** (name, icons, text).
5. **Compatibility with RAIntegration is a requirement.** When reading/writing RA formats
   (logic strings, `RACache/Data/<GameID>-User.txt`, `-Rich.txt`, bookmarks), verify against
   the actual source of `RetroAchievements/rcheevos` and `RetroAchievements/RAIntegration`
   instead of guessing. If you could not verify something, say so explicitly.
6. Do not commit or push unless the user asks.

## Skills

Task-specific reference lives in `.claude/skills/*/SKILL.md` (also reachable at
`.agents/skills/`). Read the relevant one before working in that area:

- `ra-logic-format` — RA condition/logic string syntax and RAIntegration local file formats.
- `libretro-frontend` — writing the libretro front-end and exposing memory to rcheevos.
- `linux-packaging` — cross-distro builds: AppImage, AUR, Flatpak, XDG paths.

## Checks before finishing a change

- Code builds: `cmake -S . -B build && cmake --build build -j`.
- Tests pass (once they exist): `ctest --test-dir build --output-on-failure`.
- C/C++ files formatted with `clang-format -i` (Claude Code does this automatically via a hook;
  other agents should run it manually).
