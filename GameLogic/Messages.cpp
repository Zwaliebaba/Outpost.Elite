#include "pch.h"

#include "Messages.h"

#include "EliteTypes.h"

namespace Elite
{

  void PrintMessageToken(TokenPrinter& _printer, MessageState& _message, std::uint8_t _token) noexcept
  {
    _printer.Print(_token); // 6502: mes9

    /*
     * 6502: LSR de / BCC out -- and `out` contains an `RTS`, so a clear bit 0 ends the routine.
     *
     * The shift is the test AND the consumption: `de` comes back halved, so the SECOND call prints
     * the token alone. That is what makes erasing a "... DESTROYED" message work -- the erase pass
     * re-prints the same token and the flag has already been shifted out of the way.
     */
    const ShiftResult shifted = RotateRight(_message.append, false);
    _message.append = shifted.value;
    if (!shifted.carry)
    {
      return;
    }

    _printer.Print(TOKEN_DESTROYED); // 6502: token 253
  }

  void ShowMessage(Canvas& _canvas, TokenPrinter& _printer, TextState& _text, ExtendedTextState& _extended, MessageState& _message,
                   std::uint8_t _token, std::uint8_t _view, Picture* _picture) noexcept
  {
    /*
     * 6502: MESS, and `me1` above it, which is reached by a branch from inside MESS and falls back
     * into it -- so "erase the message that is up, then show this one" is a loop through the same
     * instructions rather than two routines. It runs at most twice, because `me1` opens by zeroing
     * `DLY` and the test that sent it there is a compare against `DLY`.
     */
    for (;;)
    {
      // 6502: infrontvw -- the row loaded, then the view decides which way to go.
      if (_view != 0u)
      {
        // 6502: CLYNS -- which leaves the cursor on row 21 and clears DLY and de (§6.67).
        ClearMessageRows(_canvas, _printer, _text, _extended, _message, _picture, LayoutForView(_view));

        /*
         * 6502: a load of 25 followed by `EQUB &2C`.
         *
         * The `BIT` eats the store of the row, not the load below it, so the 25 is put in A and
         * thrown away and the row stays the 21 `CLYNS` left. The source comments the load as "the
         * text row for the message if this is not a space view" and the `EQUB` as "skip the next
         * instruction", and the two cannot both be true. Reproduced rather than fixed (§6.66,
         * ADR-003).
         */
      }
      else
      {
        _text.row = MESSAGE_ROW_SPACE_VIEW; // 6502: infrontvw -- the row stored with A still 16
      }

      // 6502: the case flags zeroed -- and the zero stays in X for the comparison below.
      _text.caseFlags = 0;

      _text.column = _message.column; // 6502: messXC through DOXC

      // 6502: me1 -- a message already up has to come off first.
      if (_message.delay == 0u)
      {
        break;
      }

      // 6502: me1 -- `DLY` zeroed, the old token printed through `mes9`, and then back into MESS.
      _message.delay = 0;
      PrintMessageToken(_printer, _message, _message.token);
    }

    _message.delay = MESSAGE_FRAMES; // 6502: DLY
    _message.token = _token;         // 6502: MCH

    /*
     * 6502: both top bits of `DTW4` -- justify AND never flush, which turns the print below into a
     * measurement: nothing reaches the screen and `DTW5` comes back holding the width.
     *
     * `DTW5` is pre-loaded with ten when `de` is set, because " DESTROYED" is ten characters and it
     * is printed after the measuring pass rather than during it.
     */
    _extended.justify = 0xC0u;
    _extended.bufferLength = ((_message.append & 1u) != 0u) ? 10u : 0u;

    _printer.Print(_message.token); // 6502: MCH printed into the buffer, not the screen

    // 6502: thirty-two less the width, halved -- centre it in 32 columns.
    _message.column = static_cast<std::uint8_t>(SubtractWithCarry(32u, _extended.bufferLength, true).value >> 1);
    _text.column = _message.column;

    // 6502: MT15 -- justification off and the buffer thrown away, so the print below draws.
    StopJustifying(_extended);

    // 6502: MCH again, and then no RTS: MESS falls into mes9.
    PrintMessageToken(_printer, _message, _message.token);
  }

} // namespace Elite
