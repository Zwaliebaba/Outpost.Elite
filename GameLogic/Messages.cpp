#include "pch.h"

#include "Messages.h"

#include "EliteTypes.h"

namespace Elite
{

void PrintMessageToken(TokenPrinter& _printer, MessageState& _message, std::uint8_t _token) noexcept
{
  _printer.Print(_token); // 6502: mes9 -- JSR TT27

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

  _printer.Print(TOKEN_DESTROYED); // 6502: LDA #253 / JMP TT27
}

void ShowMessage(Canvas& _canvas, TokenPrinter& _printer, TextState& _text,
                 ExtendedTextState& _extended, MessageState& _message, std::uint8_t _token,
                 std::uint8_t _view) noexcept
{
  /*
   * 6502: MESS, and `me1` above it, which is reached by a branch from inside MESS and falls back
   * into it -- so "erase the message that is up, then show this one" is a loop through the same
   * instructions rather than two routines. It runs at most twice, because `me1` opens `STX DLY`
   * with X at zero and the test that sent it there is `CPX DLY`.
   */
  for (;;)
  {
    // 6502: LDA #16 / LDX QQ11 / BEQ infrontvw.
    if (_view != 0u)
    {
      // 6502: JSR CLYNS -- which leaves the cursor on row 21 and clears DLY and de (§6.67).
      ClearMessageRows(_canvas, _printer, _text, _extended, _message);

      /*
       * 6502: LDA #25 / EQUB &2C.
       *
       * The `BIT` eats `STA YC`, not the load below it, so the 25 is put in A and thrown away and
       * the row stays the 21 `CLYNS` left. The source comments the load as "the text row for the
       * message if this is not a space view" and the `EQUB` as "skip the next instruction", and
       * the two cannot both be true. Reproduced rather than fixed (§6.66, ADR-003).
       */
    }
    else
    {
      _text.row = MESSAGE_ROW_SPACE_VIEW; // 6502: infrontvw -- STA YC with A still 16
    }

    // 6502: LDX #0 / STX QQ17 -- and the zero stays in X for the comparison below.
    _text.caseFlags = 0;
    _printer.SetCaseFlags(0);

    _text.column = _message.column; // 6502: LDA messXC / JSR DOXC

    // 6502: PLA / LDY #20 / CPX DLY / BNE me1 -- a message already up has to come off first.
    if (_message.delay == 0u)
    {
      break;
    }

    // 6502: me1 -- STX DLY / PHA / LDA MCH / JSR mes9 / PLA, and then back into MESS.
    _message.delay = 0;
    PrintMessageToken(_printer, _message, _message.token);
  }

  _message.delay = MESSAGE_FRAMES; // 6502: STY DLY
  _message.token = _token;         // 6502: STA MCH

  /*
   * 6502: LDA #%11000000 / STA DTW4 -- justify AND never flush, which turns the print below into
   * a measurement: nothing reaches the screen and `DTW5` comes back holding the width.
   *
   * `DTW5` is pre-loaded with ten when `de` is set, because " DESTROYED" is ten characters and it
   * is printed after the measuring pass rather than during it.
   */
  _extended.justify = 0xC0u;
  _extended.bufferLength = ((_message.append & 1u) != 0u) ? 10u : 0u;

  _printer.Print(_message.token); // 6502: LDA MCH / JSR TT27 -- into the buffer, not the screen

  // 6502: LDA #32 / SEC / SBC DTW5 / LSR A / STA messXC / JSR DOXC -- centre it in 32 columns.
  _message.column = static_cast<std::uint8_t>(SubtractWithCarry(32u, _extended.bufferLength, true).value >> 1);
  _text.column = _message.column;

  // 6502: JSR MT15 -- justification off and the buffer thrown away, so the print below draws.
  StopJustifying(_extended);

  // 6502: LDA MCH, and then no RTS: MESS falls into mes9.
  PrintMessageToken(_printer, _message, _message.token);
}

} // namespace Elite
