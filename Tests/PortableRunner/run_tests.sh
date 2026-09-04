#!/usr/bin/env bash
#
# Build and run the GameLogic suite on a machine without Visual Studio.
#
#   Tests/PortableRunner/run_tests.sh              everything
#   Tests/PortableRunner/run_tests.sh Chart        only tests whose Suite.Method contains "Chart"
#   OUTPOST_TEST_TIMES=1 Tests/PortableRunner/run_tests.sh    with a per-test duration
#
# The executable lands at x64/Debug/PortableTests, which is where MSBuild puts GameLogicTests.dll
# and therefore where the oracle expects to start walking up from (Tests/GameLogicTests/
# OracleImage.cpp). Intermediates live beside it under x64/Debug/portable-runner/, and all of it
# is gitignored.
#
# THE ORACLE IS NOT OPTIONAL. Without the assembled game the oracle tests skip themselves and
# OracleIsPresent fails by design (ADR-003 section 1). Run `python tools/labels.py --assemble`
# first; this script says so rather than letting the suite report a failure whose cause is a
# missing file.
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

if [ ! -f "$REPO/Design/Reference/Labels.txt" ]; then
  echo "warning: Design/Reference/Labels.txt is missing, so the oracle cannot load." >&2
  echo "         Run 'python tools/labels.py --assemble' -- otherwise OracleIsPresent fails" >&2
  echo "         and every test that compares against the original silently skips." >&2
fi

python3 "$HERE/generate_runner.py" "$BUILD" "$REPO" "$REPO"/Tests/GameLogicTests/*Tests.cpp

make -C "$BUILD" -j "$JOBS" --no-print-directory

exec "$EXE" "$@"
