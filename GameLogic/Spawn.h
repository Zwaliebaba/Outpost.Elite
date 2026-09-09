#pragma once

#include "Commander.h"
#include "LineHeap.h"
#include "PlanetDraw.h"
#include "Rng.h"
#include "ShipSlot.h"

#include <array>
#include <cstdint>

namespace Elite
{

  struct Universe; // Universe.h -- forward, so this header stays under it
  struct Ports;    // Ports.h, likewise

  /*
   * Taking a ship out of the bubble, and putting the system's own two in (slice 3c).
   *
   * `NWSHP` in slice 3a only had to move a pointer to make room for a new ship's line heap. Taking
   * one out is the harder half, and the ledger's note that `KILLSHP` "releases heap space" was
   * wrong about what it does: it RELOCATES. Every ship above the dead one moves down a slot, and
   * every one of their line heaps moves down by the dead ship's size, so the region stays packed
   * with no free list and no fragmentation.
   *
   * Three things have to be renumbered as the slots shift, and the routine does all three: the
   * type counts, the junk count, and any missile locked on a slot above the dead one.
   */

  /// What `KILLSHP` and `SOLAR` reach outside this slice. Everything left in it is dashboard
  /// state that slice 3d-b and 3d-c own; the scanner was here too until 3d-a built it (§6.59).
  // `SpawnEffects` was `AbortMissile`, `ShowMessage`, `ToggleStationIndicator` and
  // `ResetMissileIndicators` -- four routines `GameLogic` did not contain when `KILLSHP` and
  // `SOLAR` were ported and contains now. M3-b-1 calls them, and `MISSILE_GREEN` (now `MISSILE_READY`) moved to
  // `Dashboard.h` beside the routine that takes it.

  /*
   * Take the ship in slot X out of the bubble.
   *
   * The slot list, the ship blocks and the line heap all shuffle down together, and `INF` walks up
   * the list as they do so that each ship is written into the slot below the one it came from.
   * `P(1 0)` starts at the TOP of the dead ship's heap block and comes down by each surviving
   * ship's own size, so it always points at where the next one's heap should go.
   *
   * The space station is the exception and it does not shuffle anything: `KS4` clears the bubble
   * back to just a sun.
   */
  void KillShip(Universe& _universe, Ports& _ports, std::uint8_t _slot) noexcept;

  /*
   * Put the system's planet or sun into the bubble.
   *
   * ONE BIT OF THE TECH LEVEL is the whole of how Elite chooses a planet's look: bit 1 of the tech
   * level becomes bit 1 of the type, so type 128 gets meridians and type 130 a crater (§6.53's
   * other half). The 127s in `INWK+29` and `INWK+30` are the maximum roll and pitch counters,
   * which is what makes a planet rotate.
   */
  [[nodiscard]] NewShip AddPlanetOrSun(Universe& _universe, Ports& _ports) noexcept;

  /// The Dodo station's ship type, which is the last blueprint this build carries.
  /// Measured rather than counted: entry 33 of the pointer table is 60973, and `SHIP_DODO` is at
  /// 60973 in the assembled image.
  /// `ShipType::Dodo` is the enumerator; this note stays for the measurement.

  /// A system this advanced has a Dodo, not a Coriolis.
  inline constexpr std::uint8_t STATION_DODO_TECH_LEVEL = 10;

  /*
   * Put the space station into the bubble, and NwS1 with it.
   *
   * IT TAKES THE SUN'S PLACE AND THE SUN'S MEMORY, which is two separate instructions doing one
   * thing. Storing a zero in `FRIN+1` empties slot 1 -- the sun's -- without going anywhere near
   * `KILLSHP`; and the station's line-heap pointer is set to `LSO`, which is the sun's 200-byte
   * heap. `NWSHP` then skips its own allocation for a station, so the pointer survives. That is
   * why you never see a station and a sun at once, and why part 14 calls `WPLS` to erase the sun
   * immediately before calling this.
   *
   * AND IT SELF-MODIFIES THE BLUEPRINT TABLE. `spasto` holds the Coriolis's address, saved by
   * `BEGIN` before anything could change it; this writes that back into the table's station entry
   * and then overwrites it with the Dodo's for a tech level of ten or more. `Bubble` carries the
   * entry; see its declaration for why one field rather than a copy of the table.
   *
   * `NwS1` is six instructions called three times with X at 10, 12 and 14, and each call flips bit
   * 7 and steps X by two -- so what it negates is the three HIGH bytes of the nose vector, turning
   * the station round to face the player it has just let go.
   */
  [[nodiscard]] NewShip AddStation(Universe& _universe, Ports& _ports) noexcept;

