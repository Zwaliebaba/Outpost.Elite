// GameLogic/Text.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "Message.h"
#include "SpaceObject.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Elite
{

// The reference's text routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): printing text and numbers, and reading a
// line typed. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// What BlankLeadingZeros leaves of a number's text.
struct BlankedZeros
{
  std::size_t firstKept;   ///< the index of the first character it kept, or of the one after the last it blanked
  std::uint16_t triesLeft; ///< how many more it could have blanked: 0 once it has blanked as many as it may
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

/// DrawViewString (CS:31EC): DrawViewChar for each character of _text, from DS:_at. Returns the place after the last.
std::uint16_t DrawViewString(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _at);

/// DrawScreenChar (CS:31F9): the 8x8 glyph of _character, in _ink on textPaperPattern, at _segment:_cell, an even line
/// of the CGA's memory. Returns the place of the next character, two bytes on.
std::uint16_t DrawScreenChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell);

/// DrawScreenString (CS:32D8): DrawScreenChar for each character of _text, from _segment:_cell. Returns the place after the
/// last.
std::uint16_t DrawScreenString(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell);

/// FormatDecimal5 (CS:3407): _value as five ASCII digits into the first five of _digits, a buffer of the data segment.
/// Returns what is left of it below the last divisor: its units.
std::uint16_t FormatDecimal5(std::uint16_t _value, std::span<Counted<std::uint8_t>> _digits);

/// BlankLeadingZeros (CS:3432): up to _most of _text's leading characters below '1' become spaces. Every caller's _most is
/// less than the digits it blanks, which always end in one kept; LOOP would let 65,536 go for 0.
BlankedZeros BlankLeadingZeros(std::span<Counted<std::uint8_t>> _text, std::uint8_t _most);

/// DrawSmallViewChar (CS:349A): the 5-row letter _letter ('A'-'Z') in _ink over the space-view buffer at _place, the
/// buffer kept around the letter's shape. Returns the next letter's place, six pixels on.
SmallViewPlace DrawSmallViewChar(GameState& _state, std::uint8_t _letter, std::uint16_t _ink, SmallViewPlace _place);

/// DrawSmallViewString (CS:3527): DrawSmallViewChar for each letter of _text, from _position, which is (line+1)*256 + x: the
/// buffer's byte _position/4, and the shift bit 1 of x makes.
void DrawSmallViewString(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _position);

/// FormatCredits (CS:3543): creditsTenths into creditBalanceText, as ten digits with up to eight leading zeros
/// blanked and a decimal point before the last.
void FormatCredits(GameState& _state);

/// _message's text, up to its NUL: a constant one's from MESSAGE_TEXTS, and the five the program writes into from the data
/// segment, as they are now. Message::None has none.
[[nodiscard]] std::string_view MessageText(const GameState& _state, Message _message) noexcept;

/// UpdateMessageLine (CS:35A3): the message line's work for a frame: UpdateWarnings; then while the message drawn has frames
/// left, one fewer; else, once they run out, the view's name as the message, and the message shown, the line cleared a row at a
/// time forwards or, _backwards, down, unless it is shown already.
void UpdateMessageLine(GameState& _state, bool _backwards);

/// ClearMessageLine (CS:3609): zeroes the message line's eight rows of 32 words from _segment:0058, each row forwards
/// or, _backwards, down (REP STOSW with the direction flag set).
void ClearMessageLine(GameState& _state, std::uint16_t _segment, bool _backwards);

/// ShowBountyMessage (CS:3626): _tenths of a credit into bountyText as "nnnn.n", shown for 20 frames.
void ShowBountyMessage(GameState& _state, std::uint16_t _tenths);

/// ShowShipIdentity (CS:364D): 'CLASS: <class> TYPE: <type>' for ship type _type and class _class on the message line for
/// 30 frames; class 3 is Simple unless IsDebrisType says _slot is debris, a class 4 of type 5 is a Hermit, and type 1Ch
/// the police.
void ShowShipIdentity(GameState& _state, std::uint8_t _type, std::uint8_t _class, const SpaceObject& _slot);

/// PrintTextModeString (CS:60D2): _text to the text page at B800:_cell, in textAttribute. Returns the cell after its last.
std::uint16_t PrintTextModeString(GameState& _state, std::string_view _text, std::uint16_t _cell);

/// ToggleMenuRowHighlight (CS:6328): SwapTextAttributeNibbles, and the attribute that makes into the 36 attribute bytes
/// of a menu row at _segment:_row.
void ToggleMenuRowHighlight(GameState& _state, std::uint16_t _segment, std::uint16_t _row);

/// ClearDockedMessageLine (CS:6553): 19 spaces from text offset 78h at _segment, characters only.
void ClearDockedMessageLine(GameState& _state, std::uint16_t _segment);

/// SwapTextAttributeNibbles (CS:6580): textAttribute's nibbles exchanged, and the attribute that makes.
std::uint8_t SwapTextAttributeNibbles(GameState& _state);

/// PrintCountedTextLines (CS:65FA): PrintTextLines for the lines after a count byte, as many as it says: _lines, the
/// generated lines of such a text (a subsystem's tables). Returns the cell a row below the last line's first.
std::uint16_t PrintCountedTextLines(GameState& _state, std::span<const std::string_view> _lines, std::uint16_t _cell);

/// PrintCountedTextLines (CS:65FA) for a text of the data segment: _count, its count byte, a member of GameData, and as many
/// lines as it says after it, one after another (PrintTextLines). Returns the cell a row below the last line's first.
std::uint16_t PrintCountedTextLines(GameState& _state, const Counted<std::uint8_t>& _count, std::uint16_t _cell);

/// FormatTenths (CS:69B3): _tenths into priceText as "nnnn.n".
void FormatTenths(GameState& _state, std::uint16_t _tenths);

/// PrintTextLines (CS:6DDE): each of _lines by PrintTextModeString a row of the text page below the one before, from
/// B800:_cell. Returns the cell a row below the last line's first.
std::uint16_t PrintTextLines(GameState& _state, std::span<const std::string_view> _lines, std::uint16_t _cell);

/// PrintTextLines (CS:6DDE) for _lines texts of the data segment that follow one another from _first (TextAt), each after
/// the one before's NUL: a text the game writes into. _lines is not 0, which LOOP would count as 65,536.
std::uint16_t PrintTextLines(GameState& _state, std::string_view _first, std::uint16_t _cell, std::uint16_t _lines);

/// ReadTextLine (CS:7694): a line typed into _buffer, a buffer of the data segment of at least _most + 1 bytes, at most _most
/// characters, after ResetKeyboard and with textPaperPattern cleared; echoed by RedrawInputLine at _segment:_cell, with a
/// cursor that blinks every 300 timer ticks. Enter ends it, Backspace deletes, a key with no character does nothing, and
/// letters are lower case unless Shift is held. The buffer holds a NUL after it. Waits for keys as a rule (ADR-015).
void ReadTextLine(GameState& _state, Hardware& _hardware, std::span<Counted<std::uint8_t>> _buffer, std::uint8_t _most,
                  std::uint16_t _segment, std::uint16_t _cell);

/// ToggleInputCursor (CS:7727): the input cursor's character flips between blank and block.
void ToggleInputCursor(GameState& _state);

/// RedrawInputLine (CS:773A): a NUL after the _length characters typed into _buffer, then they and inputCursorText after
/// them, each by PrintStringForLayout from _segment:_cell in the ink FFFFh.
void RedrawInputLine(GameState& _state, std::span<Counted<std::uint8_t>> _buffer, std::uint16_t _length, std::uint16_t _segment,
                     std::uint16_t _cell);

/// PrintStringForLayout (CS:7750): _text by PrintTextModeString at B800:_cell in the text layout, and by DrawScreenString in
/// _ink at _segment:_cell otherwise. Returns the place after its last character.
std::uint16_t PrintStringForLayout(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _segment,
                                   std::uint16_t _cell);

} // namespace Elite
