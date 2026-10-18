#include "pch.h"

#include "Text.h"

#include "Arithmetic.h"
#include "Flight.h"
#include "Input.h"
#include "Ships.h"
#include "TextTables.h"
#include "Timer.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace Elite
{

namespace
{

constexpr std::uint8_t FIRST_UNMAPPED_SCAN = 0x54; // scanCodeToAscii's length
constexpr std::uint8_t NO_CHARACTER = 0xFF;        // in scanCodeToAscii: a key that types nothing
constexpr std::uint8_t LOWER_CASE_BIT = 0x20;
constexpr std::uint8_t UPPER_CASE_MASK = 0xDF;

// ShowShipIdentity's classes: 3 is Simple, unless it is debris; a Hermit is a class 4 of type 5, and type 1Ch
// is the police.
constexpr std::uint8_t CLASS_SIMPLE_OR_DEBRIS = 3;
constexpr std::uint8_t CLASS_SIMPLE = 8;
constexpr std::uint8_t CLASS_ROCK = 4;
constexpr std::uint8_t TYPE_HERMIT = 5;
constexpr std::uint8_t CLASS_HERMIT = 9;
constexpr std::uint8_t TYPE_POLICE = 0x1C;
constexpr std::uint8_t CLASS_POLICE = 10;
constexpr std::uint16_t CLASS_NAME_BYTES = 7;
constexpr std::uint16_t TYPE_NAME_BYTES = 11;
constexpr std::uint16_t CLASS_TEXT_OFFSET = 7;   // 'CLASS: '
constexpr std::uint16_t TYPE_TEXT_OFFSET = 0x15; // 'CLASS: 1234567 TYPE: '
constexpr std::uint16_t IDENTITY_FRAMES = 0x1E;

constexpr std::uint8_t FIRST_GLYPH = 0x20;        // screenFont starts at the space
constexpr std::uint8_t FIRST_SMALL_LETTER = 0x41; // smallFont starts at 'A'
constexpr std::uint16_t VIEW_ROW_BYTES = 0x40;    // a row of the space-view buffer
constexpr std::uint16_t GLYPH_ROWS = 8;
constexpr std::uint16_t SCREEN_GLYPH_BYTES = 16; // a screenFont glyph: a word a row
constexpr std::uint16_t SMALL_GLYPH_ROWS = 5;
constexpr std::uint16_t SMALL_GLYPH_BYTES = 10; // a smallFont letter: a word a row
constexpr std::uint8_t SMALL_GLYPH_SHIFT = 4;   // the half byte a letter starts at

constexpr std::uint8_t MESSAGE_DRAWN = 0xFF;
constexpr std::uint16_t VIEW_REAR = 0x400;
constexpr std::uint16_t VIEW_LEFT = 0x200;

constexpr std::uint16_t BOUNTY_DIGITS_OFFSET = 7; // 'BOUNTY:  nn.n Cr': the digits within bountyText
constexpr std::uint16_t BOUNTY_FRAMES = 0x14;     // messageFrames, with messageDrawn cleared by the same word
constexpr std::uint8_t TENTHS_BLANKS = 3;
constexpr std::uint8_t BLANK_BELOW = '1';
constexpr std::uint16_t CREDIT_DIGITS = 10;
constexpr std::uint16_t CREDIT_BLANKS = 8;

// SHL r16, CL on the 8088, which does not mask the count.
[[nodiscard]] std::uint16_t ShiftLeft(std::uint16_t _value, std::uint8_t _count) noexcept
{
  return _count >= 16 ? std::uint16_t{0} : static_cast<std::uint16_t>(_value << _count);
}

// One row of an opaque glyph: its pixels in the ink, the rest in the paper.
[[nodiscard]] std::uint16_t InkOnPaper(std::uint16_t _pixels, std::uint16_t _ink, std::uint16_t _paper) noexcept
{
  return static_cast<std::uint16_t>((_ink & _pixels) | (~_pixels & _paper));
}

// The fonts' tables, a glyph each, a word a row.
static_assert(SCREEN_FONT.front().size() == SCREEN_GLYPH_BYTES && SMALL_FONT.front().size() == SMALL_GLYPH_BYTES);

// The word of _font's glyph _glyph for its row _row: a glyph is a word a row, as the original reads the font from the glyph's
// first byte. A character with no glyph, below the font's first or past its last, would have the original read on past the
// font's table into the data segment. No text the game prints holds one: the view and screen texts are the program's own and
// what is typed, which SCAN_CODE_TO_ASCII keeps within the screen font, and the chart labels are system names in capitals.
// Measured on change 3b's base: of every glyph the 26 replays, the test suite and every short-range chart and system of the
// nine galaxies read, none was past either font. One throws std::out_of_range.
template <std::size_t Glyphs, std::size_t GlyphBytes>
[[nodiscard]] std::uint16_t FontWord(const std::array<std::array<std::uint8_t, GlyphBytes>, Glyphs>& _font, std::uint8_t _glyph,
                                     std::uint16_t _row)
{
  if (_glyph >= Glyphs)
  {
    RefuseBytesPast(std::size_t{_glyph} * GlyphBytes, GlyphBytes, Glyphs * GlyphBytes);
  }
  return WordIn(std::span<const std::uint8_t>(_font[_glyph]), std::size_t{_row} * 2);
}

// Row _row of _character's opaque glyph: its pixels in _ink, the rest in _paper.
[[nodiscard]] std::uint16_t GlyphRow(std::uint8_t _character, std::uint16_t _row, std::uint16_t _ink, std::uint16_t _paper)
{
  const auto glyph = static_cast<std::uint8_t>(_character - FIRST_GLYPH);
  return InkOnPaper(FontWord(SCREEN_FONT, glyph, _row), _ink, _paper);
}

// Row _row of the small letter _letter ('A' is 0), shifted right _shift pixels into a word of the drawing buffer, in memory
// order: the font holds each row big-endian.
[[nodiscard]] std::uint16_t SmallGlyphRow(std::uint8_t _letter, std::uint16_t _row, std::uint8_t _shift)
{
  const std::uint16_t shifted = ShiftLeft(FontWord(SMALL_FONT, _letter, _row), _shift);
  return Swap(shifted);
}

// The two bytes of the drawing buffer from its byte _at, where the text printers write a glyph's row as a word. The places the
// game prints at are on the buffer: the view texts' are constants, and a short-range chart's labels' rows are on the chart. A
// label that runs out of nudges (NudgeChartLabel) can be left below the chart, where the original prints it on into the data
// segment after the buffer: the GameState refused the first byte there it did not hold (43 of the 2,304 short-range charts of
// the nine galaxies stopped there, and none that went on had written past the buffer), and so does this, at the first byte
// past it.
[[nodiscard]] std::span<std::uint8_t> DrawingBufferWord(GameState& _state, std::uint16_t _at)
{
  return BytesWithin(DrawingBuffer(_state), _at, 2);
}

// A loop of byte moves ShowShipIdentity makes, a byte at a time: the first _to.size() bytes of a name of shipTypeNames, as many
// as the loop's count, into _to. The name holds one more byte than the field it is copied into, which the loop never moves.
template <std::size_t Bytes> void CopyNameInto(const std::array<std::uint8_t, Bytes>& _name, std::span<std::uint8_t> _to)
{
  static_assert(Bytes > 0);
  for (std::size_t moved = 0; moved < _to.size() && moved < Bytes; ++moved)
  {
    _to[moved] = _name[moved];
  }
}

// MessageLine's second half: the message in messagePointer, shown.
void ShowMessage(GameState& _state)
{
  _state.Data().messages.messageFrames = WithHigh(_state.Data().messages.messageFrames, MESSAGE_DRAWN);
  _state.Data().messages.messageShown = _state.Data().messages.messagePointer;
}

// The digits FormatDecimal5 writes and the point after them in the bounty's text, from its first digit.
constexpr std::size_t NUMBER_BYTES = 6;

} // namespace

void RefuseBytesPast(std::size_t _at, std::size_t _count, std::size_t _bytes)
{
  throw std::out_of_range(std::format("{} bytes from byte {} of a buffer of {}, past its end", _count, _at, _bytes));
}

std::string_view TextWithin(std::span<const std::uint8_t> _bytes, std::size_t _at)
{
  const std::span<const std::uint8_t> rest = BytesWithin(_bytes, _at, _bytes.size() - std::min(_at, _bytes.size()));
  const std::string_view text = TextIn(rest);
  if (text.size() == rest.size())
  {
    RefuseBytesPast(_at, rest.size() + 1, _bytes.size());
  }
  return text;
}

std::string JoinTexts(std::initializer_list<std::string_view> _pieces)
{
  std::string text;
  for (const std::string_view piece : _pieces)
  {
    text += piece;
  }
  return text;
}

std::uint16_t DrawViewChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _at)
{
  const std::uint16_t paper = _state.Data().drawing.textPaperPattern;
  std::uint16_t address = _at;
  for (std::uint16_t line = 0; line < GLYPH_ROWS; ++line)
  {
    const std::uint16_t row = GlyphRow(_character, line, _ink, paper);
    SetWordIn(DrawingBufferWord(_state, address), 0, row);
    address = static_cast<std::uint16_t>(address + VIEW_ROW_BYTES);
  }
  return static_cast<std::uint16_t>(_at + 2);
}

