#pragma once

#include "ShipBlueprint.h"

#include <array>
#include <cstdint>

namespace Elite
{

  /*
   * The local bubble of ships (slice 3a).
   *
   * 6502: FRIN, K%, UNIV and MANY. Elite does not have a world; it has a BUBBLE of at most ten
   * ships around the player, created as they come into range and destroyed as they leave. Every
   * routine that moves, draws, shoots at or is shot by a ship works on one slot of this at a time,
   * copied into `INWK` and copied back.
   */

  /// 6502: NI% -- how many bytes one ship's data block is. Thirty-seven, not thirty-six: the
  /// resolved C64 source says `NI% = 37`, and the `original-sources` listings disagree with the
  /// library on other constants, which is what `tools/c64_source.py` exists to settle.
  inline constexpr std::uint8_t SHIP_BLOCK_SIZE = 37;

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

  /// 6502: NEWB is at zero page 45 and INWK at 9, so NEWB IS INWK+36 -- the last byte of the block,
  /// and the reason NI% is thirty-seven rather than the thirty-six the workspace looks.
  inline constexpr std::uint8_t SHIP_FLAGS_OFFSET = 36;

  /// 6502: the offsets NWSHP writes before the block is copied into its slot.
  inline constexpr std::uint8_t SHIP_HEAP_LOW_OFFSET = 33;  ///< 6502: INWK+33 / INWK+34, the ship's
  inline constexpr std::uint8_t SHIP_HEAP_HIGH_OFFSET = 34; ///< own heap pointer
  inline constexpr std::uint8_t SHIP_ENERGY_OFFSET = 35;    ///< 6502: INWK+35, from blueprint byte 14

  /*
   * 6502: INWK+31 -- one byte holding five things, which is why it does not get a name saying what
   * it is FOR.
   *
   * Slice 3a called this `SHIP_MISSILES_OFFSET`, because `NWSHP` is the only routine that had
   * reached it and all `NWSHP` does is OR the blueprint's missile count into the bottom three bits.
   * The drawing code reads the same byte for something else entirely -- bit 3 says whether the ship
   * is currently on the screen, and it is what `EE51` tests to decide whether there is anything to
   * rub out. Two names for one offset is the §6.34 trap set deliberately, so there is one name and
   * the bits are documented.
   *
   * Bits 4 and the rest of the upper half are left unnamed until the routines that read them are
   * ported; guessing at them from the bit numbers is how the wrong constant gets used once.
   */
  inline constexpr std::uint8_t SHIP_STATE_OFFSET = 31;

  inline constexpr std::uint8_t SHIP_STATE_MISSILES = 0x07;  ///< 6502: blueprint byte 19 AND 7
  inline constexpr std::uint8_t SHIP_STATE_DRAWN = 0x08;     ///< 6502: bit 3 -- on the screen now
  inline constexpr std::uint8_t SHIP_STATE_EXPLODING = 0x20; ///< 6502: bit 5
  inline constexpr std::uint8_t SHIP_STATE_FIRING = 0x40;    ///< 6502: bit 6 -- laser
  inline constexpr std::uint8_t SHIP_STATE_KILLED = 0x80;    ///< 6502: bit 7 -- killed, not yet exploding

  /// 6502: the ship types NWSHP and KILLSHP single out by name.
  /// 6502: MSL -- the only type that carries a target slot in its AI byte, which `KILLSHP` has to
  /// renumber, and the one `MVEIT` runs tactics on every iteration rather than one in eight.
  inline constexpr std::uint8_t SHIP_TYPE_MISSILE = 1;
  inline constexpr std::uint8_t SHIP_TYPE_STATION = 2;      ///< 6502: SST -- skips the heap allocation
  inline constexpr std::uint8_t SHIP_TYPE_HERMIT = 15;      ///< 6502: HER -- counts as junk despite its type
  inline constexpr std::uint8_t SHIP_TYPE_CONSTRICTOR = 31; ///< 6502: CON -- the mission ship, whose
                                                            ///< death sets a mission flag
  inline constexpr std::uint8_t JUNK_TYPE_FIRST = 3;        ///< 6502: JL = ESC
  inline constexpr std::uint8_t JUNK_TYPE_LIMIT = 11;       ///< 6502: JH = SHU+2, exclusive

