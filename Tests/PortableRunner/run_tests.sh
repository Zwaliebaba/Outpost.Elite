#!/usr/bin/env bash
#
# Build and run the GameLogic suite on a machine without Visual Studio.
#
#   Tests/PortableRunner/run_tests.sh              everything
#   Tests/PortableRunner/run_tests.sh Chart        only tests whose Suite.Method contains "Chart"
#   OUTPOST_TEST_TIMES=1 Tests/PortableRunner/run_tests.sh    with a per-test duration
#   OUTPOST_TEST_SKIP=file Tests/PortableRunner/run_tests.sh  everything but the Suite.Method
#                                                  names in that file, one per line
#
# `--coverage`, `--measure` and `--record` WERE HERE AND ARE NOT ANY MORE (Modernize.md M6-b-7):
# all three drove the 6502 interpreter, and there is no interpreter.
#
# The executable lands at x64/Debug/PortableTests, where MSBuild puts GameLogicTests.dll.
# Intermediates live beside it under x64/Debug/portable-runner/, and all of it is gitignored.
#
# See README.md for what this runner is and is not.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

OUT="${OUTPOST_PORTABLE_OUT:-$REPO/x64/Debug}"
BUILD="$OUT/portable-runner"
EXE="$OUT/PortableTests"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

mkdir -p "$BUILD"

python3 "$HERE/generate_runner.py" "$BUILD" "$REPO" "$REPO"/Tests/GameLogicTests/*Tests.cpp

make -C "$BUILD" -j "$JOBS" --no-print-directory

exec "$EXE" "$@"
