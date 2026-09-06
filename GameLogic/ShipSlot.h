#pragma once

#include "Ship.h"
#include "ShipBlueprint.h"
#include "ShipFlags.h"
#include "ShipType.h"

#include <array>
#include <cstdint>

namespace Elite
{

  /*
   * The local bubble of ships (slice 3a).
   *
   * 6502: FRIN, K%, UNIV and MANY. Elite does not have a persistent universe; it has a BUBBLE of at most ten
   * ships around the player, created as they come into range and destroyed as they leave. Every
   * routine that moves, draws, shoots at or is shot by a ship works on one slot of this at a time,
   * copied into `INWK` and copied back.
   */

  /*
   * 6502: NOSH -- the most ships the bubble holds at once.
   *
   * Ten, and the assembled layout says so independently of the source: `FRIN` is at 1106 and `MANY`
   * at 1117, eleven bytes apart, which is `NOSH + 1` for the terminator. Worth the cross-check
   * because the raw `original-sources` listings carry both 10 and 20 for this name -- they serve
   * several versions of the game -- so grepping them gives whichever comes first.
   */
  inline constexpr std::uint8_t MAX_SHIPS = 10;

  /*
   * 6502: K% and LS% -- where the blocks and the ship line heap live, and why the port needs the
   * ADDRESSES rather than just the storage.
   *
   * `NWSHP` refuses to create a ship when the line heap it would need runs down into the block it
   * is about to write, and it decides that by comparing two addresses: the new heap bottom against
   * `INF`, the slot's own address. The blocks grow UP from `K%` and the heap grows DOWN from `LS%`,
   * so the check is real and reachable -- a bubble full of complex ships runs out of heap before it
   * runs out of slots.
   *
   * A port that kept only an array and an index would have nothing to compare and would create
   * ships the original refuses. So the addresses are kept as arithmetic on the side, the storage
   * stays an array, and `SlotAddress` is the bridge.
   */
  inline constexpr std::uint16_t SHIP_BLOCK_BASE = 0xF900; ///< 6502: K%
  inline constexpr std::uint16_t SHIP_HEAP_TOP = 0xFFC0;   ///< 6502: LS%, where SLSP starts

  /*
   * 6502: FRIN, K% and MANY together -- everything that is in the bubble right now.
   *
   * `UNIV` has no equivalent and needs none: it is a table of POINTERS to the ten blocks in `K%`,
   * which exists because the 6502 has no way to multiply an index by 37 cheaply. `GINF` reads it to
   * turn a slot number into an address. Here the blocks are an array and the index is the index, so
   * the table is the one piece of the original this port replaces rather than reproduces -- and the
   * replacement is exact, because nothing else ever reads `UNIV`.
   */
  struct Bubble
  {
    /// 6502: FRIN -- the ship type in each slot, zero for empty, and one byte more than there are
    /// slots because the scan for a free one runs off the end and stops on the terminator.
    std::array<std::uint8_t, MAX_SHIPS + 1> slots{};

    /// 6502: K% -- the ten data blocks the slots point at.
    std::array<Ship, MAX_SHIPS> blocks{};

    /*
     * 6502: MANY -- how many of each type are in the bubble, indexed by SHIP TYPE.
     *
     * Sized by what indexes it (§6.8): `INC MANY,X` with X a ship type, so types 0 to
     * `SHIP_TYPE_COUNT` inclusive. Entry 0 is never incremented -- type 0 means an empty slot --
     * and is kept so the index is the type rather than the type minus one.
     */
    std::array<std::uint8_t, SHIP_TYPE_COUNT + 1u> counts{};

    /// 6502: JUNK -- cargo canisters, escape pods and the rest, counted together as well as
    /// separately, because the tactics code asks "is any of this worth shooting at".
    std::uint8_t junk = 0;

    /*
     * 6502: MSTG -- which slot the player's missile is locked on, or 255 for none.
     *
     * It is bubble state and not missile state, because `KILLSHP` has to know: killing the ship a
     * missile is chasing has to unlock it, and killing a ship BELOW it in the list has to renumber
     * it, since every slot above the dead one shifts down.
     */
    std::uint8_t missileTarget = 0xFF;

    /*
     * 6502: SSPR -- and it is not a byte of its own. `MANY` is at 1117 and `SSPR` at 1119, and
     * `SST` is 2, so **`SSPR` IS `MANY + SST`**: "is the space station present" and "how many
     * space stations are in the bubble" are one byte with two names (§6.58).
     *
     * That is why nothing ever sets it when a station is created -- `NWSHP`'s `INC MANY,X` has
     * already done it -- and why `KS4`'s `STA SSPR` is how the count is cleared. The port had it
     * as a separate field for about an hour, and the sweep caught it on the first station kill.
     */
    [[nodiscard]] std::uint8_t StationPresent() const noexcept
    {
      return Count(ShipType::Station);
    }

    /// 6502: MANY,X with X = the type -- how many of a type are in the bubble.
    [[nodiscard]] constexpr std::uint8_t& Count(ShipType _type) noexcept
    {
      return counts[Byte(_type)];
    }
    [[nodiscard]] constexpr std::uint8_t Count(ShipType _type) const noexcept
    {
      return counts[Byte(_type)];
    }

    /// 6502: SLSP -- the bottom of the ship line heap, which grows DOWN from LS%. It is bubble
    /// state rather than drawing state: `NWSHP` moves it and `KILLSHP` moves it back, and what
    /// lives between it and LS% is slice 3b's.
    std::uint16_t heapBottom = SHIP_HEAP_TOP;

