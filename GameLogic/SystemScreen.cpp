#include "pch.h"

#include "SystemScreen.h"

#include "TextPrint2x.h"

#include "EliteTypes.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Ports.h"
#include "Universe.h"
#include "ViewChange.h"

/*
 * The Data on System screen (slice 2a).
 */

namespace Elite
{

  namespace
  {
    /*
     * 6502: the tokens TT25 prints, in the order it prints them.
     *
     * Most are bases rather than tokens: the game adds an index to them, so 170 is "Rich" and 171
     * "Average" and nothing in the source says so. The bases are named here for what they are the
     * start of, which is the only way the arithmetic below reads as anything.
     */
    constexpr std::uint8_t TITLE_TOKEN = 163;        ///< "DATA ON {selected system}"
    constexpr std::uint8_t ECONOMY_HEADING = 194;    ///< "ECONOMY"
    constexpr std::uint8_t ECONOMY_PROSPERITY = 170; ///< "Rich", "Average", "Poor" -- indexed
    constexpr std::uint8_t ECONOMY_MAINLY = 173;     ///< TT70's word, which replaces the three above
    constexpr std::uint8_t ECONOMY_KIND = 168;       ///< "Industrial" / "Agricultural"
    constexpr std::uint8_t GOVERNMENT_HEADING = 162;
    constexpr std::uint8_t GOVERNMENT_FIRST = 177; ///< "Anarchy" through "Corporate State"
    constexpr std::uint8_t TECH_HEADING = 196;
    constexpr std::uint8_t POPULATION_HEADING = 192;
    constexpr std::uint8_t BILLION_TOKEN = 198;
    constexpr std::uint8_t HUMAN_COLONIALS = 188;
    constexpr std::uint8_t SPECIES_SIZE = 227;       ///< "Large", "Fierce", "Small"
    constexpr std::uint8_t SPECIES_COLOUR = 230;     ///< "Green" through "Black"
    constexpr std::uint8_t SPECIES_APPEARANCE = 236; ///< "Harmless" through "Fat"
    constexpr std::uint8_t SPECIES_NOUN = 242;       ///< "Rodents" through "Humanoids"
    constexpr std::uint8_t PRODUCTIVITY_HEADING = 193;
    constexpr std::uint8_t CREDITS_TOKEN = 226; ///< " CR", after the "M" printed as a character
    constexpr std::uint8_t RADIUS_HEADING = 250;
    constexpr std::uint8_t DISTANCE_HEADING = 191;
    constexpr std::uint8_t LIGHT_YEARS_TOKEN = 195;

    /// 6502: column 9, where the title starts.
    constexpr std::uint8_t TITLE_COLUMN = 9;

    /*
     * 6502: the three `CMP` bounds in the species block, which are not the same number.
     *
     * Size is printed when its index is below 3 and there are three size words; colour and appearance
     * when theirs are below 6, and there are six of each. So the bound is the table's length in every
     * case, and a system whose bits land past the end simply loses that word.
     */
    constexpr std::uint8_t SIZE_WORDS = 3;
    constexpr std::uint8_t COLOUR_WORDS = 6;
    constexpr std::uint8_t APPEARANCE_WORDS = 6;

    /// 6502: four bits of a seed byte plus 11 -- the radius's high byte, which is why no planet in
    /// Elite is smaller than 2,816 km or larger than 6,911.
    constexpr std::uint8_t RADIUS_HIGH_BASE = 11;

