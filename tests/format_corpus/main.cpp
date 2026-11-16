// cw_format_corpus <input-dir> <out-dir>
//
// Runs the format corpus (corpus.hpp). tools/compare-with-main.sh runs it from two builds and
// compares the outputs. To regenerate the golden files after an intended format change (a
// behavioral PR, never a refactoring), write into tests/fixtures/corpus/expected after removing it.

#include "corpus.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <input-dir> <out-dir>\n", argv[0]);
        return 2;
    }
    std::string error;
    if (!cw_corpus::run(argv[1], argv[2], error)) {
        std::fprintf(stderr, "cw_format_corpus: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
