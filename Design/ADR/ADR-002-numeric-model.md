# ADR-002 — Numeric Model: 8-bit Semantics Preserved Exactly

**Status:** Proposed · 2026-09-02 · **§4 amended 2026-09-03** (the canvas representation, from
measurement rather than assertion — see §7)
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
4. **The canvas holds the C64's screen state, and resolves to 320×200 indices at the seam.**
   **Amended 2026-09-03** from "320×200 logical pixels, one byte per pixel holding a C64 colour
   index", which was asserted rather than derived and which the drawing code does not support.
   `Tests/GameLogicTests/CanvasSpikeTests.cpp` measured the shipped game; §7 below records what
   it found and why the original clause could not work.

   `Canvas` therefore holds four planes, laid out as the original's memory is so that an oracle
   comparison is a byte compare and not a translation:

   | Plane | Size | Original | Holds |
   |---|---|---|---|
   | `m_bitmap` | 0x2000 | `SCBASE` | four two-bit multicolour pixels per byte; `row * 320 + cell * 8 + subRow` |
   | `m_spaceCells` | 0x400 | `SCBASE+0x2000` | space view cell colours: high nibble for `%01`, low for `%10` |
   | `m_textCells` | 0x400 | `SCBASE+0x2400` | the same for the text view; the game switches between the two |
   | `m_colourCells` | 1000 | colour RAM | the colour for `%11`, one nibble per cell |

   plus one background index for `%00`. About 11 KB in total.

   - **Drawing is byte-wise exclusive-or into `m_bitmap`**, at the addresses the original
     computes. `ylookup` and `celllook` are ported as the tables they are.
   - **Cell colour is mutable state, not a palette.** The game writes it (`RED2`, `GREEN2`,
     `YELLOW2`, `BLACK2` for the missile indicators, `MAG2` for the text view) and EORs it
     (`BULBCOL`, to toggle the E.C.M. and station bulbs). It cannot be a constant table.
   - **`Canvas::Resolve()` produces the 320×200 indexed image** ADR-005 §1 uploads: each
     multicolour pixel doubled horizontally, its two bits selecting background, the cell's two
     nibbles, or colour RAM. **ADR-005 is unaffected** — the seam is unchanged and the resolve
     simply lives inside `Canvas`.
   - **The logical coordinate space is unchanged**: the space view is x ∈ [0,255] over
     y ∈ [0,143], centred on `X = 128`, `Y = 72`, and all line and circle arithmetic runs in it
     with the original's algorithms (`LOIN`'s seven variants, `CIRCLE2`'s step table). Two
     x-units share one multicolour pixel; the view sits four character cells in from the left,
     so x 0..255 covers cells 4..35 of 40. Sub-pixel accuracy, anti-aliasing and higher internal
     resolution stay phase-6 items that would fork the drawing code rather than change it.

5. **Erase-by-XOR is available.** `Canvas` provides XOR plotting, and the line heaps are ported,
   because `LL9` and `SUN` use the heap contents to decide what to erase. Whether the executable
   presents every intermediate state or only the end of an iteration is a presentation
   decision (ADR-005); the logic is the same either way.

## §7 — Why one colour index per pixel could not work (measured 2026-09-03)

The original clause was not merely awkward, it was unimplementable, and the reason is worth
keeping because it will come back the first time somebody proposes simplifying `Canvas`.

**The C64 build of `PIXEL` indexes `TWOS2`, not the multicolour-aligned `CTWOS2`.** `TWOS2`'s
two set bits slide along by one bit per x, so at three of the eight x offsets the mask sets the
*low* bit of one multicolour pixel and the *high* bit of the next:

```
x mod 8 = 0  mask %11000000  inside one pixel
x mod 8 = 1  mask %11000000  inside one pixel
x mod 8 = 2  mask %01100000  STRADDLES two pixels
x mod 8 = 3  mask %00110000  inside one pixel
x mod 8 = 4  mask %00011000  STRADDLES two pixels
x mod 8 = 5  mask %00001100  inside one pixel
x mod 8 = 6  mask %00000110  STRADDLES two pixels
x mod 8 = 7  mask %00000011  inside one pixel
```

There is no colour index to store for a straddling write: the operation is an exclusive-or on a
byte whose bits belong to two adjacent pixels. `CPIX2` uses the aligned `CTWOS2` and `PIXEL`
does not, so this is not a lineage artefact that a C64-specific table would fix — both are in
the shipped game.

The consequence is visible rather than theoretical. Two marks that share no bit still combine
*inside* a pixel: plotting at x = 66 (`%01100000`) and then x = 68 (`%00011000`) leaves
`%01111000` in one byte, which is three lit pixels in three different colours —

```
p0 = %01   p1 = %11   p2 = %10   p3 = %00
```

— and `%11` in the middle is a colour neither call asked for. An index-per-pixel canvas cannot
produce it.

Two further measurements the same spike took, both of which the port depends on:

- **Erase-by-redraw is exact.** Two identical `PIXEL` calls leave the bitmap byte-for-byte as it
  was, which is what `LL9` and `SUN` rely on (§5 below, plan §4.6).
- **Lines are byte-granular.** `HLOIN` from x 10 to 30 wrote `%00111111`, `%11111111`,
  `%11111100` — masked ends, whole bytes between. `TWFR`'s odd entries are not pixel-aligned
  either, so a horizontal line's right edge can end in a different colour from its body.

And one thing that looked like a defect and is not: the bitmap's left margin is four cells
(`ylookup` adds `0x20`) while `celllook` starts three cells into screen RAM. `CHPR` writes the
glyph at cell `4 + XC`, then advances the cursor, then writes the colour at
`celllook[YC] + XC` — which is now `3 + (XC + 1)`. Both land on cell `4 + XC`. Measured at three
columns rather than derived, because deriving it by hand got it wrong first time.

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
