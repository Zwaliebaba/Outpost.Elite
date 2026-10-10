#include "pch.h"

#include "Text.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Flight.h"
#include "Input.h"
#include "Ships.h"
#include "Timer.h"

#include <utility>

namespace Elite
{

namespace
{

// Where ReadTextLine's loop jumps back to: the blink restarted, a key read, the blink counted, the line
// redrawn (Hardware::LoopTurn).
constexpr std::uint16_t INPUT_RESTART_BLINK = 0x76A3;
constexpr std::uint16_t INPUT_READ_KEY = 0x76AF;
constexpr std::uint16_t INPUT_COUNT_BLINK = 0x76B7;
constexpr std::uint16_t INPUT_REDRAW = 0x76FF;
constexpr std::uint16_t CURSOR_BLINK_TICKS = 300;
constexpr std::uint8_t FIRST_UNMAPPED_SCAN = 0x54; // scanCodeToAscii's length
constexpr std::uint8_t NO_CHARACTER = 0xFF;        // in scanCodeToAscii: a key that types nothing
constexpr std::uint8_t ENTER_KEY = 0x0D;
constexpr std::uint8_t BACKSPACE_KEY = 0x00;
constexpr std::uint8_t CURSOR_HIDDEN = ' ';
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

constexpr std::uint16_t DOCKED_MESSAGE_OFFSET = 0x78; // row 1, column 20
constexpr std::uint16_t DOCKED_MESSAGE_CHARACTERS = 0x13;

constexpr std::uint8_t FIRST_GLYPH = 0x20;         // screenFont starts at the space
constexpr std::uint8_t FIRST_SMALL_LETTER = 0x41;  // smallFont starts at 'A'
constexpr std::uint16_t VIEW_ROW_BYTES = 0x40;     // a row of the space-view buffer
constexpr std::uint16_t CGA_TO_ODD_BANK = 0x2000;  // from an even line to the odd one below it
constexpr std::uint16_t CGA_TO_EVEN_BANK = 0xE050; // and back to the next even line (-0x1FB0)
constexpr std::uint16_t GLYPH_ROWS = 8;
constexpr std::uint16_t SMALL_GLYPH_ROWS = 5;
constexpr std::uint8_t SMALL_GLYPH_SHIFT = 4;  // the half byte a letter starts at
constexpr std::uint16_t TEXT_ROW_BYTES = 0x50; // a row of the 40-column text page
constexpr std::uint8_t TEXT_LAYOUT = 2;        // screenLayout of the docked text screens

constexpr std::uint16_t MESSAGE_LINE_OFFSET = 0x58; // B800:0058, x=32, y=2
constexpr std::uint16_t MESSAGE_LINE_WORDS = 0x20;
constexpr std::uint16_t MESSAGE_LINE_ROWS = 8;
constexpr std::uint16_t MESSAGE_TO_ODD_BANK = 0x1FC0; // after the 64 bytes of a row
constexpr std::uint16_t MESSAGE_TO_EVEN_BANK = 0xE010;
constexpr std::uint16_t MESSAGE_INK = 0xFFFF;
constexpr std::uint8_t MESSAGE_DRAWN = 0xFF;
constexpr std::uint16_t VIEW_REAR = 0x400;
constexpr std::uint16_t VIEW_LEFT = 0x200;

constexpr std::uint16_t BOUNTY_DIGITS_OFFSET = 7; // 'BOUNTY:  nn.n Cr': the digits within bountyText
constexpr std::uint16_t BOUNTY_FRAMES = 0x14;     // messageFrames, with messageDrawn cleared by the same word
constexpr std::uint8_t TENTHS_BLANKS = 3;
constexpr std::uint8_t BLANK_BELOW = '1';
constexpr std::uint16_t CREDIT_DIGITS = 10;
constexpr std::uint16_t CREDIT_BLANKS = 8;
constexpr std::uint16_t MENU_ROW_ATTRIBUTES = 0x24;

constexpr std::uint8_t CURSOR_GRAPHICS_TOGGLE = 0xA0; // space <-> 80h glyph
constexpr std::uint8_t CURSOR_TEXT_TOGGLE = 0xFB;     // space <-> DBh, CP437's block

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

[[nodiscard]] std::uint16_t GlyphOffset(std::uint8_t _character) noexcept
{
  return DS.screenFont.At(static_cast<std::uint8_t>(_character - FIRST_GLYPH));
}

// Row _row of _character's opaque glyph: its pixels in _ink, the rest in _paper.
[[nodiscard]] std::uint16_t GlyphRow(const GameState& _state, std::uint8_t _character, std::uint16_t _row, std::uint16_t _ink,
                                     std::uint16_t _paper)
{
  return InkOnPaper(_state.Word(Offset(GlyphOffset(_character), static_cast<std::uint16_t>(_row * 2))), _ink, _paper);
}

// Row _row of the small letter _letter ('A' is 0), shifted right _shift pixels into a word of the space-view
// buffer, in memory order: the font holds each row big-endian.
[[nodiscard]] std::uint16_t SmallGlyphRow(const GameState& _state, std::uint8_t _letter, std::uint16_t _row, std::uint8_t _shift)
{
  const std::uint16_t shifted = ShiftLeft(_state.Word(Offset(DS.smallFont.At(_letter), static_cast<std::uint16_t>(_row * 2))), _shift);
  return Swap(shifted);
}

// REP STOSW: _count words of _value at _segment:_at, forwards or, _backwards, down. Returns where DI ends.
std::uint16_t StoreWords(GameState& _state, std::uint16_t _segment, std::uint16_t _at, std::uint16_t _count, std::uint16_t _value,
                         bool _backwards)
{
  const auto step = static_cast<std::uint16_t>(_backwards ? 0xFFFE : 2);
  std::uint16_t at = _at;
  for (std::uint16_t left = _count; left != 0; --left)
  {
    _state.SetFarWord(_segment, at, _value);
    at = static_cast<std::uint16_t>(at + step);
  }
  return at;
}

// A loop of byte moves ShowShipIdentity makes (MOV AH,[BX] or MOV AL,[BX], INC BX, the byte to [DI], INC DI, LOOP):
// _count bytes from DS:_from to DS:_to.
void CopyNameInto(GameState& _state, std::uint16_t _from, std::uint16_t _to, std::uint16_t _count)
{
  for (std::uint16_t moved = 0; moved < _count; ++moved)
  {
    _state.SetByte(Offset(_to, moved), _state.Byte(Offset(_from, moved)));
  }
}

// The line ReadTextLine reads: the buffer it types into, which the original holds in SI, and where RedrawInputLine echoes it,
// in ES:DI. None of them changes from one turn of its loop to the next.
struct TypedLine
{
  std::uint16_t buffer;
  std::uint16_t segment;
  std::uint16_t cell;
};

// ReadTextLine's turns carry the length typed, in BX: the one value its registers hold at a backward jump that the next turn
// reads and that changes. AL, which GetKey shifts each code it takes into, reaches no write, no branch and no turn that does
// not change memory: every turn writes cursorBlinkTicks or takes a code from keyBuffer.
void TypedLineTurn(Hardware& _hardware, std::uint16_t _loop, std::uint16_t _length)
{
  _hardware.LoopTurn(_loop, {_length});
}

// ReadTextLine's redraw (0x76FF): the line typed and the cursor printed, then the JMP back to counting the blink.
void RedrawTypedLine(GameState& _state, Hardware& _hardware, const TypedLine& _line, std::uint16_t _length)
{
  (void)RedrawInputLine(_state, _line.buffer, _length, _line.segment, _line.cell);
  TypedLineTurn(_hardware, INPUT_COUNT_BLINK, _length);
}

// ReadTextLine's blink (0x76A3): 300 ticks to the next, the cursor flipped, and the line redrawn.
void RestartInputBlink(GameState& _state, Hardware& _hardware, const TypedLine& _line, std::uint16_t _length)
{
  _state.Set(DS.cursorBlinkTicks, CURSOR_BLINK_TICKS);
  ToggleInputCursor(_state);
  RedrawTypedLine(_state, _hardware, _line, _length);
}

// MessageLine's second half (0x35B9): the message at messagePointer, unless it is already shown, on the message line,
// cleared first a row at a time forwards or, _backwards, down. The original keeps SI round ClearMessageLine on the stack.
// Returns whether it drew the message.
bool ShowMessage(GameState& _state, bool _backwards)
{
  _state.Set(DS.messageDrawn, MESSAGE_DRAWN);
  const std::uint16_t message = _state.Get(DS.messagePointer);
  const bool shown = message == _state.Get(DS.messageShown);
  _state.Set(DS.messageShown, message);
  if (shown)
  {
    return false;
  }
  ClearMessageLine(_state, GameState::VIDEO_SEGMENT, _backwards);
  (void)DrawScreenString(_state, message, MESSAGE_INK, GameState::VIDEO_SEGMENT, MESSAGE_LINE_OFFSET);
  return true;
}

// The bounty's digits within bountyText.
[[nodiscard]] constexpr std::uint16_t BountyDigits() noexcept
{
  return static_cast<std::uint16_t>(DS.bountyText.offset + BOUNTY_DIGITS_OFFSET);
}

} // namespace

std::uint16_t DrawViewChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _at)
{
  const std::uint16_t paper = _state.Get(DS.textPaperPattern);
  std::uint16_t address = _at;
  for (std::uint16_t line = 0; line < GLYPH_ROWS; ++line)
  {
    const std::uint16_t row = GlyphRow(_state, _character, line, _ink, paper);
    _state.SetWord(address, row);
    address = static_cast<std::uint16_t>(address + VIEW_ROW_BYTES);
  }
  return static_cast<std::uint16_t>(_at + 2);
}