void DrawViewString(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _at)
{
  // Each character is read as it is reached, as the original reads it from memory.
  std::uint16_t at = _at;
  for (const char character : _text)
  {
    at = DrawViewChar(_state, static_cast<std::uint8_t>(character), _ink, at);
  }
}

void FormatDecimal5(std::uint16_t _value, std::span<std::uint8_t> _digits)
{
  std::uint16_t value = _value;
  std::size_t digit = 0;
  for (const std::uint16_t divisor : DECIMAL_DIVISORS_16)
  {
    if (divisor == 1)
    {
      break;
    }
    // Repeated subtraction until it borrows: one count more than the quotient, less 1 again by adding 2Fh.
    const auto subtractions = static_cast<std::uint8_t>(value / divisor + 1);
    value = static_cast<std::uint16_t>(value % divisor);
    _digits[digit] = static_cast<std::uint8_t>(subtractions + 0x2F);
    ++digit;
  }
  _digits[digit] = static_cast<std::uint8_t>(Low(value) + '0');
}

void BlankLeadingZeros(std::span<std::uint8_t> _text, std::uint8_t _most)
{
  std::size_t character = 0;
  std::uint16_t triesLeft = _most;
  do
  {
    if (_text[character] >= BLANK_BELOW)
    {
      return;
    }
    _text[character] = ' ';
    ++character;
    --triesLeft;
  } while (triesLeft != 0);
}

