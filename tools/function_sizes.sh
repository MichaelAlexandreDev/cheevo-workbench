#!/usr/bin/env bash
# Prints every function of the project's own code that is longer than <threshold> lines, one per
# line as "<path>\t<name>\t<lines>", sorted by path and line. The count is clang-tidy's
# readability-function-size measure: lines from the opening to the closing brace, blanks and
# comments included.
#
# Usage: tools/function_sizes.sh <build-dir> [threshold=80] [clang-tidy]
# <build-dir> must hold compile_commands.json (CMAKE_EXPORT_COMPILE_COMMANDS is on).
set -euo pipefail

# Internal mode: measure one translation unit. Prints "<file>\t<line>\t<name>\t<lines>\t<own>",
# where <own> is 1 when the function is in the unit's own file.
if [[ "${1:-}" == "--unit" ]]; then
    unit="$2" build_dir="$3" threshold="$4" clang_tidy="$5"
    "$clang_tidy" -p "$build_dir" --quiet --checks='-*,readability-function-size' \
        --config="{CheckOptions: {readability-function-size.LineThreshold: $threshold}}" \
        "$unit" 2>/dev/null |
        awk -v unit="$unit" -v q="'" '
            /warning: function .* exceeds recommended size/ {
                split($0, loc, ":")
                file = loc[1]
                line = loc[2]
                name = substr($0, index($0, q) + 1)
                name = substr(name, 1, index(name, q) - 1)
            }
            / note: [0-9]+ lines including whitespace and comments/ {
                n = $0
                sub(/.* note: /, "", n)
                sub(/ lines.*/, "", n)
                print file "\t" line "\t" name "\t" n "\t" (file == unit ? 1 : 0)
            }'
    exit
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(cd "${1:?usage: function_sizes.sh <build-dir> [threshold] [clang-tidy]}" && pwd)"
threshold="${2:-80}"
clang_tidy="${3:-clang-tidy}"

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "function_sizes.sh: no compile_commands.json in $build_dir" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Translation units of our own code. A file compiled by several targets is measured once.
grep -o '"file": *"[^"]*"' "$build_dir/compile_commands.json" |
    sed 's/^"file": *"//; s/"$//' |
    grep -E "^$root/(src|tests)/" |
    sort -u >"$work/units"

# One clang-tidy per unit, in parallel; each writes its own file so the outputs never interleave.
# A failing unit stops xargs (exit 255), so a broken compile cannot hide a long function.
self="${BASH_SOURCE[0]}"
export self work build_dir threshold clang_tidy
# shellcheck disable=SC2016 # expanded by the inner bash, from the exported variables
xargs -a "$work/units" -P "$(nproc)" -I{} bash -c '
    out="$work/$(printf %s "$1" | tr / _).tsv"
    "$self" --unit "$1" "$build_dir" "$threshold" "$clang_tidy" >"$out" ||
        { echo "function_sizes.sh: clang-tidy failed on $1" >&2; exit 255; }
' _ {}

# A function in an included file is reported by every unit that includes it (main.cpp is
# #included by test_main_windows.cpp with main renamed). Deduplicate by (file, line) and keep the
# report from the function's own unit.
cat "$work"/*.tsv |
    while IFS=$'\t' read -r file line name lines own; do
        printf '%s\t%s\t%s\t%s\t%s\n' "$(realpath -m --relative-to="$root" "$file")" "$line" \
            "$name" "$lines" "$own"
    done |
    sort -t$'\t' -k1,1 -k2,2n -k5,5nr |
    awk -F'\t' '!seen[$1 FS $2]++ { print $1 "\t" $3 "\t" $4 }'
