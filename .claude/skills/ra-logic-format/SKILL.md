---
name: ra-logic-format
description: Reference for RetroAchievements achievement logic strings (memory sizes, prefixes, flags, hit counts, alt groups) and RAIntegration local file formats. Use when parsing, generating, displaying or testing achievement/leaderboard/Rich Presence logic, or reading/writing RACache files.
---

# RetroAchievements logic format

**Ground truth is the source code, not this file.** Before implementing a parser/serializer,
read the current `rcheevos` sources (`src/rcheevos/condition.c`, `operand.c`, `condset.c`,
`trigger.c`, `richpresence.c`, `lboard.c`) and, for local files, RAIntegration
(`src/data/context/GameContext.cpp`, `src/services/impl/` and the local-badges/user-file code).
This file is a map to orient you; if it disagrees with the source, the source wins — and
update this file.

Prefer **using rcheevos itself** for parsing/evaluation (`rc_parse_trigger`, `rc_runtime_*`)
instead of reimplementing it. Write our own serializer only where rcheevos has none.

## Structure

- A trigger is a **core group** optionally followed by **alt groups**, separated by `S`.
  `core S alt1 S alt2` → core AND (alt1 OR alt2).
- Conditions inside a group are separated by `_`.
- Condition: `[flag:]operand [cmp operand] [(hits) | .hits.]`

## Operand memory sizes (prefix after `0x`)

| Prefix | Size |
|---|---|
| `0xH` | 8-bit |
| `0x` + space or none (e.g. `0x1234`) | 16-bit |
| `0xW` | 24-bit |
| `0xX` | 32-bit |
| `0xI` / `0xJ` / `0xG` | 16 / 24 / 32-bit big-endian |
| `0xM`…`0xT` | single bit 0…7 |
| `0xL` / `0xU` | lower / upper nibble |
| `0xK` | bit count |
| `fF`, `fM`, `fB`… | floats (check source for the full list) |

Constants: decimal (`5`), hex (`h1F`), float (`f1.5`).

## Operand modifiers

| Prefix | Meaning |
|---|---|
| `d` | delta (value on previous frame) |
| `p` | prior (last different value) |
| `b` | BCD |
| `~` | bitwise invert |
| `{recall}` | value stored by `K:` Remember (verify syntax in source) |

## Condition flags

| Flag | Name |
|---|---|
| `R:` | ResetIf |
| `P:` | PauseIf |
| `A:` / `B:` | AddSource / SubSource |
| `C:` / `D:` | AddHits / SubHits |
| `N:` / `O:` | AndNext / OrNext |
| `I:` | AddAddress (pointers) |
| `M:` / `G:` | Measured / Measured% |
| `Q:` | MeasuredIf |
| `T:` | Trigger (challenge indicator) |
| `Z:` | ResetNextIf |
| `K:` | Remember |

Comparisons: `=`, `!=`, `<`, `<=`, `>`, `>=`. Arithmetic in AddSource chains: `*`, `/`, `&`,
`^`, `%`, `+`, `-` (verify which are supported in the current rcheevos).

Hit count: `.N.` (e.g. `0xH001234=1.10.`). Confirm the canonical serialized form in source.

## Example

`0xH001234=5_0xH005678>d0xH005678` → byte at 0x1234 equals 5 AND byte at 0x5678 increased
this frame.

## Addresses for PlayStation

RA exposes PS1 main RAM (2 MB) at RA addresses `0x000000–0x1FFFFF`. Console address
`0x80000000 + n` maps to RA address `n`. Scratchpad mapping: check `rc_libretro` /
`rc_console_memory_regions` for the current layout.

## RAIntegration local files

- `RACache/Data/<GameID>-User.txt` — local (unpublished) achievements/leaderboards. Text format, one asset
  per line, colon-separated fields with quoted strings; the first line is a version header.
  **Read the RAIntegration serializer before writing this format** — field order and escaping
  matter for round-tripping.
- `RACache/Data/<GameID>-Rich.txt` — Rich Presence script.
- `RACache/Bookmarks/<GameID>-Bookmarks.json` — memory bookmarks (separate directory, verified
  in `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp`).

Compatibility test: a file written by us must load in RAIntegration unchanged, and a file
written by RAIntegration must round-trip through us byte-for-byte (except where documented).