SmallViewPlace DrawSmallViewChar(GameState& _state, std::uint8_t _letter, std::uint16_t _ink, SmallViewPlace _place)
{
  const auto letter = static_cast<std::uint8_t>(_letter - FIRST_SMALL_LETTER);
  std::uint16_t address = _place.at;
  for (std::uint16_t line = 0; line < SMALL_GLYPH_ROWS; ++line)
  {
    // The letter's pixels cleared from the buffer, then set in the ink: two word writes.
    const std::uint16_t pixels = SmallGlyphRow(letter, line, _place.shift);
    const std::span<std::uint8_t> word = DrawingBufferWord(_state, address);
    SetWordIn(word, 0, static_cast<std::uint16_t>(WordIn(word, 0) & ~pixels));
    SetWordIn(word, 0, static_cast<std::uint16_t>(WordIn(word, 0) | (pixels & _ink)));
    address = static_cast<std::uint16_t>(address + VIEW_ROW_BYTES);
  }
  const auto shift = static_cast<std::uint8_t>(_place.shift ^ SMALL_GLYPH_SHIFT);
  return SmallViewPlace{static_cast<std::uint16_t>(_place.at + (shift == 0 ? 1 : 2)), shift};
}

void DrawSmallViewString(GameState& _state, std::string_view _text, std::uint16_t _ink, std::uint16_t _position)
{
  // The position shifted right twice for the byte; the shift is 4 where bit 1 of x is clear, else 0.
  const auto shift = static_cast<std::uint8_t>(~(Low(_position) << 1) & SMALL_GLYPH_SHIFT);
  SmallViewPlace next{static_cast<std::uint16_t>(_position >> 2), shift};
  for (const char letter : _text)
  {
    next = DrawSmallViewChar(_state, static_cast<std::uint8_t>(letter), _ink, next);
  }
}

void FormatCredits(GameState& _state)
{
  CommanderState& commander = _state.Data().commander;
  std::uint8_t(&text)[sizeof(commander.creditBalanceText)] = commander.creditBalanceText;
  std::uint32_t value = commander.creditsTenths;
  std::size_t digit = 0;
  for (const std::array<std::uint8_t, 4>& divisorBytes : DECIMAL_DIVISORS_32)
  {
    const auto low = static_cast<std::uint16_t>(divisorBytes[0] | (divisorBytes[1] << 8));
    const auto high = static_cast<std::uint16_t>(divisorBytes[2] | (divisorBytes[3] << 8));
    if (low == 1) // only the low word is tested
    {
      break;
    }
    const std::uint32_t divisor = low | (std::uint32_t{high} << 16);
    const auto subtractions = static_cast<std::uint8_t>(value / divisor + 1);
    value %= divisor;
    text[digit] = static_cast<std::uint8_t>(subtractions + 0x2F);
    ++digit;
  }
  text[digit] = static_cast<std::uint8_t>(value + '0');

  std::size_t blank = 0;
  for (std::uint16_t left = CREDIT_BLANKS; left != 0 && text[blank] == '0'; --left)
  {
    text[blank] = ' ';
    ++blank;
  }
  // The tenths move one place right for the point.
  constexpr std::size_t TENTHS = CREDIT_DIGITS - 1;
  text[TENTHS + 1] = std::uint8_t{text[TENTHS]};
  text[TENTHS] = '.';
}

