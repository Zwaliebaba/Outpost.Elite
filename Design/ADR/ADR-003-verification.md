# ADR-003 — Verification: the Assembled Original Is the Oracle

**Status:** Proposed · 2026-09-02
**Depends on:** ADR-001 (fidelity), ADR-002 (exact semantics — without it there is nothing to compare)
**Feeds:** the acceptance column of every slice in the plan; ADR-004 (test project shape)

## Context

A port of 700 routines by hand will have mistakes. The question is how each one is found. Three
classes of behaviour need three different instruments: pure computation (most of the game),
pixels (the screens), and the whole (does it stay deterministic and does the game as a sequence
of steps match).

The upstream tree builds with BeebAsm into the exact code blocks the game loads
(ELTA…ELTK at `&1D00` and `&6A00`, `WORDS`, `IANTOK`, `SHIPS` at their addresses), and the
build can be told to skip the encryption and the workspace-noise matching
(`encrypt=no match=no`), which yields plain binaries whose bytes are the annotated code.

## Decision

### §1 The 6502 oracle

- `Tests/GameLogicTests/Cpu6502.h/.cpp` is a straightforward NMOS 6502 interpreter
  (all documented opcodes, decimal mode, the flags exactly), ~600 lines, **written here, no
  third-party core**. It is test-only code; nothing in `GameLogic` or the executable sees it.
- `OracleFixture` loads the reference binaries into a 64 KB array at their load addresses,
  reads the label map produced by `tools/labels.py` from BeebAsm's listing, and exposes
  `Call(label, Cpu6502::State in) -> State`: it pushes a sentinel return address, `JSR`s the
  label and runs until the `RTS` pops the sentinel or an instruction budget is exceeded. Memory
  is a plain array the test can read and write (zero page, `INWK`, the bitmap at `SCBASE`).
- The binaries are **not committed** (ADR-001 §5) but they **are already on disk**: the
  vendored tree ships `versions/c64/4-reference-binaries/gma85-ntsc/ELTA.bin`…`ELTK.bin`, which
  is precisely the variant this port targets. So the oracle does not have to wait for slice 0b
  to assemble anything — **what it still needs from 0b is the label-to-address map**, since a
  binary without labels is 32 KB of bytes with no way to say "call `TT20`".
- **Amended 2026-09-03: there is no `Oracle.json`.** The draft put the paths in a config file.
  In the event that was solving a problem nobody had: the paths *inside* the repository are
  fixed, the only unknown is where the repository is, and the running test binary already knows
  that. `OracleImage` walks up from its own module until it finds `Outpost.slnx` and reads
  fixed relative paths from there. No config file to keep in step with reality on each machine,
  and still no environment variables.
- When the label map or the binaries are missing, every oracle test logs what is missing and
  the command that fixes it (`python tools/labels.py --assemble`) and then passes, so a machine
  without an assembled game is still usable. **One test, `OracleIsPresent`, fails instead**, so
  a green run can never quietly mean "none of this executed" (R9).
- **A routine short enough to hand-assemble needs none of that**, which is how the first oracle
  suite ran before BeebAsm existed on the machine: the test assembles the twenty bytes of the
  routine under test and compares. That trick does not scale past a few dozen bytes, but it is
  what let slice 0c prove the whole approach end to end rather than merely compile it.
- **Test pattern.** For a pure routine `F` with byte inputs, the test sets the same inputs in
  the oracle's memory and in the port's `ZeroPage`, runs both, and compares every output the
  routine's commentary lists — plus the flags the callers read. 8×8-bit inputs are tested
  exhaustively (65,536 cases run in milliseconds); wider inputs are sampled with a fixed seed
  and the corners. Routines that consume `DORND` are tested with the RNG state as an input.
