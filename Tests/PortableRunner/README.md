# The portable test runner

Runs `Tests/GameLogicTests/` on a machine without Visual Studio — same test files, same oracle,
same assertions, a different way of calling them.

```sh
python tools/labels.py --assemble        # once: the oracle needs the assembled game
Tests/PortableRunner/run_tests.sh        # <!--count:tests-->349 tests, about a minute from cold
Tests/PortableRunner/run_tests.sh Chart  # only tests whose Suite.Method contains "Chart"
```

Needs `g++` with C++20, `make`, and Python 3. Nothing else.

## Why this exists

MSVC is the authority (ADR-004 §1), and it stays the authority: it is what the game ships built
with, and a disagreement between the two runners is decided in MSVC's favour every time.

What MSVC is not, is *available*. The port is written against the assembled original a routine at
a time, and getting a carry chain right takes ten or twenty compile-run cycles. On a machine with
no Visual Studio those cycles either do not happen, or they happen on CI at four minutes each.
This runner turns that loop into a minute from cold and a few seconds when one file changed and a
filter narrows the run to the suite being worked on — and every defect found while writing slices
1c-c-b, 2a, 2b, 2c and 2d was found here first, with MSVC agreeing exactly afterwards. That has
held for every slice since.

**Measured 2026-09-05 at 348 tests: 64 seconds from cold, 21 seconds warm.** Those 21 are the RUN
rather than the build, and almost all of them are a handful of exhaustive sweeps: 2,048 system data
screens compared character for character, 393,216 keystrokes through `gnum`, 65,536 pairs through
several of the arithmetic routines, and the whole-canvas comparisons in the drawing and explosion
suites. That is the cost of the sweeps being exhaustive rather than sampled, and it is why
`run_tests.sh` takes a filter. The cold figure grows with the number of translation units — it was
thirty-seven seconds at 313 tests and 52 files — so treat it as the shape and not the constant.

The second reason is CI. Measured 2026-09-05 at 310 tests, on the same commit: the Ubuntu leg of
`.github/workflows/build-and-test.yml` ran the whole suite in **72 seconds** end to end (59s of it
the build-and-run step, BeebAsm cached) against the Windows job's **4m24s** — of which 51s is the
tests and the rest is two MSBuild passes over the test project, two over the executable, and a
37-second `vswhere` preflight. Re-measured at 348 tests the two are 87 seconds and 5m07s, so the
ratio has held while both grew. The ratio is what the argument rests on, not the absolute figures.
So a push that breaks the port says so while the Windows job is still locating Visual Studio.

**What the ratio does not buy is agreement.** The Ubuntu leg is faster than the authority; it is
also *more permissive*, and two Windows-only compile errors have reached CI through it — `const
bool near` after `<windows.h>`, and `Assert::AreEqual` on a `std::vector<bool>` proxy that MSVC's
`ToString` static-asserts on and g++ does not (§6.116). Each time, the fix is to teach this runner
that one specific thing; the shim now refuses the second by name.

## How it works

`Shim/` supplies three headers that shadow the ones MSVC provides, and nothing else:

| Header | Stands in for | What it does |
|---|---|---|
| `CppUnitTest.h` | `VC\Auxiliary\VS\UnitTest\include\CppUnitTest.h` | `TEST_CLASS`/`TEST_METHOD` become a struct and a member function; `Assert::*` throw |
| `NeuronCore.h` | `NeuronCore/NeuronCore.h` | the three Win32 calls under test reach for: two answered from `/proc/self/exe`, one from the environment |
| `pch.h` | `Tests/GameLogicTests/pch.h` | includes the two above |

`generate_runner.py` then parses the test files for `TEST_CLASS` and `TEST_METHOD` and writes one
translation unit per test file that calls every method in it, plus a `main`, plus a makefile.
`run_tests.sh` regenerates, builds with `make -j`, and runs.

**No file under `Tests/GameLogicTests/`, `GameLogic/` or `Outpost/` changes, and none of it is
copied.** That is the property that makes this trustworthy: there is one suite, and the two runners
disagree only about how it is invoked. A test added to a `.cpp` is picked up by both without being
registered anywhere.

Three files from `Outpost/` are compiled here too — `SaveStore.cpp`, `Presentation.cpp` and
`KeyMap.cpp` — because each is the executable's and yet holds a DECISION rather than an API call:
the commander store, the palette and viewport arithmetic and the frame pacing, and the key map.
`ShellTests.cpp` covers them and runs on both legs. Everything else in `Outpost/` is Win32 and
D3D12 and is not compiled here; `tools/check_outpost.py` checks the names and arities it uses,
and only the Windows build checks the types.

The build output goes to `x64/Debug/PortableTests`, deliberately the same directory MSBuild puts
`GameLogicTests.dll` in, because the oracle finds the repository by walking up from wherever its
own binary lives.

## What it is not

- **Not a second authority.** If the two runners disagree, MSVC is right and this is broken.
- **Not the same compiler.** g++ and MSVC differ on integer promotion in edge cases, on which
  narrowing conversions warn, and on the order of evaluation the standard leaves open. Code that
  compiles here can fail there. That is a reason to run both, not a reason to trust one.
- **Not a complete CppUnitTest.** No `TEST_METHOD_INITIALIZE`, no `TEST_METHOD_CLEANUP`, no
  attributes, no data-driven tests. The suites use none of them. A test that needs one should get
  it added here in the same commit, rather than being written differently for the two runners.
- **Not tolerant of a non-ASCII checkout path.** `Shim/NeuronCore.h` widens the executable's path
  a byte at a time. A repository under a path with non-ASCII characters fails here and works under
  MSVC.
- **Not a substitute for assembling the game.** Without `Design/Reference/Labels.txt` and the
  assembled blocks, every oracle test skips and `OracleIsPresent` fails, exactly as it does on
  Windows (ADR-003 §1, Risk R9). `run_tests.sh` warns before it gets that far.
- **Not safe in a worktree that has lost its submodule.** `git worktree add` leaves
  `Upstream/elite-source-code-library` as an empty gitlink directory, and every `git checkout -f`
  in that worktree puts the empty directory back over any symlink placed there. The suite then
  reports `N passed, 1 failed` on EVERY run — `OracleIsPresent`, by design — and a harness that
  reads only that line calls every mutant caught. Verify `N passed, 0 failed` before believing a
  mutation result (`AGENTS.md` §6, §6.119).

## The one rule this tree breaks

`Shim/CppUnitTest.h` and `Shim/NeuronCore.h` deliberately share their names with the real
headers, which ADR-004 §2 otherwise forbids repo-wide. Shadowing *is* the mechanism: the files
under test say `#include "pch.h"` and `pch.h` says `#include "NeuronCore.h"`, and the whole point
is that neither line changes. `Shim/` is on the include path for this runner and for nothing else,
which is why the two headers live in their own directory rather than beside the scripts.
