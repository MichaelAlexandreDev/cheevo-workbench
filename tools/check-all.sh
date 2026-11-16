#!/usr/bin/env bash
# Runs every check a change must pass before merging, stopping at the first failure:
#   1. clang-format --dry-run --Werror on the tracked C/C++ files;
#   2. a Debug build with warnings as errors (project code only) and ctest;
#   3. an ASan+UBSan build and ctest, with LeakSanitizer on (tools/lsan.supp).
#
# It builds in its own directories (build-check/ by default, or $CW_CHECK_DIR), so the developer's
# build/ and build-asan/ caches are never reconfigured.
#
# Where LeakSanitizer cannot run (e.g. under ptrace in a sandbox), call it with
# ASAN_OPTIONS=detect_leaks=0 and say so in the PR: the user's options are kept.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
check_dir="${CW_CHECK_DIR:-$root/build-check}"
cd "$root"

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi

step() {
    printf '\n==> %s\n' "$*"
}

# Configures, builds and tests one build directory. Extra arguments go to the configure step.
build_and_test() {
    local dir="$1"
    shift
    # Keep the generator of an existing directory; CMake refuses to switch it.
    local gen=("${generator[@]}")
    if [[ -f "$dir/CMakeCache.txt" ]]; then
        gen=()
    fi
    cmake -S "$root" -B "$dir" "${gen[@]}" -DCMAKE_BUILD_TYPE=Debug -DCW_WERROR=ON "$@" >/dev/null
    cmake --build "$dir" -j
    ctest --test-dir "$dir" --output-on-failure
}

step "clang-format"
mapfile -d '' sources < <(git ls-files -z -- '*.c' '*.h' '*.cpp' '*.hpp')
clang-format --dry-run --Werror "${sources[@]}"
echo "${#sources[@]} files formatted"

step "Debug build (warnings are errors) + ctest"
build_and_test "$check_dir/debug"

step "ASan+UBSan build + ctest"
sanitize="-fsanitize=address,undefined -fno-omit-frame-pointer"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
export LSAN_OPTIONS="suppressions=$root/tools/lsan.supp:print_suppressions=0${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
build_and_test "$check_dir/asan" "-DCMAKE_CXX_FLAGS=$sanitize"

step "All checks passed"