PrintedText DrawViewString(GameState& _state, std::uint16_t _text, std::uint16_t _ink, std::uint16_t _at)
{
  PrintedText drawn{_text, _at};
  for (;;)
  {
    const std::uint8_t character = _state.Byte(drawn.end);
    if (character == 0)
    {
      return drawn;
    }
    drawn.end = Offset(drawn.end, 1);
    drawn.nextCell = DrawViewChar(_state, character, _ink, drawn.nextCell);
  }
}

std::uint16_t DrawScreenChar(GameState& _state, std::uint8_t _character, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell)
{
  std::uint16_t address = _cell;
  std::uint16_t down = CGA_TO_ODD_BANK;
  std::uint16_t up = CGA_TO_EVEN_BANK;
  for (std::uint16_t line = 0; line < GLYPH_ROWS; ++line)
  {
    // The paper is read again for each row, as the original does.
    const std::uint16_t row = GlyphRow(_state, _character, line, _ink, _state.Get(DS.textPaperPattern));
    _state.SetFarWord(_segment, address, row);
    if (line + 1 < GLYPH_ROWS)
    {
      address = static_cast<std::uint16_t>(address + down);
      std::swap(down, up);
    }
  }
  return static_cast<std::uint16_t>(_cell + 2);
}

PrintedText DrawScreenString(GameState& _state, std::uint16_t _text, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell)
{
  PrintedText drawn{_text, _cell};
  for (;;)
  {
    const std::uint8_t character = _state.Byte(drawn.end);
    if (character == 0)
    {
      return drawn;
    }
    drawn.end = Offset(drawn.end, 1);
    drawn.nextCell = DrawScreenChar(_state, character, _ink, _segment, drawn.nextCell);
  }
}

