#pragma once

#include "Commander.h"
#include "ExtendedTokens.h"
#include "Rng.h"
#include "TextPrint.h"

#include <array>
#include <cstdint>

namespace Elite
{

/*
 * Elite's economy (slice 2c, the price model).
 *
 * Seventeen goods, and no price is stored for any of them. A price is the item's base, plus a
 * masked slice of a per-market random byte, adjusted by the system's economy through a gradient
 * that can push either way -- so an agricultural world is cheap for food and dear for machinery
 * without either fact being written down anywhere.
 *
 * The gradient's sign does OPPOSITE things to the two halves of the model. For a price, a
 * negative gradient subtracts; for the quantity on the shelf, a negative gradient ADDS. That is
 * the economics rather than an oversight -- what a world makes cheaply it also has plenty of --
 * and the two branches are forty instructions apart in the original, in routines that otherwise
 * read the same.
 */

/// The number of tradeable goods. The market screen loops to 17, so seventeen it is -- but see
/// GenerateMarket, which fills only sixteen.
inline constexpr int MARKET_ITEM_COUNT = 17;

/// 6502: QQ23 -- four bytes an item, in the order the routines index them.
struct MarketItem
{
  std::uint8_t basePrice = 0;    ///< QQ23+0
  std::uint8_t gradient = 0;     ///< QQ23+1, sign in bit 7 and magnitude in the low five bits
  std::uint8_t baseQuantity = 0; ///< QQ23+2
  std::uint8_t mask = 0;         ///< QQ23+3, how much of the market's random byte reaches this item
};

/// The market as a docked player sees it: what everything costs and how much there is.
struct MarketState
{
  std::uint8_t randomiser = 0; ///< 6502: QQ26, one DORND byte that perturbs the whole market
  std::array<std::uint8_t, MARKET_ITEM_COUNT> price{};
  std::array<std::uint8_t, MARKET_ITEM_COUNT> availability{}; ///< 6502: AVL
};

/// 6502: QQ23 -- the shipped table, read out of the assembled game.
[[nodiscard]] MarketItem MarketItemAt(int _item) noexcept;

/*
 * 6502: var -- the economy's contribution, which is the gradient's magnitude times the economy.
 *
 * The original spells the multiply as repeated addition with `ADC` and no `CLC` inside the loop,
 * so a carry out of one addition feeds the next. With an economy of at most 7 and a magnitude of
 * at most 31 the sum never reaches 256 and the carry never fires -- but the port keeps the chain
 * rather than writing a multiply, because "never" here is a property of the data rather than of
 * the code, and the data is not this function's to assume.
 */
[[nodiscard]] std::uint8_t EconomyAdjustment(std::uint8_t _gradient, std::uint8_t _economy) noexcept;

/*
 * 6502: TT151's arithmetic half -- what one item costs, in tenths of a credit.
 *
 * The printing half of TT151 belongs with the market screen; this is the part that decides the
 * number, and it is the part every other price in the game is derived from.
 */
[[nodiscard]] std::uint8_t MarketPrice(int _item, std::uint8_t _economy, std::uint8_t _randomiser) noexcept;

/*
 * 6502: GVL -- roll a new market, which happens on arrival at every system.
 *
 * Takes the RNG rather than a byte because the original calls DORND itself, and where the
 * randomness comes from is part of the behaviour: a caller that supplied its own byte would give
 * a different market from the same save.
 *
 * Fills sixteen items, not seventeen: the loop's bound is a comparison against 63 on an index
 * that steps by four, so it stops one short of Alien Items.
 *
 * Alien Items are still zeroed, but somewhere unexpected. The original does it inside `var`,
 * which computes the economy adjustment and, on its way out, writes zero to AVL+16 -- so every
 * path that works out a price also enforces that Alien Items cannot be bought. The port zeroes
 * it here instead, because EconomyAdjustment is arithmetic and has no market to reach into. The
 * effect is the same for every caller the game has; if a future one computes a price without
 * generating a market, it has to do this itself.
 */
void GenerateMarket(Rng& _rng, std::uint8_t _economy, MarketState& _outMarket) noexcept;

/*
 * 6502: LCASH -- spend an amount, in tenths of a credit.
 *
 * Returns false when it cannot be afforded, and the cash is then exactly as it was. The original
 * gets there by an unusual route: the four subtractions run unconditionally, and if the top one
 * borrowed the routine FALLS THROUGH into MCASH, which adds the same amount straight back. So
 * the commander is briefly in debt, and no caller can tell.
 */
[[nodiscard]] bool SpendCash(CommanderBlock& _commander, std::uint16_t _tenths) noexcept;

/// 6502: MCASH -- receive an amount. Cannot fail, and returns with the carry clear so that a
/// caller sharing LCASH's exit reads it as "not affordable".
void ReceiveCash(CommanderBlock& _commander, std::uint16_t _tenths) noexcept;

/*
 * 6502: GCASH -- what a quantity costs, in tenths.
 *
 * The multiply times FOUR. Prices are quoted in tenths of a credit but held in units of
 * four-tenths, so every total in the game passes through this and a port that dropped the two
 * shifts would sell everything at a quarter price.
 */
[[nodiscard]] std::uint16_t TotalPrice(std::uint8_t _price, std::uint8_t _quantity) noexcept;

/*
 * 6502: tnpr -- is there room for this much more of an item?
 *
 * Two rules, and which applies depends on the item: the first thirteen are tonne canisters and
 * share the hold's capacity, and the last four -- gold, platinum, gem-stones and alien items --
 * are measured in kilos or grams and are each capped at 200 of their own unit.
 *
 * The tonne path counts the hold and adds ONE MORE than it should, because the `CPX` that chose
 * the path left the carry set and the first `ADC` consumes it. That is not a defect: `CRGO` holds
 * two more than the capacity it describes, and the original's own comment says the pair is
 * deliberate -- "A contains the number of canisters plus 1, while CRGO contains our cargo
 * capacity plus 2". Fix either one alone and the hold is off by a tonne.
 *
 * Tribbles are cargo too. Two hundred and fifty-six of them weigh a tonne, so the HIGH byte of
 * the tribble count is added in, and its addition takes the carry the loop left.
 */
[[nodiscard]] bool CargoFits(const CommanderBlock& _commander, std::uint8_t _item, std::uint8_t _amount) noexcept;

/*
 * 6502: TT152 -- the units an item is sold in, from two bits of its own gradient byte.
 *
 * Three answers and they are not laid out alike. Tonnes print "t" and a space; grams print "g"
 * and a space; kilos print "kg" and NO space, because TT161 falls into TT16a and TT16a's `JMP
 * DASC` returns rather than reaching the space. So the column after the units is one character to
 * the left for the three items sold by the kilo, which is visible on the market screen and is the
 * original's.
 */
void PrintMarketUnits(TokenPrinter& _printer, CharacterPrinter& _characters, std::uint8_t _gradient) noexcept;

/*
 * 6502: TT151 -- one line of the market screen: name, price, and how much there is.
 *
 * The price is recomputed here rather than read from anywhere, which is why MarketPrice exists
 * and why this takes the economy and the randomiser rather than a table of prices. An item with
 * none in stock prints a dash instead of a quantity, at a column of its own.
 *
 * `_misJumped` is MJ, and it makes the whole line print nothing at all -- there is no market in
 * witchspace. It is game state, so it arrives as a value.
 *
 * The market is taken by REFERENCE and printing it changes it. `var`, which every price passes
 * through, writes zero to AVL+16 on its way out -- so working out any price at all makes Alien
 * Items unavailable, and the last line of the screen is always a dash. That is the third place
 * this one side effect has surfaced: GenerateMarket has to reproduce it, EconomyAdjustment
 * deliberately does not, and here it is again.
 */
void PrintMarketItem(TokenPrinter& _printer, CharacterPrinter& _characters, TextState& _text, int _item,
                     std::uint8_t _economy, MarketState& _market, bool _misJumped) noexcept;

/*
 * 6502: TT167 -- the market screen.
 *
 * The screen reset at the top is TRADEMODE, which is TT66 and a keyboard flush; a caller does
 * that first. What is here is the title, the rule, the column headings and the seventeen lines --
 * and it is seventeen, not the sixteen GenerateMarket fills, so Alien Items appear with whatever
 * availability the zeroing in `var` left them.
 */
void PrintMarketScreen(TokenPrinter& _printer, CharacterPrinter& _characters, TextState& _text,
                       std::uint8_t _economy, MarketState& _market, bool _misJumped) noexcept;

/*
 * What one keystroke did to a number the player is typing.
 *
 * 6502: gnum's exits -- and there are FOUR labels for six outcomes, which is why this enum has
 * six entries. `OUT` is the only way out of the number-building path, and it begins `LDA #&10 /
 * STA COL2 / LDA R / RTS`: neither LDA nor STA touches the carry, so the carry a caller reads is
 * whichever comparison branched to OUT.
 *
 *   BCC OUT   (the key was below '0')      carry CLEAR -- the number is finished
 *   BCS OUT   (the value reached 26)       carry SET   -- too big
 *   BCS OUT   (the value passed QQ25)      carry SET   -- too big
 *   NWDAV1/3  ("Y" or "N")                 carry CLEAR, because JSR TT26 exits CLC
 *   falling out of the twelve-key loop     carry CLEAR, same reason
 *
 * That distinction is load-bearing: the buy screen's `JSR gnum / BCS TQ4` re-asks for a quantity
 * on a set carry and accepts the number on a clear one. Reporting one `Complete` for both would
 * give a port that silently accepts a purchase the original refuses.
 */
enum class DigitResult
{
  Accepted,    ///< 6502: TT226 -- the digit went in and the routine asks for another
  Complete,    ///< 6502: OUT with the carry CLEAR -- the number is finished and usable
  TooBig,      ///< 6502: OUT with the carry SET -- 26 or more, or past what is available
  TakeAll,     ///< 6502: NWDAV1 -- "Y", meaning all of what is available
  TakeNone,    ///< 6502: NWDAV3 -- "N"
  LeaveScreen, ///< 6502: BAY2 -- a letter, which abandons the screen entirely
};

/*
 * Where a blocking key read comes from.
 *
 * 6502: TT217 -- "scan the keyboard until a key is pressed". The game BLOCKS here, inside a
 * screen's own loop, and that is a genuine architectural problem for this port rather than a
 * detail: ADR-004 section 1 says GameLogic's input is an `InputFrame`, which is a poll and not a
 * wait. The two cannot both be true of the same code.
 *
 * This slice does not resolve it, and deliberately: the routines that read the keyboard are
 * ported against this seam, exactly as the charts were ported against the seams for the drawing
 * they could not yet do. Whoever builds 2e decides how the seam is driven -- a pumped thread, a
 * coroutine, or rewriting the docked screens as state machines fed by `InputFrame`. The last of
 * those stops being a line-by-line port, which is the cost worth knowing before choosing it.
 */
class KeySource
{
public:
  virtual ~KeySource() = default;

