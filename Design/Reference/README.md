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

That needs **BeebAsm** under `Tools-ext/beebasm/out/` — `beebasm.exe` from MSVC or `beebasm` from
any other toolchain; the tool looks for both — which is also not committed (it is GPL, and it is a
tool we run rather than a library we link). To build it, either:

```
git clone https://github.com/stardot/beebasm.git Tools-ext/beebasm
cd Tools-ext/beebasm && git checkout ca2cc5fd2fa3f73da3b0682ad004b2aca99840c3
mkdir -p out && g++ -std=c++14 -O2 -o out/beebasm src/*.cpp          # Linux, macOS, MSYS
```

or, from `Tools-ext/beebasm/out/` inside a VS x64 developer environment:

```
cl /nologo /std:c++14 /EHsc /O2 /D_CRT_SECURE_NO_WARNINGS /wd4996 ..\src\*.cpp /Fe:beebasm.exe
```

The commit is the one CI pins (`BEEBASM_REF` in `.github/workflows/build-and-test.yml`); an
unpinned tool is an unreproducible build. Do not use BeebAsm's own `CMakeLists.txt`: it passes GCC
warning flags and links `stdc++`/`m`, so it does not configure under MSVC.

What the tool produces:

| File | Contents |
|---|---|
| `Labels.txt` | Every label in the assembled C64 build and its runtime address, one per line. 1,927 of them. |
| `Binaries.txt` | Which assembled block loads at which address. Fourteen rows. |
| `LoaderLabels.txt`, `LoaderBinaries.txt` | The same pair for `elite-loader.asm`, which assembles over the screen bitmap and so gets an image of its own that the oracle never loads (§6.104). Read by `tools/extract_tables.py` for `sdump`/`cdump` and by `TableTests`. |
| `compile-source.txt`, `compile-data.txt`, `compile-loader.txt` | The full assembly logs, kept because the load addresses only appear in them. |
| `labels-source.txt`, `labels-data.txt`, `labels-loader.txt` | BeebAsm's raw dumps, before normalising. |

**A fourth assembly is missing, and it is a known gap rather than a decision.** `tools/labels.py`
builds `elite-source.asm`, `elite-data.asm` and `elite-loader.asm`. It does not build
`elite-sprites.asm` — 84 lines, 448 bytes, seven sprite definitions — so those bytes are the one
piece of the original's data that `extract_tables.py --check` cannot verify. Adding it on the
`LOADER_ASSEMBLY` pattern (its own reference pair, kept out of the oracle image, because
`CODE% = &7C3A`) is the prerequisite ADR-005 §1 names for the sprite work, and it is worth doing on
its own regardless.

The assembled `.bin` files land in the vendored tree under
`Upstream/elite-source-code-library/versions/c64/3-assembled-output/`, and are ignored there.
**One of them overwrites a vendored file**: `COMLOD.unprot.bin` is tracked upstream at 18,016
bytes and the loader assembly writes 18,011 — the difference is outside `sdump` and `cdump`, so
`extract_tables.py --check` passes either way, but a run of `--assemble` leaves the submodule
dirty by that one file. Restore it with `git -C Upstream/elite-source-code-library checkout -- .`
rather than committing it.

## There are no emulator measurements, and there will not be

Slice 0b-b was cancelled on 2026-09-03 by owner ruling: no VICE, no reference run. Do not add a
screenshot set here expecting the corpus to depend on it.

The two things it was carrying were re-answered in
[Elite-Conversion-Plan.md §6.5](../Elite-Conversion-Plan.md):

- **Golden canvases** are now checked against the oracle rather than against screenshots. The
  game's drawing routines write their bitmap into the loaded image, so a test decodes those
  bytes and compares pixel for pixel. Exact, and no emulator.
- **The step cadence was measured after all, and from the shipped binary rather than an
  emulator.** `Cpu6502` counts cycles (Risk R3, built 2026-09-03), the title loop was measured
  and paced by it (§6.110) and the flight loop the same (§6.114). What an emulator would have
  added is the 5–10% the VIC-II steals, which the counter does not model and which is written
  down beside every figure that depends on it.