std::uint16_t DrawnScreenStringAx(const GameState& _state, std::uint16_t _text, PrintedText _drawn, std::uint16_t _ink, std::uint16_t _ax)
{
  // The NUL read into AL over the last row the last DrawScreenChar drew, of which AH stays.
  std::uint16_t ax = _ax;
  if (_drawn.end != _text)
  {
    ax = GlyphRow(_state, _state.Byte(static_cast<std::uint16_t>(_drawn.end - 1)), GLYPH_ROWS - 1, _ink, _state.Get(DS.textPaperPattern));
  }
  return WithLow(ax, 0);
}

std::uint16_t FormatDecimal5(GameState& _state, std::uint16_t _value, std::uint16_t _digits)
{
  std::uint16_t value = _value;
  std::uint16_t divisors = DS.decimalDivisors16.offset;
  std::uint16_t digit = _digits;
  for (;;)
  {
    const std::uint16_t divisor = _state.Word(divisors);
    divisors = static_cast<std::uint16_t>(divisors + 2);
    if (divisor == 1)
    {
      break;
    }
    // Repeated subtraction until it borrows: one count more than the quotient, less 1 again by adding 2Fh.
    const auto subtractions = static_cast<std::uint8_t>(value / divisor + 1);
    value = static_cast<std::uint16_t>(value % divisor);
    _state.SetByte(digit, static_cast<std::uint8_t>(subtractions + 0x2F));
    digit = static_cast<std::uint16_t>(digit + 1);
  }
  _state.SetByte(digit, static_cast<std::uint8_t>(Low(value) + '0'));
  return value;
}

BlankedZeros BlankLeadingZeros(GameState& _state, std::uint16_t _text, std::uint8_t _most)
{
  BlankedZeros blanked{_text, _most};
  do
  {
    if (_state.Byte(blanked.firstKept) >= BLANK_BELOW)
    {
      return blanked;
    }
    _state.SetByte(blanked.firstKept, ' ');
    ++blanked.firstKept;
    --blanked.triesLeft;
  } while (blanked.triesLeft != 0);
  return blanked;
}

BlankedZeros BlankedLeadingZeros(const GameState& _state, std::uint16_t _text, std::uint8_t _most)
{
  // Each character it blanked is a space, and the first it kept, at least '1', is not.
  BlankedZeros blanked{_text, _most};
  while (blanked.triesLeft != 0 && _state.Byte(blanked.firstKept) == ' ')
  {
    ++blanked.firstKept;
    --blanked.triesLeft;
  }
  return blanked;
}

SmallViewPlace DrawSmallViewChar(GameState& _state, std::uint8_t _letter, std::uint16_t _ink, SmallViewPlace _place)
{
  const auto letter = static_cast<std::uint8_t>(_letter - FIRST_SMALL_LETTER);
  std::uint16_t address = _place.at;
  for (std::uint16_t line = 0; line < SMALL_GLYPH_ROWS; ++line)
  {
    // The letter's pixels cleared from the buffer, then set in the ink: AND [DI],BP and OR [DI],AX, two word
    // writes (34C2, 34C4).
    const std::uint16_t pixels = SmallGlyphRow(_state, letter, line, _place.shift);
    _state.SetWord(address, static_cast<std::uint16_t>(_state.Word(address) & ~pixels));
    _state.SetWord(address, static_cast<std::uint16_t>(_state.Word(address) | (pixels & _ink)));
    address = static_cast<std::uint16_t>(address + VIEW_ROW_BYTES);
  }
  const auto shift = static_cast<std::uint8_t>(_place.shift ^ SMALL_GLYPH_SHIFT);
  return SmallViewPlace{static_cast<std::uint16_t>(_place.at + (shift == 0 ? 1 : 2)), shift};
}

DrawnSmallText DrawSmallViewString(GameState& _state, std::uint16_t _text, std::uint16_t _ink, std::uint16_t _position)
{
  // SHR DI,1 twice for the byte; SHL CL,1 / NOT CL / AND CL,4 for the shift, 4 where bit 1 of x is clear.
  const auto shift = static_cast<std::uint8_t>(~(Low(_position) << 1) & SMALL_GLYPH_SHIFT);
  DrawnSmallText drawn{_text, SmallViewPlace{static_cast<std::uint16_t>(_position >> 2), shift}};
  for (;;)
  {
    const std::uint8_t letter = _state.Byte(drawn.end);
    if (letter == 0)
    {
      return drawn;
    }
    drawn.end = Offset(drawn.end, 1);
    drawn.next = DrawSmallViewChar(_state, letter, _ink, drawn.next);
  }
}

