#pragma once

#include "Ports.h"
#include "Universe.h"

#include <cstdint>

#include "Arith.h"
#include "Dashboard.h"
#include "Controls.h"
#include "Lasers.h"
#include "Rng.h"
#include "LineHeap.h"
#include "Scanner.h"
#include "Spawn.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ViewChange.h"
#include "ShipSlot.h"

namespace Elite
{

  /*
   * What the flight loop calls but does not need (slice 3d-d-i).
   *
   * Four routines the loop uses to answer "how far away is that, roughly" without dividing -- two
   * that OR sign bytes together to find the largest, one that sums three squares, and one that
   * doubles a coordinate and adds another to it -- and the damping the loop applies to the
   * controls before it reads them. None of them needs the loop, which is why they come first
   * (§6.69).
   */

  /*
   * 6502: MAS1 -- K(3 2 1) = INWK(Y) doubled, plus INWK(X), written back over INWK(X).
   *
   * The doubling is a sixteen-bit `ASL`/`ROL` with the carry caught in a third byte by
   * `LDA #0 / ROR A`, which turns the overflow into a SIGN rather than losing it -- so what
   * `MVT3` then adds to is a signed 24-bit value built out of a 16-bit one.
   *
   * `MVT3` ends `STA K+3` on every one of its three paths, so the `STA INWK+2,X` that follows it
   * stores `K+3` and the port needs nothing returned from the call. Returns the high byte with the
   * sign cleared, which is what the caller compares against a distance.
   */
  [[nodiscard]] std::uint8_t DoubleAndAddCoordinate(Ship& _work, std::uint8_t _from, std::uint8_t _to) noexcept;

  /*
   * 6502: MAS2, and `m` above it -- OR the three sign bytes of a ship block together and drop the
   * sign, which is "the largest of the three distances, to within a factor of two".
   *
   * TWO ENTRY POINTS. `m` is `LDA #0` and then falls in, so it starts from nothing; `MAS2` ORs into
   * whatever the caller left in A. The third such routine this slice has met, after `DILX`'s four
   * and `CLYNS`'s two (§6.63, §6.67), and the only one where both are used deliberately.
   */
  [[nodiscard]] std::uint8_t LargestAxisFrom(const Bubble& _bubble, std::uint8_t _slot, std::uint8_t _a) noexcept;

  /// 6502: `m` -- `MAS2` entered with A cleared, which is the ordinary way in.
  [[nodiscard]] inline std::uint8_t LargestAxis(const Bubble& _bubble, std::uint8_t _slot) noexcept
  {
    return LargestAxisFrom(_bubble, _slot, 0);
  }

  /*
   * 6502: MAS3 -- A = x^2 + y^2 + z^2 of a ship block's HIGH bytes, saturating at 255.
   *
   * Two `ADC R`s with no `CLC`, both reading the carry `SQUA2` exits with -- which is never set, so
   * the additions are the plain ones they look like. That is measured over all 512 inputs rather
   * than assumed, and it is why `MAS3` needed no change when the flag was modelled (§6.70).
   */
  [[nodiscard]] std::uint8_t SumOfSquares(const Bubble& _bubble, std::uint8_t _slot) noexcept;

  /// 6502: MAS4 -- the same OR as `MAS2` but over `INWK`'s high bytes rather than a slot's sign
  /// bytes, and without the mask. Four instructions, and it is here because the loop calls it.
  [[nodiscard]] std::uint8_t LargestShipAxis(const Ship& _work, std::uint8_t _a) noexcept;

  /*
   * 6502: cntr -- creep a centre-based control reading one step towards 128.
   *
   * The value runs 1 to 255 with 128 as centred, so damping is "add one below the middle, subtract
   * one above it". Flight loop part 2 is its only caller and it calls it THREE times: twice on
   * `JSTX`, so the roll creeps back by two per pass, and once on `JSTY`.
   *
   * `_dampingDisabled` is `DAMP`, which is a configuration byte the "CAPS LOCK" option toggles
   * between 0 and &FF, and it reads backwards on purpose: NON-ZERO means the damping is switched
   * off. `_dockingComputer` is `auto`, and it wins -- the autopilot always gets damping, whatever
   * the player set.
   *
   * ITS LAST TWO INSTRUCTIONS CANNOT RUN. `.REDU DEX / BEQ BUMP` is reached only when `BUMP`'s
   * `INX` wraps to zero, which needs X = 255 on entry to `BUMP`; but `BUMP` is entered only with
   * X < 128 (from `BPL`) or with X = 128 (undoing the `DEX`), so the wrap never happens. The port
   * leaves them out and the sweep proves it rather than assuming it: a trap on `REDU` records no
   * hits across all 2,304 inputs (§6.71).
   */
  [[nodiscard]] std::uint8_t DampTowardsCentre(std::uint8_t _value, std::uint8_t _dockingComputer, std::uint8_t _dampingDisabled) noexcept;

