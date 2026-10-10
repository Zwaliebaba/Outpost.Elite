#include "pch.h"

#include "Text.h"

#include "Arithmetic.h"
#include "DataOverlay.h"

#include <utility>

namespace Elite
{

namespace
{

constexpr std::uint16_t UPDATE_WARNINGS = 0x36B6;
constexpr std::uint16_t IS_DEBRIS_TYPE = 0x53FE;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t RESET_KEYBOARD = 0x7668;
constexpr std::uint16_t TOGGLE_INPUT_CURSOR = 0x7727;
constexpr std::uint16_t REDRAW_INPUT_LINE = 0x773A;
constexpr std::uint16_t WAIT_FOR_TIMER_TICK = 0x7772;

// Where ReadTextLine's loop jumps back to: the blink restarted, a key read, the blink counted, the line
// redrawn.
constexpr std::uint16_t INPUT_RESTART_BLINK = 0x76A3;
constexpr std::uint16_t INPUT_READ_KEY = 0x76AF;
constexpr std::uint16_t INPUT_COUNT_BLINK = 0x76B7;
constexpr std::uint16_t INPUT_REDRAW = 0x76FF;
constexpr std::uint16_t CURSOR_BLINK_TICKS = 300;
constexpr std::uint8_t FIRST_UNMAPPED_SCAN = 0x54; // scanCodeToAscii's length
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
constexpr std::uint16_t TENTHS_BLANKS = 3;
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

// ReadTextLine's redraw (0x76FF): the line and the cursor printed, then back to counting the blink.
void RedrawTypedLine(Guest& _guest)
{
  _guest.Call(REDRAW_INPUT_LINE);
  _guest.JumpBack(INPUT_COUNT_BLINK);
}

// ReadTextLine's blink (0x76A3): 300 ticks to the next, the cursor flipped, and the line redrawn.
void RestartInputBlink(Guest& _guest)
{
  _guest.Set(DS.cursorBlinkTicks, CURSOR_BLINK_TICKS);
  _guest.Call(TOGGLE_INPUT_CURSOR);
  RedrawTypedLine(_guest);
}

// MessageLine's second half (0x35B9): the message at messagePointer, unless it is already shown.
void ShowMessage(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.messageDrawn, MESSAGE_DRAWN);
  regs.si = _guest.Get(DS.messagePointer);
  const bool shown = regs.si == _guest.Get(DS.messageShown);
  _guest.Set(DS.messageShown, regs.si);
  if (shown)
  {
    return;
  }
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = Guest::VIDEO_SEGMENT;
  const std::uint16_t message = regs.si;
  ClearMessageLine(_guest);
  regs.si = message;
  regs.di = MESSAGE_LINE_OFFSET;
  regs.bx = MESSAGE_INK;
  DrawScreenString(_guest);
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

void DrawViewString(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  for (;;)
  {
    regs.ax = WithLow(regs.ax, _guest.Byte(regs.si));
    if (Low(regs.ax) == 0)
    {
      return;
    }
    ++regs.si;
    DrawViewCharEntry(_guest);
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

void DrawScreenString(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  for (;;)
  {
    regs.ax = WithLow(regs.ax, _guest.Byte(regs.si));
    if (Low(regs.ax) == 0)
    {
      return;
    }
    ++regs.si;
    const std::uint16_t next = regs.si;
    DrawScreenCharEntry(_guest);
    regs.si = next;
  }
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

void DrawSmallViewString(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t cx = regs.cx;
  regs.cx = regs.di;
  regs.di = static_cast<std::uint16_t>(regs.di >> 2);
  regs.cx = WithLow(regs.cx, static_cast<std::uint8_t>(~(Low(regs.cx) << 1) & SMALL_GLYPH_SHIFT));
  for (;;)
  {
    regs.ax = WithLow(regs.ax, _guest.Byte(regs.si));
    if (Low(regs.ax) == 0)
    {
      break;
    }
    ++regs.si;
    DrawSmallViewCharEntry(_guest);
  }
  regs.cx = cx;
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

void UpdateMessageLine(Guest& _guest)
{
  _guest.Call(UPDATE_WARNINGS);
  if (_guest.Get(DS.messageDrawn) != 0)
  {
    // messageFrames is a byte here, though ShowBountyMessage writes it as a word.
    const std::uint8_t frames = _guest.Byte(DS.messageFrames.offset);
    if (frames != 0)
    {
      _guest.SetByte(DS.messageFrames.offset, static_cast<std::uint8_t>(frames - 1));
      return;
    }
    Machine::Registers& regs = _guest.Regs();
    const std::uint16_t view = _guest.Get(DS.viewAngle);
    regs.si = view == 0           ? DS.frontViewText.offset
              : view == VIEW_REAR ? DS.rearViewText.offset
              : view == VIEW_LEFT ? DS.leftViewText.offset
                                  : DS.rightViewText.offset;
    _guest.Set(DS.messagePointer, regs.si);
  }
  ShowMessage(_guest);
}

void ClearMessageLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = MESSAGE_TO_ODD_BANK;
  regs.dx = MESSAGE_TO_EVEN_BANK;
  regs.bp = MESSAGE_LINE_ROWS;
  regs.di = MESSAGE_LINE_OFFSET;
  regs.si = MESSAGE_LINE_WORDS;
  regs.ax = 0;
  do
  {
    regs.cx = regs.si;
    regs.di = StoreWords(_guest.State(), regs.es, regs.di, regs.cx, regs.ax, _guest.Flag(Machine::FLAG_DIRECTION));
    regs.cx = 0;
    regs.di = static_cast<std::uint16_t>(regs.di + regs.bx);
    std::swap(regs.bx, regs.dx);
    --regs.bp;
  } while (regs.bp != 0);
}

void ShowBountyMessage(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto digits = static_cast<std::uint16_t>(DS.bountyText.offset + BOUNTY_DIGITS_OFFSET);
  regs.di = digits;
  FormatDecimal5Entry(_guest);
  regs.di = digits;
  regs.cx = TENTHS_BLANKS;
  BlankLeadingZerosEntry(_guest);
  regs.di = digits;
  // The tenths digit moves right, and the point takes its place, as one word.
  const auto tenths = static_cast<std::uint16_t>(digits + 4);
  regs.ax = static_cast<std::uint16_t>((_guest.Byte(tenths) << 8) | '.');
  _guest.SetWord(tenths, regs.ax);
  regs.ax = DS.bountyText.offset;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, BOUNTY_FRAMES);
}

void ShowShipIdentity(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (High(regs.ax) == CLASS_SIMPLE_OR_DEBRIS)
  {
    _guest.Push(regs.ax);
    _guest.Call(IS_DEBRIS_TYPE);
    regs.ax = _guest.Pop();
    if (!_guest.Flag(Machine::FLAG_ZERO))
    {
      SetHigh(regs.ax, CLASS_SIMPLE);
    }
  }
  if (High(regs.ax) == CLASS_ROCK && Low(regs.ax) == TYPE_HERMIT)
  {
    SetHigh(regs.ax, CLASS_HERMIT);
  }
  if (Low(regs.ax) == TYPE_POLICE)
  {
    SetHigh(regs.ax, CLASS_POLICE);
  }
  // The class's name through AH, then the type's through AL.
  regs.bx = DS.shipClassNames.At(High(regs.ax));
  regs.di = Offset(DS.shipIdentityText.offset, CLASS_TEXT_OFFSET);
  for (regs.cx = CLASS_NAME_BYTES; regs.cx != 0; --regs.cx)
  {
    SetHigh(regs.ax, _guest.Byte(regs.bx));
    ++regs.bx;
    _guest.SetByte(regs.di, High(regs.ax));
    ++regs.di;
  }
  regs.ax = static_cast<std::uint16_t>(Low(regs.ax) << 2);
  regs.bx = static_cast<std::uint16_t>(regs.ax * 3);
  regs.ax = DS.shipTypeNames.offset;
  regs.bx = Offset(regs.bx, regs.ax);
  regs.di = Offset(DS.shipIdentityText.offset, TYPE_TEXT_OFFSET);
  for (regs.cx = TYPE_NAME_BYTES; regs.cx != 0; --regs.cx)
  {
    SetLow(regs.ax, _guest.Byte(regs.bx));
    ++regs.bx;
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.di;
  }
  regs.ax = DS.shipIdentityText.offset;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, IDENTITY_FRAMES);
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

void ToggleMenuRowHighlight(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SwapTextAttributeNibblesEntry(_guest);
  std::uint16_t attribute = regs.si;
  for (std::uint16_t cell = 0; cell < MENU_ROW_ATTRIBUTES; ++cell)
  {
    _guest.SetFarByte(regs.es, attribute, Low(regs.ax));
    attribute = static_cast<std::uint16_t>(attribute + 2);
  }
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

void PrintCountedTextLines(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = _guest.Byte(regs.si);
  ++regs.si;
  do
  {
    const std::uint16_t line = regs.di;
    PrintTextModeStringEntry(_guest);
    regs.di = static_cast<std::uint16_t>(line + TEXT_ROW_BYTES);
    ++regs.si;
    --regs.cx;
  } while (regs.cx != 0);
}

void FormatTenths(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = DS.priceText.offset;
  FormatDecimal5Entry(_guest);
  regs.di = DS.priceText.offset;
  regs.cx = TENTHS_BLANKS;
  BlankLeadingZerosEntry(_guest);
  regs.ax = WithLow(regs.ax, _guest.Get(DS.data8040));
  _guest.Set(DS.data8041, Low(regs.ax));
  regs.ax = WithLow(regs.ax, '.');
  _guest.Set(DS.data8040, Low(regs.ax));
}

void PrintTextLines(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  do
  {
    const std::uint16_t line = regs.di;
    PrintTextModeStringEntry(_guest);
    ++regs.si;
    regs.di = Offset(line, TEXT_ROW_BYTES);
  } while (--regs.cx != 0);
}

void ReadTextLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(RESET_KEYBOARD);
  _guest.Set(DS.textPaperPattern, 0);
  _guest.Set(DS.inputCharsLeft, Low(regs.cx));
  regs.bx = 0;
  RestartInputBlink(_guest);
  for (;;)
  {
    const auto blink = static_cast<std::uint16_t>(_guest.Get(DS.cursorBlinkTicks) - 1);
    _guest.Set(DS.cursorBlinkTicks, blink);
    if (blink == 0)
    {
      _guest.JumpBack(INPUT_RESTART_BLINK);
      RestartInputBlink(_guest);
      continue;
    }
    _guest.JumpBack(INPUT_READ_KEY);
    _guest.Call(WAIT_FOR_TIMER_TICK);
    _guest.Call(GET_KEY);
    if (_guest.Flag(Machine::FLAG_ZERO))
    {
      continue;
    }
    if (High(regs.ax) >= FIRST_UNMAPPED_SCAN)
    {
      _guest.JumpBack(INPUT_COUNT_BLINK);
      continue;
    }
    // The key's character: inc dl / je skips FFh, a key with none, and leaves DL zero.
    regs.cx = DS.scanCodeToAscii.At(High(regs.ax));
    SetLow(regs.dx, static_cast<std::uint8_t>(_guest.Byte(regs.cx) + 1));
    if (Low(regs.dx) == 0)
    {
      _guest.JumpBack(INPUT_COUNT_BLINK);
      continue;
    }
    SetLow(regs.dx, static_cast<std::uint8_t>(Low(regs.dx) - 1));
    const std::uint8_t character = Low(regs.dx);
    if (character == ENTER_KEY)
    {
      if (_guest.Byte(DS.inputCursorText.offset) != CURSOR_HIDDEN)
      {
        _guest.Call(TOGGLE_INPUT_CURSOR);
        _guest.Call(REDRAW_INPUT_LINE);
      }
      return;
    }
    if (character == BACKSPACE_KEY)
    {
      if (regs.bx != 0)
      {
        --regs.bx;
        _guest.Set(DS.inputCharsLeft, static_cast<std::uint8_t>(_guest.Get(DS.inputCharsLeft) + 1));
        _guest.Call(REDRAW_INPUT_LINE);
      }
      _guest.JumpBack(INPUT_COUNT_BLINK);
      continue;
    }
    if (character >= 'A' && character <= 'Z')
    {
      // Lower case, unless GetKey's AL says Shift was held: shr al,1 into the carry.
      SetLow(regs.dx, static_cast<std::uint8_t>(character | LOWER_CASE_BIT));
      const bool shift = (Low(regs.ax) & 1) != 0;
      SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) >> 1));
      if (shift)
      {
        SetLow(regs.dx, static_cast<std::uint8_t>(Low(regs.dx) & UPPER_CASE_MASK));
      }
    }
    _guest.SetByte(Offset(regs.bx, regs.si), Low(regs.dx));
    ++regs.bx;
    const auto left = static_cast<std::uint8_t>(_guest.Get(DS.inputCharsLeft) - 1);
    _guest.Set(DS.inputCharsLeft, left);
    if ((left & 0x80) != 0)
    {
      // No room: the character is taken back.
      --regs.bx;
      _guest.Set(DS.inputCharsLeft, static_cast<std::uint8_t>(left + 1));
      _guest.JumpBack(INPUT_REDRAW);
    }
    RedrawTypedLine(_guest);
  }
}

void ToggleInputCursor(GameState& _state)
{
  const std::uint16_t cursor = DS.inputCursorText.offset;
  const std::uint8_t toggle = _state.Get(DS.screenLayout) == TEXT_LAYOUT ? CURSOR_TEXT_TOGGLE : CURSOR_GRAPHICS_TOGGLE;
  _state.SetByte(cursor, static_cast<std::uint8_t>(_state.Byte(cursor) ^ toggle));
}

void RedrawInputLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t di = regs.di;
  const std::uint16_t si = regs.si;
  const std::uint16_t bx = regs.bx;
  _guest.SetByte(static_cast<std::uint16_t>(regs.bx + regs.si), 0);
  regs.bx = MESSAGE_INK;
  PrintStringForLayout(_guest);
  regs.si = DS.inputCursorText.offset;
  PrintStringForLayout(_guest);
  regs.bx = bx;
  regs.si = si;
  regs.di = di;
}

