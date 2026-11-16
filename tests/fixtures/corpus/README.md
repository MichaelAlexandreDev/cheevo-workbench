# Format corpus

Synthetic fixtures that freeze the bytes the application writes (decision D9 in
`docs/planning/refactor-plan.md`). Everything here was written by hand for the tests: made-up game
ID 1, made-up titles, no game data.

- `input/` — what the corpus loads (layout in `tests/format_corpus/corpus.hpp`).
- `expected/` — what `cw_format_corpus` writes for that input today, including `results.txt` with
  every parse result and error message. `cw_format_tests` fails on any byte that differs.

A refactoring must not change `expected/`. A PR that changes a format on purpose regenerates it
and explains every difference in its description:

```sh
cmake --build build -j
rm -rf tests/fixtures/corpus/expected
./build/tests/cw_format_corpus tests/fixtures/corpus/input tests/fixtures/corpus/expected
```

Adding a fixture follows the same steps; check the new expected file by hand before committing it.
