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

/// DrawViewString (CS:31EC): DrawViewChar for each character of the text at SI. Out: SI at the terminator.
void DrawViewString(Guest& _guest);

/// DrawScreenString (CS:32D8): DrawScreenChar for each character of the text at SI. Out: SI at the terminator, DI past
/// the text.
void DrawScreenString(Guest& _guest);

/// DrawSmallViewString (CS:3527): DrawSmallViewChar for each letter at SI, from DI = (line+1)*256 + x.
void DrawSmallViewString(Guest& _guest);

/// UpdateMessageLine (CS:35A3): the message line's work for one frame. Clobbers all.
void UpdateMessageLine(Guest& _guest);

/// ClearMessageLine (CS:3609): zeroes the message line at ES:0058. Clobbers AX, BX, CX, DX, BP, SI, DI.
void ClearMessageLine(Guest& _guest);

/// ShowBountyMessage (CS:3626): shows AX tenths of a credit in bountyText for 20 frames.
void ShowBountyMessage(Guest& _guest);

/// ShowShipIdentity (CS:364D): 'CLASS: <class> TYPE: <type>' for ship type AL and class AH on the message line for 30
/// frames; class 3 is Simple unless IsDebrisType says the slot at DI is debris, a class 4 of type 5 is a Hermit, and
/// type 1Ch the police. Out: AX = shipIdentityText, BX, CX = 0 and DI past the copies.
void ShowShipIdentity(Guest& _guest);

/// ToggleMenuRowHighlight (CS:6328): the swapped textAttribute into 36 attribute bytes at ES:SI. Out: AL.
void ToggleMenuRowHighlight(Guest& _guest);

/// PrintCountedTextLines (CS:65FA): a count byte at SI, then that many lines, one a row from DI. Out: SI past the
/// last line; AX and CX clobbered.
void PrintCountedTextLines(Guest& _guest);

/// FormatTenths (CS:69B3): AX tenths into priceText as "nnnn.n".
void FormatTenths(Guest& _guest);

/// PrintTextLines (CS:6DDE): CX lines from SI, one a text row from DI. Out: SI past the last line, DI a row below it,
/// CX = 0; AX and ES as PrintTextModeString leaves them.
void PrintTextLines(Guest& _guest);

/// ReadTextLine (CS:7694): a line typed into the buffer at SI, at most CL characters, echoed at DI with a blinking
/// cursor; Enter ends it, Backspace deletes, letters are lower case unless Shift is held. Waits for keys. Out: BX the
/// length; AX, CX, DX clobbered.
void ReadTextLine(Guest& _guest);

/// RedrawInputLine (CS:773A): the BX characters at SI, then the cursor, printed for the layout. Preserves BX, SI, DI.
void RedrawInputLine(Guest& _guest);

/// PrintStringForLayout (CS:7750): PrintTextModeString in the text layout, DrawScreenString otherwise.
void PrintStringForLayout(Guest& _guest);

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

/// Where PrintTextModeString stops.
struct PrintedText
{
  std::uint16_t end;      ///< the text's NUL
  std::uint16_t nextCell; ///< the text page's cell after the last it wrote
};

/// Where DrawSmallViewChar draws a letter: the byte of the space-view buffer, and the shift, 0 or 4, of the
/// letter's left edge in it.
struct SmallViewPlace
{
  std::uint16_t at;
  std::uint8_t shift;
};

/// DrawViewChar (CS:3130): the 8x8 glyph of _character, in _ink on textPaperPattern, at DS:_at in the space-view
/// buffer. Returns the place of the next character, two bytes on.
std::uint16_t DrawViewChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _at);

/// DrawScreenChar (CS:31F9): the 8x8 glyph of _character, in _ink on textPaperPattern, at _segment:_cell, an even line
/// of the CGA's memory. Returns the place of the next character, two bytes on.
std::uint16_t DrawScreenChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell);

/// FormatDecimal5 (CS:3407): _value as five ASCII digits at DS:_digits. Returns what is left of it below the last
/// divisor: its units.
std::uint16_t FormatDecimal5(GameState& _state, std::uint16_t _value, std::uint16_t _digits);

/// BlankLeadingZeros (CS:3432): up to _most of the leading characters below '1' at DS:_text become spaces; a _most of
/// 0 lets 65,536 go, as LOOP counts.
BlankedZeros BlankLeadingZeros(GameState& _state, std::uint16_t _text, std::uint8_t _most);

/// DrawSmallViewChar (CS:349A): the 5-row letter _letter ('A'-'Z') in _ink over the space-view buffer at _place, the
/// buffer kept around the letter's shape. Returns the next letter's place, six pixels on.
SmallViewPlace DrawSmallViewChar(GameState& _state, std::uint8_t _letter, std::uint16_t _ink, SmallViewPlace _place);

/// FormatCredits (CS:3543): creditsTenths into creditBalanceText, as ten digits with up to eight leading zeros
/// blanked and a decimal point before the last.
void FormatCredits(GameState& _state);

/// PrintTextModeString (CS:60D2): the text at DS:_text to the text page at B800:_cell, in textAttribute.
PrintedText PrintTextModeString(GameState& _state, std::uint16_t _text, std::uint16_t _cell);

/// ClearDockedMessageLine (CS:6553): 19 spaces from text offset 78h at _segment, characters only.
void ClearDockedMessageLine(GameState& _state, std::uint16_t _segment);

/// SwapTextAttributeNibbles (CS:6580): textAttribute's nibbles exchanged, and the attribute that makes.
std::uint8_t SwapTextAttributeNibbles(GameState& _state);

/// ToggleInputCursor (CS:7727): the input cursor's character flips between blank and block.
void ToggleInputCursor(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its
// results back there. The registers the contract leaves to the routine it hands to Guest::Clobber.

void DrawViewCharEntry(Guest& _guest);
void DrawScreenCharEntry(Guest& _guest);
void FormatDecimal5Entry(Guest& _guest);
void BlankLeadingZerosEntry(Guest& _guest);
void DrawSmallViewCharEntry(Guest& _guest);
void FormatCreditsEntry(Guest& _guest);
void PrintTextModeStringEntry(Guest& _guest);
void ClearDockedMessageLineEntry(Guest& _guest);
void SwapTextAttributeNibblesEntry(Guest& _guest);
void ToggleInputCursorEntry(Guest& _guest);

} // namespace Elite
