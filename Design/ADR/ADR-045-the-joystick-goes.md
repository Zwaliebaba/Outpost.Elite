# ADR-045 — The joystick goes: the keyboard and the mouse fly the game

**Status:** accepted 2026-10-14, with the change that implements it, step 8 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D81. It comes before steps 6 and 7, which it makes smaller. **Amended 2026-10-14:** at the owner's request, `amstradPresent` goes too (item 6).

## Context

The 1987 game reads three input devices: the keyboard, the mouse and a joystick. The joystick is the IBM game port's, timed by counting the polls of its one-shots, or the Amstrad's, whose stick sends scan codes 77h to 7Ch through the keyboard. The port is modelled on an instruction clock that counts the 8088's cycles over the instructions native code stands in for, which nothing else reads. The owner ruled that the joystick goes (D81), which also retires step 8's XInput.

## Decision

1. **Steering and fire read the keyboard or the mouse.** `ReadSteering` and `ReadFireButton` lose their joystick branches; `ReadJoystickSteering`, `ReadJoystickAxes`, the Amstrad stick's ramps and the axis scaling go.
2. **The device goes with what timed it.** `GamePort`, the platform's game port, `Hardware`'s port calls and the instruction clock (`InstructionCycles`, `CountInstructionCycles` and the cycles the interrupts and the BIOS's handlers added to it) go. Paced time is untouched: it never read that clock.
3. **The disc menu offers the keyboard and the mouse.** J, its joystick choice and calibration, and their texts go; J is a key the menu ignores, as it ignores every other, and the menu's line reads `D...Delete Commander` alone. `inputDevice` keeps 1987's numbers, 0 the keyboard and 2 the mouse.
4. **The state loses the joystick's ten bytes**: the Amstrad stick's four ramps and the calibration, the stick's type and the Amstrad's detection flag. The state is 14,410 bytes written by its members. `amstradPresent` stays: it is the Amstrad machine's (D12), not its stick's, and the missile launch past the drawing buffer reads it (`BytesAfterDrawBuffer`). The Amstrad keys' scan codes leave `Keys`.
5. **The stick's tests go**: `GamePortTests`, and the twins `TwinStickNative`, `TwinStickFlight` and `TwinAmstradFlight`, with their 39 answers, pictures and outcomes. `TwinDiscMenu` keeps its keys, J's among them, which it now plays as ignored keys.

6. **`amstradPresent` goes**, with the ROM test that set it (`Hardware::AmstradRom`). The game runs as on an IBM PC (D12), where Start cleared it, and nothing but the missile launch past the drawing buffer read it after Start, which now reads the 0 Start left (`AMSTRAD_PRESENT`). The state is 14,409 bytes, and its start hash `6bfcf1f3…eaad`.

## What moved, and how it was measured

The state was dumped by member at every digest point of the corpus and of every twin before the change (26 replays, 134 points; 36 twins, 356 points), and again after it. Compared by path:

| What | Result |
|---|---|
| Members gone | the eight paths of the ten bytes above, and nothing else |
| Members that moved | one: `TwinDiscMenu`'s answer 14, `saveLoad.inputDevice` 0 where it was 1, since the Amstrad's stick can no longer be chosen; the next key, K, made it 0 before too, and every later point is as it was |
| Points | every point of the 26 replays and the 33 twins kept, identical in every other member; the 39 of the three twins retired |
| Pictures | 28 of the corpus and 47 of the twins, each a screen showing the disc menu, whose line lost `J...Joystick`, or J's error, which no longer shows; no outcome moved |
| Files | the disc menu's text-page screenshot, `ELITE10.LO` of `TwinScreenshots` |

Every digest moved, as the state is ten bytes shorter, and the start state's hash with it (`6386d30d…f8ed`, `StateDigestTests`). They were re-recorded from the run after the change once the comparison above held; the pictures and outcomes were then recorded by `OUTPOST_ELITE_RECORD_GUARD`, and a second recording reproduced every file byte for byte. The suite then passes in check mode under g++: 178 tests.

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

For item 6, the re-base was run against the dump of the tree with the joystick gone, with every gate on: every member at every point of the 26 replays and the 33 twins as it was, and every picture and outcome as recorded. Only the digests moved.

## What this forecloses

- **A joystick or a gamepad**, unless the owner rules one back in, natively and as new code.
- **The instruction clock.** Nothing in the platform counts the original's cycles.
