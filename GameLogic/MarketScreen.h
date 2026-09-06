#pragma once

#include "Commander.h"
#include "ExtendedTokens.h"
#include "Market.h"
#include "Rng.h"
#include "TextPrint.h"
#include "Tokens.h"

#include <cstdint>

namespace Elite
{

  struct Universe; // Universe.h -- forward, because it names types these headers declare
  struct Ports;    // Ports.h, likewise

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
  /*
   * `TradeScreenEffects` WAS HERE AND IS NOT ANY MORE (M3-b-3b).
   *
   * All four of its methods were in front of routines the library already had, and the executable's
   * implementations were forwarding calls with a null check in front of them:
   *
   *   `ClearToView`       is `TT66`, which is `Elite::SetUpScreen` since slice 3d-d-iii-a.
   *   `SetUpTradeScreen`  is `TRADEMODE`, which is `TT66` and then `FLKB` -- the flush is the only
   *                       part that is not the library's, and it is `LineEntryEffects`' until
   *                       M3-b-3c makes it `Keyboard::Flush`.
   *   `ClearBottomRows`   is `CLYNS`, which is `Elite::ClearMessageRows`.
   *   `BeepAndPause`      is `dn2` -- `JSR BEEP / LDY #50 / JMP DELAY` -- and both halves exist:
   *                       `Elite::Beep` since slice 5a and `Presenter::WaitFrames` since this one.
   *
   * THE TEXT STATE `TTX66` ENDS ON STAYS THE ROUTINE'S, and the comment that used to be here is
   * worth keeping because it took a redrawing screen to see it: the equipment shop loops back to
   * the top after every purchase and carries the cursor and the case flags over from the first
   * pass, so a caller that reset them would print its title in the wrong place and in the wrong
   * case. `SetUpScreen` does what `TTX66` does, which is why the callers do not.
   */

  /// 6502: dn2's `LDY #50` -- fifty VERTICAL SYNCS and not a second (§6.17), which is why
  /// `Presenter::WaitFrames` counts frames.
  inline constexpr std::uint8_t BEEP_PAUSE_FRAMES = 50;

  /*
   * Everything a trading screen prints, draws and reads through.
   *
   * One struct because the alternative is an eleven-argument function written three times. The
   * members are the seams and the state the screens share; what varies between them stays an
   * argument.
   */
  // `TradeScreen` was these seven references: the three printers and the sink are `Ports`', the
  // text state and the generator are `Universe`'s, and the keyboard and `TRADEMODE` are `Ports`'
  // two docked seams. It went in M3-a-3.

  /// 6502: QQ11 -- which trading screen this is. The value decides whether the cargo listing offers
  /// each item for sale or only lists it, so it is an argument rather than a constant.
  inline constexpr std::uint8_t BUY_CARGO_VIEW = 2;
  inline constexpr std::uint8_t SELL_CARGO_VIEW = 4;
  inline constexpr std::uint8_t INVENTORY_VIEW = 8;
  inline constexpr std::uint8_t EQUIP_SHIP_VIEW = 32;

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
  void BuyScreen(Universe& _universe, Ports& _ports, bool _misJumped) noexcept;

  /*
   * 6502: TT210 -- list what is in the hold, and on the Sell Cargo screen offer each item for sale.
   *
   * ONE routine for two screens, told apart by `LDA QQ11 / CMP #4`. The inventory screen falls into
   * it from TT213 with QQ11 = 8 and gets a list; the sell screen enters at the top with QQ11 = 4
   * and gets a list with a "SELL (Y/N)?" after every line. That is why this takes the view rather
   * than being written twice.
   *
   * The selling half has two details worth knowing. It prints the item's line a second time with
   * PRINTING SWITCHED OFF -- `LDX #255 / STX QQ17 / JSR TT151` -- purely to leave the price in
   * QQ24, so a routine whose job is to print is called for its arithmetic. And because every price
   * goes through `var`, that call also zeroes Alien Items' availability (§6.16): SELLING anything
   * makes them unavailable, which is the fourth place this one side effect has surfaced.
   *
   * A refused quantity jumps to NWDAV4, which prints "ITEM?", beeps, and re-enters at the TOP of
   * the item's line -- so the whole line is printed again, not just the question.
   *
   * The Trumble tail at the end runs on the inventory screen only. Its extended tokens are all
   * blank in this version of Elite, so nothing of it is visible except the count and a possible
   * "s" -- but it calls DORND, so it moves the random state, and that is observable.
   */
  void ListCargo(Universe& _universe, Ports& _ports,
                 std::uint8_t _view) noexcept;

  /*
   * 6502: TT213 -- the inventory screen, which FALLS INTO TT210.
   *
   * A title, a rule, the fuel and cash lines, and the words "LARGE CARGO BAY" when the hold is a
   * big one. That last test is `LDA CRGO / CMP #26`, and 26 sits between the two capacities as they
   * are STORED rather than as they are described: a standard hold is 22 and a large one 37, because
   * CRGO holds two more than the tonnage it stands for (§6.15).
   *
   * The rule is NLIN4, which is the canvas's rather than this routine's, so a caller draws
   * DrawSeparator at row 19 -- the same split the market screen already uses for NLIN3.
   */
  void InventoryScreen(Universe& _universe, Ports& _ports) noexcept;

} // namespace Elite