std::string MessageText(const GameState& _state, Message _message)
{
  // Each text holds its NUL, which nothing writes: the last of bountyText, shipIdentityText, scoopedCargoText and
  // arrivalGalaxyText's record. The countdown's is after its two digits, where the original's NUL is.
  const HyperspaceState& hyperspace = _state.Data().hyperspace;
  switch (_message)
  {
  case Message::Bounty:
    return std::string(TextIn(_state.Data().text.bountyText));
  case Message::ShipIdentity:
    return std::string(TextIn(_state.Data().text.shipIdentityText));
  case Message::ScoopedCargo:
    return std::string(TextIn(_state.Data().equipment.scoopedCargoText));
  case Message::ArrivalGalaxy:
    return std::string(TextIn(BytesOf(hyperspace.arrivalGalaxyText)));
  case Message::HyperspaceCountdown:
    return JoinTexts({HYPERSPACE_COUNTDOWN_MESSAGE, TextIn(BytesOf(hyperspace.hyperspaceCountdownDigits))});
  default:
    break;
  }
  const auto index = static_cast<std::size_t>(_message);
  return std::string(index < MESSAGE_TEXTS.size() ? MESSAGE_TEXTS[index] : std::string_view{});
}

void UpdateMessageLine(GameState& _state)
{
  UpdateWarnings(_state);
  if (High(_state.Data().messages.messageFrames) != 0)
  {
    // messageFrames is a byte here, though ShowBountyMessage writes it as a word.
    const std::uint8_t frames = Low(_state.Data().messages.messageFrames);
    if (frames != 0)
    {
      _state.Data().messages.messageFrames = WithLow(_state.Data().messages.messageFrames, static_cast<std::uint8_t>(frames - 1));
      return;
    }
    const std::uint16_t view = _state.Data().playerShip.viewAngle;
    _state.Data().messages.messagePointer = view == 0           ? Message::FrontView
                                            : view == VIEW_REAR ? Message::RearView
                                            : view == VIEW_LEFT ? Message::LeftView
                                                                : Message::RightView;
  }
  ShowMessage(_state);
}

void ShowBountyMessage(GameState& _state, std::uint16_t _tenths)
{
  const std::span<std::uint8_t> digits = std::span<std::uint8_t>(_state.Data().text.bountyText).subspan(BOUNTY_DIGITS_OFFSET, NUMBER_BYTES);
  FormatDecimal5(_tenths, digits);
  BlankLeadingZeros(digits, TENTHS_BLANKS);
  // The tenths digit moves right, and the point takes its place, as one word.
  const std::uint8_t tenths = digits[4];
  digits[4] = '.';
  digits[5] = tenths;
  _state.Data().messages.messagePointer = Message::Bounty;
  _state.Data().messages.messageFrames = BOUNTY_FRAMES;
}

void ShowShipIdentity(GameState& _state, std::uint8_t _type, std::uint8_t _class, const SpaceObject& _slot)
{
  // The type and the class, kept across IsDebrisType.
  std::uint8_t shipClass = _class;
  if (shipClass == CLASS_SIMPLE_OR_DEBRIS && !IsDebrisType(_slot))
  {
    shipClass = CLASS_SIMPLE;
  }
  if (shipClass == CLASS_ROCK && _type == TYPE_HERMIT)
  {
    shipClass = CLASS_HERMIT;
  }
  if (_type == TYPE_POLICE)
  {
    shipClass = CLASS_POLICE;
  }
  // The class's name, then the type's, a byte at a time, the class's seven bytes of its eight.
  const std::span<std::uint8_t> identity(_state.Data().text.shipIdentityText);
  const std::array<std::uint8_t, 8>& className = SHIP_CLASS_NAMES[shipClass];
  for (std::size_t moved = 0; moved < CLASS_NAME_BYTES; ++moved)
  {
    identity[CLASS_TEXT_OFFSET + moved] = std::uint8_t{className[moved]};
  }
  CopyNameInto(SHIP_TYPE_NAMES[_type], identity.subspan(TYPE_TEXT_OFFSET, TYPE_NAME_BYTES));
  _state.Data().messages.messagePointer = Message::ShipIdentity;
  _state.Data().messages.messageFrames = IDENTITY_FRAMES;
}

std::optional<std::uint8_t> TypedCharacter(const KeyPress& _key) noexcept
{
  if (_key.scanCode == 0 || _key.scanCode >= FIRST_UNMAPPED_SCAN)
  {
    return std::nullopt;
  }
  std::uint8_t character = SCAN_CODE_TO_ASCII[_key.scanCode];
  if (character == NO_CHARACTER)
  {
    return std::nullopt;
  }
  if (character >= 'A' && character <= 'Z')
  {
    // Bit 20h set, and cleared after it when GetKey says Shift was held: lower case unless Shift is held.
    character = static_cast<std::uint8_t>(character | LOWER_CASE_BIT);
    if (_key.shift)
    {
      character = static_cast<std::uint8_t>(character & UPPER_CASE_MASK);
    }
  }
  return character;
}

} // namespace Elite
