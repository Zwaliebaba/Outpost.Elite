#pragma once

#include <cstdint>

#include "Canvas.h"
#include "ExtendedTokens.h"
#include "TextPrint.h"
#include "Tokens.h"

namespace Elite
{

/*
 * In-flight messages (slice 3d-c).
 *
 * The line of text that appears under the space view -- "INCOMING MISSILE", "E.C.M. SYSTEM
 * DESTROYED", the system name on arrival. One at a time, on a timer, and drawn by the same token
 * printer every docked screen uses.
 */

/// 6502: LDA #16 / STA YC -- the row a message uses on the space view. Every other view keeps
/// whatever `CLYNS` left, which is 21, because the `BIT` eats the store (§6.66).
inline constexpr std::uint8_t MESSAGE_ROW_SPACE_VIEW = 16;

/// 6502: recursive token 93, " DESTROYED", which `mes9` appends when `de` says to.
inline constexpr std::uint8_t TOKEN_DESTROYED = 253;

/// 6502: LDY #20 / STY DLY -- twenty frames, which is how long a message stays up.
inline constexpr std::uint8_t MESSAGE_FRAMES = 20;

/*
 * 6502: mes9 -- print a token, and " DESTROYED" after it if `de` says so.
 *
 * `LSR de` both TESTS and CONSUMES the flag: the shift is the test, so a second call prints the
 * token alone. That is what makes erasing a "DESTROYED" message work -- the erase pass re-prints
 * the same token and `de` has already been shifted out of the way.
 */
void PrintMessageToken(TokenPrinter& _printer, MessageState& _message, std::uint8_t _token) noexcept;

/*
 * 6502: MESS -- put a message on screen, and me1, which erases the old one first.
 *
 * `me1` is `STX DLY / PHA / LDA MCH / JSR mes9 / PLA` and then falls into `MESS`, so "erase the
 * one that is up and show this one" is one instruction stream rather than two calls.
 *
 * THE ROW IS 16 ON THE SPACE VIEW AND 21 EVERYWHERE ELSE, and the second of those is not what the
 * source says. `LDA #25 / EQUB &2C / .infrontvw / STA YC` puts the `BIT` where it eats the STORE
 * rather than the load, so the 25 is loaded and thrown away and the row is whatever `CLYNS` left
 * (§6.66). Reproduced, not fixed (ADR-003).
 */
void ShowMessage(Canvas& _canvas, TokenPrinter& _printer, TextState& _text,
                 ExtendedTextState& _extended, MessageState& _message, std::uint8_t _token,
                 std::uint8_t _view) noexcept;

} // namespace Elite
