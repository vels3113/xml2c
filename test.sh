#!/usr/bin/env bash
# Usage: ./test.sh [suite]
# Suites: unit (default), asan, ubsan, coverage, valgrind, fuzz, tidy, cppcheck, bench, memprofile, massif, all
set -euo pipefail

SUITE=${1:-all}

./build.sh

case "$SUITE" in
    unit)     make test ;;
    asan)     make asan ;;
    ubsan)    make ubsan ;;
    coverage) make coverage ;;
    valgrind) make valgrind ;;
    tidy)     make tidy ;;
    cppcheck) make cppcheck ;;
    all)
        make test
        make asan
        make ubsan
        make coverage
        make valgrind
        make tidy
        make cppcheck
        ;;
    *) echo "Unknown suite: '$SUITE'. Valid: unit asan ubsan coverage valgrind fuzz tidy cppcheck bench memprofile massif all" >&2; exit 1 ;;
esac