  /*
   * Build the system: a sun, a planet, and however many Trumbles have bred.
   *
   * The Trumble arithmetic is the first thing it does and it is a population model in nine
   * instructions: a random number under sixteen is added to the count, forced to at least four,
   * and then shifted up -- so a pair breeds into a swarm over a few jumps, and the `BPL` that
   * guards the high byte is what stops it overflowing into something else.
   *
   * `FIST` is HALVED here, which is Elite's legal-status decay: half your bounty is forgiven at
   * every jump.
   */
  /*
   * And it does not return where it looks as though it does. `SOLAR` ends by creating the planet
   * with no `RTS`, so it falls into `NWSTARS`, which falls into `nWq`, which falls into `WPSHPS`,
   * which falls into `FLFLLS`. **Five routines, five rows in the ledger, one fall-through** --
   * arriving in a new system fills the stardust, clears the ships and resets both line heaps as
   * part of the same call (§6.58).
   */
  void BuildSystem(Universe& _universe, Ports& _ports, bool _carryIn) noexcept;

  /*
   * A ship block for the death sequence's debris, and it ends in a SECOND `DORND`.
   *
   * `ZINF` clears the block, one random byte gives the x and y SIGNS and the `INWK+32` AI byte,
   * and 25 goes into all three high bytes so the wreckage starts at a fixed distance in a random
   * direction. Then it falls into `DORND` again -- so what comes back is a fresh random pair, and
   * `DEATH` uses it for the pitch, the roll and the type.
   *
   * A COMPARE FEEDING A ROTATE is the trick worth naming: the compare puts "was the byte at least
   * 245" in the carry and the rotate shifts that into bit 0, so one byte in eleven gets its AI
   * flag set -- and the two top bits are then forced on, making the rest of it hostile and slow.
   *
   * THE FIRST `DORND` ROTATES THE CALLER'S CARRY IN. `ZINF` touches no flag, so `_carryIn` is
   * whatever `Ze` was called with -- in `DEATH`, what the previous piece's last `DORND` left in
   * its addition. The second does not: what `Ze` falls into is `DORND2`, a `CLC` in front of
   * `DORND`, so the bit 7 the rotate shifted out never reaches it. The first version of this
   * routine passed a clear carry to both and matched the shipped game for four pieces of wreckage
   * before the fifth landed one random step off (§6.117).
   */
  /// The high byte the debris starts at in all three axes, so it appears at one distance in
  /// a random direction rather than at a random distance.
  inline constexpr std::uint8_t DEBRIS_DISTANCE = 25;

  /// The compare whose CARRY becomes bit 0 of the AI byte, so roughly one wreck in eleven
  /// gets its flag set.
  inline constexpr std::uint8_t DEBRIS_AI_THRESHOLD = 245;

  /// The orientation `fq1` gives every piece: nose along z, side along x.
  inline constexpr std::uint8_t DEBRIS_ORIENTATION = 0x60;

  [[nodiscard]] RngResult SeedDebris(Ship& _work, Rng& _rng, bool _carryIn) noexcept;

  /*
   * Point a ship along the z axis, give it the player's speed, and create it.
   *
   * `INWK+14 = &60` is the nose vector's z, `INWK+22 = &60 OR 128` the side vector's x with its
   * sign set, and `INWK+27` is `DELTA` ROTATED left, so the caller's carry lands in bit 0 and the
   * speed is twice `DELTA` or one more than that. Nothing between the entry and the rotate touches
   * the flag, so `_carryIn` is whatever the caller branched on: in `DEATH` it is the bit that
   * chose a plate over a canister, so the plates fly one unit faster (§6.117). The
   * upstream comment says "double DELTA speed (i.e. 6)", which is neither the rotate nor the 12
   * that `DELTA` actually holds by then. The type is whatever the caller left in X.
   */
  [[nodiscard]] NewShip AddDebris(Bubble& _bubble, Ship& _work, ShipType _shipType, std::uint8_t _speed, bool _carryIn,
                                  const Blueprint*& _blueprint) noexcept;