void PrintStringForLayout(Guest& _guest)
{
  if (_guest.Get(DS.screenLayout) == TEXT_LAYOUT)
  {
    PrintTextModeStringEntry(_guest);
    return;
  }
  DrawScreenString(_guest);
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
constexpr Machine::NativeContract CLOBBERS_AX_CX{REGISTER_AX | REGISTER_CX, 0};
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

void DrawScreenCharEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t character = Low(regs.ax);
  regs.di = DrawScreenChar(_guest.State(), character, regs.bx, regs.es, regs.di);
  // The original leaves the last row it drew in AX, and DrawScreenString keeps its high byte.
  regs.ax = GlyphRow(_guest.State(), character, GLYPH_ROWS - 1, regs.bx, _guest.Get(DS.textPaperPattern));
  _guest.Clobber(CLOBBERS_SI);
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

void FormatCreditsEntry(Guest& _guest)
{
  FormatCredits(_guest.State());
  _guest.Regs().si = DS.creditBalanceText.offset;
  _guest.Clobber(PRESERVES_ALL);
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

void ToggleInputCursorEntry(Guest& _guest)
{
  ToggleInputCursor(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x3130, "DrawViewChar", &DrawViewCharEntry, PRESERVES_ALL},
  NativeEntry{0x31EC, "DrawViewString", &DrawViewString, PRESERVES_ALL},
  NativeEntry{0x31F9, "DrawScreenChar", &DrawScreenCharEntry, CLOBBERS_SI},
  NativeEntry{0x32D8, "DrawScreenString", &DrawScreenString, PRESERVES_ALL},
  NativeEntry{0x3407, "FormatDecimal5", &FormatDecimal5Entry, PRESERVES_ALL},
  NativeEntry{0x3432, "BlankLeadingZeros", &BlankLeadingZerosEntry, PRESERVES_ALL},
  NativeEntry{0x349A, "DrawSmallViewChar", &DrawSmallViewCharEntry, PRESERVES_ALL},
  NativeEntry{0x3527, "DrawSmallViewString", &DrawSmallViewString, PRESERVES_ALL},
  NativeEntry{0x3543, "FormatCredits", &FormatCreditsEntry, PRESERVES_ALL},
  NativeEntry{0x35A3, "UpdateMessageLine", &UpdateMessageLine, CLOBBERS_ALL_BUT_ES},
  NativeEntry{0x3609, "ClearMessageLine", &ClearMessageLine, CLOBBERS_ALL_BUT_ES},
  NativeEntry{0x3626, "ShowBountyMessage", &ShowBountyMessage, PRESERVES_ALL},
  NativeEntry{0x364D, "ShowShipIdentity", &ShowShipIdentity, PRESERVES_ALL},
  NativeEntry{0x60D2, "PrintTextModeString", &PrintTextModeStringEntry, PRESERVES_ALL},
  NativeEntry{0x6328, "ToggleMenuRowHighlight", &ToggleMenuRowHighlight, PRESERVES_ALL},
  NativeEntry{0x6553, "ClearDockedMessageLine", &ClearDockedMessageLineEntry, CLOBBERS_AX_CX_DI},
  NativeEntry{0x6580, "SwapTextAttributeNibbles", &SwapTextAttributeNibblesEntry, PRESERVES_ALL},
  NativeEntry{0x65FA, "PrintCountedTextLines", &PrintCountedTextLines, CLOBBERS_AX_CX},
  NativeEntry{0x69B3, "FormatTenths", &FormatTenths, PRESERVES_ALL},
  NativeEntry{0x6DDE, "PrintTextLines", &PrintTextLines, PRESERVES_ALL},
  // ReadTextLine waits for keys as a rule.
  NativeEntry{0x7694, "ReadTextLine", &ReadTextLine, CLOBBERS_AX_CX_DX, Machine::NativeReturn::Near, 0, Machine::NativeWait::Always},
  NativeEntry{0x7727, "ToggleInputCursor", &ToggleInputCursorEntry, PRESERVES_ALL},
  NativeEntry{0x773A, "RedrawInputLine", &RedrawInputLine, PRESERVES_ALL},
  NativeEntry{0x7750, "PrintStringForLayout", &PrintStringForLayout, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> TextEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
