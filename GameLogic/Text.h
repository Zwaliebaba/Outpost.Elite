// GameLogic/Text.h
#pragma once

#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's text routines, ported (plan §5 Phase 3, ADR-010): printing text and numbers. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv. The routines de-assembled so far (ADR-012)
// take values and give values back, and their entries, at the end, keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> TextEntries() noexcept;

/// ShowShipIdentity (CS:364D): 'CLASS: <class> TYPE: <type>' for ship type AL and class AH on the message line for 30
/// frames; class 3 is Simple unless IsDebrisType says the slot at DI is debris, a class 4 of type 5 is a Hermit, and
/// type 1Ch the police. Out: AX = shipIdentityText, BX, CX = 0 and DI past the copies.
void ShowShipIdentity(Guest& _guest);

/// ReadTextLine (CS:7694): a line typed into the buffer at SI, at most CL characters, echoed at DI with a blinking
/// cursor; Enter ends it, Backspace deletes, letters are lower case unless Shift is held. Waits for keys. Out: BX the
/// length; AX, CX, DX clobbered.
void ReadTextLine(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// What BlankLeadingZeros leaves of a number's text.
struct BlankedZeros
{
  std::uint16_t firstKept; ///< the first character it kept, or the one after the last it blanked
  std::uint16_t triesLeft; ///< how many more it could have blanked: 0 once it has blanked as many as it may
};

/// Where PrintTextModeString stops, and DrawViewString and DrawScreenString.
struct PrintedText
{
  std::uint16_t end;      ///< the text's NUL
  std::uint16_t nextCell; ///< the text page's cell after the last it wrote, or the place after the last character drawn
};

/// Where PrintTextLines and PrintCountedTextLines stop.
struct PrintedLines
{
  std::uint16_t end;      ///< past the last line's NUL
  std::uint16_t nextLine; ///< the text page's cell a row below the last line's first
};

/// Where RedrawInputLine's two prints stop: the line typed, and the cursor after it.
struct RedrawnInputLine
{
  PrintedText line;
  PrintedText cursor;
};

/// Where DrawSmallViewChar draws a letter: the byte of the space-view buffer, and the shift, 0 or 4, of the
/// letter's left edge in it.
struct SmallViewPlace
{
  std::uint16_t at;
  std::uint8_t shift;
};

/// Where DrawSmallViewString stops.
struct DrawnSmallText
{
  std::uint16_t end;   ///< the text's NUL
  SmallViewPlace next; ///< the place of the letter after the last drawn
};

/// DrawViewChar (CS:3130): the 8x8 glyph of _character, in _ink on textPaperPattern, at DS:_at in the space-view
/// buffer. Returns the place of the next character, two bytes on.
std::uint16_t DrawViewChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _at);

/// DrawViewString (CS:31EC): DrawViewChar for each character of the text at DS:_text, from DS:_at.
PrintedText DrawViewString(GameState& _state, std::uint16_t _text, std::uint16_t _ink, std::uint16_t _at);

/// DrawScreenChar (CS:31F9): the 8x8 glyph of _character, in _ink on textPaperPattern, at _segment:_cell, an even line
/// of the CGA's memory. Returns the place of the next character, two bytes on.
std::uint16_t DrawScreenChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell);

/// DrawScreenString (CS:32D8): DrawScreenChar for each character of the text at DS:_text, from _segment:_cell.
PrintedText DrawScreenString(GameState& _state, std::uint16_t _text, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell);

/// What DrawScreenString leaves in AX, for the entries of the routines that call it, whose originals leave it there:
/// the NUL in AL, over the last row of the last glyph it drew in _ink, or over _ax when it drew none. _text is where it
/// began and _drawn what it returned.
[[nodiscard]] std::uint16_t DrawnScreenStringAx(const GameState& _state, std::uint16_t _text, PrintedText _drawn, std::uint16_t _ink,
                                                std::uint16_t _ax);

/// FormatDecimal5 (CS:3407): _value as five ASCII digits at DS:_digits. Returns what is left of it below the last
/// divisor: its units.
std::uint16_t FormatDecimal5(GameState& _state, std::uint16_t _value, std::uint16_t _digits);

/// BlankLeadingZeros (CS:3432): up to _most of the leading characters below '1' at DS:_text become spaces; a _most of
/// 0 lets 65,536 go, as LOOP counts.
BlankedZeros BlankLeadingZeros(GameState& _state, std::uint16_t _text, std::uint8_t _most);

/// What BlankLeadingZeros returned for the digits at DS:_text, _most of 1 to 255, read back from the spaces it left
/// there: for the entries of the routines that call it, whose originals leave its DI and CX.
[[nodiscard]] BlankedZeros BlankedLeadingZeros(const GameState& _state, std::uint16_t _text, std::uint8_t _most);

/// DrawSmallViewChar (CS:349A): the 5-row letter _letter ('A'-'Z') in _ink over the space-view buffer at _place, the
/// buffer kept around the letter's shape. Returns the next letter's place, six pixels on.
SmallViewPlace DrawSmallViewChar(GameState& _state, std::uint8_t _letter, std::uint16_t _ink, SmallViewPlace _place);

/// DrawSmallViewString (CS:3527): DrawSmallViewChar for each letter of the text at DS:_text, from _position, which is
/// (line+1)*256 + x: the buffer's byte _position/4, and the shift bit 1 of x makes.
DrawnSmallText DrawSmallViewString(GameState& _state, std::uint16_t _text, std::uint16_t _ink, std::uint16_t _position);

