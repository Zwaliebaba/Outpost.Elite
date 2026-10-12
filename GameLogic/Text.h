// GameLogic/Text.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "Message.h"
#include "SpaceObject.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace Elite
{

// The reference's text routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): printing text and numbers, and reading a
// line typed. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── Texts and buffers of the game's state, by member (D38) ──
//
// The text and number routines read and write texts and buffers that the original reaches by address. Native code reaches them by
// member: a member's bytes, the bytes of members that follow one another in their group's struct as they did in the 1987 data
// segment, where a text or a buffer runs across them, and bytes taken within them, never past their end.

/// _member's bytes, a member of GameData at any depth, a record, a word or an array of them, as bytes that count their own changes.
template <typename Member> [[nodiscard]] std::span<Counted<std::uint8_t>, sizeof(Member)> BytesOf(Member& _member) noexcept
{
  return std::span<Counted<std::uint8_t>, sizeof(Member)>(reinterpret_cast<Counted<std::uint8_t>*>(&_member), sizeof(Member));
}

/// BytesOf, to read.
template <typename Member> [[nodiscard]] std::span<const Counted<std::uint8_t>, sizeof(Member)> BytesOf(const Member& _member) noexcept
{
  return std::span<const Counted<std::uint8_t>, sizeof(Member)>(reinterpret_cast<const Counted<std::uint8_t>*>(&_member), sizeof(Member));
}

/// The bytes from _first's first to _last's last: members of one of GameData's structs that follow one another there, as they followed
/// one another in the 1987 data segment, where a text or a buffer the original reaches runs across them. Each use holds them together
/// with a static_assert on their places in GameData (GAME_DATA_OFFSET).
template <typename First, typename Last> [[nodiscard]] std::span<Counted<std::uint8_t>> BytesThrough(First& _first, Last& _last) noexcept
{
  auto* const first = reinterpret_cast<Counted<std::uint8_t>*>(&_first);
  auto* const end = reinterpret_cast<Counted<std::uint8_t>*>(&_last) + sizeof(Last);
  return std::span<Counted<std::uint8_t>>(first, static_cast<std::size_t>(end - first));
}

/// BytesThrough, to read.
template <typename First, typename Last>
[[nodiscard]] std::span<const Counted<std::uint8_t>> BytesThrough(const First& _first, const Last& _last) noexcept
{
  const auto* const first = reinterpret_cast<const Counted<std::uint8_t>*>(&_first);
  const auto* const end = reinterpret_cast<const Counted<std::uint8_t>*>(&_last) + sizeof(Last);
  return std::span<const Counted<std::uint8_t>>(first, static_cast<std::size_t>(end - first));
}

/// Throws the std::out_of_range of _count bytes from _at of a buffer of _bytes: what the original would reach past the buffer's end,
/// into whatever followed it in the data segment, which native code does not reach (BytesWithin).
[[noreturn]] void RefuseBytesPast(std::size_t _at, std::size_t _count, std::size_t _bytes);

/// _count of _bytes from its _at. Bytes past _bytes' end throw std::out_of_range: the original would reach on into whatever followed
/// them in the data segment, and every use says why the game does not, or that it stops there as the GameState did.
template <typename Byte, std::size_t Extent>
[[nodiscard]] std::span<Byte> BytesWithin(std::span<Byte, Extent> _bytes, std::size_t _at, std::size_t _count)
{
  if (_at > _bytes.size() || _count > _bytes.size() - _at)
  {
    RefuseBytesPast(_at, _count, _bytes.size());
  }
  return std::span<Byte>(_bytes).subspan(_at, _count);
}

/// The byte _at of _bytes, as BytesWithin takes it.
template <typename Byte, std::size_t Extent> [[nodiscard]] Byte& ByteWithin(std::span<Byte, Extent> _bytes, std::size_t _at)
{
  return BytesWithin(_bytes, _at, 1)[0];
}

/// The text in _bytes from its _at up to its NUL, which must be within them: a NUL past their end throws std::out_of_range, as
/// BytesWithin does, where the original would read on.
[[nodiscard]] std::string_view TextWithin(std::span<const Counted<std::uint8_t>> _bytes, std::size_t _at);

/// _pieces one after another: a text the original reads as one, from constants of its image and bytes the game writes between
/// them, which the state holds apart since change 3c.
[[nodiscard]] std::string JoinTexts(std::initializer_list<std::string_view> _pieces);

/// _byte, a character the game writes into a text, as a text of its own (JoinTexts).
[[nodiscard]] inline std::string_view CharText(const Counted<std::uint8_t>& _byte) noexcept
{
  return std::string_view(reinterpret_cast<const char*>(&_byte), 1);
}

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// Where DrawSmallViewChar draws a letter: the byte of the drawing buffer (DrawingBuffer), and the shift, 0 or 4, of the
/// letter's left edge in it.
struct SmallViewPlace
{
  std::uint16_t at;
  std::uint8_t shift;
};

/// DrawViewChar (CS:3130): the 8x8 glyph of _character, in _ink on textPaperPattern, at byte _at of the drawing buffer
/// (DrawingBuffer, DS:0000-1FFF in 1987, so that the byte is the original's DS:_at too). Returns the place of the next
/// character, two bytes on.
std::uint16_t DrawViewChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _at);