    /*
     * 6502: TT75 through TT207 -- the inhabitants.
     *
     * Kept together because the four words share `QQ19`, which the third one writes and the fourth
     * one reads: the noun's index is `(QQ15+5 & 3) + QQ19`, so the appearance a system was given
     * decides which noun it gets. Splitting them would hide that.
     */
    void PrintInhabitants(TokenPrinter& _printer, const SystemSeeds& _seeds) noexcept
    {
      // 6502: the seed byte shifted twice and PUSHED, because the next two words read different
      // slices of the SAME shifted byte and the mask below destroys the top of it.
      const std::uint8_t shifted = static_cast<std::uint8_t>(_seeds.bytes[5] >> 2);

      // 6502: three bits, and anything past the table branches to `TT205` and skips the word.
      const std::uint8_t size = static_cast<std::uint8_t>(shifted & 0x07u);
      if (size < SIZE_WORDS)
      {
        PrintThenSpace(_printer, static_cast<std::uint8_t>(SPECIES_SIZE + size));
      }

      // 6502: TT205 -- pulled back and shifted three more, so five shifts in all from `QQ15+5`.
      const std::uint8_t colour = static_cast<std::uint8_t>(shifted >> 3);
      if (colour < COLOUR_WORDS)
      {
        PrintThenSpace(_printer, static_cast<std::uint8_t>(SPECIES_COLOUR + colour));
      }

      /*
       * 6502: TT206 -- two seed bytes folded and masked to three bits, into `QQ19`.
       *
       * This one is STORED, and the noun below adds to it. So the appearance is not just a word: it
       * is half of the noun's index, and a system that loses its appearance word (index 6 or 7) still
       * has that index folded into what it is called.
       */
      const std::uint8_t appearance = static_cast<std::uint8_t>((_seeds.bytes[3] ^ _seeds.bytes[1]) & 0x07u);
      if (appearance < APPEARANCE_WORDS)
      {
        PrintThenSpace(_printer, static_cast<std::uint8_t>(SPECIES_APPEARANCE + appearance));
      }

      // 6502: TT207 -- two bits of a seed byte added to `QQ19`, masked, and used as a token.
      const std::uint8_t noun = static_cast<std::uint8_t>(((_seeds.bytes[5] & 0x03u) + appearance) & 0x07u);
      _printer.Print(static_cast<std::uint8_t>(SPECIES_NOUN + noun));
    }
  } // namespace

  void PrintDistanceLine(TokenPrinter& _printer, CharacterPrinter& _characters, TextState& _text, std::uint16_t _distance) noexcept
  {
    // 6502: the two distance bytes tested together -- no distance at all skips to `INCYC`, and
    // no newline either.
    if (_distance == 0)
    {
      MoveCursorDown(_text);
      return;
    }

    // 6502: TT63 -- token 191 through `TT68`.
    PrintThenColon(_printer, DISTANCE_HEADING);

    // 6502: `pr5` with five digits and ONE decimal place, so `QQ8` is in tenths of a light year
    // and 65535 would read as 6553.5.
    PrintValue(_characters, _distance, 5, true);

    // 6502: token 195, and then the routine FALLS INTO `TT60` -- so the light years, the cursor
    // move, sentence case and a newline all come from running off the end into the next routine.
    PrintTitleLine(_printer, _text, LIGHT_YEARS_TOKEN);
  }

