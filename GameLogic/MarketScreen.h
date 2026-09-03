#pragma once

#include "Commander.h"
#include "ExtendedTokens.h"
#include "Market.h"
#include "TextPrint.h"
#include "Tokens.h"

#include <cstdint>

namespace Elite
{

/*
 * The docked trading screens (slice 2c).
 *
 * These are the routines the slice first deferred as "loops around gnum, so they need the key
 * dispatch and land with 2e". That was the same mistake plan section 6.12 records twice already:
 * reading a dependency as ownership. What they wait on is a KEY, and a key is a seam -- the same
 * shape as the circles the charts were ported without.
 */

/*
 * What a trading screen needs from outside GameLogic.
 *
 * `ClearBottomRows` is declared here as well as on `ChartEffects`, deliberately. They are two
 * independent statements of what a screen needs rather than one interface pretending to be
 * shared, and the executable satisfies both with one object.
 */
class TradeScreenEffects
{
public:
  virtual ~TradeScreenEffects() = default;

  /*
   * 6502: TRADEMODE -- TT66 (set QQ11, clear the screen, draw the border box), FLKB (flush the
   * keyboard buffer) and DOVDU19 (a palette change this build does not act on).
   *
   * The PIXELS are the seam's. The text state TTX66 ends on -- column 1, row 1, ALL CAPS -- is
   * set by the caller below, the same split CLYNS already uses. What TTX66 also does to the ball
   * line heap, the laser, DLY and `de` is flight state and belongs to phase 3.
   */
  virtual void SetUpTradeScreen(std::uint8_t _view) = 0;

  /// 6502: CLYNS -- clear the bottom three text rows of the upper screen.
  virtual void ClearBottomRows() = 0;

  /*
   * 6502: dn2 -- JSR BEEP / LDY #50 / JMP DELAY.
   *
   * Fifty VERTICAL SYNCS, not a duration: DELAY is one of the three routines in the C64 build
   * that calls WSCAN (§6.17), so this is one second on PAL and five sixths of one on NTSC. A
   * presenter that implemented it as a wall-clock second would be right on one machine only.
   */
  virtual void BeepAndPause() = 0;
};

/*
 * Everything a trading screen prints, draws and reads through.
 *
 * One struct because the alternative is an eleven-argument function written three times. The
 * members are the seams and the state the screens share; what varies between them stays an
 * argument.
 */
struct TradeScreen
{
  TokenPrinter& printer;
  CharacterPrinter& characters;
  TextState& text;
  KeySource& keys;
  TradeScreenEffects& effects;
};

/*
 * 6502: TT219 -- the buy screen.
 *
 * Prints the market a line at a time and, for every item with any stock, asks how much. The
 * asking is a RETRY LOOP rather than a single question: a quantity that is too large, will not
 * fit, or cannot be afforded prints a one-word complaint, beeps, and asks again for the SAME
 * item. Only a letter gets out, through gnum's `JMP BAY2`.
 *
 * Three things worth knowing before reading it.
 *
 * A quantity of ZERO skips the room check. `LDA R / BEQ P%+4 / BCS Tc` steps over the branch that
 * would have complained, so buying nothing always succeeds -- it costs nothing, adds nothing, and
 * does not even beep, because `PLA / BEQ TT222` skips the confirmation.
 *
 * The purchase is COMMITTED BEFORE the cash is checked. `JSR LCASH` subtracts, and only then does
 * `BCC Tc` notice it could not be afforded -- LCASH having already put the money back by falling
 * into MCASH. So the commander is briefly in debt inside a routine that reports failure.
 *
 * And the market is changed by PRINTING it, not only by buying: `var` zeroes Alien Items'
 * availability on every price it computes (§6.16), so the seventeenth line always offers nothing.
 */
void BuyScreen(TradeScreen& _screen, CommanderBlock& _commander, MarketState& _market,
               std::uint8_t _economy, bool _misJumped) noexcept;

} // namespace Elite