  /*
   * 6502: DENGY -- take one unit off the energy banks, and say whether that emptied them.
   *
   * `DEC ENERGY / PHP / BNE P%+5 / INC ENERGY / PLP / RTS`. The `PHP` is the point: the flag the
   * caller sees is the one the DECREMENT set, not the one the `INC` that undoes it would have. So
   * the banks never reach zero through this -- one is the floor -- and the caller still learns that
   * they tried to.
   *
   * `ENERGY` at zero decrements to 255, which is not guarded and does not need to be: nothing calls
   * this with the banks already empty.
   */
  [[nodiscard]] bool DrainEnergy(FlightStatus& _status) noexcept;

  /*
   * 6502: SHD, which FALLS INTO DENGY -- bump a shield by one, and PAY FOR IT.
   *
   * `INX / BEQ SHD-2`, and `SHD-2` is the `DEX / RTS` two bytes above the label. So a shield already
   * at 255 is put back and the routine returns; anything less is incremented AND THEN RUNS ON INTO
   * `DENGY`, which takes a unit off the energy banks.
   *
   * A port that read this as a saturating increment -- which is exactly what the four instructions
   * look like -- would recharge the shields for free (§6.83). Flight loop part 13 calls it twice
   * per eighth frame, so the difference is the whole economy of running with the shields down.
   */
  [[nodiscard]] std::uint8_t RechargeShield(FlightStatus& _status, std::uint8_t _shield) noexcept;

  /*
   * 6502: FAROF2, with `FAROF` one instruction above it -- is every one of a ship's high bytes
   * below `_limit`?
   *
   * Three compares and two branches, and the carry it returns is the answer: SET when the ship is
   * inside the box, clear when any axis is outside it. `FAROF` is `LDA #224` and then this, which
   * is the distance at which the flight loop stops caring about a ship at all.
   */
  [[nodiscard]] bool WithinRange(const Ship& _work, std::uint8_t _limit) noexcept;

  /// 6502: FAROF -- `WithinRange` at the limit the loop uses, which is 224.
  [[nodiscard]] inline bool WithinLoopRange(const Ship& _work) noexcept
  {
    return WithinRange(_work, 224u);
  }

  /*
   * 6502: HITCH -- have we hit this ship?
   *
   * FIVE WAYS TO SAY NO BEFORE IT MEASURES ANYTHING. A non-zero `INWK+8` (the ship is not in front
   * of us), a negative type (the planet or the sun), an exploding ship, or a large x or y offset all
   * take the same `BNE HI1` out with the carry as `CLC` left it. Only then does it square x and y,
   * add them, and compare the sum against the blueprint's own target area at `(XX0),0` and
   * `(XX0),1`.
   *
   * The overflow path is not the same as the near misses. `BCS TN10` reaches a `CLC / RTS` of its
   * own, which says "no" for a sum too big to compare rather than for a ship too far to the side --
   * the same answer by a different route, and the port keeps them apart because the original does.
   */
  [[nodiscard]] bool IsHit(const Ship& _work, const Blueprint& _blueprint, ShipType _type) noexcept;

  /*
   * `SpawnChildEffects` WAS HERE AND IS NOT ANY MORE (M4-a-1).
   *
   * It was one method -- `JSR SFS1` with A = the AI flag and X = the type -- and `SFS1` has been
   * `Elite::SpawnChildShip` in `Spawn.cpp` since slice 4a-b, so by §6.73's rule it should have gone
   * with the other seven in M3-b-1. It could not, and the reason was the SUITE rather than the
   * code: `TheWreckageMatchesSPINAndSPIN2` traps `SFS1` on the oracle to `SEC` and let the port's
   * seam answer `true`, so both sides are told "there was always room". Call the routine for real
   * and the bubble fills at ten slots, the carry flips, and the two are no longer comparing the
   * same thing (Modernize.md M3-b's own note).
   *
   * The answer is this slice's pattern rather than that one's: `SPIN` and `SPIN2` ANSWER what to
   * drop and the caller drops it, so what the suite compares is the ANSWER and no trap has to lie
   * to it. See `Drop` below.
   */

