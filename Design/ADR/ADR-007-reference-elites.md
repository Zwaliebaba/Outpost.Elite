# ADR-007 — The reference becomes ELITES.EXE

**Status:** accepted 2026-10-09, from the owner's ruling D15 in [Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner), with the change that implements it. It replaces ADR-001's reference file (item 2) and the file offset in its item 3. ADR-001 forecloses "any reference other than this file" except through a new ADR, and this is that ADR.

## Context

ADR-001 made `ELITEL.EXE` the reference because it was the only copy the owner had. A second copy, `ELITES.EXE`, has since arrived. It is the build the owner wants ported: it draws the ships as filled faces where `ELITEL.EXE` draws only their edges.

Both were measured on 2026-10-09: with `python Tools/ScanReference.py` (Capstone 5.0.7), with the address translation described in item 3, and by running both on the PC host (ADR-006) and in DOSBox-X.

**The two are one release, built twice.** Both carry the version string `Release:1? 1-10-87`, the same MZ checksum field (0x1399), the same entry point and stack offset, and seven relocations at the same kind of sites. They use the same video modes (CGA 4, 1 and 2, through the BIOS) and the same I/O ports, so nothing in the PC host changes. Both are cracked identically: the scan finds the same four NOPs at the same two sites, 0x0054 and 0x0537–0x0538, after the same forced store at 0x0532.

| | `ELITEL.EXE` (wireframe) | `ELITES.EXE` (solid) |
|---|---|---|
| Size | 97,200 bytes | 98,144 bytes |
| SHA-256 | `440b06de…af95e55e` | `18b5076a54733dea2d45b1e3b280fd1377067b6cb1b27166d3873aa7e7744363` |
| Code segment | 36,704 bytes | 36,672 bytes |
| Data segment | 60,432 bytes, at image paragraph 08F6 | 61,408 bytes, at image paragraph 08F4 |
| Scan reaches | 12,554 instructions, 32,404 bytes | 12,556 instructions, 32,409 bytes |
| D5 flag | DS:25E4, file offset 0xB584 | DS:25E4, file offset 0xB564 |
| First `GetKey` | CS:7636 | CS:7616 |

**What differs.**

- **Code.** It is byte-identical up to CS:3A3F. The renderer is the one real change:
  - ELITEL's edge-drawing loops are replaced by a face loop.
  - The face loop walks a list of entries. An edge entry it draws with the existing `DrawClippedLine` (CS:1603). A face entry it fills, triangle by triangle, in a colour from a new 32-byte table at DS:74B9.
  - It fills by calling CS:1BFB, inside the self-modifying triangle filler at CS:1B7A. ELITEL carries that filler too, but nothing calls it there (Reference-Map.md had it as dead).
  - Two smaller changes go with it. The far station is drawn as a filled disc rather than a circle's outline. The delay after vertical retrace, before the space view is copied out, is 2,000 iterations where ELITEL's was 700.
  - After the renderer, the code shifts by small amounts, by -20h at the end.
- **Data.** 96.6% of it is identical in order. The ship blueprints grow by their face lists. The near clipping distance (`nearClipZ`) is 100 where ELITEL's is 10. Everything after the blueprints moves by up to 3D0h; the planet-description phrases, for example, are the same 195 strings at new addresses.

## Decision

1. **The reference is `ELITES.EXE`, byte for byte, with its patches** and the hash above. ADR-001's items 1 and 3 to 6 stand as written, with item 3's byte at file offset 0xB564.

2. **`ELITEL.EXE` leaves the tree.** It stays in the history, and nothing builds, tests or measures against it. There is one reference, as the owner ruled.

3. **Phase 1's map is translated, not redone.** Code addresses are translated by aligning the two instruction streams with their addresses masked, and data addresses by aligning the two data segments. The rule for each row:
   - A symbol in an aligned run moves by its run's delta.
   - A routine outside one is found by its first ten instructions' shape.
   - A pointer table is found where every one of its pointers translates.

   Hex numbers in the notes are translated by explicit rules that tell an address from a value: a number continuing a `CS:`/`DS:` list takes that segment, and named sets cover values and unprefixed data. The results, measured:
   - Of the symbol table's 1,101 rows, 1,097 translated and 713 moved. The 46 placeholder names that embed an address were renamed.
   - Four rows could not translate by alignment: the two edge-drawing routines the face loop replaced, and two text tables whose pointers moved with what they point at. The tables were placed from the code that loads them (DS:9763 and DS:976F). The two routines have no counterpart in `ELITES.EXE`; the solid renderer that replaced them is mapped in its own right (Reference-Map.md).
   - A later check found what these rules missed, and the same maps corrected it: 94 placeholder names in the notes, such as `Routine7636` for `GetKey`, that held an `ELITEL.EXE` address but named no row; three segment constants (08F6h, 13CFh, 138Fh); and the figures in the plan's §1.

4. **The measured figures are re-measured on `ELITES.EXE`** (ADR-003 and ADR-006), and the pinned boot is re-pinned:
   - The boot reaches its first `GetKey` in 39,255 instructions and 761,619 cycles. Builds by g++ and clang++ write byte-identical traces.
   - The boot trace still agrees with DOSBox-X's in every register.

## What this forecloses

- `ELITEL.EXE` as the reference, a second reference beside this one, and a wireframe mode in the port, unless a new ADR brings one back.
- Comparing anything against `ELITEL.EXE`'s figures; they are history.