void FormatCredits(GameState& _state)
{
  const std::uint16_t text = DS.creditBalanceText.offset;
  std::uint32_t value = _state.Word(DS.creditsTenths.offset) | (std::uint32_t{_state.Get(DS.data75F5)} << 16);
  std::uint16_t divisors = DS.decimalDivisors32.offset;
  std::uint16_t digit = text;
  for (;;)
  {
    const std::uint16_t low = _state.Word(divisors);
    const std::uint16_t high = _state.Word(static_cast<std::uint16_t>(divisors + 2));
    divisors = static_cast<std::uint16_t>(divisors + 4);
    if (low == 1) // only the low word is tested
    {
      break;
    }
    const std::uint32_t divisor = low | (std::uint32_t{high} << 16);
    const auto subtractions = static_cast<std::uint8_t>(value / divisor + 1);
    value %= divisor;
    _state.SetByte(digit, static_cast<std::uint8_t>(subtractions + 0x2F));
    digit = static_cast<std::uint16_t>(digit + 1);
  }
  _state.SetByte(digit, static_cast<std::uint8_t>(value + '0'));

  std::uint16_t blank = text;
  for (std::uint16_t left = CREDIT_BLANKS; left != 0 && _state.Byte(blank) == '0'; --left)
  {
    _state.SetByte(blank, ' ');
    ++blank;
  }
  // The tenths move one place right for the point.
  const auto tenths = static_cast<std::uint16_t>(text + CREDIT_DIGITS - 1);
  _state.SetByte(static_cast<std::uint16_t>(tenths + 1), _state.Byte(tenths));
  _state.SetByte(tenths, '.');
}

bool UpdateMessageLine(GameState& _state, bool _backwards)
{
  UpdateWarnings(_state);
  if (_state.Get(DS.messageDrawn) != 0)
  {
    // messageFrames is a byte here, though ShowBountyMessage writes it as a word.
    const std::uint8_t frames = _state.Byte(DS.messageFrames.offset);
    if (frames != 0)
    {
      _state.SetByte(DS.messageFrames.offset, static_cast<std::uint8_t>(frames - 1));
      return false;
    }
    const std::uint16_t view = _state.Get(DS.viewAngle);
    _state.Set(DS.messagePointer, view == 0           ? DS.frontViewText.offset
                                  : view == VIEW_REAR ? DS.rearViewText.offset
                                  : view == VIEW_LEFT ? DS.leftViewText.offset
                                                      : DS.rightViewText.offset);
  }
  return ShowMessage(_state, _backwards);
}

void ClearMessageLine(GameState& _state, std::uint16_t _segment, bool _backwards)
{
  // REP STOSW a row at a time, DI going on from where each leaves it to the other bank.
  std::uint16_t row = MESSAGE_LINE_OFFSET;
  std::uint16_t down = MESSAGE_TO_ODD_BANK;
  std::uint16_t up = MESSAGE_TO_EVEN_BANK;
  for (std::uint16_t line = 0; line < MESSAGE_LINE_ROWS; ++line)
  {
    row = Offset(StoreWords(_state, _segment, row, MESSAGE_LINE_WORDS, 0, _backwards), down);
    std::swap(down, up);
  }
}

void ShowBountyMessage(GameState& _state, std::uint16_t _tenths)
{
  const std::uint16_t digits = BountyDigits();
  FormatDecimal5(_state, _tenths, digits);
  BlankLeadingZeros(_state, digits, TENTHS_BLANKS);
  // The tenths digit moves right, and the point takes its place, as one word.
  const std::uint16_t tenths = Offset(digits, 4);
  _state.SetWord(tenths, Join(_state.Byte(tenths), '.'));
  _state.Set(DS.messagePointer, DS.bountyText.offset);
  _state.Set(DS.messageFrames, BOUNTY_FRAMES);
}

BlankedZeros BlankedBountyZeros(const GameState& _state)
{
  return BlankedLeadingZeros(_state, BountyDigits(), TENTHS_BLANKS);
}

void ShowShipIdentity(GameState& _state, std::uint8_t _type, std::uint8_t _class, const ObjectSlot& _slot)
{
  // PUSH AX / POP AX round IsDebrisType keep the type and the class.
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
  // The class's name, then the type's, a byte at a time.
  CopyNameInto(_state, DS.shipClassNames.At(shipClass), Offset(DS.shipIdentityText.offset, CLASS_TEXT_OFFSET), CLASS_NAME_BYTES);
  CopyNameInto(_state, DS.shipTypeNames.At(_type), Offset(DS.shipIdentityText.offset, TYPE_TEXT_OFFSET), TYPE_NAME_BYTES);
  _state.Set(DS.messagePointer, DS.shipIdentityText.offset);
  _state.Set(DS.messageFrames, IDENTITY_FRAMES);
}

PrintedText PrintTextModeString(GameState& _state, std::uint16_t _text, std::uint16_t _cell)
{
  const std::uint8_t attribute = _state.Get(DS.textAttribute);
  PrintedText printed{_text, _cell};
  for (;;)
  {
    const std::uint8_t character = _state.Byte(printed.end);
    if (character == 0)
    {
      return printed;
    }
    ++printed.end;
    _state.SetVideoWord(printed.nextCell, Join(attribute, character));
    printed.nextCell = static_cast<std::uint16_t>(printed.nextCell + 2);
  }
}

std::uint8_t ToggleMenuRowHighlight(GameState& _state, std::uint16_t _segment, std::uint16_t _row)
{
  const std::uint8_t attribute = SwapTextAttributeNibbles(_state);
  std::uint16_t cell = _row;
  for (std::uint16_t left = MENU_ROW_ATTRIBUTES; left != 0; --left)
  {
    _state.SetFarByte(_segment, cell, attribute);
    cell = Offset(cell, 2);
  }
  return attribute;
}

