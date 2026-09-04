#include "pch.h"

#include "StateTokens.h"

/*
 * The control codes that print game state (slice 2c).
 *
 * These are the last six tokens the recursive printer could not expand, and every docked screen
 * needs at least one of them: the inventory prints the fuel and the cash, the buy screen prints
 * the cash after a purchase, and the system data screens print both system names.
 */

namespace Elite
{

  namespace
  {
    /// 6502: LDA #9 / STA U -- the cash prints to nine digits, one of which is after the point.
    constexpr std::uint8_t CASH_WIDTH = 9;

    /// 6502: CMP #13 / BEQ -- the carriage return that ends the commander's name.
    constexpr std::uint8_t NAME_TERMINATOR = 13;

    /// 6502: LDA #226 -- recursive token 66, " CR", which csh prints after the number.
    constexpr std::uint8_t CREDITS_TOKEN = 226;

    /// 6502: LDA #105 / LDA #195 / LDA #119 -- "FUEL", "LIGHT YEARS" and the cash line.
    constexpr std::uint8_t FUEL_TOKEN = 105;
    constexpr std::uint8_t LIGHT_YEARS_TOKEN = 195;
    constexpr std::uint8_t CASH_LINE_TOKEN = 119;
  } // namespace

  void StateTokens::Print(std::uint8_t _token, TextSink& _sink)
  {
    switch (_token)
    {
    case 0:
      PrintCash(_sink);
      return;
    case 1:
      PrintGalaxyNumber(_sink);
      return;
    case 2:
      PrintCurrentSystem();
      return;
    case 3:
      // 6502: JMP cpl -- the selected system, from QQ15, and printing it twists the seeds.
      (void)PrintSystemName(m_printer, m_selected);
      return;
    case 4:
      PrintCommanderName(_sink);
      return;
    case 5:
      PrintFuelAndCash(_sink);
      return;
    default:
      return;
    }
  }

  void StateTokens::PrintCash(TextSink& _sink)
  {
    /*
     * 6502: csh -- LDX #3 / pc1: LDA CASH,X / STA K,X / DEX / BPL pc1, then LDA #9 / STA U / SEC /
     * JSR BPRNT, then LDA #226 and FALL THROUGH INTO plf.
     *
     * The fall-through is the part a reading misses. `csh` ends on `LDA #226` with no jump, and the
     * twenty bytes from csh to plf in the assembled build say so exactly -- so the cash is always
     * followed by " CR" AND a newline, and a caller that wanted the bare number would have to print
     * it some other way. Nothing in the game does.
     *
     * The copy is byte for byte in the same order, because CASH and K are both stored most
     * significant first. That is the one place in the commander block where the two conventions
     * agree, and it is why the loop is a straight copy rather than a reversal.
     */
    NumberWorkspace work;
    const std::size_t cash = static_cast<std::size_t>(Field::Cash);
    for (std::size_t index = 0; index < 4; ++index)
    {
      work.k[index] = m_commander.bytes[cash + index];
    }
    work.u = CASH_WIDTH;

    PrintNumber(_sink, work, true);

    PrintThenNewline(m_printer, CREDITS_TOKEN);
  }

  void StateTokens::PrintGalaxyNumber(TextSink& _sink)
  {
    /*
     * 6502: tal -- CLC / LDX GCNT / INX / JMP pr2.
     *
     * One-based on screen and zero-based in the block, which is why the INX is here and not at
     * every call site. The CLC is the "no decimal point" argument to pr2 rather than arithmetic.
     */
    const std::uint8_t number = static_cast<std::uint8_t>(m_commander.At(Field::GalaxyNumber) + 1u);
    PrintByteValue(_sink, number, false);
  }

  void StateTokens::PrintCurrentSystem()
  {
    /*
     * 6502: ypl -- BIT MJ / BMI ypl16 / JSR TT62 / JSR cpl, then FALL THROUGH into TT62 again.
     *
     * One swap loop used twice: once by JSR to put the current system's seeds where cpl reads them,
     * and once by falling into it to put them back. Elegant, and it has a consequence.
     *
     * PRINTING A NAME TWISTS THE SEEDS. cpl leaves QQ15 holding something different from what it
     * was handed, so the swap back does not restore the current system -- it stores the TWISTED
     * seeds into QQ2. The port reproduces that rather than tidying it, because a caller that
     * printed the current system's name twice would otherwise get two different answers from the
     * game and one from the port.
     *
     * In witchspace there is no system, and the routine prints nothing at all.
     */
    if (m_misJumped)
    {
      return;
    }

    SystemSeeds swapped = m_current;
    m_current = m_selected;
    m_selected = swapped;

    (void)PrintSystemName(m_printer, m_selected);

    swapped = m_current;
    m_current = m_selected;
    m_selected = swapped;
  }

  void StateTokens::PrintCommanderName(TextSink& _sink)
  {
    /*
     * 6502: cmn -- LDY #0 / QUL4: LDA NAME,Y / CMP #13 / BEQ / JSR TT26 / INY / BNE QUL4.
     *
     * The terminator is a carriage return rather than a length, and the loop's own bound is Y
     * wrapping to zero -- so a name with no carriage return in it would print 256 characters and
     * stop. The block reserves eight bytes, so this reads at most that many before the terminator
     * it is guaranteed to find.
     *
     * The characters go through TT26 rather than TT27, so they are printed as they are and the case
     * flags do not touch them.
     */
    for (std::size_t index = 0; index < m_name.size(); ++index)
    {
      if (m_name[index] == NAME_TERMINATOR)
      {
        return;
      }
      _sink.Put(m_name[index]);
    }
  }

  void StateTokens::PrintFuelAndCash(TextSink& _sink)
  {
    /*
     * 6502: fwl -- LDA #105 / JSR TT68 / LDX QQ14 / SEC / JSR pr2 / LDA #195 / JSR plf, then FALL
     * THROUGH into PCASH, which is LDA #119 / BNE TT27.
     *
     * That last instruction is a branch used as a jump: A has just been loaded with 119, which is
     * not zero, so the BNE is always taken and saves a byte over a JMP.
     *
     * Token 119 contains control code 0, so this routine prints the cash by way of the token
     * printer rather than by calling csh -- which means control code 5 contains control code 0, and
     * the recursion is the game's rather than the port's.
     */
    PrintThenColon(m_printer, FUEL_TOKEN);

    // 6502: LDX QQ14 / SEC / JSR pr2 -- the fuel is in tenths of a light year, so it prints with a
    // decimal point in a width of three.
    PrintByteValue(_sink, m_commander.At(Field::Fuel), true);

    PrintThenNewline(m_printer, LIGHT_YEARS_TOKEN);
    m_printer.Print(CASH_LINE_TOKEN);
  }

} // namespace Elite
