#pragma once

#include "Commander.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

/*
 * The six tokens that print game state rather than text (slice 2c).
 *
 * 6502: TT27's first six branches -- `TAX / BEQ csh / ... / DEX / BEQ fwl`. Tokens 0 to 5 are
 * control codes that reach into the commander and the universe instead of into the token table,
 * and slice 1c-a declared them a seam because nothing that could answer them existed yet. This
 * closes it.
 *
 *   0  csh   the cash, to nine digits with a decimal point, then " CR" and a newline
 *   1  tal   the galaxy number, ONE-BASED -- `LDX GCNT / INX`
 *   2  ypl   the CURRENT system's name, which is not the same as 3
 *   3  cpl   the SELECTED system's name, which slice 2a already ports
 *   4  cmn   the commander's name, up to the carriage return that ends it
 *   5  fwl   the fuel level, a newline, and then the cash -- so 5 contains 0
 *
 * Two of them have behaviour a reading would miss, and both are recorded on the implementations.
 */
class StateTokens : public ValueTokens
{
public:
  /*
   * The seeds are held by REFERENCE and by value in different places for a reason: `ypl` swaps
   * the current system's seeds with the selected system's, prints, and swaps back -- and because
   * printing a name twists the seeds, what comes back is not what went in. So both sets are
   * state this can change, and the caller sees the change.
   */
  StateTokens(TokenPrinter& _printer, TextState& _text, const CommanderBlock& _commander,
              std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name, SystemSeeds& _current,
              SystemSeeds& _selected, bool _misJumped) noexcept
    : m_printer(_printer)
    , m_text(_text)
    , m_commander(_commander)
    , m_name(_name)
    , m_current(_current)
    , m_selected(_selected)
    , m_misJumped(_misJumped)
  {
  }

  void Print(std::uint8_t _token, TextSink& _sink) override;

private:
  void PrintCash(TextSink& _sink);        ///< 6502: csh, which falls into plf
  void PrintGalaxyNumber(TextSink& _sink);///< 6502: tal
  void PrintCurrentSystem();              ///< 6502: ypl
  void PrintCommanderName(TextSink& _sink);///< 6502: cmn
  void PrintFuelAndCash(TextSink& _sink); ///< 6502: fwl, which falls into PCASH

  TokenPrinter& m_printer;
  TextState& m_text;
  const CommanderBlock& m_commander;
  std::span<const std::uint8_t, COMMANDER_NAME_SIZE> m_name;
  SystemSeeds& m_current;
  SystemSeeds& m_selected;
  bool m_misJumped = false;
};

} // namespace Elite