  /*
   * 6502: what `SPIN` and `SPIN2` decide before a single `SFS1` runs -- the typed stage result
   * M4-a is named for.
   *
   * `SPIN2` is a loop around one `JSR SFS1` and `SPIN` is a roll in front of `SPIN2`, so between
   * them they choose exactly three things: a type, a count, and the AI byte the children get. The
   * fourth field is not a decision but a CARRY: both routines have a path that spawns nothing and
   * hands the caller's own flag straight back (`oh` is a bare `RTS`), and `MA47` runs its second
   * `JSR SPIN` on whatever the first left, so the flag has to survive the split (M2-d).
   */
  struct Drop
  {
    ShipType type;         ///< 6502: X on the way into `SFS1`
    std::uint8_t count;    ///< 6502: CNT, and zero means `oh`
    std::uint8_t aiFlag;   ///< 6502: A on the way into `SFS1`, which is `LDA #0` for both callers
    bool carryIfNone;      ///< 6502: the flag `oh`'s `RTS` hands back when the count is zero
  };

  /*
   * WHAT USED TO BE HERE: `PLT`, `OIL`, `AST`, `SPL` and `THG`, added by slice 3d-d-iii-b under a
   * comment saying they were "the ship types parts 5, 8 and 11 name that `ShipSlot.h` does not".
   *
   * They moved to `ShipSlot.h` on 2026-09-05, beside `MSL`, `SST` and the junk range, because
   * slice 4a needs the cargo four from `Spawn.cpp` -- which sits UNDER the flight loop and cannot
   * include it. A type number is a property of the ship table, not of the routine that first
   * happened to want one (§6.121).
   */

  /*
   * 6502: SPIN2 -- spawn `_count` ships of one type, one after another.
   *
   * `.spl BEQ oh` READS A FLAG THE INSTRUCTION ABOVE IT DID NOT SET. `STA CNT` leaves the flags
   * alone, so the `BEQ` at the top of the loop is testing whatever the CALLER left in Z -- which
   * for `SPIN2`'s only caller is the `AND #3` two instructions earlier, and for `SPIN` is its own
   * `AND #15`. Both happen to describe the count, which is what makes the loop look ordinary.
   *
   * The loop's back edge is `BNE spl+2`, which lands one instruction PAST the `BEQ`, so the test
   * runs once on entry and never again: after that it is `DEC CNT / BNE` that decides. A port that
   * kept the test inside the loop would agree with the game on every input and be a different
   * routine.
   */
  /*
   * AND WHAT IS LEFT OF IT AFTER THE SPLIT IS THE COUNT AND NOTHING ELSE, which is the finding
   * M4-a-1 produces rather than a shape it imposes. Every subtlety above is about a LOOP, and the
   * loop is `PerformDrop`'s now; `SPIN2`'s own decision is "this type, this many, AI byte zero",
   * and the `BEQ oh` the caller's `AND` set is `count == 0` seen from the other side.
   */
  [[nodiscard]] Drop PlanItems(ShipType _type, std::uint8_t _count, bool _carryIn) noexcept;

  /*
   * 6502: SPIN -- a destroyed ship drops some of its cargo, or does not.
   *
   * Half the time nothing happens at all: `JSR DORND / BPL oh` throws the whole call away on a
   * clear bit 7. That is the ONLY thing the roll decides.
   *
   * THE COUNT IS NOT RANDOM. `TYA / TAX / LDY #0 / AND (XX0),Y / AND #15` reads as "copy the type
   * into X for `SFS1`", and it does that -- but the copy goes through A, so the `AND` that follows
   * masks the TYPE and not the random byte `DORND` left behind. A ship of a given type against a
   * given blueprint always drops the same amount, on the half of the calls that drop anything.
   * The port had it the obvious way round and the oracle disagreed on the first blueprint whose
   * byte 0 differed from the roll (§6.74).
   */
  /// The roll is HERE and not in `PerformDrop`, because `MA47`'s two `JSR SPIN`s roll, spawn, roll
  /// and spawn in that order -- the second roll runs on the first drop's carry (M2-d), so planning
  /// both up front would run the generator twice before either loop.
  [[nodiscard]] Drop PlanDebris(Rng& _rng, const Blueprint& _blueprint, ShipType _type, bool _carryIn) noexcept;

