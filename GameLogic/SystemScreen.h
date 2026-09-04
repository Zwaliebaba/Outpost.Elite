#pragma once

#include "MarketScreen.h"
#include "Universe.h"

#include <cstdint>

namespace Elite
{

/*
 * The Data on System screen (slice 2a).
 *
 * 6502: TT25, with TT146, TT70 and the four species labels it branches through. 2a's row deferred
 * this as "cursor and canvas work" and it is neither: every line of it is a token, a number or a
 * seed bit, and the only thing it reaches outside GameLogic for is TRADEMODE -- the same seam the
 * four trading screens and the status screen already use. Plan section 6.12's pattern, for the
 * fifth time.
 */

/// 6502: TT25's `LDA #1 / JSR TRADEMODE` -- QQ11 = 1, which is this screen's view number.
inline constexpr std::uint8_t DATA_ON_SYSTEM_VIEW = 1;

/*
 * 6502: TT25 -- print everything the game knows about one system.
 *
 * The screen takes its system as ARGUMENTS rather than finding it, because the original does: the
 * C64's TT25 never calls TT111, so QQ3 to QQ8 and QQ15 are whatever the caller last left there.
 * On the docked key path that is the short-range chart's crosshairs; on arrival it is the system
 * just jumped to.
 *
 * Three things in it are worth knowing before reading it.
 *
 * THE ECONOMY IS TWO WORDS FROM ONE NUMBER, and the second is not a lookup. `QQ3 >> 2` picks
 * "Industrial" or "Agricultural"; the first word comes from `(QQ3 + 1) >> 1`, and where that is 2
 * the routine jumps to TT70 and prints "Mainly" instead -- and TT70 jumps back INTO the middle of
 * the second word's code, skipping the first word's ADC. So economies 2 and 3 read "Mainly
 * Industrial" and "Mainly Agricultural", and the branch that produces them is a comparison
 * against a shifted value rather than anything that names an economy.
 *
 * THE INHABITANTS ARE FOUR OPTIONAL WORDS, each one skipped when its own three bits come out too
 * high. Size, colour and appearance are printed only when their index is below 3, 6 and 6
 * respectively; the noun always prints. So "Blue Fat Insects" and "Slimy Frogs" come out of the
 * same four instructions, and a system whose seeds are unlucky gets a bare noun. Bit 7 of QQ15+4
 * decides whether any of it runs at all: clear means human colonials, and the whole block is
 * skipped.
 *
 * AND THE RADIUS IS A SIXTEEN-BIT NUMBER ASSEMBLED FROM TWO SEED BYTES. The low byte is QQ15+3 as
 * it stands and the high byte is the bottom nibble of QQ15+5 plus eleven -- so every system's
 * radius is between 2,816 and 6,911 km, which is why no planet in Elite is a gas giant and none
 * is an asteroid.
 */
void SystemDataScreen(TradeScreen& _screen, SystemSeeds& _seeds, const SystemData& _data,
                      std::uint16_t _distance) noexcept;

/*
 * 6502: TT146 -- the distance line, and the branch that decides there is not one.
 *
 * A distance of zero is the system you are already at, and then the routine does not print a
 * blank line: it jumps to INCYC, which moves the cursor down WITHOUT a newline. The other exit
 * runs off the end of TT63 into TT60, so the light years, the cursor move, sentence case and a
 * newline all come from a fall-through rather than from a call.
 *
 * Exposed separately because it is not only this screen's: `T95` in TT102 ends `JMP TT146`, so
 * moving the crosshairs on a chart prints the distance through the same routine.
 */
void PrintDistanceLine(TokenPrinter& _printer, CharacterPrinter& _characters, TextState& _text,
                       std::uint16_t _distance) noexcept;

} // namespace Elite