  void SystemDataScreen(Universe& _universe, Ports& _ports, const SystemData& _data, std::uint16_t _distance) noexcept
  {
    // 6502: TRADEMODE on view 1, which sets the cursor and the case flags too.
    // The screen's own layout for the wide surface, named here because nothing downstream could
    // work it out (Resolution.md section 6.2, slice RS-5-d).
    SetUpScreen(_universe, _ports, DATA_ON_SYSTEM_VIEW, DATA_LAYOUT);
    _ports.keyboard.Flush();

    // 6502: column 9, then token 163 through `NLIN3` -- the rule `NLIN3` falls into is the
    // canvas's, and a caller draws it, exactly as the market and status screens do.
    _universe.text.column = TITLE_COLUMN;
    _ports.printer.Print(TITLE_TOKEN);

    // 6502: TTX69 -- a row down, sentence case, and a newline.
    MoveDownAndNewline(_ports.printer, _universe.text);

    // 6502: TT146.
    PrintDistanceLine(_ports.printer, _ports.characters, _universe.text, _distance);

    // 6502: token 194 through `TT68`.
    PrintThenColon(_ports.printer, ECONOMY_HEADING);

    /*
     * 6502: TT70 and TT71 -- the economy stepped up and halved, then compared against 2.
     *
     * The prosperity word, folded out of the economy by a shift and a comparison rather than looked
     * up. `(QQ3 + 1) >> 1` is 0, 1, 1, 2, 2, 3, 3, 4 across the eight economies; where it is 2 the
     * routine prints "Mainly" and jumps past this block entirely, which is economies 3 and 4.
     *
     * The branch to `TT71` reads the COMPARISON's carry and not the shift's -- the reload between
     * them leaves the flags alone -- so it asks whether that shifted value is below 2, not whether
     * anything was shifted out. Below, the economy indexes the three words directly; above, a
     * subtraction of five runs with the carry that same comparison set and folds 5, 6 and 7 back
     * onto 0, 1 and 2. So Rich, Average and Poor appear twice each, once industrial and once
     * agricultural, from one three-word table.
     */
    const std::uint8_t prosperity = RotateRight(static_cast<std::uint8_t>(_data.economy + 1u), false).value;
    if (prosperity == 2u)
    {
      // 6502: TT70 -- token 173, then straight to `TT72`, which lands PAST the addition above.
      _ports.printer.Print(ECONOMY_MAINLY);
    }
    else
    {
      std::uint8_t word = _data.economy;
      if (prosperity > 2u)
      {
        // 6502: reached with the carry SET by the comparison, so it is a plain subtraction of
        // five and not five-and-a-borrow. The clear after it is what makes the addition below plain.
        word = AddWithCarry(word, static_cast<std::uint8_t>(5u ^ 0xFFu), true).value;
      }
      _ports.printer.Print(static_cast<std::uint8_t>(ECONOMY_PROSPERITY + word));
    }

    // 6502: TT72 -- the economy halved twice, plus 168, through `TT60`.
    PrintTitleLine(_ports.printer, _universe.text, static_cast<std::uint8_t>(ECONOMY_KIND + (_data.economy >> 2)));

    // 6502: token 162 through `TT68`, then the government plus 177 through `TT60`.
    PrintThenColon(_ports.printer, GOVERNMENT_HEADING);
    PrintTitleLine(_ports.printer, _universe.text, static_cast<std::uint8_t>(GOVERNMENT_FIRST + _data.government));

    /*
     * 6502: token 196 through `TT68`, then the tech level stepped up and printed by `pr2`.
     *
     * The tech level is printed ONE HIGHER than it is stored, so the screen's "Tech.Level: 1" is a
     * QQ5 of zero. Everything else in the game -- the equipment shop's availability test, the
     * market -- works on the stored value, which is the off-by-one plan section 6.15 records the
     * other half of.
     */
    PrintThenColon(_ports.printer, TECH_HEADING);
    PrintByteValue(_ports.characters, static_cast<std::uint8_t>(_data.techLevel + 1u), false);

    // 6502: TTX69.
    MoveDownAndNewline(_ports.printer, _universe.text);

    // 6502: token 192 through `TT68`, then `pr2` with a decimal point -- three digits, so the
    // population is in hundreds of millions and prints as billions.
    PrintThenColon(_ports.printer, POPULATION_HEADING);
    PrintByteValue(_ports.characters, _data.population, true);
    PrintTitleLine(_ports.printer, _universe.text, BILLION_TOKEN);

    // 6502: TT75 -- an open bracket, then the seed byte's sign decides which species word.
    _ports.printer.Print('(');
    if ((_universe.selectedSeeds.bytes[4] & 0x80u) != 0u)
    {
      PrintInhabitants(_ports.printer, _universe.selectedSeeds);
    }
    else
    {
      // 6502: token 188, then straight to `TT76`.
      _ports.printer.Print(HUMAN_COLONIALS);
    }

    // 6502: TT76 -- an "S" and a close bracket. The plural is unconditional, which is why the
    // screen says "Human Colonials" and never "Human Colonial".
    _ports.printer.Print('S');
    PrintTitleLine(_ports.printer, _universe.text, ')');

    // 6502: token 193 through `TT68`, then `pr6` on the productivity pair and a space.
    PrintThenColon(_ports.printer, PRODUCTIVITY_HEADING);
    PrintValue(_ports.characters, _data.productivity, 5, false);
    PrintSpace(_ports.printer);

    /*
     * 6502: `QQ17` cleared, an "M", then token 226 through `TT60`.
     *
     * QQ17 is cleared to ALL CAPS for one character, because the "M" of "M CR" would otherwise come
     * out lower case: the sentence-case state has seen a letter by now, and TT27 would fold it. The
     * token after it puts nothing back, so the next line's case comes from TT60's own chain.
     */
    _ports.printer.SetCaseFlags(0);
    _ports.printer.Print('M');
    PrintTitleLine(_ports.printer, _universe.text, CREDITS_TOKEN);

    // 6502: token 250 through `TT68`.
    PrintThenColon(_ports.printer, RADIUS_HEADING);

    // 6502: four bits of one seed byte plus 11 as the high byte, another as the low, through `pr5`.
    const std::uint16_t radius = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>((_universe.selectedSeeds.bytes[5] & 0x0Fu) + RADIUS_HIGH_BASE) << 8) | _universe.selectedSeeds.bytes[3]);
    PrintValue(_ports.characters, radius, 5, false);
    PrintSpace(_ports.printer);

    // 6502: both letters through the CHARACTER printer, so the case flags do not touch them and
    // the "km" stays lower case whatever `QQ17` holds.
    _ports.characters.Put('k');
    _ports.characters.Put('m');

    // 6502: TTX69, then PDESC -- and PDESC's mission overrides are phase 4's, which is what
    // Galaxy.h's header records.
    MoveDownAndNewline(_ports.printer, _universe.text);
    PrintSystemDescription(_ports.tokens, _universe.rng, _universe.selectedSeeds);
  }

} // namespace Elite
