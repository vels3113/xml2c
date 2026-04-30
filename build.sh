#!/usr/bin/env bash
# Usage: ./build.sh [preset]
# Presets: debug (default), asan, ubsan, release, coverage, profiling
set -euo pipefail

PRESET=${1:-debug}
make format
cmake --preset "$PRESET"
cmake --build --preset "$PRESET" -j