  /// 6502: TT217 -- block until a key is pressed, and return its character.
  virtual std::uint8_t NextKey() = 0;
};

/*
 * 6502: gnum's body, from the instruction after the key read to whichever exit it takes.
 *
 * The loop around this is a keyboard read, so the loop belongs with the key dispatch and the step
 * is what can be compared. Four things about the step are worth knowing.
 *
 * The multiply by ten is `ASL A / STA T / ASL A / ASL A / ADC T / ADC S` -- twice, kept, times
 * eight, add the kept copy back, add the digit -- and NEITHER addition has a `CLC`. Each takes
 * the carry the shift or addition before it produced.
 *
 * "Y" and "N" are accepted whenever the value so far is ZERO, not only on the first keystroke.
 * So typing 0 and then Y is accepted, because `LDX R / BNE` cannot tell them apart.
 *
 * A value of 26 or more refuses further digits. That is a cap on the NUMBER OF DIGITS by
 * proxy -- three digits cannot be typed past 26 -- rather than a cap on the quantity.
 *
 * And the finished number may EXCEED what is available: `CMP QQ25 / BEQ TT226 / BCS OUT` lets an
 * equal value carry on and a larger one finish, so the caller has to check. It is the buy screen
 * that says no, not this.
 */
[[nodiscard]] DigitResult TypeDigit(std::uint8_t& _value, std::uint8_t _key, std::uint8_t _available) noexcept;

/// What gnum returned: the number in R, and which of its exits produced it.
struct NumberEntry
{
  std::uint8_t value = 0;                        ///< 6502: R, which is also what OUT leaves in A
  DigitResult outcome = DigitResult::Complete;
};

/*
 * 6502: gnum -- the whole routine, loop and all.
 *
 * TypeDigit above is one step of this; here is the loop around it. Three things it owns rather
 * than the step:
 *
 * TWELVE keys, from `LDX #12 / STX T1`. Twelve accepted digits in a row fall out of the loop into
 * OUT, so the number ends without the player pressing anything to end it. Since a value of 26 or
 * more refuses further digits, the twelfth key is only reachable by typing digits that keep the
 * value under 26 -- zeros, in practice.
 *
 * THE ECHO. `TT226` prints the key through TT26, and so do the "Y" and "N" paths, so what the
 * player typed appears on screen. The exits that end the number do not echo.
 *
 * THE COLOUR. `LDA #MAG2 / STA COL2` on entry and `LDA #&10 / STA COL2` at OUT, so the digits are
 * typed in purple and the screen goes back to white afterwards -- and it does that on EVERY exit,
 * including the ones that abandon the number.
 */
[[nodiscard]] NumberEntry ReadNumber(KeySource& _keys, CharacterPrinter& _characters, TextState& _text,
                                     std::uint8_t _available) noexcept;

} // namespace Elite
