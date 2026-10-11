# ADR-024 — The game's state as members on the 1987 layout, with fields that count their own changes (D33)

**Status:** accepted 2026-10-11, with the change that implements it: `Counted`, the record `SpaceObject`, `GameData` generated from them, and native code converted from `DataOverlay.h`'s `Get`/`Set` to member syntax. It is the first of the natural layout's three changes, piece 3 of D33's route ([Reverse-Engineering-Plan.md §5](../Reverse-Engineering-Plan.md#phase-5--the-native-platform-and-the-natural-layout)). It settles the choice ADR-015 item 5 left open. The owner played main at `7242663`, with it, and reported that it works perfectly (2026-10-11).

## Context

**How native code reached the game's state.** Through `DataOverlay.h`, by address:
- `_state.Get(DS.fuel)` and `_state.Set(DS.fuel, v)`;
- `_state.Byte(DS.t.At(i))`;
- `SlotByte` and `SlotWord` through `ObjectSlot` for the ship slots.

`GameData`, the packed struct at the 1987 layout (ADR-018), held the bytes. Every write went through `GameState::Store`, which counted a byte that changed.

**Why the count has to survive.** Paced time is kept (ADR-023 item 2): `Platform::NoteTurn` calls a turn idle only when its signature, the memory change count and the port-write count all equal the last turn's (ADR-015). A plain member write would bypass the count and move every replay. So the count is not an artefact of the emulated memory. It is part of what makes a waiting loop wait, and it lasts as long as paced time does.

**Only equality matters.** The count is compared between consecutive turns, never read as a number. A write must advance it exactly when it changes a stored value, and leave it alone when it stores the value already there; how far it advances does not matter.

## Decision

**1. `Counted<T>`, a byte or a word of the game's memory** (`GameLogic/Counted.h`), for `std::uint8_t` and `std::uint16_t`.
- **Storage.** It holds its `sizeof(T)` bytes, little-endian as the 8088 keeps them, with alignment 1. It sits anywhere in the packed struct, and a helper can take it by reference at either width.
- **Reading and writing.**
  - It reads as a value through `operator T`.
  - Assignment from a value compares, stores and counts only on a change.
- **What does not compile:**
  - copy assignment (`a = b`): write `a = T{b}`, which counts;
  - compound assignment and increment: write `a = static_cast<T>(a + 1)`.

  So no write path can skip the count.
- **Copies are explicit.**
  - **Why.** A copy is outside its `GameState`, and a write to it would count into whatever its address rounds down to (item 2), silently corrupting memory.
  - **What is explicit.** The copy constructors of `Counted`, `SpaceObject`, `GameData` and every struct the generator nests in it. They stay trivial, so `GameData` stays trivially copyable and the reference's image still loads into it as bytes.
  - **What no longer compiles:** `auto slot = ...` for `auto& slot = ...`, a by-value parameter or capture, and a return by value.
  - **How it is held.** The tree compiled unchanged, so nothing copied one. A planted copy of a record and one of a field each fail under g++ and clang++, and static asserts hold the rule.

**2. A field finds its count from its own address.**
- **The block.** The `GameState` owns a block of the count followed by `GameData`, 0x10008 bytes, allocated at 128 KiB alignment (`::operator new` with `std::align_val_t{0x20000}`).
- **The lookup.** `ChangeCountAt` rounds any member's address down to that alignment, which is the block, where the count is.
- **What this serves.** It works the same for fields of nested structs, arrays, and the 36 slots that share one record type. It needs no offset in each field and no state outside the `GameState`, and a `GameState` on the stack works, because the block is always on the heap.
- **One count.** The raw accessors, the code segment's image and video memory count into the same count. A word written whole now counts once, where `Store` counted each changed byte.

**Rejected, and why:**

| Alternative | Why not |
|---|---|
| `alignas(0x20000)` on the block | MSVC caps the alignment it accepts. Recalled, not tested here |
| The count after `GameData` | Finding it would need `GameData`'s size |
| An offset in each field's type | It cannot serve nested structs and the slots, which share a type at many offsets |
| A pointer in each field | It moves the 1987 layout |
| A count shared by the process | Tests run several games in one process, and a write in one would make another's loop look busy |
| A copy assignment that counts | It would stop `GameData` being trivially copyable |

**3. `SpaceObject`, the ship slot as a record of 64 bytes at its 1987 offsets.**
- **Where it sits.** The generator's `RECORDS` table makes `shipSlots` hold `gap6930` (the sun's slot, which the symbol table leaves unnamed), `planetSlot`, `stationSlot`, `firstShipSlot[17]` and `debrisSlots[16]`.
- **Why a new name.** `ObjectSlot` was taken by value in 123 parameter declarations, and a record keeping that name would have copied silently. `ObjectSlot.h` is deleted.
- **Renamed fields:** `State` is `aiState`, and `Class` is `behaviorClass`.
- **Two names for one byte** are anonymous unions of two `Counted<std::uint8_t>`:
  - `scanned` and `framesAway` at +34;
  - `blipX` and `spinRoll` at +26;
  - `blipY` and `spinPitch` at +27;
  - `cargo` and `flashFrames` at +2C.
