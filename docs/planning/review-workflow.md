# Reviewer workflow (coordinator agent)

How the project is run with two agents. Written as a handoff so another agent can take over the
**reviewer/coordinator** role. Read `AGENTS.md` first: its hard rules and status line override
anything here.

## Roles

- **Implementer** (currently a Codex session the user calls "Luna"): writes the code for one
  roadmap step on the branch the reviewer prepared. Its `.git` is read-only, so it **never runs
  git commands that write**. It ends each task with a PR summary in
  `/tmp/cheevo-pr-issue-<N>.md` (Summary, Decisions, Verification, Manual test route).
- **Reviewer/coordinator** (you): prepares branches, writes the implementer's prompts, reviews and
  verifies its work, fixes small problems (or sends them back), does all git and GitHub work,
  updates docs, and talks to the user.
- **The user** (SrStarrk777, RA Junior Developer, Arch Linux) relays prompts between agents,
  tests every PR manually and approves every merge. They speak **Brazilian Portuguese**: reply
  in PT-BR, explain non-obvious decisions briefly (they are learning), keep code/commits/docs in
  English. Study notes in `docs/study/` are PT-BR.

## Current state (2026-11-03)

This section goes stale quickly: check `docs/roadmap.md`, `git log` and `gh issue list` first.

- `main` = `3d0982f`. Milestones 1-6 are merged and 263 doctest cases pass. Steps 2.6, 6.1 and 6.2
  and the Japanese UI fonts await manual UI validation; 4.3, 4.4 and 5.1 had only partial manual
  testing.
- Open issues: #23 and #24 (6.x) and #35 and #40 (implemented, awaiting validation); #25 (step 7,
  server integration) stays blocked by hard rule 1.
- Next, in order:
  1. One combined manual validation route for the pending steps above. The achievement `-User.txt`
     round trip with RAIntegration already passed (see the milestone 4 note in `docs/roadmap.md`);
     extend it to leaderboards, Rich Presence, code notes and bookmarks.
  2. CI (build, tests, ASan/UBSan) using the synthetic core. LeakSanitizer needs a suppression for
     a small fixed leak inside fontconfig's XML parser.
  3. Choose a license before the repository is ever made public.
- The user has low vision, so UI size and legibility are requirements. New fixed measurements must
  use `cw::ui_px()` so they follow the user's font size.
- The official tools for this check live outside the repository in
  `~/.local/share/cheevo-workbench-tools/` (launcher `ralibretro.sh`, dedicated Wine prefix).
- An English fan translation of the game exists locally in `.utils/` (RA hash
  `490add2c060de6fb12ed1b80a8486920`; the original is `ca1c2444593a4dd5ce4946070cec61b3`). Build the
  set against the original. Never commit the patch, patched images or extracted game text.

## The loop for every step

1. **Prepare.** `git switch -c issue-<N>-<slug> main`. Read the step in `docs/roadmap.md` and
   its draft in `docs/planning/issues.md` (frozen drafts; statuses there are not updated).
2. **Prompt the implementer** (in PT-BR, the user pastes it): branch already active; "NÃO rode
   comandos git que escrevem"; follow AGENTS.md and the relevant `.claude/skills/*/SKILL.md`;
   list the exact reference files to verify against; state the design decisions; tests on
   synthetic data; build/ctest/ASan+UBSan/clang-format; study notes + roadmap + AGENTS status;
   PR summary in `/tmp/cheevo-pr-issue-<N>.md`. Heavy, self-contained work goes to the
   implementer; small fixes you do yourself.
3. **Review** `git diff main` plus untracked files. Check every format claim against the real
   source in `reference/` and the **pinned** `third_party/rcheevos` (v12.5.0 — `reference/rcheevos`
   is upstream master with a different layout). Look for: data loss (overwrite of valid files,
   dropped unknown fields), wrong behavior on game switch/reload, keyboard focus leaking into
   the game, UI state keyed on values that change. Write findings most-severe first with
   `file:line` and the source you checked; separate bugs from suggestions.