- **Where the oracle stops.** Routines that touch hardware registers (`VIC`, `SID`, `CIA`)
  or the Kernal are not called through the oracle *as a whole*; their computational parts are
  (e.g. `SCAN` computes blip coordinates before poking sprite registers — the coordinates are
  compared, the pokes are captured by a memory-write hook and compared as a list). The
  interrupt-driven sound and music players are run by calling the handler N times with the
  hook recording writes to `$D400–$D41C`, which is exactly the port's `SoundEvent` stream.

### §2 Golden canvases

- `Canvas` can be dumped to PNG and hashed. A golden test runs a scripted `InputFrame`
  sequence from `Game::Reset` and compares the hash at named steps against a committed value;
  on mismatch it writes the actual PNG and a diff image beside the expected one
  (`tools/golden_diff.py`).
- **Amended 2026-09-03: there is no emulator, so goldens are no longer accepted by eye.** The
  owner has ruled out a reference run. The replacement is stronger: the game's drawing routines
  write their bitmap into the oracle's own 64 KB image, so a test calls them, decodes those
  bytes into canvas form and compares pixel for pixel. Anything the oracle can draw is checked
  exactly rather than judged by a person against a screenshot.
- Goldens keep a narrower job: whole screens assembled across many calls, where composing the
  equivalent oracle run costs more than it proves. Their first acceptance is then a reasoned
  reading of the drawing code rather than a photograph. They are few and named for what they
  prove (`TitleScreen_Frame1`, `LongRangeChart_Lave`, `Dashboard_AfterLaunch`), never one per
  frame, and a change needs a visual diff attached (Risk R10).
- Where the oracle can reach the same pixels (the line routines write the C64 bitmap at
  `SCBASE`), the oracle is preferred: the test decodes the multicolour bitmap bytes into the
  canvas format and compares directly. Goldens cover what the oracle cannot compose.

### §3 Replay determinism

- `Game::StateHash()` hashes the whole workspace (zero page, `K%`, the commander, the RNG,
  the canvas). A replay test runs a scripted input sequence twice in one process and once each
  in Debug and Release (the CI leg compares the recorded hash sequence across configurations)
  and requires identical hashes at every step. `GameLogic` has no clock, no OS entropy and no
  pointers as keys, and a CI grep guards `<chrono>`, `<random>`, `rand(` and `float` out of it.
- The replay scripts double as the reproduction path for any bug report: the script that
  reaches the state is the ticket.

### §4 Bringing the oracle up

- Slice 0c proves the interpreter on `DORND`: one routine, one seed, one expected result read
  from the commentary. Then `MULTU` exhaustively. If those two are not green the interpreter is
  wrong, and it is fixed before anything else is ported.
- Label addresses: BeebAsm's verbose listing (`-v`) prints each label with its address;
  `tools/labels.py` parses that into `Labels.json`. If the listing proves awkward, a one-line
  `PRINT` per label appended to a copy of the master file does the same job — the plan does
  not depend on which.

## Alternatives considered

- **Compare against the running emulator (VICE) over a monitor connection.** Real hardware
  timing, no interpreter to write; but slow, flaky in CI, and it cannot call one routine in
  isolation with chosen inputs, which is the whole point.
- **Trust the commentary and test by hand.** Moxon's commentary is excellent and is what makes
  the port readable, but it describes intent, and a port's bugs are in the gap between intent
  and the bytes. The oracle is the bytes.
- **Property-based tests only.** Useful in addition (the replay suite is one), insufficient
  alone: nothing about a wrong `FMLTU` violates a property you would think to write.

## Consequences

- Every slice from 1b onward starts with the oracle test and ends with it green. The port's
  speed of progress is bounded by writing tests, which is the right bound.
- The test project depends on files outside the repository. That is deliberate (ADR-001 §5)
  and made loud (§1, R9), and a new machine's setup is: clone the upstream tree, build it,
  point `Oracle.json` at it.
- The interpreter is a small, permanent asset: any future 6502 port in this family of
  repositories inherits it.
