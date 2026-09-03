# Design/Reference/

Inputs the 6502 oracle needs, and measurements taken from the original game.

**Almost everything here is generated and deliberately not committed.** `Labels.txt`,
`Binaries.txt`, `labels-*.txt` and `compile-*.txt` are derived from source we do not own and
are regenerable in one command, so they live on disk rather than in the history that gets
published (ADR-001 §5). A fresh clone has none of them, and the test suite says so loudly:
`OracleIsPresent` fails, and every oracle test logs the command below.

## Regenerating the oracle inputs

```
python tools/labels.py --assemble
```

That needs **BeebAsm** at `Tools-ext/beebasm/out/beebasm.exe`, which is also not committed (it
is GPL, and it is a tool we run rather than a library we link). To build it:

```
git clone https://github.com/stardot/beebasm.git Tools-ext/beebasm
cl /nologo /std:c++14 /EHsc /O2 /D_CRT_SECURE_NO_WARNINGS /wd4996 ..\src\*.cpp /Fe:beebasm.exe
```

Run that `cl` line from `Tools-ext/beebasm/out/` inside a VS x64 developer environment. Do not
use BeebAsm's own `CMakeLists.txt`: it passes GCC warning flags and links `stdc++`/`m`, so it
does not configure under MSVC. Compiling the sources directly is simpler than patching it.

What the tool produces:

| File | Contents |
|---|---|
| `Labels.txt` | Every label in the assembled C64 build and its runtime address, one per line. ~1,782 of them. |
| `Binaries.txt` | Which assembled block loads at which address. Eleven rows, `ELTA.bin`…`ELTK.bin`. |
| `compile-source.txt` | The full assembly log, kept because the load addresses only appear in it. |
| `labels-source.txt`, `labels-data.txt` | BeebAsm's raw dumps, before normalising. |

The assembled `.bin` files land in the vendored tree under
`Upstream/elite-source-code-library/versions/c64/3-assembled-output/`, and are ignored there.

## There are no emulator measurements, and there will not be

Slice 0b-b was cancelled on 2026-09-03 by owner ruling: no VICE, no reference run. Do not add a
screenshot set here expecting the corpus to depend on it.

The two things it was carrying were re-answered in
[Elite-Conversion-Plan.md §6.5](../Elite-Conversion-Plan.md):

- **Golden canvases** are now checked against the oracle rather than against screenshots. The
  game's drawing routines write their bitmap into the loaded image, so a test decodes those
  bytes and compares pixel for pixel. Exact, and no emulator.
- **The step cadence has no measurement and no planned source for one.** That is a real open
  risk (R3), not a solved problem. Whoever reaches slice 2e picks between counting cycles in the
  interpreter and choosing a configurable default, and says which they did.
