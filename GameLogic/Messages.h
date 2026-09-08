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

  /// The row a message uses on the space view. Every other view keeps whatever `CLYNS`
  /// left, which is 21, because a data byte swallows the store (§6.66).
  inline constexpr std::uint8_t MESSAGE_ROW_SPACE_VIEW = 16;

  /// Recursive token 93, " DESTROYED", which `mes9` appends when `de` says to.
  inline constexpr std::uint8_t TOKEN_DESTROYED = 253;

  /// DLY set to twenty frames, which is how long a message stays up.
  inline constexpr std::uint8_t MESSAGE_FRAMES = 20;

  /*
   * Print a token, and " DESTROYED" after it if `de` says so.
   *
   * `LSR de` both TESTS and CONSUMES the flag: the shift is the test, so a second call prints the
   * token alone. That is what makes erasing a "DESTROYED" message work -- the erase pass re-prints
   * the same token and `de` has already been shifted out of the way.
   */
  void PrintMessageToken(TokenPrinter& _printer, MessageState& _message, std::uint8_t _token) noexcept;

  /*
   * Put a message on screen, and me1, which erases the old one first.
   *
   * `me1` stores the new delay, saves the new token, erases the message currently up, restores
   * the token and then falls into `MESS` -- so "erase the one that is up and show this one" is
   * one instruction stream rather than two calls.
   *
   * THE ROW IS 16 ON THE SPACE VIEW AND 21 EVERYWHERE ELSE, and the second of those is not what
   * the source says. A data byte assembling as a three-byte instruction sits where it swallows
   * the STORE rather than the load, so the 25 is loaded and thrown away and the row is whatever
   * `CLYNS` left (§6.66). Reproduced, not fixed (ADR-003).
   */
  void ShowMessage(Canvas& _canvas, TokenPrinter& _printer, TextState& _text, ExtendedTextState& _extended, MessageState& _message,
                   std::uint8_t _token, std::uint8_t _view, Picture* _picture = nullptr) noexcept;

} // namespace Elite