  /*
   * 6502: the wreckage, and the two the energy bomb cannot touch.
   *
   * `PLT` to `SPL` is a RANGE and is used as one: `SFS1` gives a random tumble to everything from
   * the plate to the splinter and to nothing else, so the four numbers being consecutive is part of
   * the behaviour rather than an accident of the table. `THG` and `CON` are two of the bomb's three
   * exemptions, and `CON` is also the boundary above which a laser is halved unless it is military.
   */
  inline constexpr std::uint8_t SHIP_TYPE_ALLOY_PLATE = 4; ///< 6502: PLT
  inline constexpr std::uint8_t SHIP_TYPE_CANISTER = 5;    ///< 6502: OIL
  inline constexpr std::uint8_t SHIP_TYPE_ASTEROID = 7;    ///< 6502: AST
  inline constexpr std::uint8_t SHIP_TYPE_SPLINTER = 8;    ///< 6502: SPL
  inline constexpr std::uint8_t SHIP_TYPE_THARGOID = 29;   ///< 6502: THG

  /// 6502: ESC and CYL. `ESC` is `JL`, the bottom of the junk range, and `CYL` is the boundary
  /// `ANGRY` compares `TYPE` against -- everything below it is wreckage, a missile or a station.
  inline constexpr std::uint8_t SHIP_TYPE_ESCAPE_POD = 3;
  inline constexpr std::uint8_t SHIP_TYPE_COBRA_MK3 = 11;

  /// 6502: SHU, ANA, COPS, SH3, WRM and TGL -- the six `TACTICS` names when it decides what a
  /// station launches, what an Anaconda escorts itself with, and what a rock hermit turns into.
  /// `SHU + 1` is the Transporter, which the source never names and which `TA1` counts.
  inline constexpr std::uint8_t SHIP_TYPE_SHUTTLE = 9;
  inline constexpr std::uint8_t SHIP_TYPE_ANACONDA = 14;
  inline constexpr std::uint8_t SHIP_TYPE_VIPER = 16;
  inline constexpr std::uint8_t SHIP_TYPE_SIDEWINDER = 17;
  inline constexpr std::uint8_t SHIP_TYPE_WORM = 23;
  inline constexpr std::uint8_t SHIP_TYPE_THARGON = 30;

  /*
   * 6502: INWK, and one entry of K% -- a single ship, as thirty-seven bytes.
   *
   * BYTES AND NOT FIELDS, deliberately. The original addresses this block by offset from three
   * different directions -- `INWK,X` and `INWK+10,Y` walk it as vectors, `MVS4` steps Y through it
   * in sixes, and `NWSHP` copies it wholesale through `(INF),Y` -- and the same byte is a
   * coordinate to one routine and half a rotation matrix to the next. A struct would have to pick
   * one reading and would make the other two into casts.
   *
   * Named offsets go on top of it as the routines that use them are ported, which is how
   * `CommanderBlock` is handled and for the same reason (ADR-002 §3).
   */
  struct ShipBlock
  {
    std::array<std::uint8_t, SHIP_BLOCK_SIZE> bytes{};

    [[nodiscard]] constexpr std::uint8_t& operator[](std::size_t _offset) noexcept
    {
      return bytes[_offset];
    }
    [[nodiscard]] constexpr std::uint8_t operator[](std::size_t _offset) const noexcept
    {
      return bytes[_offset];
    }
  };

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
    std::array<ShipBlock, MAX_SHIPS> blocks{};

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
      return counts[SHIP_TYPE_STATION];
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
     * what `BlueprintAddress(SHIP_TYPE_STATION)` returns from the immutable region.
     *
     * IT MUST BE SEEDED, and zero is not a value the game can hold here: a zero entry in `XX21`
     * means "this build does not carry that type" and `NWSHP` refuses the ship. So an unseeded
     * bubble refuses to create a station rather than creating a wrong one, which is §6.95's rule
     * applied to a second byte -- the flight world has to be built in a state the game could be in.
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
  [[nodiscard]] std::uint16_t BlueprintFor(const Bubble& _bubble, std::uint8_t _shipType) noexcept;

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
  [[nodiscard]] ShipBlock* SlotBlock(Bubble& _bubble, std::uint8_t _slot) noexcept;

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
  [[nodiscard]] NewShip AddShip(Bubble& _bubble, ShipBlock& _work, std::uint8_t _shipType, std::uint16_t& _blueprint) noexcept;

} // namespace Elite
