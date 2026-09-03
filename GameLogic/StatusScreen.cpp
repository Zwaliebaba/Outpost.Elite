#include "pch.h"

#include "StatusScreen.h"

#include "EliteTypes.h"

/*
 * The Status Mode screen (slice 2c).
 */

namespace Elite
{

namespace
{
/// 6502: LDA #7 / JSR DOXC and LDA #126 / JSR NLIN3 -- the title, whose rule is the canvas's.
constexpr std::uint8_t TITLE_COLUMN = 7;
constexpr std::uint8_t TITLE_TOKEN = 126;

/// 6502: LDA #230 -- the base of the three condition tokens, "Docked", "Green", "Yellow", "Red".
constexpr std::uint8_t CONDITION_BASE = 230;

/// 6502: CPY #128 -- the energy level below which the condition is Red rather than Yellow.
constexpr std::uint8_t LOW_ENERGY = 128;

/// 6502: LDA #125 / JSR spc and LDA #19 -- "LEGAL STATUS:" and the base of the three answers.
constexpr std::uint8_t LEGAL_HEADING_TOKEN = 125;
constexpr std::uint8_t LEGAL_BASE = 19;

/// 6502: CPY #50 -- fugitive at fifty, offender below it.
constexpr std::uint8_t FUGITIVE_AT = 50;

/// 6502: LDA #16 / JSR spc -- "RATING:", then the rating itself at 21 + the shift count.
constexpr std::uint8_t RATING_HEADING_TOKEN = 16;
constexpr std::uint8_t RATING_BASE = 21;

/// 6502: LDA #18 / JSR plf2 -- "EQUIPMENT:".
constexpr std::uint8_t EQUIPMENT_HEADING_TOKEN = 18;

/// 6502: the three pieces of equipment with flags of their own, and their tokens.
constexpr std::uint8_t ESCAPE_POD_TOKEN = 112;
constexpr std::uint8_t FUEL_SCOOPS_TOKEN = 111;
constexpr std::uint8_t ECM_TOKEN = 108;

/// 6502: LDA #113 / STA XX4 / stqv ... CMP #117 / BCC stqv -- the four consecutive flags at BOMB.
constexpr std::uint8_t BOMB_GROUP_FIRST_TOKEN = 113;
constexpr std::uint8_t BOMB_GROUP_LAST_TOKEN = 117;

/// 6502: ADC #96 -- "FRONT", "REAR", "LEFT", "RIGHT" for the four laser mounts.
constexpr std::uint8_t VIEW_NAME_BASE = 96;
constexpr int LASER_MOUNTS = 4;

/*
 * 6502: POW = 15, Mlas = 50, Armlas = INT(128.5 + 1.5 * POW) = 151, from elite-source.asm.
 *
 * The power byte IS the laser's identity: there is no separate type. A beam laser is a pulse
 * laser's power with bit 7 set, and the other two are values that happen not to collide. A mount
 * holding anything else prints as a pulse laser, because 103 is the value A starts at and only a
 * match overwrites it.
 */
constexpr std::uint8_t PULSE_POWER = 15;
constexpr std::uint8_t BEAM_POWER = 128 + PULSE_POWER;
constexpr std::uint8_t MILITARY_POWER = 151;
constexpr std::uint8_t MINING_POWER = 50;

constexpr std::uint8_t PULSE_TOKEN = 103;
constexpr std::uint8_t BEAM_TOKEN = 104;
constexpr std::uint8_t MILITARY_TOKEN = 117;
constexpr std::uint8_t MINING_TOKEN = 118;

/// 6502: LDA #205 / JSR DETOK -- "DOCKED", which is an EXTENDED token rather than a recursive one.
constexpr std::uint8_t DOCKED_TOKEN = 205;

/*
 * 6502: st4 and the shift loop before st3 -- the rating, which is not a table lookup.
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

  // 6502: st4 -- LDX #9 / CMP #25 / BCS st3 / DEX / CMP #10 / BCS st3 / DEX / CMP #2 / BCS st3 /
  // DEX / BNE st3. The last DEX leaves 6 and the BNE is therefore always taken.
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

  // 6502: TAX (X = 0) / LDA TALLY / LSR A / LSR A / INX / LSR A / BNE P%-2.
  std::uint8_t value = static_cast<std::uint8_t>(static_cast<std::uint8_t>(_kills) >> 2);
  std::uint8_t shifts = 0;
  do
  {
    ++shifts;
    value = static_cast<std::uint8_t>(value >> 1);
  } while (value != 0);

  return shifts;
}

/// 6502: the four CPYs that turn a laser's power byte into the token that names it.
[[nodiscard]] std::uint8_t LaserToken(std::uint8_t _power) noexcept
{
  if (_power == BEAM_POWER)
  {
    return BEAM_TOKEN;
  }
  if (_power == MILITARY_POWER)
  {
    return MILITARY_TOKEN;
  }
  if (_power == MINING_POWER)
  {
    return MINING_TOKEN;
  }
  return PULSE_TOKEN;
}
} // namespace

void StatusScreen(TradeScreen& _screen, const CommanderBlock& _commander, const ShipCondition& _condition,
                  std::uint8_t _crosshairX, std::uint8_t _crosshairY, SystemSeeds& _outSelected) noexcept
{
  // 6502: LDA #8 / JSR TRADEMODE, and the text state TTX66 ends on.
  _screen.effects.SetUpTradeScreen(INVENTORY_VIEW);
  _screen.text.column = 1;
  _screen.text.row = 1;
  _screen.printer.SetCaseFlags(0);
  _screen.text.caseFlags = 0;

  /*
   * 6502: JSR TT111 -- the system nearest the crosshairs, whose seeds the title line then prints.
   *
   * It is called for what it leaves behind rather than for anything it draws, which is why its
   * result goes straight into the selected system rather than being read here.
   */
  _outSelected = FindNearestSystem(_commander.GalaxySeeds(), _crosshairX, _crosshairY,
                                   _commander.At(Field::SystemX), _commander.At(Field::SystemY))
                   .seeds;

