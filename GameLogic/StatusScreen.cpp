#include "pch.h"

#include "StatusScreen.h"

#include "EliteTypes.h"
#include "Ports.h"
#include "Universe.h"
#include "MarketScreen.h"
#include "TextPrint2x.h"
#include "ViewChange.h"

/*
 * The Status Mode screen (slice 2c).
 */

namespace Elite
{

  namespace
  {
    /// The title's column and its token, whose rule is the canvas's.
    constexpr std::uint8_t TITLE_COLUMN = 7;
    constexpr std::uint8_t TITLE_TOKEN = 126;

    /// The base of the three condition tokens. Recursive tokens are the argument less 160,
    /// so 230 is token 70 "GREEN", 231 is 71 "RED" and 232 is 72 "YELLOW" -- RED BEFORE YELLOW,
    /// which is why the healthy branch is the one that adds two. "DOCKED" is not in this run.
    constexpr std::uint8_t CONDITION_BASE = 230;

    /// The energy level below which the condition is Red rather than Yellow.
    constexpr std::uint8_t LOW_ENERGY = 128;

    /// "LEGAL STATUS:" and the base of the three answers.
    constexpr std::uint8_t LEGAL_HEADING_TOKEN = 125;
    constexpr std::uint8_t LEGAL_BASE = 19;

    /// Fugitive at fifty, offender below it.
    constexpr std::uint8_t FUGITIVE_AT = 50;

    /// "RATING:", then the rating itself at 21 plus the shift count.
    constexpr std::uint8_t RATING_HEADING_TOKEN = 16;
    constexpr std::uint8_t RATING_BASE = 21;

    /// "EQUIPMENT:".
    constexpr std::uint8_t EQUIPMENT_HEADING_TOKEN = 18;

    /// The three pieces of equipment with flags of their own, and their tokens.
    constexpr std::uint8_t ESCAPE_POD_TOKEN = 112;
    constexpr std::uint8_t FUEL_SCOOPS_TOKEN = 111;
    constexpr std::uint8_t ECM_TOKEN = 108;

    /// The four consecutive flags at BOMB, walked from token 113 up to but not
    /// including 117.
    constexpr std::uint8_t BOMB_GROUP_FIRST_TOKEN = 113;
    constexpr std::uint8_t BOMB_GROUP_LAST_TOKEN = 117;

    /// "FRONT", "REAR", "LEFT", "RIGHT" for the four laser mounts.
    constexpr std::uint8_t VIEW_NAME_BASE = 96;
    constexpr int LASER_MOUNTS = 4;

    constexpr std::uint8_t PULSE_TOKEN = 103;
    constexpr std::uint8_t BEAM_TOKEN = 104;
    constexpr std::uint8_t MILITARY_TOKEN = 117;
    constexpr std::uint8_t MINING_TOKEN = 118;

    /// "DOCKED", which goes through `DETOK` -- an EXTENDED token rather than a recursive one.
    constexpr std::uint8_t DOCKED_TOKEN = 205;

    /*
     * st4 and the shift loop before st3 -- the rating, which is not a table lookup.
     *
     * Under 256 kills the tally's low byte is shifted right twice and then once at a time until it
     * reaches zero, and the NUMBER OF SHIFTS is the rating. So each band is twice as wide as the one
     * below it. Over 256 kills it switches to three comparisons on the high byte.
     *
     * Returns the value the original leaves in X, which is 1 to 9.
     */
    [[nodiscard]] std::uint8_t Rating(std::uint16_t _kills) noexcept
    {
      const std::uint8_t high = static_cast<std::uint8_t>(_kills >> 8);

      // A rating starting at 9 and stepped down once for each threshold the high byte
      // misses. The last step leaves 6 and its branch is therefore always taken.
      if (high != 0)
      {
        if (high >= 25u)
        {
          return 9;
        }
        if (high >= 10u)
        {
          return 8;
        }
        if (high >= 2u)
        {
          return 7;
        }
        return 6;
      }

      // Two shifts, then one at a time with the count going up, until the byte is zero.
      std::uint8_t value = static_cast<std::uint8_t>(static_cast<std::uint8_t>(_kills) >> 2);
      std::uint8_t shifts = 0;
      do
      {
        ++shifts;
        value = static_cast<std::uint8_t>(value >> 1);
      } while (value != 0);

      return shifts;
    }

    /// The four CPYs that turn a laser's power byte into the token that names it. A mount
    /// holding anything else prints as a pulse laser, because 103 is the value A starts at and only
    /// a match overwrites it.
    [[nodiscard]] std::uint8_t LaserToken(Laser _laser) noexcept
    {
      if (_laser == LASER_BEAM)
      {
        return BEAM_TOKEN;
      }
      if (_laser == LASER_MILITARY)
      {
        return MILITARY_TOKEN;
      }
      if (_laser == LASER_MINING)
      {
        return MINING_TOKEN;
      }
      return PULSE_TOKEN;
    }
  } // namespace