- **A byte of a word** keeps the word, and each use reads or writes it through `Low`, `High`, `WithLow` or `WithHigh`, with a comment:
  - the middle bytes of the position, and the low byte of z;
  - the sun's and planet's scale shift and colour, the low and high bytes of `pitch`;
  - the station's collided bit, bit 0 of `yaw`'s low byte, still inside the yaw;
  - `viewZ`'s high byte, and `compassZ`'s, which is the station's depth.

  Keeping the collided bit in the yaw keeps the drawn station turning as it did (the inventory's catch).
- **Word writes as byte writes.** What the original wrote as one word across two fields is now two byte writes: `xHigh` and `yHigh`, the colour word, the spin, the compass dot, the cargo word, and aggression with bounty. The count advances differently; the idle decisions do not (item 6).

**4. What was converted.** Native code made 2,222 `DS.` uses at the start (`45cbc9c`, after the swap). 1,725 of them are now member syntax:
- 1,697 by script:
  - 825 reads, 774 writes and 18 indexed uses, the unions' members included;
  - 80 call sites of the 14 helpers and one lambda that now take a `Counted&`. Eight of those helpers no longer take the `GameState` at all.
- 28 by hand: run-time field selectors, now member pointers, among them `ObjectByte` and `ObjectWord` for the slots.
- The slots: 472 of 502 `Get`/`Set` uses by script, and the rest by hand. 26 `.Offset()` calls became `GameState::OffsetOf`.
- The tests: 204 uses became 132; 83 of those left are the twin rig's helpers, which apply a field's offset in both rigs.

**What remains is change 2's:**

| Class | Uses | What they are |
|---|---|---|
| Scalar | 96 | `Get`/`Set` of the 25 fields that hold addresses, `messagePointer` 16 of them |
| Raw | 324 | offsets handed to routines, held, added to, stored or used as text; and 20 reads of a container whose first byte is an unnamed gap, such as `creditsTenths` and `marketRandomState` |
| Indexed | 34 | entry offsets handed to routines that take an offset, pointer tables, and byte-indexed structs of named bytes (`keyDown`, `cargoHold`) |
| Slot | 43 | slot offsets used to walk the slots and to compare with stored slots |

**5. Where C++ is stricter than the 8088.**
- **Unchecked indexes.** An offset that ran into the next bytes is now an array index, where reading past the end is undefined. All 20 conversions were reviewed, and two rely on the game's data rather than a mask:
  - `colorFillBytes[conditionColor]`, which is 0 to 3;
  - `notePeriods[pitch - 12]`, whose pitch comes from the constant title tune.
- **Slot lookup.** `ObjectAt` throws past DS:FFC0, where the original would wrap at 64 KiB. Nothing the game does reaches it.
- **The sentinels.** The target and owner values 0 and 1 reach `ObjectAt(0)` and `ObjectAt(1)`, records at offsets where no `SpaceObject` lives. They work because every access is a byte copy. Change 2 turns these slot offsets into indices with explicit values for "the player" and "police".

**6. Measured 2026-10-11,** on the change and on main with it.

| Check | Result |
|---|---|
| `GameLogicTests` | 154 of 154 under g++ and clang++: the 150 before and 4 in `CountedTests` |
| Warnings | none, with g++ `-Wshadow` and clang++ `-Wimplicit-int-conversion -Wshadow` on top of `-Wall -Wextra -Wpedantic -Werror -Wconversion` |
| The corpus | all 26 replays meet all 134 state digests under `ReferenceRunner` built with each compiler; 134 pictures and 134 outcome lines checked, 0 differ |
| The twins | all 356 known answers met, each checked exactly once; 356 pictures and 356 outcome lines checked, 0 differ |
| Timing | every turn's signature and the clock hashed on `45cbc9c` and on the change: the 26 replays' hashes are identical over 42,696,850 turns, and the whole suite's over 77,460,360 turns |
| Run time (g++, Release, all 26 replays in sequence, medians of 15 alternating rounds) | 1.630 s before, 1.619 s after, wall time; 1.487 s and 1.493 s user time |
| clang-tidy | clean on the 80 portable translation units, and on every changed one on main |

**7. Not verified here.**
- **MSVC and its Debug build.**
  - A member pointer to an anonymous union's member (`&SpaceObject::cargo`, in `Ships.cpp`) is untested on MSVC.
  - The implicitly deleted assignments could raise C4626 and C5027; both are believed off by default.
  - In Debug, `memcpy` is not inlined, which may slow the corpus there.

  CI builds and runs all three.
- **Playing it.** Nobody has played this build. The owner ruled that piece 3 does not wait for a play-test (D34).

## What this forecloses

- **A plain member of the game's state while paced time holds.** A field that does not count would let a loop that writes go idle, and move the replays. The natural layout (change 3) keeps `Counted`.
- **A copy of a member or a record that can be written.** Copies are explicit, and a write needs the field's own block.
- **`Get`/`Set` through `DataOverlay.h` for a converted field.** What remains of the overlay is change 2's, and change 3 deletes it.