4. **Verify** yourself; do not trust the summary:
   - `cmake -S . -B build && cmake --build build -j` → **0 warnings**.
   - `ctest --test-dir build --output-on-failure` (or `./build/tests/cw_tests`).
   - ASan+UBSan build in a separate dir (`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined
     -fno-omit-frame-pointer"`), run the tests with `UBSAN_OPTIONS=halt_on_error=1`. LeakSanitizer
     fails under ptrace in the Codex sandbox: use `ASAN_OPTIONS=detect_leaks=0` there and say so.
   - `clang-format --dry-run -Werror` on changed C/C++ files (no hook does it for you).
   - **Mutation check**: break one key line on purpose (e.g. byte order, a path component, the
     "do not overwrite malformed" guard), confirm tests fail, restore. Report the count.
   - **Real run** with the user's files (never commit them; they live in gitignored `.utils/`):

     ```sh
     ./build/src/frontend/cheevo-workbench \
         --core /usr/lib/libretro/mednafen_psx_libretro.so \
         --system ".utils/ps1-bios-pacote/ps1 bios" \
         ".utils/Twilight Syndrome - Tansaku-hen (Japan)/Twilight Syndrome - Tansaku-hen (Japan).cue"
     ```

     Expected log: `Game hash ca1c2444593a4dd5ce4946070cec61b3, game ID 18238` (matches the RA
     site). For behavior the UI hides, write a one-off program in a temp dir linking
     `build/src/frontend/libcw_frontend.a`, `build/src/devtools/libcw_devtools.a`,
     `build/librcheevos.a` and SDL3.
5. **Fix or send back.** When you fix a review finding, first write a test that fails, then fix.
   Record notable lessons in the step's study notes ("Na revisão" / "No teste do usuário").
6. **Commit and PR.** Commit messages: imperative subject, body with what/why and the
   verification numbers. Never stage ROM/BIOS/disc/save files or anything in `.utils/` (check
   `git status` before `git add`). Then:

   ```sh
   git push -u origin issue-<N>-<slug>
   gh pr create --repo MichaelAlexandreDev/cheevo-workbench --base main \
       --title "[<step>] <title>" --body-file <file>
   ```

   PR body: `Closes #<N>`, **What**, **Verified** (numbers), **Manual test** (short numbered
   steps with the real game). The repository is **private**; keep it so. If you post the
   implementer's review on the PR, label it as such.
7. **User test.** Give the user a short numbered route in PT-BR, and ask only for the steps
   that failed. Fix what they find.
8. **Merge only after the user's explicit approval**, squash: `gh pr merge <PR> --squash`.
   Stacked PRs: after the lower one merges, **merge `main` into the upper branch** (no rebase,
   no force push), resolve conflicts with the branch's version, check
   `git diff <tested tip> HEAD` is empty, push, `gh pr edit <PR> --base main`, merge. Then
   `git switch main && git pull --ff-only`, rebuild and run the tests.
9. **Docs after each step:** study section (format in `docs/study/README.md`), roadmap status,
   `AGENTS.md` status line; when a whole milestone is done, its "Resumo da etapa".

## Hard lines (from AGENTS.md, repeated because they cost the most if broken)

- No requests to RetroAchievements servers and no code that sends any. Reading public docs is
  fine.
- No ROMs, BIOS, game data in the repo, tests or fixtures; tests use synthetic buffers.
- Never copy code from `reference/RALibretro` (GPL). Read, then write our own.
- Don't commit or push unless the user asked for this workflow (they have, for step branches);
  never merge without their approval.

## Lessons so far (check for these in reviews)

- ImGui identifies widgets by label: a label that changes (a live value) loses clicks. Use
  `"text###stable_id"`.
- ImGui keyboard navigation and text fields can swallow or leak keys: game input must ignore
  keys while `WantTextInput`.
- rcheevos: `rc_runtime_validate_addresses` permanently marks unreadable memrefs; reset alone
  does not undo it (see `LogicTester::reset`). Callbacks without user data use `thread_local`.
- Writes to user files: unique tmp + atomic rename; never overwrite a file that failed to parse.
- Verify performance claims by measuring; report modest gains honestly.
- `tests/test_main_windows.cpp` compiles `main.cpp` into the test with `main()` renamed and presses
  real widgets through ImGui's test engine hooks. When moving code, update only the harness, never
  the expectations. Those hooks do not report tabs, closed combo boxes or
  clipped widgets, so a control missing from a list may simply be off-screen.
- `GetContentRegionAvail()` inside a scrolling table returns the table's inner width, not the visible
  width. Measure before `BeginTable`. A helper's unit test does not prove the window uses it right.
- ImGui's `ClearFonts()` writes its previous font size back into the style: set the new size after
  calling it. Do not unload fontconfig between queries, because it would leak and rebuild its
  configuration each time.
- rcheevos triggers start in Waiting: logic that is always true never fires. Test triggers with
  logic that is false first, such as a hit target or a value that changes.