  // ---- slice 4a-b: putting a ship into the bubble from inside the bubble ------------------------

  /// Twenty-eight units to the right and HALF that ahead -- the second is the first shifted
  /// down rather than a constant of its own -- which is where a fired missile appears.
  inline constexpr std::uint8_t SPAWN_AHEAD_X = 28;
  inline constexpr std::uint8_t SPAWN_AHEAD_Z = 14;

  /// The AI byte `SESCP` and `SFRMIS` hand `SFS1`: hostile, aggression 15, and bit 0 clear
  /// so it has no target yet.
  inline constexpr std::uint8_t SPAWN_CHILD_AI = 0xFE;

  /// The speed a station's child leaves at, which is why a Viper launched from a Coriolis is
  /// already moving when you see it.
  inline constexpr std::uint8_t STATION_CHILD_SPEED = 32;

  /*
   * Put a ship 28 to the right and 14 ahead of us, pointing away.
   *
   * `ZINF` then four stores then a FALL INTO `fq1`, which is `AddDebris` above: the same three
   * bytes of orientation and the same rotate on the speed. So the carry that rotate brings in has
   * a second source, and it is as unobvious as `DEATH`'s: doubling `MSTG` to build the AI byte
   * leaves the carry holding BIT 7 OF THE MISSILE TARGET, and neither the OR nor the store that
   * follow touch it (§6.121). `MSTG` is 255 when nothing is locked on, so an unlocked missile
   * launches one unit faster than a locked one.
   *
   * The answer is `NWSHP`'s carry: clear means the bubble was full, and `FRMIS` shows "MISSILE
   * JAMMED" rather than spending the missile.
   */
  [[nodiscard]] NewShip SpawnShipAhead(Bubble& _bubble, Ship& _work, ShipType _shipType, std::uint8_t _speed,
                                       std::uint8_t _missileTarget, const Blueprint*& _blueprint) noexcept;

  /*
   * Move a ship along one axis by twice A, sign and all.
   *
   * Five instructions, and the first four are `TAS7`'s opening exactly: the double pushes the sign
   * into the carry and a zero rotated right catches it. Then a jump to `MVT1` rather than `TAS7`'s
   * own arithmetic, so this one adds to a SHIP COORDINATE where that one adds to `K3`.
   */
  void MoveShipAlongAxis(Ship& _work, std::uint8_t _amount, std::uint8_t _axis) noexcept;

  /*
   * Spawn a child from the ship in slot `_parent`: wreckage, a Viper out of a
   * station, an escape pod, a missile fired at us.
   *
   * IT SWAPS `INWK` OUT AND BACK. The new ship is built in the caller's own workspace, so the
   * routine copies `INWK` into `XX3`, loads the PARENT's block over it, edits that, calls `NWSHP`
   * and copies `XX3` back -- and `XX0` and `INF` are pushed and pulled around the same call. That
   * is not tidiness: `SFS1`'s callers are `TACTICS` and the flight loop, both of which are in the
   * middle of working on `INWK` when they call it.
   *
   * Three edits, and each is a different kind of ship. A STATION's child comes out of the slot
   * rather than the middle: three `SFS2` calls push it along the station's own nose, roof and side
   * vectors, at speed 32. Anything from an alloy plate to a splinter -- the cargo range -- gets a
   * random spin and a random speed of at most 15. Everything else takes the parent's position
   * unchanged, and every one of them gets `_aiFlag` in `INWK+32` and bit 0 of `INWK+29` cleared.
   *
   * `SESCP` is this routine entered two bytes early with the escape pod's type already in X, and
   * `SFRMIS` enters two bytes before `SFS1`, at the load of that AI byte -- so all three share one
   * body and differ only in what they arrive holding.
   */
  [[nodiscard]] NewShip SpawnChildShip(Bubble& _bubble, Ship& _work, Rng& _rng, std::uint8_t _parent,
                                       ShipType _parentType, std::uint8_t _aiFlag, ShipType _shipType,
                                       const Blueprint*& _blueprint) noexcept;

  /// `SFS1` with the escape pod's type and the standard AI byte already loaded.
  [[nodiscard]] NewShip SpawnEscapePod(Bubble& _bubble, Ship& _work, Rng& _rng, std::uint8_t _parent,
                                       ShipType _parentType, const Blueprint*& _blueprint) noexcept;

} // namespace Elite