  // 6502: LDA #7 / JSR DOXC / LDA #126 / JSR NLIN3 -- the rule itself is the canvas's.
  _screen.text.column = TITLE_COLUMN;
  _screen.printer.Print(TITLE_TOKEN);

  /*
   * 6502: LDA #15 / LDY QQ12 / BNE wearedocked.
   *
   * The `LDA #15` is dead: docked, `wearedocked` loads its own token; in space, `LDA #230`
   * overwrites it four instructions later. The original's own comment calls it "left over from
   * the cassette version", and it is not reproduced.
   */
  if (_condition.docked != 0)
  {
    // 6502: wearedocked -- JSR DETOK / JSR TT67K / JMP st6+3.
    _screen.extended.Print(DOCKED_TOKEN);

    /*
     * 6502: TT67K -- LDA #12 falling straight into CHPR, so the newline goes through the CHARACTER
     * printer rather than through TT27. TT67, two routines away, is the one that goes via TT27.
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
    _screen.characters.Put(12);
  }
  else
  {
    /*
     * 6502: LDA #230 / LDY JUNK / LDX FRIN+2,Y / BEQ st6 / LDY ENERGY / CPY #128 / ADC #1.
     *
     * Green when the first slot past the junk is empty. Otherwise the carry from `CPY #128` picks
     * between Yellow and Red, and the ADC adds one MORE than it looks like it does -- `ADC #1`
     * with the carry set adds two.
     */
    std::uint8_t condition = CONDITION_BASE;
    if (_condition.firstShip != 0)
    {
      const bool healthy = _condition.energy >= LOW_ENERGY;
      condition = AddWithCarry(condition, 1, healthy).value;
    }

    // 6502: st6 -- JSR plf.
    PrintThenNewline(_screen.printer, condition);
  }

  // 6502: st6+3 -- LDA #125 / JSR spc.
  PrintThenSpace(_screen.printer, LEGAL_HEADING_TOKEN);

  /*
   * 6502: LDA #19 / LDY FIST / BEQ st5 / CPY #50 / ADC #1.
   *
   * Clean, Offender or Fugitive, and the same trick: the comparison's carry is what makes the
   * third one reachable at all.
   */
  std::uint8_t legal = LEGAL_BASE;
  const std::uint8_t fist = _commander.At(Field::LegalStatus);
  if (fist != 0)
  {
    legal = AddWithCarry(legal, 1, fist >= FUGITIVE_AT).value;
  }
  PrintThenNewline(_screen.printer, legal);

  // 6502: LDA #16 / JSR spc / ... / st3: TXA / CLC / ADC #21 / JSR plf.
  PrintThenSpace(_screen.printer, RATING_HEADING_TOKEN);
  PrintThenNewline(_screen.printer, static_cast<std::uint8_t>(RATING_BASE + Rating(_commander.Kills())));

  // 6502: LDA #18 / JSR plf2.
  PrintThenIndent(_screen.printer, _screen.text, EQUIPMENT_HEADING_TOKEN);

  // 6502: LDA ESCP / BEQ P%+7 / LDA #112 / JSR plf2, and the same shape twice more.
  if (_commander.At(Field::EscapePod) != 0)
  {
    PrintThenIndent(_screen.printer, _screen.text, ESCAPE_POD_TOKEN);
  }
  if (_commander.At(Field::FuelScoops) != 0)
  {
    PrintThenIndent(_screen.printer, _screen.text, FUEL_SCOOPS_TOKEN);
  }
  if (_commander.At(Field::Ecm) != 0)
  {
    PrintThenIndent(_screen.printer, _screen.text, ECM_TOKEN);
  }

  /*
   * 6502: LDA #113 / STA XX4 / stqv: TAY / LDX BOMB-113,Y / BEQ P%+5 / JSR plf2 / INC XX4 ...
   *
   * Four flags that happen to be consecutive in the block -- the energy bomb, the energy unit,
   * the docking computer and the galactic hyperdrive -- walked by using the TOKEN NUMBER as the
   * index and subtracting 113 from the base address. So the loop counter and the thing printed
   * are the same byte, which is why there is no separate table.
   */
  for (std::uint8_t token = BOMB_GROUP_FIRST_TOKEN; token < BOMB_GROUP_LAST_TOKEN; ++token)
  {
    const std::size_t field =
      static_cast<std::size_t>(Field::EnergyBomb) + static_cast<std::size_t>(token - BOMB_GROUP_FIRST_TOKEN);
    if (_commander.bytes[field] != 0)
    {
      PrintThenIndent(_screen.printer, _screen.text, token);
    }
  }

  // 6502: LDX #0 / st: STX CNT / LDY LASER,X / BEQ st1 / ... / CPX #4 / BCC st.
  for (int mount = 0; mount < LASER_MOUNTS; ++mount)
  {
    const std::uint8_t power =
      _commander.bytes[static_cast<std::size_t>(Field::Lasers) + static_cast<std::size_t>(mount)];
    if (power == 0)
    {
      continue;
    }

    // 6502: TXA / CLC / ADC #96 / JSR spc -- the mount's name and a space.
    PrintThenSpace(_screen.printer, static_cast<std::uint8_t>(VIEW_NAME_BASE + mount));
    PrintThenIndent(_screen.printer, _screen.text, LaserToken(power));
  }
}

} // namespace Elite