    /*
     * 6502: XX21+2*SST-2 and XX21+2*SST-1 -- the space station's entry in the blueprint pointer
     * table, and THE ONLY BYTES OF THAT TABLE THE GAME EVER WRITES.
     *
     * `XX21` is at &D000, the first 66 bytes of the ship data region, and the port holds that
     * region as a `const` array because nothing writes it -- which was true until `NWSPS`, whose
     * fourteen instructions above the fall into `NWSHP` are a SELF-MODIFICATION: they store either
     * the Coriolis's address or the Dodo's into this entry, and everything downstream then reads
     * the table normally. A grep of the whole build for `STA XX21` finds those four stores and
     * nothing else, which is what makes one field the right model rather than a mutable copy of
     * the table: the port would be modelling writes the game does not make.
     *
     * `spasto` needs no field of its own. `BEGIN` copies this entry into it at boot, before
     * anything can have changed it, so `spasto` is permanently the Coriolis's address -- which is
     * what `BlueprintAddress(ShipType::Station)` returns from the immutable region.
     *
     * IT MUST BE SEEDED, and zero is not a value the game can hold here: a zero entry in `XX21`
     * means "this build does not carry that type" and `NWSHP` refuses the ship. So an unseeded
     * bubble refuses to create a station rather than creating a wrong one, which is §6.95's rule
     * applied to a second byte -- the flight universe has to be built in a state the game could be in.
     */
    std::uint16_t stationBlueprint = 0;
  };

  /*
   * 6502: LDA XX21-2,Y / LDA XX21-1,Y -- a blueprint address out of the table AS IT STANDS.
   *
   * The difference from `BlueprintAddress` is one ship type. Everything but the station reads the
   * assembled region, which is `const`; the station reads whatever the last `NWSPS` put in the
   * table, because that is where the Coriolis and the Dodo differ. Both of the routines that index
   * the table by type -- `NWSHP` and the flight loop's part 4 -- go through here for that reason.
   */
  [[nodiscard]] std::uint16_t BlueprintFor(const Bubble& _bubble, ShipType _shipType) noexcept;

  /// 6502: what GINF computes -- the ADDRESS of slot X's block, which is what `NWSHP` compares the
  /// heap against. The blocks are an array here; this is the address the original would have used.
  [[nodiscard]] constexpr std::uint16_t SlotAddress(std::uint8_t _slot) noexcept
  {
    return static_cast<std::uint16_t>(SHIP_BLOCK_BASE + _slot * SHIP_BLOCK_SIZE);
  }

  /*
   * 6502: GINF -- the address of slot X's data block.
   *
   * `TXA / ASL A / TAY / LDA UNIV,Y / STA INF / LDA UNIV+1,Y / STA INF+1 / RTS`, which is a
   * doubling and a table read because the 6502 cannot index by 37. Here it is the index, and the
   * routine survives as a named function only because the ledger counts it and because a caller
   * that asked for slot 10 in the original would read past `UNIV`.
   */
  [[nodiscard]] Ship* SlotBlock(Bubble& _bubble, std::uint8_t _slot) noexcept;

  /// What `NWSHP` left behind: whether the ship was created, and where.
  struct NewShip
  {
    bool created = false; ///< 6502: the carry -- SET on success, CLEAR on either refusal
    std::uint8_t slot = 0;
  };

  /*
   * 6502: NWSHP -- put the ship in `_work` into a free slot.
   *
   * TWO WAYS TO FAIL and they are different: no free slot, or no room in the ship line heap. Both
   * return with the carry clear, and the second is the interesting one -- it reads byte 5 of the
   * blueprint, takes that much off `SLSP`, and refuses if what is left would run down into the
   * block it is about to write. A bubble full of Anacondas runs out of heap before it runs out of
   * slots, so this is reachable rather than defensive.
   *
   * THE SECOND SUBTRACTION HAS NO `SEC`, and the original says so -- the `\SEC` in the source is
   * commented out. `LDA INWK+33 / SBC INF` runs on whatever carry the `SBC #0` above it left, so
   * the comparison is carry-dependent by construction. In practice that carry is always set,
   * because SLSP's high byte is never small enough for the first subtraction to borrow out of it;
   * the port reproduces the chain rather than assuming that, and the oracle sweep is what says so.
   *
   * A NEGATIVE TYPE skips all of it. The planet and the sun are types 128 and 129, they have no
   * blueprint and no heap, and `BMI NW2` takes them straight to the bookkeeping.
   *
   * `NEWB` is not a parameter because it is not a separate byte: it is `_work[36]`, the last byte
   * of the block, which the routine ORs into and then copies along with everything else.
   *
   * `XX0` IS A PARAMETER, and it is one because the routine WRITES it: `LDA XX21-1,Y / STA XX0+1 /
   * LDA XX21-2,Y / STA XX0` is how the new ship's blueprint becomes the current one. The port had
   * it as a local for as long as the only caller was `SOS1`, whose types are all negative and take
   * the `BMI NW2` path past those stores -- so the omission could not be seen until `NWSPS` created
   * a real ship. The oracle caught it on the first frame that spawned a station.
   */
  [[nodiscard]] NewShip AddShip(Bubble& _bubble, Ship& _work, ShipType _shipType, std::uint16_t& _blueprint) noexcept;

} // namespace Elite