  /*
   * 6502: `SPIN2`'s loop body -- `LDA #0 / JSR SFS1 / DEC CNT / BNE spl+2`, run over a `Drop`.
   *
   * `.spl BEQ oh` READS A FLAG THE INSTRUCTION ABOVE IT DID NOT SET, and the loop's back edge lands
   * one instruction PAST that `BEQ`, so the test runs once on entry and never again. `Drop::count`
   * carries the answer the flag encoded, and this runs the loop the same number of times: a port
   * that put the test back inside the loop would agree with the game on every input and be a
   * different routine.
   *
   * Returns the exit carry: `carryIfNone` for an empty drop, and otherwise the LAST `SFS1`'s --
   * `NWSHP`'s `NW3` CLC for a full bubble and `NWL3` SEC for a ship that was made. That answer is
   * real now rather than a seam's constant, which is the whole point of the split.
   */
  [[nodiscard]] bool PerformDrop(Universe& _universe, const Drop& _drop) noexcept;

  /*
   * 6502: KY12 to KY20 -- the flight keys the loop reads that `DOKEY` does not.
   *
   * The same key logger, indexed by the same internal key numbers (§6.73). `DOKEY` handles the six
   * that steer; these are the ones that do something.
   */
  inline constexpr std::size_t KEY_ENERGY_BOMB = 3;       ///< 6502: KY12 -- Tab
  inline constexpr std::size_t KEY_ESCAPE_POD = 7;        ///< 6502: KY13 -- Escape
  inline constexpr std::size_t KEY_ARM_MISSILE = 42;      ///< 6502: KY14 -- "T"
  inline constexpr std::size_t KEY_UNARM_MISSILE = 34;    ///< 6502: KY15 -- "U"
  inline constexpr std::size_t KEY_FIRE_MISSILE = 28;     ///< 6502: KY16 -- "M"
  inline constexpr std::size_t KEY_ECM = 50;              ///< 6502: KY17 -- "E"
  inline constexpr std::size_t KEY_WARP = 30;             ///< 6502: KY18 -- "J"
  inline constexpr std::size_t KEY_DOCKING_COMPUTER = 44; ///< 6502: KY19 -- "C"
  inline constexpr std::size_t KEY_CANCEL_DOCKING = 23;   ///< 6502: KY20 -- "P"

  /// 6502: the bitmap mode the energy bomb switches the upper half of the screen to.
  inline constexpr std::uint8_t BOMB_BITMAP_MODE = 0xD0;

  /*
   * What the flight loop reaches that phase 4 owns, plus the sound.
   *
   * It WAS a `DashboardEffects` and is not since M3-b-2a: the E.C.M.'s two calls and the frame's
   * sounds are `SoundEffects.cpp`'s routines over `Universe::sound`. It WAS a `SpawnChildEffects`
   * as well, because part 11's `SPIN` and `SPIN2` dropped wreckage through `SFS1`; M4-a-1 took that
   * away, and the flight loop reaches nothing of phase 4's through a seam any more.
   */
  /*
   * `FlightLoopEffects` WAS HERE AND IS NOT ANY MORE (M3-b-2b).
   *
   * It was `SpawnChildEffects` plus the docking music's pair, and with the pair gone there was
   * nothing of its own left to declare -- so `Ports::loop` became a `SpawnChildEffects&` and the
   * derived class was not needed to hold it. M4-a-1 removed the last of it, and `Ports` is ten
   * members rather than eleven.
   *
   * `StartDockingMusic` was `JSR startbd` and `StopDockingMusic` was `JSR stopbd`, and both are
   * `Music.cpp`'s routines over `Universe::music`, writing the chip through `Ports::sid`. `stopbd`
   * READS A BYTE THE SEAM COULD NOT SEE: `BIT MULIE / BMI itsoff`, the title screen's bracket
   * around its `RESET`, which is `Status::titleReset` and is why the call takes it. The executable
   * was passing it in from the universe it happened to hold; the library passes it from the
   * universe it is given.
   *
   * `FRS1` AND `ANGRY` WERE SEAMS HERE AND ARE NOT ANY MORE (M3-b-1d).
   *
   * `SpawnAhead` was `JSR FRS1` with X = the type -- "put a ship right in front of us" -- and
   * `Anger` was `JSR ANGRY` with the ship's slot and type. Slice 4a-b built both, in `Spawn.cpp`
   * and `Tactics.cpp`, and the flight loop calls them: §6.73's rule again.
   *
   * TWO THINGS THEY TAUGHT SURVIVE THEM. `ANGRY`'s SLOT had to be an argument because the two
   * callers point `INF` at different ships -- part 3 angers the missile's TARGET (`LDX MSTG /
   * JSR GINF`) and part 11 the ship OUR LASER hit (`XSAV`'s) -- and the seam that took only the
   * type guessed `MSTG` and read block 255 the first time a laser landed without a lock (§6.142).
   * And `ANGRY`'s EXIT CARRY is part 11's, because it falls from there into `JSR LL9` and a ship
   * the laser has just killed seeds its explosion cloud on that flag (§6.157). `Elite::Anger`
   * takes the slot and answers the carry; the seam only ever forwarded to it.
   */

