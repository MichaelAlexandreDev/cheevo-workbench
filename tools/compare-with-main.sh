#!/usr/bin/env bash
# Proof for a mechanical refactoring PR (decision D9 in docs/planning/refactor-plan.md): builds a
# base revision (default: main) and this checkout (working tree included), then
#   1. runs the format corpus from both builds on this checkout's fixtures and diffs the outputs:
#      every byte the application writes must be identical;
#   2. compares the doctest case names of all test executables: none may be lost.
#
# Usage: tools/compare-with-main.sh [--allow-missing-corpus] [base]
#   --allow-missing-corpus  skip step 1 when the base predates cw_format_corpus
#
# The base is extracted with `git archive`, so nothing is written to .git. Submodules are not in
# the archive: the base reuses this checkout's third_party/, so both revisions must pin the same
# submodule commits. Builds live in build-check/compare/ (or $CW_CHECK_DIR/compare) and are reused,
# so later runs are incremental.
set -euo pipefail

allow_missing=0
base=main
for arg in "$@"; do
    case "$arg" in
    --allow-missing-corpus) allow_missing=1 ;;
    -*)
        echo "usage: $0 [--allow-missing-corpus] [base]" >&2
        exit 2
        ;;
    *) base="$arg" ;;
    esac
done

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dir="${CW_CHECK_DIR:-$root/build-check}/compare"
cd "$root"
export LC_ALL=C

fail() {
    echo "compare-with-main: $*" >&2
    exit 1
}

base_rev="$(git rev-parse --verify --quiet "$base^{commit}")" || fail "unknown revision '$base'"
if [[ "$(git ls-tree -r "$base_rev" third_party)" != "$(git ls-tree -r HEAD third_party)" ]]; then
    fail "$base and HEAD pin different third_party submodules; this script cannot compare them"
fi
if git submodule status | grep -q '^[^ ]'; then
    fail "third_party is not checked out at the pinned commits (run git submodule update)"
fi

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi

# Configures (once) and builds one tree.
build() {
    local source="$1" build_dir="$2"
    if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
        cmake -S "$source" -B "$build_dir" "${generator[@]}" -DCMAKE_BUILD_TYPE=Debug >/dev/null
    fi
    cmake --build "$build_dir" -j >/dev/null
}

# Every doctest case name of every test executable in a build, sorted and unique.
test_names() {
    find "$1" -type f -perm -u+x -name 'cw_*tests' | sort | while read -r exe; do
        "$exe" --list-test-cases --no-intro | grep -v -e '^\[doctest\]' -e '^=*$'
    done | sort -u
}

# git archive stamps every file with the commit time, so ninja cannot see that a different base
# has different sources (an older commit looks older than the objects already built). The base
# build is therefore reused only for the same revision and rebuilt from scratch otherwise.
if [[ "$(cat "$dir/base-rev" 2>/dev/null)" != "$base_rev" ]]; then
    echo "==> Extracting $base ($(git rev-parse --short "$base_rev"))"
    rm -rf "$dir/base-src" "$dir/base-build" "$dir/base-rev"
    mkdir -p "$dir/base-src"
    git archive "$base_rev" | tar -x -C "$dir/base-src"
    rm -rf "$dir/base-src/third_party"
    ln -s "$root/third_party" "$dir/base-src/third_party"
    echo "$base_rev" >"$dir/base-rev"
else
    echo "==> Reusing the build of $base ($(git rev-parse --short "$base_rev"))"
fi

echo "==> Building $base"
build "$dir/base-src" "$dir/base-build"
echo "==> Building this checkout"
build "$root" "$dir/head-build"

status=0
compared_bytes=1
fixtures="$root/tests/fixtures/corpus/input"
base_corpus="$dir/base-build/tests/cw_format_corpus"
if [[ ! -x "$base_corpus" ]]; then
    if [[ "$allow_missing" -eq 0 ]]; then
        fail "$base has no cw_format_corpus; pass --allow-missing-corpus to compare test names only"
    fi
    echo "==> Format corpus: skipped, $base predates it"
    compared_bytes=0
else
    echo "==> Format corpus"
    rm -rf "$dir/out"
    "$base_corpus" "$fixtures" "$dir/out/base"
    "$dir/head-build/tests/cw_format_corpus" "$fixtures" "$dir/out/head"
    if diff -r "$dir/out/base" "$dir/out/head"; then
        echo "identical: $(find "$dir/out/head" -type f | wc -l) files"
    else
        echo "the bytes written differ from $base (diff above)" >&2
        status=1
    fi
fi

echo "==> Test case names"
test_names "$dir/base-build" >"$dir/base-tests.txt"
test_names "$dir/head-build" >"$dir/head-tests.txt"
lost="$(comm -23 "$dir/base-tests.txt" "$dir/head-tests.txt")"
echo "$base: $(wc -l <"$dir/base-tests.txt") cases; this checkout: $(wc -l <"$dir/head-tests.txt")"
if [[ -n "$lost" ]]; then
    printf 'lost test cases:\n%s\n' "$lost" >&2
    status=1
fi

if [[ "$status" -eq 0 && "$compared_bytes" -eq 1 ]]; then
    echo "==> Same bytes, no test lost"
elif [[ "$status" -eq 0 ]]; then
    echo "==> No test lost (bytes not compared)"
fi
exit "$status"
