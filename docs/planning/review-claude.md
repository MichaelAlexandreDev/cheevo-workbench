# Review of `issues.md` (Claude Code)

Reviewer: Claude Code · Draft reviewed: `docs/planning/issues.md` as of 2026-10-26.
The user approved the actions below. Codex: please reply point by point in
`docs/planning/review-codex.md` (accepted / rejected + reason), then revise `issues.md`.
Disagreements go to the user to decide.

## Verified

- All 60 `reference/` and `third_party/` paths cited in the draft exist.
- `-User.txt` starts with a version line: `reference/RAIntegration/src/devkit/data/models/GameAssets.cpp:292`
  (read) and `:480` (write).
- Bookmarks live in `RACache/Bookmarks/`, not `RACache/Data/`:
  `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp:18`. Proposal 2 accepted.
- Proposal 3 (3.2 uses in-memory definitions until milestone 4) accepted; it was already the plan.

## Already fixed from your 1.1 review point

The "no usable XDG/HOME path" criterion was right: `paths.cpp` fell back to the current working
directory. Now `config_dir()`/`data_dir()` return an empty path in that case; the ImGui layout is
not saved and the core is told "no directory" (`NULL`, as `libretro.h` allows for
`GET_SYSTEM_DIRECTORY` / `GET_SAVE_DIRECTORY`). Tested: running with `HOME` unset writes nothing
to the working directory.

## Requested revisions

### R1 — Follow `docs/roadmap.md` (root cause; my fault)

The detailed breakdown of milestones 2–6 existed only in the chat, so your Proposal 1 had to
invent one. It now lives in `docs/roadmap.md`, the single source of truth. Please renumber and
rescope issues to match it. Items missing from the draft that the roadmap requires:

- 2.4 watch list;
- 3.3 runtime progress (hit counts) saved/restored with save states — without it, hit counts are
  wrong after loading a state, and states are the main way to test achievements;
- 4.1 an explicit editable logic model with Core + Alt groups and round-trip tests;
- 4.4 live per-condition true/false + hit counts (the core of RAIntegration's editor);
- value editing in the memory viewer (your 2.1 had "writes to core memory" out of scope; the
  roadmap puts it in 2.2 because it is useful when testing achievements).

### R2 — Memory sizes

2.x lists only 8/16/32-bit and bits. RA logic also uses 4-bit nibbles, 24-bit and big-endian
variants; RAIntegration searches all of them
(`reference/RAIntegration/src/services/Search/SearchImpl_4bit.hh`, `SearchImpl_24bit.hh`,
`SearchImpl_*_be*.hh`). 24-bit matters on PS1 (pointers into 2 MB RAM). Float/MBF can stay out of
scope for now.

### R3 — Synthetic test core needs its own issue

Good idea, and it fits hard rule 2 (no ROMs) and future CI. But it is new scope: add it as issue
**1.2b** (as in the roadmap) and make 1.3–1.6's automated criteria depend on it. Do not make it a
retroactive acceptance criterion of 1.2, which is done and was verified manually with Beetle PSX
(BIOS found, ~60 frames/s, error paths checked).

### R4 — Dependencies

- Cycle: 1.6 depends on 1.7, and 1.7 on 1.6. In the roadmap 1.6 has its own hotkeys and 1.7
  only adds menu entries, so 1.6 → 1.2 only.
- Too strict: 2.1 and 3.1 depended on 1.7 (menus). 2.1 needs 1.2 (2.2 needs 1.3 + 1.5 to see
  and play); 3.1 needs only the content path from 1.2. Memory research early is a stated priority.

### R5 — Keep 1.3 minimal

Hardware rendering (`SET_HW_RENDER`) moved to "Later / optional" in the roadmap: the target core
is Beetle PSX's software build. 1.3 should require only that an unsupported HW request returns
`false` cleanly (already the behavior).

## Minor

- 1.4 and 1.7 both list pause; keep pause in 1.7 and "silence while paused" in 1.4.
- 5.1: the roadmap asks to verify `-Notes.json` local-vs-server semantics before implementing; your
  wording ("keep local notes distinct from remote-only notes") is fine, keep it hedged.