  /*
   * `NWSPS` WAS A SEAM HERE AND IS NOT ANY MORE.
   *
   * Slice 3d-d-iii-b left it behind one because the fourteen instructions above its fall into
   * `NWSHP` self-modify `XX21`, and the port's blueprint region is `const` -- so what it needed was
   * a decision about where the mutable entry lives, not a transcription. `Bubble::stationType`
   * is that decision, and it is measured rather than guessed: `NWSPS` is the ONLY writer of the
   * table in the whole build, and it writes one entry. `Spawn.h` has the routine now, and both
   * callers -- part 14 and `TT110` -- run it for real. §6.73 for the fourth time.
   */

  /*
   * 6502: M% and the fifteen parts after it -- how a frame in space ends.
   *
   * Three of its jumps leave and three do not, and telling them apart is the routine's whole shape
   * (§6.82). The three that leave are `JMP DOENTRY`, `JMP DEATH` and `JMP ESCAPE`, none of which
   * returns -- so the port hands back an outcome the way `TT102`'s dispatch hands back a label.
   */
  enum class LoopOutcome : std::uint8_t
  {
    Continued, ///< the frame finished; the loop goes round again
    Docked,    ///< 6502: JMP DOENTRY, from part 9's docking check
    Died,      ///< 6502: JMP DEATH, from part 9 or part 15
    Escaped,   ///< 6502: JMP ESCAPE, from part 3's escape pod
  };

  // `FlightLoop` was the argument list the flight half took: `FlightScreen&` plus the keys, the
  // controls, the line heap, the clipper's flag, the projection, the axes and three seams. Every
  // byte of it is `Universe`'s since M3-a and every seam is `Ports`'.

  /*
   * 6502: M% to `MA3` -- the head of a frame: the seed, the Trumbles, the controls and the keys.
   *
   * IT SEEDS THE RANDOM NUMBER GENERATOR FROM THE PLANET. `LDA K% / STA RAND` puts the planet's own
   * x low byte into the first byte of `RAND` on every single frame, so the sequence is stirred by
   * where the planet is -- which is itself a function of everything the player has done. Elite's
   * randomness is not a generator left running; it is a generator being pushed.
   *
   * AND THE PITCH READS A CARRY THE ROLL LEFT BEHIND. The roll's magnitude ends `CMP #8 / BCS P%+3
   * / LSR A`, and the pitch's begins `EOR #%11111111 / ADC #4` with no `SEC` or `CLC` between them
   * -- and `cntr` touches no flags on any of its three paths. So the four added to the pitch is
   * four or five depending on the low bit of the roll (§6.85).
   */
  /// 6502: the two messages `FRMIS` can end on. 201 is "MISSILE JAMMED".
  inline constexpr std::uint8_t MESSAGE_MISSILE_JAMMED = 201;

