#!/bin/sh
# Builds and runs the Celestial host unit tests (Astronomy Engine + calculator + selector + format).
# Usage: tests/celestial/run_tests.sh   (from anywhere; needs a host C/C++ compiler)
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/celestial_tests"
mkdir -p "$OUT"

CC="${CC:-cc}"
CXX="${CXX:-c++}"
WARN="-Wall -Wextra"

"$CC" -std=c99 -O2 -DASTRONOMY_ENGINE_NO_CURRENT_TIME -c "$ROOT/astronomy.c" -o "$OUT/astronomy.o"
"$CXX" -std=c++17 -O2 $WARN -c "$ROOT/NavStars.cpp" -o "$OUT/NavStars.o"
"$CXX" -std=c++17 -O2 $WARN -c "$ROOT/CelestialCalculator.cpp" -o "$OUT/CelestialCalculator.o"
"$CXX" -std=c++17 -O2 $WARN -c "$ROOT/CelestialSelector.cpp" -o "$OUT/CelestialSelector.o"
"$CXX" -std=c++17 -O2 $WARN -c "$HERE/test_celestial.cpp" -o "$OUT/test_celestial.o"
"$CXX" "$OUT"/*.o -o "$OUT/test_celestial"

"$OUT/test_celestial"