void ClearDockedMessageLine(GameState& _state, std::uint16_t _segment)
{
  std::uint16_t cell = DOCKED_MESSAGE_OFFSET;
  for (std::uint16_t left = DOCKED_MESSAGE_CHARACTERS; left != 0; --left)
  {
    _state.SetFarByte(_segment, cell, ' ');
    cell = Offset(cell, 2);
  }
}

std::uint8_t SwapTextAttributeNibbles(GameState& _state)
{
  const std::uint8_t attribute = _state.Get(DS.textAttribute);
  const auto swapped = static_cast<std::uint8_t>((attribute >> 4) | (attribute << 4));
  _state.Set(DS.textAttribute, swapped);
  return swapped;
}

PrintedLines PrintCountedTextLines(GameState& _state, std::uint16_t _text, std::uint16_t _cell)
{
  // MOV CL,[SI] / XOR CH,CH, then the same loop as PrintTextLines's.
  return PrintTextLines(_state, Offset(_text, 1), _cell, _state.Byte(_text));
}

void FormatTenths(GameState& _state, std::uint16_t _tenths)
{
  const std::uint16_t digits = DS.priceText.offset;
  FormatDecimal5(_state, _tenths, digits);
  BlankLeadingZeros(_state, digits, TENTHS_BLANKS);
  // The tenths digit moves right, and the point takes its place, a byte at a time.
  _state.Set(DS.data8041, _state.Get(DS.data8040));
  _state.Set(DS.data8040, '.');
}

PrintedLines PrintTextLines(GameState& _state, std::uint16_t _text, std::uint16_t _cell, std::uint16_t _lines)
{
  PrintedLines printed{_text, _cell};
  std::uint16_t left = _lines;
  do
  {
    printed.end = Offset(PrintTextModeString(_state, printed.end, printed.nextLine).end, 1);
    printed.nextLine = Offset(printed.nextLine, TEXT_ROW_BYTES);
  } while (--left != 0);
  return printed;
}

std::uint16_t ReadTextLine(GameState& _state, Hardware& _hardware, std::uint16_t _buffer, std::uint8_t _most, std::uint16_t _segment,
                           std::uint16_t _cell)
{
  const TypedLine line{_buffer, _segment, _cell};
  ResetKeyboard(_state, _hardware);
  _state.Set(DS.textPaperPattern, 0);
  _state.Set(DS.inputCharsLeft, _most);
  std::uint16_t length = 0;
  RestartInputBlink(_state, _hardware, line, length);
  for (;;)
  {
    const auto blink = static_cast<std::uint16_t>(_state.Get(DS.cursorBlinkTicks) - 1);
    _state.Set(DS.cursorBlinkTicks, blink);
    if (blink == 0)
    {
      TypedLineTurn(_hardware, INPUT_RESTART_BLINK, length);
      RestartInputBlink(_state, _hardware, line, length);
      continue;
    }
    TypedLineTurn(_hardware, INPUT_READ_KEY, length);
    WaitForTimerTick(_state, _hardware);
    const KeyPress key = GetKey(_state, _hardware);
    if (key.scanCode == 0)
    {
      continue;
    }
    if (key.scanCode >= FIRST_UNMAPPED_SCAN)
    {
      TypedLineTurn(_hardware, INPUT_COUNT_BLINK, length);
      continue;
    }
    std::uint8_t character = _state.Byte(DS.scanCodeToAscii.At(key.scanCode));
    if (character == NO_CHARACTER)
    {
      TypedLineTurn(_hardware, INPUT_COUNT_BLINK, length);
      continue;
    }
    if (character == ENTER_KEY)
    {
      if (_state.Byte(DS.inputCursorText.offset) != CURSOR_HIDDEN)
      {
        ToggleInputCursor(_state);
        (void)RedrawInputLine(_state, _buffer, length, _segment, _cell);
      }
      return length;
    }
    if (character == BACKSPACE_KEY)
    {
      if (length != 0)
      {
        --length;
        _state.Set(DS.inputCharsLeft, static_cast<std::uint8_t>(_state.Get(DS.inputCharsLeft) + 1));
        (void)RedrawInputLine(_state, _buffer, length, _segment, _cell);
      }
      TypedLineTurn(_hardware, INPUT_COUNT_BLINK, length);
      continue;
    }
    if (character >= 'A' && character <= 'Z')
    {
      // OR DL,20h, and AND DL,DFh after it when GetKey says Shift was held: lower case unless Shift is held.
      character = static_cast<std::uint8_t>(character | LOWER_CASE_BIT);
      if (key.shift)
      {
        character = static_cast<std::uint8_t>(character & UPPER_CASE_MASK);
      }
    }
    _state.SetByte(Offset(length, _buffer), character);
    ++length;
    const auto left = static_cast<std::uint8_t>(_state.Get(DS.inputCharsLeft) - 1);
    _state.Set(DS.inputCharsLeft, left);
    if ((left & 0x80) != 0)
    {
      // No room: the character is taken back.
      --length;
      _state.Set(DS.inputCharsLeft, static_cast<std::uint8_t>(left + 1));
      TypedLineTurn(_hardware, INPUT_REDRAW, length);
    }
    RedrawTypedLine(_state, _hardware, line, length);
  }
}

void ToggleInputCursor(GameState& _state)
{
  const std::uint16_t cursor = DS.inputCursorText.offset;
  const std::uint8_t toggle = _state.Get(DS.screenLayout) == TEXT_LAYOUT ? CURSOR_TEXT_TOGGLE : CURSOR_GRAPHICS_TOGGLE;
  _state.SetByte(cursor, static_cast<std::uint8_t>(_state.Byte(cursor) ^ toggle));
}