  void StatusScreen(Universe& _universe, Ports& _ports, const ShipCondition& _condition) noexcept
  {
    /*
     * TRADEMODE on view 8, which sets the cursor and the case flags too.
     *
     * The layout is this screen's own and is named here because nothing downstream could work it
     * out: `TT213` starts the inventory screen with the same #8, so the view byte the game keeps
     * says "a trade screen" and not "the status screen" (Resolution.md section 6.2, RS-5-a).
     */
    SetUpTradeScreen(_universe, _ports, INVENTORY_VIEW, STATUS_LAYOUT);

    /*
     * The system nearest the crosshairs, whose seeds the title line then prints.
     *
     * It is called for what it leaves behind rather than for anything it draws, which is why its
     * result goes straight into the selected system rather than being read here.
     */
    _universe.selectedSeeds = FindNearestSystem(_universe.commander.galaxySeeds, _universe.crosshairX, _universe.crosshairY,
                                                _universe.commander.systemX, _universe.commander.systemY)
                                .seeds;

    // Column 7, then token 126 through `NLIN3` -- the rule itself is the canvas's.
    _universe.text.column = TITLE_COLUMN;
    _ports.printer.Print(TITLE_TOKEN);

    /*
     * A token loaded, the docked flag read, and a branch to `wearedocked`.
     *
     * That first token, 15, is DEAD: docked, `wearedocked` loads its own; in space, the condition
     * base overwrites it four instructions later. The original's own comment calls it "left over
     * from the cassette version", and it is not reproduced.
     */
    if (_condition.docked != 0)
    {
      // The token, a newline, and straight on to the legal status.
      _ports.tokens.Print(DOCKED_TOKEN);

      /*
       * A carriage return falling straight into CHPR, so the newline goes through
       * the CHARACTER printer rather than through TT27. TT67, two routines away, is the one that
       * goes via TT27.
       *
       * The two are NOT interchangeable in principle: TT27's path clears the "first letter seen"
       * bit when it prints a non-letter, and CHPR does not touch QQ17 at all. So swapping them
       * would capitalise the next letter differently.
       *
       * They are interchangeable HERE, and a mutation swapping them survives twenty situations
       * compared on both the characters and the final case flags -- whatever the difference does to
       * QQ17 is overwritten before anything reads it. That is an unresolved mutation rather than a
       * proven equivalence: the argument depends on what the token after this one does to the
       * flags, which is a fact about token 125 rather than about this routine. The faithful call
       * costs nothing and is kept.
       */
      _ports.characters.Put(12);
    }
    else
    {
      /*
       * The condition base, the first slot past the junk, and the energy compared.
       *
       * Green when the first slot past the junk is empty. Otherwise the carry that comparison
       * leaves picks between Red and Yellow, and the addition of one adds one MORE than it looks
       * like it does: with the carry set it adds two.
       */
      std::uint8_t condition = CONDITION_BASE;
      if (_condition.firstShip != 0)
      {
        const bool healthy = _condition.energy >= LOW_ENERGY;
        condition = AddWithCarry(condition, 1, healthy).value;
      }

      // JSR plf.
      PrintThenNewline(_ports.printer, condition);
    }

    // The heading and a space.
    PrintThenSpace(_ports.printer, LEGAL_HEADING_TOKEN);

    /*
     * The legal base, the status byte, and the same fifty compared.
     *
     * Clean, Offender or Fugitive, and the same trick: the comparison's carry is what makes the
     * third one reachable at all.
     */
    std::uint8_t legal = LEGAL_BASE;
    const std::uint8_t legalStatus = _universe.commander.legalStatus;
    if (legalStatus != 0)
    {
      legal = AddWithCarry(legal, 1, legalStatus >= FUGITIVE_AT).value;
    }
    PrintThenNewline(_ports.printer, legal);

    // The heading and a space, then 21 plus the rating.
    PrintThenSpace(_ports.printer, RATING_HEADING_TOKEN);
    PrintThenNewline(_ports.printer, static_cast<std::uint8_t>(RATING_BASE + Rating(_universe.commander.kills.Value())));

    // The heading, indented.
    PrintThenIndent(_ports.printer, _universe.text, EQUIPMENT_HEADING_TOKEN);

    // A flag tested and its token printed if set, and the same shape twice more.
    if (_universe.commander.escapePod != 0)
    {
      PrintThenIndent(_ports.printer, _universe.text, ESCAPE_POD_TOKEN);
    }
    if (_universe.commander.fuelScoops != 0)
    {
      PrintThenIndent(_ports.printer, _universe.text, FUEL_SCOOPS_TOKEN);
    }
    if (_universe.commander.ecm != 0)
    {
      PrintThenIndent(_ports.printer, _universe.text, ECM_TOKEN);
    }

    /*
     * Four flags walked by their token number.
     *
     * Four flags that happen to be consecutive in the block -- the energy bomb, the energy unit,
     * the docking computer and the galactic hyperdrive -- walked by using the TOKEN NUMBER as the
     * index and subtracting 113 from the base address. So the loop counter and the thing printed
     * are the same byte, which is why there is no separate table.
     */
    // The walk is over BYTES from BOMB, so it goes through the codec: the FOUR it reaches are the
    // bomb, the energy unit, the docking computer and the galactic hyperdrive. The escape pod is
    // the byte after them and is printed above, on its own flag.
    const std::array<std::uint8_t, COMMANDER_BLOCK_SIZE> equipment = _universe.commander.ToBytes();
    for (std::uint8_t token = BOMB_GROUP_FIRST_TOKEN; token < BOMB_GROUP_LAST_TOKEN; ++token)
    {
      const std::size_t field = static_cast<std::size_t>(Field::EnergyBomb) + static_cast<std::size_t>(token - BOMB_GROUP_FIRST_TOKEN);
      if (equipment[field] != 0)
      {
        PrintThenIndent(_ports.printer, _universe.text, token);
      }
    }

    // The four mounts walked, skipping any that holds nothing.
    for (int mount = 0; mount < LASER_MOUNTS; ++mount)
    {
      const Laser laser = _universe.commander.lasers[static_cast<std::size_t>(mount)];
      if (!laser.Fitted())
      {
        continue;
      }

      // The mount number plus 96 -- the mount's name and a space.
      PrintThenSpace(_ports.printer, static_cast<std::uint8_t>(VIEW_NAME_BASE + mount));
      PrintThenIndent(_ports.printer, _universe.text, LaserToken(laser));
    }
  }

} // namespace Elite
