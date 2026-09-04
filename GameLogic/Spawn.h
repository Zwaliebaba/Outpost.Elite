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

/// What `KILLSHP` and `SOLAR` reach outside this slice. It carries `BubbleEffects` because
/// `SOLAR` falls all the way through into `WPSHPS`, which needs the scanner.
class SpawnEffects : public BubbleEffects
{
public:
  ~SpawnEffects() override = default;

  /// 6502: ABORT -- unlock the player's missile and put its indicator back to green. The missile
  /// display is the dashboard's, which is slice 3d.
  virtual void AbortMissile(std::uint8_t _colour) = 0;

  /// 6502: GREEN2 -- the palette byte `KILLSHP` hands `ABORT`.
  static constexpr std::uint8_t MISSILE_GREEN = 0x57;

  /// 6502: MESS -- put a message on screen. Slice 3d.
  virtual void ShowMessage(std::uint8_t _token) = 0;

  /// 6502: SPBLB -- the space station indicator on the dashboard. Slice 3d.
  virtual void ToggleStationIndicator() = 0;

  /// 6502: msblob -- the missile indicators. Slice 3d, and `SOS1` reaches it.
  virtual void ResetMissileIndicators() = 0;
};

/*
 * 6502: KILLSHP -- take the ship in slot X out of the bubble.
 *
 * The slot list, the ship blocks and the line heap all shuffle down together, and `INF` walks up
 * the list as they do so that each ship is written into the slot below the one it came from.
 * `P(1 0)` starts at the TOP of the dead ship's heap block and comes down by each surviving
 * ship's own size, so it always points at where the next one's heap should go.
 *
 * The space station is the exception and it does not shuffle anything: `KS4` clears the bubble
 * back to just a sun.
 */
void KillShip(Bubble& _bubble, LineHeap& _heap, PlanetSunState& _state, ShipBlock& _work,
              CommanderBlock& _commander, SpawnEffects& _effects, std::uint8_t _slot) noexcept;

/*
 * 6502: SOS1 -- put the system's planet or sun into the bubble.
 *
 * `LDA tek / AND #%00000010 / ORA #%10000000` is the whole of how Elite chooses a planet's look:
 * bit 1 of the tech level becomes bit 1 of the type, so type 128 gets meridians and type 130 a
 * crater (§6.53's other half). The 127s in `INWK+29` and `INWK+30` are the maximum roll and
 * pitch counters, which is what makes a planet rotate.
 */
[[nodiscard]] NewShip AddPlanetOrSun(Bubble& _bubble, ShipBlock& _work, SpawnEffects& _effects,
                                     std::uint8_t _techLevel) noexcept;

/*
 * 6502: SOLAR -- build the system: a sun, a planet, and however many Trumbles have bred.
 *
 * The Trumble arithmetic is the first thing it does and it is a population model in nine
 * instructions: a random number under sixteen is added to the count, forced to at least four,
 * and then shifted up -- so a pair breeds into a swarm over a few jumps, and the `BPL` that
 * guards the high byte is what stops it overflowing into something else.
 *
 * `LSR FIST` is Elite's legal-status decay: half your bounty is forgiven at every jump.
 */
/*
 * And it does not return where it looks as though it does. `SOLAR` ends `LDA #129 / JSR NWSHP`
 * with no `RTS`, so it falls into `NWSTARS`, which falls into `nWq`, which falls into `WPSHPS`,
 * which falls into `FLFLLS`. **Five routines, five rows in the ledger, one fall-through** --
 * arriving in a new system fills the stardust, clears the ships and resets both line heaps as
 * part of the same call (§6.58).
 */
void BuildSystem(Canvas& _canvas, DrawWorkspace& _draw, Stardust& _dust, PlanetSunState& _state,
                 Bubble& _bubble, ShipBlock& _work, CommanderBlock& _commander, Rng& _rng,
                 SpawnEffects& _effects, std::uint8_t _techLevel,
                 const std::array<std::uint8_t, 6>& _seeds, std::uint8_t _viewType,
                 bool _carryIn) noexcept;

} // namespace Elite
