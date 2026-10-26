# Codex review of Claude's issue review

Reviewed `docs/planning/review-claude.md` against the current `docs/roadmap.md` and the cited
source files. The roadmap is treated as the source of truth. No changes were made to
`docs/roadmap.md`.

## R1 — Accepted

I replaced the earlier proposed two-issue split for milestones 2–6 with the exact steps in
`docs/roadmap.md`. The revised issue list adds 2.4 (watch list), 3.3 (runtime progress with save
states), 4.1 (editable Core/Alt logic model and round-trip tests), and 4.4 (live condition state
and hit counts). Memory value editing is now in 2.2, while 2.1 is the platform-independent memory
access layer. The earlier proposals to keep milestone 3 offline and store bookmarks in the
separate `RACache/Bookmarks/` directory are retained; the latter is mapped in
`reference/RAIntegration/src/services/impl/FileLocalStorage.cpp:112-116`. The `-User.txt` version
line is read at `reference/RAIntegration/src/devkit/data/models/GameAssets.cpp:292` and written at
`reference/RAIntegration/src/devkit/data/models/GameAssets.cpp:480`.

## R2 — Accepted with a source qualification

**Decision:** Accept the expanded size support in the project issues; reject the claim that the
current RAIntegration search UI exposes every one of those sizes.

The issue scope now includes bit, low/high nibble, 8-, 16-, 24-, and 32-bit sizes and the
little-/big-endian variants required by the roadmap. Float and MBF formats remain out of scope.
The format families are present in RAIntegration's `reference/RAIntegration/src/devkit/data/Memory.hh`
and in the compiled rcheevos `third_party/rcheevos/include/rc_runtime_types.h:36-60` and
`third_party/rcheevos/src/rcheevos/operand.c`. `cmake/ThirdParty.cmake:39-45` selects the
`third_party/rcheevos` sources for the library; its `third_party/rcheevos/src/rc_version.h:10-12`
identifies that checkout as v12.5.0.

One claim in R2 needs qualification: RAIntegration has a `SearchImpl_24bit.hh`, but its search
type list in `reference/RAIntegration/src/ui/viewmodels/MemorySearchViewModel.cpp:131-145` does
not expose 24-bit search, and there is no 24-bit big-endian search implementation in
`reference/RAIntegration/src/services/Search/`. `Memory.hh` does define `TwentyFourBitBigEndian`
(`:29-34`). The issues therefore treat 24-bit search, including big-endian, as a requirement of
this project's roadmap, without claiming that the current RAIntegration search UI supports it.

## R3 — Accepted

I added a separate planned issue 1.2b for the synthetic libretro test core. Issue 1.2 remains
complete and does not gain retroactive test-core acceptance criteria. Automated acceptance for
1.3–1.6 uses 1.2b; the roadmap's implementation dependencies remain 1.2. The test core is a small
project-authored test fixture, not a bundled third-party libretro core, and it uses no ROM or BIOS.

## R4 — Accepted

I aligned issue dependencies to the roadmap table. In particular, 1.6 depends on 1.2 only; 1.7
depends on 1.3–1.6; 2.1 and 3.1 depend on 1.2; and 2.2 depends on 2.1, 1.3, and 1.5. The
remaining issue dependencies follow their roadmap rows.

## R5 — Accepted

Issue 1.3 now covers software frames rendered to a GL texture, the listed pixel formats, pitch,
duplicate frames, and geometry/aspect changes. Hardware rendering is deferred as optional work;
an unsupported `SET_HW_RENDER` request must return `false` cleanly. This matches
`docs/roadmap.md`'s “Later / optional” section.

## Minor — Accepted

- **Pause ownership:** Pause/resume stays in 1.7. Issue 1.4 only requires silence while the
  front-end is paused and frame pacing from core FPS/audio rather than vsync.
- **Code-note semantics:** The updated roadmap and issues explicitly separate user-authored
  workbench notes from RAIntegration's server-response cache. The source checks for this decision
  are recorded in the follow-up section below.

## Follow-up — revised code-notes roadmap (2026-10-26)

The updated `docs/roadmap.md` resolves both dependency ambiguities recorded in the earlier review:

- Step 2.3 now depends on 2.1 and 2.2, matching its result handoff to the memory viewer.
- Step 2.5 owns notes in the viewer, search results, and watch list. Step 5.1 only integrates
  editing in the achievement editor and depends on 2.5 and 4.3.

I verified the code-note behavior cited by the roadmap in the checked-out `reference/RAIntegration`
sources:

- `reference/RAIntegration/src/devkit/data/models/MemoryNotesModel.cpp:289-364` tracks edits
  against the original notes; `:623-665` serializes changed notes as address-and-quoted-note
  records and parses those records. The `N0:` prefix for the first note is supplied by the
  surrounding asset serialization.
- `reference/RAIntegration/src/devkit/data/models/GameAssets.cpp:313-320` recognizes the `N` asset
  type, and `:560-592` writes it when serializing assets.
- `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp:76-80` maps memory notes to
  `RACache/Data/<GameID>-Notes.json`.
- `reference/RAIntegration/src/devkit/context/impl/RcClient.cpp:211-224,232-251` stores a copy of
  the `codenotes2` server response in that cache. `reference/RAIntegration/src/services/impl/
  OfflineRcClient.cpp:100-119` reads the cache for offline `codenotes2` requests.
- `reference/RAIntegration/src/ui/viewmodels/AssetUploadViewModel.cpp:117-124` queues modified
  notes; `:514-585` builds the code-note API request. Thus RAIntegration uploads modified notes
  from its asset workflow; the workbench's 2.5 remains a local comparison/copy workflow.

I agree with the updated separation between the workbench notes file, the read-only RAIntegration
cache, and the optional `N0:` export. One dependency question remains for your decision: 2.5 says
notes appear in search results and the watch list, but lists only 2.2 and 3.1 as dependencies. If
the dependency rows are strict prerequisites, consider adding 2.3 and 2.4; the current roadmap
orders those steps before 2.5, so I kept its declared dependency row unchanged in `issues.md`.
No roadmap source fact above appears inconsistent with the inspected RAIntegration files.

## Previous XDG review item

The earlier working-directory fallback has been corrected. In `src/frontend/paths.cpp:18-41`, an
unusable XDG/HOME path or directory-creation failure returns an empty path. In
`src/frontend/main.cpp:201-205`, that leaves `ImGuiIO::IniFilename` null; in
`src/frontend/libretro_core.cpp:216-221`, unavailable system/save directories are passed as
`nullptr`. This agrees with Claude's review note and resolves the 1.1 follow-up from the previous
draft.
