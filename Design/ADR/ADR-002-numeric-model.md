# ADR-002 — Numeric Model: 8-bit Semantics Preserved Exactly

**Status:** Proposed · 2026-09-02
**Depends on:** ADR-001 (fidelity)
**Feeds:** ADR-003 (the oracle only works if this holds), every `GameLogic` file

## Context

Elite's logic is 8-bit integer arithmetic with a handful of 16- and 24-bit compound values,
and it depends on the details:

- **Coordinates** are sign-magnitude: `x_lo`, `x_hi`, `x_sgn` (bit 7 is the sign) — three bytes
  per axis, and the sign is separate from the magnitude, so `-0` exists and comparisons are
  on magnitude.
- **Multiplication** is by shift-and-add (`MULTU`, `MU11`) *or by logarithm tables*
  (`FMLTU` on this lineage uses `LOG`/`LOGL`/`ANTILOG`/`ANTILOGODD`) — the two give slightly
  different results, and which one a routine uses is part of its behaviour.
- **Rotation** (`MVS4`/`MVS5`) applies small-angle approximations with shifts, and `TIDY`
  re-orthonormalises the matrix with `NORM`, whose square root is a table.
- **The RNG** (`DORND`) is four bytes of state with a specific carry chain; universe generation
  (`TT20`, `TT54`) is a seed-twisting scheme whose every bit matters for names and prices.
- **Trig** is `SNE` (32 sine entries) and `ACT` (32 arctan entries) — tables, not functions.
- **Carry and overflow** flags are reused across routine boundaries in places (a routine
  returns with `C` set to mean something).

A port that used `int` and `float` "for clarity" would be a different game in ways that show
up as wrong prices in a system three galaxies away.

## Decision

1. **Types.** Game state uses `std::uint8_t`, `std::int8_t`, `std::uint16_t` as the original
   did, with a small set of helpers in `EliteTypes.h`:
   - `struct SignMag24` for the three-byte sign-magnitude coordinate (`lo`, `hi`, `sgn`), with
     the original comparisons and the `MVT3`/`MVT6`-style add/subtract as member functions;
   - `struct Flags { bool c; bool n; bool z; bool v; }` returned by the few routines whose
     callers read a flag, rather than a hidden global;
   - `Wrap8(x)`, `AddC(a, b, c)` → `(sum, carry)` helpers so that intent reads.
   - **No `float`, `double` or `DirectXMath` anywhere in `GameLogic`.** A CI guard greps for
     them (the same mechanism Frontier uses for `<chrono>`).
2. **Tables are data, not functions.** `SNE`, `ACT`, `LOG`, `LOGL`, `ANTILOG`, `ANTILOGODD`,
   `TWOS` and kin are extracted byte-for-byte (slice 1a). A port of `FMLTU` looks up the same
   tables the original did; it does not call `std::log`.
3. **Widths are not widened for convenience.** Where the original truncates to 8 bits, the
   port truncates. Where 16-bit intermediate values are formed from two bytes, the port forms
   the same 16-bit value and truncates the same way. Widening is allowed only in a helper whose
   result is then narrowed identically (e.g. computing `a*b` in `unsigned` and taking the byte
   the original took).
4. **The canvas is 320×200 logical pixels**, one byte per pixel holding a C64 colour index, and
   all line and circle arithmetic is in that space with the original's algorithms
   (`LOIN`'s seven variants, `CIRCLE2`'s step table). Sub-pixel accuracy, anti-aliasing and
   higher internal resolution are phase-6 items that would fork the drawing code, not change it.
5. **Erase-by-XOR is available.** `Canvas` provides XOR plotting, and the line heaps are ported,
   because `LL9` and `SUN` use the heap contents to decide what to erase. Whether the executable
   presents every intermediate state or only the end of an iteration is a presentation
   decision (ADR-005); the logic is the same either way.

## Alternatives considered

- **Native `int` with care.** Simpler to read; impossible to test against the oracle beyond
  "roughly the same", which is no test. The DOS port took this route with `int32_t` fixed point
  and found it had to add 273 assertions to convince itself — and it was porting an x86 rewrite
  that had already made the same compromise once.
- **Emulate the 6502 and run the original.** Cheapest fidelity, zero C++ port. It is the test
  oracle (ADR-003) precisely so that it does not have to be the product.
- **Preserve semantics only where tests show it matters.** That is this decision after the
  fact: the tests will show it matters everywhere the oracle reaches, and the places it does
  not reach (cadence, sound synthesis) are already carved out.

## Consequences

- `GameLogic` reads like annotated 6502 in places. That is intended; the annotation is Moxon's
  and the reader has bbcelite.com for the deep dives. A tidying pass is allowed *after* a
  routine is oracle-green and stays green.
- The port will be slower per operation than a float rewrite and faster than it needs to be by
  three orders of magnitude; a C64 did this at 1 MHz.
- Anything that wants to be "better than the original" (phase 6) will have to be a parallel
  path with the fidelity path still selectable, which is the intended cost.