/// FormatCredits (CS:3543): creditsTenths into creditBalanceText, as ten digits with up to eight leading zeros
/// blanked and a decimal point before the last.
void FormatCredits(GameState& _state);

/// UpdateMessageLine (CS:35A3): the message line's work for a frame: UpdateWarnings; then while the message drawn has frames
/// left, one fewer; else, once they run out, the view's name as the message, and the message shown, the line cleared a row at a
/// time forwards or, _backwards, down, unless it is shown already. Returns whether it drew one.
bool UpdateMessageLine(GameState& _state, bool _backwards);

/// ClearMessageLine (CS:3609): zeroes the message line's eight rows of 32 words from _segment:0058, each row forwards
/// or, _backwards, down (REP STOSW with the direction flag set).
void ClearMessageLine(GameState& _state, std::uint16_t _segment, bool _backwards);

/// ShowBountyMessage (CS:3626): _tenths of a credit into bountyText as "nnnn.n", shown for 20 frames.
void ShowBountyMessage(GameState& _state, std::uint16_t _tenths);

/// What ShowBountyMessage's BlankLeadingZeros returned for the bounty's digits, read back from bountyText: for the entries and
/// the register code of the routines that call it, whose originals leave its CX.
[[nodiscard]] BlankedZeros BlankedBountyZeros(const GameState& _state);

/// PrintTextModeString (CS:60D2): the text at DS:_text to the text page at B800:_cell, in textAttribute.
PrintedText PrintTextModeString(GameState& _state, std::uint16_t _text, std::uint16_t _cell);

/// ToggleMenuRowHighlight (CS:6328): SwapTextAttributeNibbles, and the attribute that makes into the 36 attribute bytes
/// of a menu row at _segment:_row. Returns that attribute.
std::uint8_t ToggleMenuRowHighlight(GameState& _state, std::uint16_t _segment, std::uint16_t _row);

/// ClearDockedMessageLine (CS:6553): 19 spaces from text offset 78h at _segment, characters only.
void ClearDockedMessageLine(GameState& _state, std::uint16_t _segment);

/// SwapTextAttributeNibbles (CS:6580): textAttribute's nibbles exchanged, and the attribute that makes.
std::uint8_t SwapTextAttributeNibbles(GameState& _state);

/// PrintCountedTextLines (CS:65FA): PrintTextLines for the lines after the count byte at DS:_text.
PrintedLines PrintCountedTextLines(GameState& _state, std::uint16_t _text, std::uint16_t _cell);

/// FormatTenths (CS:69B3): _tenths into priceText as "nnnn.n".
void FormatTenths(GameState& _state, std::uint16_t _tenths);

/// PrintTextLines (CS:6DDE): _lines lines from DS:_text, each by PrintTextModeString a row of the text page below the
/// one before, from B800:_cell; 65,536 for 0, as LOOP counts.
PrintedLines PrintTextLines(GameState& _state, std::uint16_t _text, std::uint16_t _cell, std::uint16_t _lines);

/// ToggleInputCursor (CS:7727): the input cursor's character flips between blank and block.
void ToggleInputCursor(GameState& _state);

/// RedrawInputLine (CS:773A): a NUL after the _length characters typed at DS:_buffer, then they and inputCursorText after them,
/// each by PrintStringForLayout from _segment:_cell in the ink FFFFh.
RedrawnInputLine RedrawInputLine(GameState& _state, std::uint16_t _buffer, std::uint16_t _length, std::uint16_t _segment,
                                 std::uint16_t _cell);

/// PrintStringForLayout (CS:7750): the text at DS:_text by PrintTextModeString at B800:_cell in the text layout, and by
/// DrawScreenString in _ink at _segment:_cell otherwise.
PrintedText PrintStringForLayout(GameState& _state, std::uint16_t _text, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its
// results back there. The registers the contract leaves to the routine it hands to Guest::Clobber, unless a
// caller reads what the original leaves in one: then the entry leaves that, and the contract compares it.

void DrawViewCharEntry(Guest& _guest);
void DrawViewStringEntry(Guest& _guest);
void DrawScreenCharEntry(Guest& _guest);
void DrawScreenStringEntry(Guest& _guest);
void FormatDecimal5Entry(Guest& _guest);
void BlankLeadingZerosEntry(Guest& _guest);
void DrawSmallViewCharEntry(Guest& _guest);
void DrawSmallViewStringEntry(Guest& _guest);
void FormatCreditsEntry(Guest& _guest);
void UpdateMessageLineEntry(Guest& _guest); ///< Out: ES = B800h once it draws a message.
void ClearMessageLineEntry(Guest& _guest);
void ShowBountyMessageEntry(Guest& _guest);
void PrintTextModeStringEntry(Guest& _guest);
void ToggleMenuRowHighlightEntry(Guest& _guest);
void ClearDockedMessageLineEntry(Guest& _guest);
void SwapTextAttributeNibblesEntry(Guest& _guest);
void PrintCountedTextLinesEntry(Guest& _guest);
void FormatTenthsEntry(Guest& _guest);
void PrintTextLinesEntry(Guest& _guest);
void ToggleInputCursorEntry(Guest& _guest);
void RedrawInputLineEntry(Guest& _guest); ///< In: SI the buffer, BX the length, ES:DI the place. Out: AX, and ES in the text layout.
void PrintStringForLayoutEntry(Guest& _guest);

} // namespace Elite