  /*
   * 6502: FRMIS -- fire the missile that is locked on.
   *
   * `FRS1` puts one in front of us and hands back a carry; a clear one means the bubble is full,
   * and `FR1` prints "MISSILE JAMMED" and stops. Otherwise the target is told it has been shot at,
   * the lock is dropped, the count goes down and the launch is heard.
   *
   * `LDX MSTG / JSR GINF / LDA FRIN,X / JSR ANGRY` reads the TARGET's type out of the slot the lock
   * names -- so what gets angry is the ship being shot at, not the missile.
   */
  void FireMissile(Universe& _universe, Ports& _ports) noexcept;

  // `LoopSpawnEffects` was the adapter that answered `SpawnEffects` out of a universe and its
  // ports. It went with the seam in M3-b-1: the spawn routines take the two objects it held.

  [[nodiscard]] LoopOutcome BeginFlightFrame(Universe& _universe, Ports& _ports) noexcept;

  /*
   * 6502: MA3 to `JMP MAL1` -- parts 4 to 12, once per occupied slot, and `KS1` under them.
   *
   * The loop is a `JMP MAL1` back edge rather than a counted loop, and `KS1` is inside it: killing
   * a ship shuffles the slots down, so the index is NOT advanced afterwards and the slot that took
   * the dead one's place is processed next. A port that wrote a `for` over the slots would skip
   * every ship behind a kill.
   *
   * What each iteration is: copy the block into `INWK`, look up its blueprint, let the energy bomb
   * kill it, move it, copy it back, test it for a collision, draw it, decide whether our laser hit
   * it, and finally either kill it or write its two changed bytes back.
   */
  [[nodiscard]] LoopOutcome MoveEveryShip(Universe& _universe, Ports& _ports) noexcept;

  /*
   * 6502: MA18 to `JMP STARS` -- parts 13 to 16, once per frame after the ships.
   *
   * Everything here is on a clock: `LDA MCNT / AND #7` recharges the shields and the banks every
   * eighth frame, `AND #31` gives the rest of the part a sixteen-step cycle, and steps 10, 15 and
   * 20 of that cycle are the energy warning, the docking-computer reminder and the cabin
   * temperature. So a frame in the flight loop does one sixteenth of the housekeeping and the
   * player never sees the seam.
   */
  [[nodiscard]] LoopOutcome EndFlightFrame(Universe& _universe, Ports& _ports) noexcept;

  /// 6502: `M%` from end to end -- the opening, every ship, and the tail.
  [[nodiscard]] LoopOutcome MainFlightLoop(Universe& _universe, Ports& _ports) noexcept;

  /*
   * 6502: TT17 -- scan the keyboard for the flight controls, once a frame.
   *
   * THE C64 HAS ITS OWN `TT17` AND IT IS NOT THE COMMON ONE. `library/common/.../tt17.asm` is three
   * instructions -- `LDA JSTX / EOR #&FF / RTS` -- and the master file includes
   * `library/c64/main/subroutine/tt17.asm` instead, which calls `DOKEY` on BOTH of its paths.
   * Reading the common file is how a port ends up believing the frame has no keyboard scan in it,
   * which is what happened here (§6.111).
   *
   * IT IS THE LAST THING `MLOOP` DOES BEFORE `TT102`. Part 5 ends `JSR TT17` and falls into part 6,
   * which dispatches the key that was pressed -- so the game reads the hardware TWICE a frame and
   * for two different questions: this one fills the key LOGGER with what is being held, and
   * `TT102` takes the one key that was pressed. `M%` then reads the logger at the top of the next
   * frame, which is why the controls are scanned at the end of a frame rather than the start.
   *
   * BOTH PATHS ARE HERE. `LDA QQ11 / BNE TT17afterall` chooses between them and they differ in
   * what they hand back rather than in what they do: the space view's returns `thiskey` alone, and
   * a chart's returns `thiskey` with the crosshair steps in X and Y. The port's `TT102` takes its
   * key from the window's queue, so what is left to return is the steps -- zero on both axes off a
   * chart, because the cursor keys are read only when one is showing.
   *
   * The joystick half of `TT17afterall` is not ported: `JSTK` is zero for a keyboard player from
   * the moment the title screen is dismissed with a key, and this build has no joystick.
   */
  [[nodiscard]] CrosshairStep ScanFlightControls(Universe& _universe, Ports& _ports, std::uint8_t _view) noexcept;

} // namespace Elite
