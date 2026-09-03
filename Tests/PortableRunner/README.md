# The portable test runner

Runs `Tests/GameLogicTests/` on a machine without Visual Studio — same test files, same oracle,
same assertions, a different way of calling them.

```sh
python tools/labels.py --assemble        # once: the oracle needs the assembled game
Tests/PortableRunner/run_tests.sh        # 124 tests, about 18 seconds from cold
Tests/PortableRunner/run_tests.sh Chart  # only tests whose Suite.Method contains "Chart"
```

Needs `g++` with C++20, `make`, and Python 3. Nothing else.

## Why this exists

MSVC is the authority (ADR-004 §1), and it stays the authority: it is what the game ships built
with, and a disagreement between the two runners is decided in MSVC's favour every time.

What MSVC is not, is *available*. The port is written against the assembled original a routine at
a time, and getting a carry chain right takes ten or twenty compile-run cycles. On a machine with
no Visual Studio those cycles either do not happen, or they happen on CI at four minutes each.
This runner turns that loop into eighteen seconds from cold and a quarter of a second when only
one file changed — and every defect found while writing slices 1c-c-b, 2b, 2c and 2d was found
here first, with MSVC agreeing exactly afterwards.

The second reason is CI. Measured on the same commit: the Ubuntu leg of
`.github/workflows/build-and-test.yml` runs the whole suite in **34 seconds** end to end (52s on
a cold BeebAsm cache) against the Windows job's **4m35s**. So a push that breaks the port says so
while the Windows job is still locating Visual Studio. It is cheaper too, though by less than it
feels: GitHub charges roughly twice the per-minute rate for a Windows runner, so the saving is
that factor times the 8× shorter run — not the order of magnitude the rate alone suggests.

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

One file from `Outpost/` is compiled here too: `SaveStore.cpp`, the commander store. It is the
executable's, not `GameLogic`'s, because the determinism guard forbids file access there — and it
was committed and left uncompiled for a day because the only machine that could build it ran
Windows. Ten lines of `GetEnvironmentVariableW` in the shim made that untrue, and the first test
written against it found a defect. `Main.cpp` is not compiled here and does not need to be: it is a
`wWinMain` with nothing to assert.

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

## The one rule this tree breaks

`Shim/CppUnitTest.h` and `Shim/NeuronCore.h` deliberately share their names with the real
headers, which ADR-004 §2 otherwise forbids repo-wide. Shadowing *is* the mechanism: the files
under test say `#include "pch.h"` and `pch.h` says `#include "NeuronCore.h"`, and the whole point
is that neither line changes. `Shim/` is on the include path for this runner and for nothing else,
which is why the two headers live in their own directory rather than beside the scripts.