/// DrawViewString (CS:31EC): DrawViewChar for each character of _text, from byte _at of the drawing buffer.
void DrawViewString(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _at);

/// DrawScreenChar (CS:31F9): the 8x8 glyph of _character, in _ink on textPaperPattern, at _segment:_cell, an even line
/// of the CGA's memory. _segment, ES, is the CGA's video segment; another throws std::out_of_range, where the original would
/// draw through the data segment (CheckScreenSegment in Text.cpp says why the game does not). Returns the place of the next
/// character, two bytes on.
std::uint16_t DrawScreenChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell);

/// DrawScreenString (CS:32D8): DrawScreenChar for each character of _text, from _segment:_cell. Returns the place after the
/// last.
std::uint16_t DrawScreenString(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell);

/// FormatDecimal5 (CS:3407): _value as five ASCII digits into the first five of _digits, a buffer of the data segment.
void FormatDecimal5(std::uint16_t _value, std::span<Counted<std::uint8_t>> _digits);

/// BlankLeadingZeros (CS:3432): up to _most of _text's leading characters below '1' become spaces. Every caller's _most is
/// less than the digits it blanks, which always end in one kept; LOOP would let 65,536 go for 0.
void BlankLeadingZeros(std::span<Counted<std::uint8_t>> _text, std::uint8_t _most);

/// DrawSmallViewChar (CS:349A): the 5-row letter _letter ('A'-'Z') in _ink over the drawing buffer at _place, the
/// buffer kept around the letter's shape. Returns the next letter's place, six pixels on.
SmallViewPlace DrawSmallViewChar(GameState& _state, std::uint8_t _letter, std::uint16_t _ink, SmallViewPlace _place);

/// DrawSmallViewString (CS:3527): DrawSmallViewChar for each letter of _text, from _position, which is (line+1)*256 + x: the
/// buffer's byte _position/4, and the shift bit 1 of x makes.
void DrawSmallViewString(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _position);

/// FormatCredits (CS:3543): creditsTenths into creditBalanceText, as ten digits with up to eight leading zeros
/// blanked and a decimal point before the last.
void FormatCredits(GameState& _state);

/// _message's text, up to its NUL: a constant one's from MESSAGE_TEXTS, and the five the program writes into from the state, as
/// they are now: the hyperspace countdown's composed from its constant message and the digits written after it (change 3c).
/// Message::None has none.
[[nodiscard]] std::string MessageText(const GameState& _state, Message _message);

/// UpdateMessageLine (CS:35A3): the message line's work for a frame: UpdateWarnings; then while the message drawn has frames
/// left, one fewer; else, once they run out, the view's name as the message, and the message shown, the line cleared a row at a
/// time forwards or, _backwards, down, unless it is shown already.
void UpdateMessageLine(GameState& _state, bool _backwards);

/// ClearMessageLine (CS:3609): zeroes the message line's eight rows of 32 words from _segment:0058, each row forwards
/// or, _backwards, down (REP STOSW with the direction flag set). _segment is the CGA's video segment, as DrawScreenChar's.
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
/// of a menu row at _segment:_row. _segment is the CGA's video segment, as DrawScreenChar's.
void ToggleMenuRowHighlight(GameState& _state, std::uint16_t _segment, std::uint16_t _row);

/// ClearDockedMessageLine (CS:6553): 19 spaces from text offset 78h at _segment, characters only. _segment is the CGA's video
/// segment, as DrawScreenChar's.
void ClearDockedMessageLine(GameState& _state, std::uint16_t _segment);

/// SwapTextAttributeNibbles (CS:6580): textAttribute's nibbles exchanged, and the attribute that makes.
std::uint8_t SwapTextAttributeNibbles(GameState& _state);

/// PrintCountedTextLines (CS:65FA): PrintTextLines for the lines after a count byte, as many as it says: _lines, the
/// generated lines of such a text (a subsystem's tables).
void PrintCountedTextLines(GameState& _state, std::span<const std::string_view> _lines, std::uint16_t _cell);

/// FormatTenths (CS:69B3): _tenths into priceText as "nnnn.n".
void FormatTenths(GameState& _state, std::uint16_t _tenths);

/// PrintTextLines (CS:6DDE): each of _lines by PrintTextModeString a row of the text page below the one before, from
/// B800:_cell.
void PrintTextLines(GameState& _state, std::span<const std::string_view> _lines, std::uint16_t _cell);

/// PrintTextLines (CS:6DDE) for _lines texts that follow one another in _texts, each after the NUL of the one before, as the original
/// steps past a NUL to the next: texts the game writes into, or composes from constants and what it writes (JoinTexts). Every text's
/// NUL but the last's must be within _texts; the last may end at _texts' end. _lines is not 0, which LOOP would count as 65,536.
void PrintTextLines(GameState& _state, std::string_view _texts, std::uint16_t _cell, std::uint16_t _lines);

/// _bytes, a member of texts the game writes into, as PrintTextLines reads them.
[[nodiscard]] inline std::string_view TextsOf(std::span<const Counted<std::uint8_t>> _bytes) noexcept
{
  return std::string_view(reinterpret_cast<const char*>(_bytes.data()), _bytes.size());
}

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