RedrawnInputLine RedrawInputLine(GameState& _state, std::uint16_t _buffer, std::uint16_t _length, std::uint16_t _segment,
                                 std::uint16_t _cell)
{
  _state.SetByte(Offset(_length, _buffer), 0);
  const PrintedText line = PrintStringForLayout(_state, _buffer, MESSAGE_INK, _segment, _cell);
  return RedrawnInputLine{line, PrintStringForLayout(_state, DS.inputCursorText.offset, MESSAGE_INK, _segment, line.nextCell)};
}

PrintedText PrintStringForLayout(GameState& _state, std::uint16_t _text, std::uint16_t _ink, std::uint16_t _segment, std::uint16_t _cell)
{
  // A tail jump to either printer.
  if (_state.Get(DS.screenLayout) == TEXT_LAYOUT)
  {
    return PrintTextModeString(_state, _text, _cell);
  }
  return DrawScreenString(_state, _text, _ink, _segment, _cell);
}

// ── Their entries ──

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract CLOBBERS_SI{REGISTER_SI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DX{REGISTER_AX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_ALL_BUT_ES{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0};

} // namespace

void DrawViewCharEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t character = Low(regs.ax);
  const std::uint16_t paper = _guest.Get(DS.textPaperPattern);
  regs.di = DrawViewChar(_guest.State(), character, regs.bx, regs.di);
  // The original leaves the last row it drew in AX and the paper in BP, and DrawViewString keeps BP and AH.
  regs.ax = GlyphRow(_guest.State(), character, GLYPH_ROWS - 1, regs.bx, paper);
  regs.bp = paper;
  _guest.Clobber(PRESERVES_ALL);
}

void DrawViewStringEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t text = regs.si;
  const PrintedText drawn = DrawViewString(_guest.State(), regs.si, regs.bx, regs.di);
  regs.si = drawn.end;
  regs.di = drawn.nextCell;
  // The original reads the NUL into AL over what the last DrawViewChar leaves: its last row in AX, of which AH
  // stays, and the paper in BP.
  if (drawn.end != text)
  {
    const std::uint16_t paper = _guest.Get(DS.textPaperPattern);
    regs.ax = GlyphRow(_guest.State(), _guest.Byte(static_cast<std::uint16_t>(drawn.end - 1)), GLYPH_ROWS - 1, regs.bx, paper);
    regs.bp = paper;
  }
  SetLow(regs.ax, 0);
  _guest.Clobber(PRESERVES_ALL);
}

void DrawScreenCharEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t character = Low(regs.ax);
  regs.di = DrawScreenChar(_guest.State(), character, regs.bx, regs.es, regs.di);
  // The original leaves the last row it drew in AX, and DrawScreenString keeps its high byte.
  regs.ax = GlyphRow(_guest.State(), character, GLYPH_ROWS - 1, regs.bx, _guest.Get(DS.textPaperPattern));
  _guest.Clobber(CLOBBERS_SI);
}

void DrawScreenStringEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t text = regs.si;
  const PrintedText drawn = DrawScreenString(_guest.State(), regs.si, regs.bx, regs.es, regs.di);
  regs.si = drawn.end;
  regs.di = drawn.nextCell;
  // The original reads the NUL into AL over the last row the last DrawScreenChar drew, of which AH stays.
  if (drawn.end != text)
  {
    regs.ax = GlyphRow(_guest.State(), _guest.Byte(static_cast<std::uint16_t>(drawn.end - 1)), GLYPH_ROWS - 1, regs.bx,
                       _guest.Get(DS.textPaperPattern));
  }
  SetLow(regs.ax, 0);
  _guest.Clobber(PRESERVES_ALL);
}

void FormatDecimal5Entry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t units = FormatDecimal5(_guest.State(), regs.ax, regs.di);
  // The original leaves the units in AX, AL made their digit, and FormatTenths and PrintCargoQuantity keep AH.
  regs.ax = WithLow(units, static_cast<std::uint8_t>(Low(units) + '0'));
  _guest.Clobber(PRESERVES_ALL);
}

void BlankLeadingZerosEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const BlankedZeros blanked = BlankLeadingZeros(_guest.State(), regs.di, Low(regs.cx));
  regs.di = blanked.firstKept;
  regs.cx = blanked.triesLeft;
  _guest.Clobber(PRESERVES_ALL);
}

void DrawSmallViewCharEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The original leaves the last row's pixels in the ink in AX, and the mask it cleared them with in BP: the
  // contract keeps both.
  const auto letter = static_cast<std::uint8_t>(Low(regs.ax) - FIRST_SMALL_LETTER);
  const std::uint16_t lastRow = SmallGlyphRow(_guest.State(), letter, SMALL_GLYPH_ROWS - 1, Low(regs.cx));
  const SmallViewPlace next = DrawSmallViewChar(_guest.State(), Low(regs.ax), regs.bx, SmallViewPlace{regs.di, Low(regs.cx)});
  regs.ax = static_cast<std::uint16_t>(lastRow & regs.bx);
  regs.bp = static_cast<std::uint16_t>(~lastRow);
  regs.di = next.at;
  SetLow(regs.cx, next.shift);
  _guest.Clobber(PRESERVES_ALL);
}

void DrawSmallViewStringEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t text = regs.si;
  const DrawnSmallText drawn = DrawSmallViewString(_guest.State(), regs.si, regs.bx, regs.di);
  regs.si = drawn.end;
  regs.di = drawn.next.at;
  // The original reads the NUL into AL over what the last DrawSmallViewChar leaves: its last row's pixels in the
  // ink in AX, of which AH stays, and the mask it cleared them with in BP. It keeps CX round the letters.
  if (drawn.end != text)
  {
    const auto letter = static_cast<std::uint8_t>(_guest.Byte(static_cast<std::uint16_t>(drawn.end - 1)) - FIRST_SMALL_LETTER);
    const auto shift = static_cast<std::uint8_t>(drawn.next.shift ^ SMALL_GLYPH_SHIFT);
    const std::uint16_t lastRow = SmallGlyphRow(_guest.State(), letter, SMALL_GLYPH_ROWS - 1, shift);
    regs.ax = static_cast<std::uint16_t>(lastRow & regs.bx);
    regs.bp = static_cast<std::uint16_t>(~lastRow);
  }
  SetLow(regs.ax, 0);
  _guest.Clobber(PRESERVES_ALL);
}

void FormatCreditsEntry(Guest& _guest)
{
  FormatCredits(_guest.State());
  _guest.Regs().si = DS.creditBalanceText.offset;
  _guest.Clobber(PRESERVES_ALL);
}

void ClearMessageLineEntry(Guest& _guest)
{
  ClearMessageLine(_guest.State(), _guest.Regs().es, _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_ALL_BUT_ES);
}

void ShowBountyMessageEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  ShowBountyMessage(_guest.State(), regs.ax);
  // The original leaves DI on the digits, CX as BlankLeadingZeros leaves it, and AX the message.
  regs.cx = BlankedBountyZeros(_guest.State()).triesLeft;
  regs.di = BountyDigits();
  regs.ax = DS.bountyText.offset;
  _guest.Clobber(PRESERVES_ALL);
}

void ShowShipIdentityEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t type = Low(regs.ax);
  ShowShipIdentity(_guest.State(), type, High(regs.ax), ObjectSlot(_guest.State(), regs.di));
  // The original leaves AX the text, BX past the type's name, CX counted down to 0 and DI past the copies, which the
  // contract compares.
  regs.ax = DS.shipIdentityText.offset;
  regs.bx = Offset(DS.shipTypeNames.At(type), TYPE_NAME_BYTES);
  regs.cx = 0;
  regs.di = Offset(DS.shipIdentityText.offset, TYPE_TEXT_OFFSET + TYPE_NAME_BYTES);
  _guest.Clobber(PRESERVES_ALL);
}

void UpdateMessageLineEntry(Guest& _guest)
{
  // MOV AX,0B800h / MOV ES,AX before a message is drawn, which the contract compares.
  if (UpdateMessageLine(_guest.State(), _guest.Flag(Machine::FLAG_DIRECTION)))
  {
    _guest.Regs().es = Guest::VIDEO_SEGMENT;
  }
  _guest.Clobber(CLOBBERS_ALL_BUT_ES);
}

void PrintTextModeStringEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t attribute = _guest.Get(DS.textAttribute);
  const PrintedText printed = PrintTextModeString(_guest.State(), regs.si, regs.di);
  regs.si = printed.end;
  regs.di = printed.nextCell;
  regs.es = Guest::VIDEO_SEGMENT;
  // The original prints from AX, the attribute in AH, and leaves the NUL in AL: PrintTextLines keeps AX.
  regs.ax = Join(attribute, 0);
  _guest.Clobber(PRESERVES_ALL);
}

void ToggleMenuRowHighlightEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, ToggleMenuRowHighlight(_guest.State(), regs.es, regs.si));
  _guest.Clobber(PRESERVES_ALL);
}

void ClearDockedMessageLineEntry(Guest& _guest)
{
  ClearDockedMessageLine(_guest.State(), _guest.Regs().es);
  _guest.Clobber(CLOBBERS_AX_CX_DI);
}

void SwapTextAttributeNibblesEntry(Guest& _guest)
{
  SetLow(_guest.Regs().ax, SwapTextAttributeNibbles(_guest.State()));
  _guest.Clobber(PRESERVES_ALL);
}

void PrintCountedTextLinesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t attribute = _guest.Get(DS.textAttribute);
  const PrintedLines printed = PrintCountedTextLines(_guest.State(), regs.si, regs.di);
  regs.si = printed.end;
  regs.di = printed.nextLine;
  regs.es = Guest::VIDEO_SEGMENT;
  // The last PrintTextModeString leaves the attribute in AH and the NUL in AL, and the LOOP leaves CX at 0:
  // RedrawEquipHelpText keeps both.
  regs.ax = Join(attribute, 0);
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void FormatTenthsEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  FormatTenths(_guest.State(), regs.ax);
  // The original leaves DI and CX as BlankLeadingZeros leaves them, AH FormatDecimal5's units' high byte, which is
  // 0 below 10, and the point in AL.
  const BlankedZeros blanked = BlankedLeadingZeros(_guest.State(), DS.priceText.offset, TENTHS_BLANKS);
  regs.di = blanked.firstKept;
  regs.cx = blanked.triesLeft;
  regs.ax = Join(0, '.');
  _guest.Clobber(PRESERVES_ALL);
}

void PrintTextLinesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t attribute = _guest.Get(DS.textAttribute);
  const PrintedLines printed = PrintTextLines(_guest.State(), regs.si, regs.di, regs.cx);
  regs.si = printed.end;
  regs.di = printed.nextLine;
  regs.cx = 0;
  regs.es = Guest::VIDEO_SEGMENT;
  // The last PrintTextModeString leaves the attribute in AH and the NUL in AL.
  regs.ax = Join(attribute, 0);
  _guest.Clobber(PRESERVES_ALL);
}

void ReadTextLineEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = ReadTextLine(_guest.State(), _guest.Devices(), regs.si, Low(regs.cx), regs.es, regs.di);
  // RedrawInputLine, which it always calls, leaves ES on the text page in the text layout.
  if (_guest.Get(DS.screenLayout) == TEXT_LAYOUT)
  {
    regs.es = Guest::VIDEO_SEGMENT;
  }
  _guest.Clobber(CLOBBERS_AX_CX_DX);
}

void ToggleInputCursorEntry(Guest& _guest)
{
  ToggleInputCursor(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void RedrawInputLineEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const bool textLayout = _guest.Get(DS.screenLayout) == TEXT_LAYOUT;
  const RedrawnInputLine redrawn = RedrawInputLine(_guest.State(), regs.si, regs.bx, regs.es, regs.di);
  // PUSH DI / SI / BX and their POPs keep those; the second PrintStringForLayout leaves AX, and in the text layout ES.
  if (textLayout)
  {
    regs.es = Guest::VIDEO_SEGMENT;
    regs.ax = Join(_guest.Get(DS.textAttribute), 0);
  }
  else
  {
    const std::uint16_t ax = DrawnScreenStringAx(_guest.State(), regs.si, redrawn.line, MESSAGE_INK, regs.ax);
    regs.ax = DrawnScreenStringAx(_guest.State(), DS.inputCursorText.offset, redrawn.cursor, MESSAGE_INK, ax);
  }
  _guest.Clobber(PRESERVES_ALL);
}

void PrintStringForLayoutEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const bool textLayout = _guest.Get(DS.screenLayout) == TEXT_LAYOUT;
  const std::uint16_t text = regs.si;
  const PrintedText printed = PrintStringForLayout(_guest.State(), regs.si, regs.bx, regs.es, regs.di);
  regs.si = printed.end;
  regs.di = printed.nextCell;
  if (textLayout)
  {
    // PrintTextModeString's: ES the text page, and AX the attribute it printed in with the NUL in AL.
    regs.es = Guest::VIDEO_SEGMENT;
    regs.ax = Join(_guest.Get(DS.textAttribute), 0);
  }
  else
  {
    regs.ax = DrawnScreenStringAx(_guest.State(), text, printed, regs.bx, regs.ax);
  }
  _guest.Clobber(PRESERVES_ALL);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x3130, "DrawViewChar", &DrawViewCharEntry, PRESERVES_ALL},
  NativeEntry{0x31EC, "DrawViewString", &DrawViewStringEntry, PRESERVES_ALL},
  NativeEntry{0x31F9, "DrawScreenChar", &DrawScreenCharEntry, CLOBBERS_SI},
  NativeEntry{0x32D8, "DrawScreenString", &DrawScreenStringEntry, PRESERVES_ALL},
  NativeEntry{0x3407, "FormatDecimal5", &FormatDecimal5Entry, PRESERVES_ALL},
  NativeEntry{0x3432, "BlankLeadingZeros", &BlankLeadingZerosEntry, PRESERVES_ALL},
  NativeEntry{0x349A, "DrawSmallViewChar", &DrawSmallViewCharEntry, PRESERVES_ALL},
  NativeEntry{0x3527, "DrawSmallViewString", &DrawSmallViewStringEntry, PRESERVES_ALL},
  NativeEntry{0x3543, "FormatCredits", &FormatCreditsEntry, PRESERVES_ALL},
  NativeEntry{0x35A3, "UpdateMessageLine", &UpdateMessageLineEntry, CLOBBERS_ALL_BUT_ES},
  NativeEntry{0x3609, "ClearMessageLine", &ClearMessageLineEntry, CLOBBERS_ALL_BUT_ES},
  NativeEntry{0x3626, "ShowBountyMessage", &ShowBountyMessageEntry, PRESERVES_ALL},
  NativeEntry{0x364D, "ShowShipIdentity", &ShowShipIdentityEntry, PRESERVES_ALL},
  NativeEntry{0x60D2, "PrintTextModeString", &PrintTextModeStringEntry, PRESERVES_ALL},
  NativeEntry{0x6328, "ToggleMenuRowHighlight", &ToggleMenuRowHighlightEntry, PRESERVES_ALL},
  NativeEntry{0x6553, "ClearDockedMessageLine", &ClearDockedMessageLineEntry, CLOBBERS_AX_CX_DI},
  NativeEntry{0x6580, "SwapTextAttributeNibbles", &SwapTextAttributeNibblesEntry, PRESERVES_ALL},
  NativeEntry{0x65FA, "PrintCountedTextLines", &PrintCountedTextLinesEntry, PRESERVES_ALL},
  NativeEntry{0x69B3, "FormatTenths", &FormatTenthsEntry, PRESERVES_ALL},
  NativeEntry{0x6DDE, "PrintTextLines", &PrintTextLinesEntry, PRESERVES_ALL},
  // ReadTextLine waits for keys as a rule.
  NativeEntry{0x7694, "ReadTextLine", &ReadTextLineEntry, CLOBBERS_AX_CX_DX, Machine::NativeReturn::Near, 0, Machine::NativeWait::Always},
  NativeEntry{0x7727, "ToggleInputCursor", &ToggleInputCursorEntry, PRESERVES_ALL},
  NativeEntry{0x773A, "RedrawInputLine", &RedrawInputLineEntry, PRESERVES_ALL},
  NativeEntry{0x7750, "PrintStringForLayout", &PrintStringForLayoutEntry, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> TextEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
