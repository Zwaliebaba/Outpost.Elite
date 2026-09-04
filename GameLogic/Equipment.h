#pragma once

#include "Commander.h"
#include "MarketScreen.h"

#include <cstdint>

namespace Elite
{

/*
 * The equipment shop (slice 2c).
 *
 * 6502: EQSHP, with prx, qv, eq and refund. A list of what this station sells, a number typed in,
 * and a chain of thirteen comparisons that fits whatever was bought.
 */

/// 6502: PRXS has fourteen entries -- the twelve the cassette version sells plus the military and
/// mining lasers the C64 build adds.
inline constexpr int EQUIPMENT_ITEM_COUNT = 14;

/*
 * 6502: prx -- the price of one item, in tenths of a credit.
 *
 * `prx-3` is a second entry point three bytes earlier that decrements A first, because the shop
 * numbers its items from one on screen and from zero in the table.
 */
[[nodiscard]] std::uint16_t EquipmentPrice(std::uint8_t _item, std::uint8_t _fuel) noexcept;

/*
 * 6502: the four instructions before EQL1 -- LDA #70 / SEC / SBC QQ14 / ASL A / STA PRXS.
 *
 * Fuel is the only price the game computes, and it computes it into the TABLE rather than into a
 * variable: what you pay is what is missing from the tank, at two credits a light year, and the
 * doubling is the price. So `prx` reads a table one of whose entries was written moments ago,
 * which is why EquipmentPrice takes the fuel level.
 */
[[nodiscard]] std::uint16_t FuelPrice(std::uint8_t _fuel) noexcept;

/*
 * 6502: qv -- the four-view menu, and which one the player picks.
 *
 * Clears the screen first ONLY when the tech level is 8 or more, because a station that sells
 * enough equipment has a list long enough to collide with the menu. A key that is not 0 to 3
 * clears the bottom rows and asks again, for ever.
 */
[[nodiscard]] std::uint8_t ChooseView(TradeScreen& _screen, std::uint8_t _techLevel) noexcept;

/*
 * 6502: refund -- fit a laser, and give back what the old one cost.
 *
 * The refund is by TYPE rather than by price: the old laser's power byte is matched against the
 * four the game sells and the matching item's price is added back. A power that matches none of
 * them falls through to the mining laser's price, which is the same "anything else is the last
 * one" shape the status screen uses to name lasers.
 */
void Refund(CommanderBlock& _commander, std::uint8_t _view, std::uint8_t _newPower, std::uint8_t _fuel) noexcept;

/*
 * 6502: EQSHP -- the Equip Ship screen.
 *
 * A loop: show what is for sale, take a number, fit it, print the cash, and start again. It ends
 * three ways, and only one of them is quiet.
 *
 *   * Nothing entered, or a number too large: straight to the docking bay, no sound.
 *   * An item already fitted, or not enough cash: a one-line complaint, a beep, and out.
 *   * A letter: gnum's `JMP BAY2`, which leaves without going through either.
 *
 * WHAT THE STATION SELLS is `tek + 3`, capped at 14 -- so a tech level 0 system sells the first
 * three items and anything from tech level 9 up sells all thirteen. The cap is `CMP #12 / BCC /
 * LDA #14`, which jumps from 11 to 14 rather than counting up, so no station ever sells exactly
 * twelve or thirteen items.
 *
 * THE PRICE IS TAKEN BEFORE THE ITEM IS CHECKED. `JSR eq` subtracts, and the "already fitted"
 * branches then hand it back with MCASH -- so buying an escape pod you already own moves the
 * money out and back rather than never moving it.
 */
void EquipShipScreen(TradeScreen& _screen, CommanderBlock& _commander, std::uint8_t _techLevel) noexcept;

} // namespace Elite
