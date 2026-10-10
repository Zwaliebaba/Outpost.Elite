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

// REP STOSW at ES:DI, forwards or, with DF set, backwards.
void StoreWords(Guest& _guest, std::uint16_t _value)
{
  Machine::Registers& regs = _guest.Regs();
  const auto step = static_cast<std::uint16_t>((regs.flags & Machine::FLAG_DIRECTION) != 0 ? 0xFFFE : 2);
  for (; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarWord(regs.es, regs.di, _value);
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
}

// The original's jump back to CS:_target, in a routine that waits: the end of a loop's turn, with IP where
// the jump lands, so that paced time sees the turn as it sees the original's (Guest::LoopTurn).
void JumpBack(Guest& _guest, std::uint16_t _target)
{
  _guest.Regs().ip = _target;
  _guest.LoopTurn();
}

// ReadTextLine's redraw (0x76FF): the line and the cursor printed, then back to counting the blink.
void RedrawTypedLine(Guest& _guest)
{
  _guest.Call(REDRAW_INPUT_LINE);
  JumpBack(_guest, INPUT_COUNT_BLINK);
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

void DrawViewChar(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint16_t glyph = GlyphOffset(Low(regs.ax));
  const std::uint16_t paper = _guest.Get(DS.textPaperPattern);
  std::uint16_t address = regs.di;
  std::uint16_t row = 0;
  for (std::uint16_t line = 0; line < GLYPH_ROWS; ++line)
  {
    row = InkOnPaper(_guest.Word(glyph), regs.bx, paper);
    glyph = static_cast<std::uint16_t>(glyph + 2);
    _guest.SetWord(address, row);
    address = static_cast<std::uint16_t>(address + VIEW_ROW_BYTES);
  }
  regs.ax = row;
  regs.bp = paper;
  regs.di = static_cast<std::uint16_t>(regs.di + 2);
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
    DrawViewChar(_guest);
  }
}

void DrawScreenChar(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = GlyphOffset(Low(regs.ax));
  std::uint16_t address = regs.di;
  std::uint16_t down = CGA_TO_ODD_BANK;
  std::uint16_t up = CGA_TO_EVEN_BANK;
  for (std::uint16_t line = 0; line < GLYPH_ROWS; ++line)
  {
    regs.ax = InkOnPaper(_guest.Word(regs.si), regs.bx, _guest.Get(DS.textPaperPattern));
    regs.si = static_cast<std::uint16_t>(regs.si + 2);
    _guest.SetFarWord(regs.es, address, regs.ax);
    if (line + 1 < GLYPH_ROWS)
    {
      address = static_cast<std::uint16_t>(address + down);
      std::swap(down, up);
    }
  }
  regs.di = static_cast<std::uint16_t>(regs.di + 2);
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
    DrawScreenChar(_guest);
    regs.si = next;
  }
}

void FormatDecimal5(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint16_t value = regs.ax;
  std::uint16_t divisors = DS.decimalDivisors16.offset;
  std::uint16_t digit = regs.di;
  for (;;)
  {
    const std::uint16_t divisor = _guest.Word(divisors);
    divisors = static_cast<std::uint16_t>(divisors + 2);
    if (divisor == 1)
    {
      break;
    }
    // Repeated subtraction until it borrows: one count more than the quotient, less 1 again by adding 2Fh.
    const auto subtractions = static_cast<std::uint8_t>(value / divisor + 1);
    value = static_cast<std::uint16_t>(value % divisor);
    _guest.SetByte(digit, static_cast<std::uint8_t>(subtractions + 0x2F));
    digit = static_cast<std::uint16_t>(digit + 1);
  }
  regs.ax = WithLow(value, static_cast<std::uint8_t>(Low(value) + '0'));
  _guest.SetByte(digit, Low(regs.ax));
}

void BlankLeadingZeros(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = WithHigh(regs.cx, 0);
  do
  {
    if (_guest.Byte(regs.di) >= BLANK_BELOW)
    {
      return;
    }
    _guest.SetByte(regs.di, ' ');
    ++regs.di;
    --regs.cx;
  } while (regs.cx != 0);
}

void DrawSmallViewChar(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto letter = static_cast<std::uint8_t>(Low(regs.ax) - FIRST_SMALL_LETTER);
  std::uint16_t glyph = DS.smallFont.At(letter);
  const std::uint8_t shift = Low(regs.cx);
  std::uint16_t address = regs.di;
  std::uint16_t mask = letter;
  std::uint16_t pixels = 0;
  for (std::uint16_t line = 0; line < SMALL_GLYPH_ROWS; ++line)
  {
    // The row is a big-endian word, shifted into place and swapped back to memory order.
    const std::uint16_t shifted = ShiftLeft(_guest.Word(glyph), shift);
    pixels = static_cast<std::uint16_t>((shifted >> 8) | (shifted << 8));
    mask = static_cast<std::uint16_t>(~pixels);
    glyph = static_cast<std::uint16_t>(glyph + 2);
    pixels = static_cast<std::uint16_t>(pixels & regs.bx);
    _guest.SetWord(address, static_cast<std::uint16_t>((_guest.Word(address) & mask) | pixels));
    address = static_cast<std::uint16_t>(address + VIEW_ROW_BYTES);
  }
  regs.ax = pixels;
  regs.bp = mask;
  regs.cx = WithLow(regs.cx, static_cast<std::uint8_t>(shift ^ SMALL_GLYPH_SHIFT));
  regs.di = static_cast<std::uint16_t>(regs.di + (Low(regs.cx) == 0 ? 1 : 2));
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
    DrawSmallViewChar(_guest);
  }
  regs.cx = cx;
}

void FormatCredits(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t text = DS.creditBalanceText.offset;
  std::uint32_t value = _guest.Word(DS.creditsTenths.offset) | (std::uint32_t{_guest.Get(DS.data75F5)} << 16);
  std::uint16_t divisors = DS.decimalDivisors32.offset;
  std::uint16_t digit = text;
  for (;;)
  {
    const std::uint16_t low = _guest.Word(divisors);
    const std::uint16_t high = _guest.Word(static_cast<std::uint16_t>(divisors + 2));
    divisors = static_cast<std::uint16_t>(divisors + 4);
    if (low == 1) // only the low word is tested
    {
      break;
    }
    const std::uint32_t divisor = low | (std::uint32_t{high} << 16);
    const auto subtractions = static_cast<std::uint8_t>(value / divisor + 1);
    value %= divisor;
    _guest.SetByte(digit, static_cast<std::uint8_t>(subtractions + 0x2F));
    digit = static_cast<std::uint16_t>(digit + 1);
  }
  _guest.SetByte(digit, static_cast<std::uint8_t>(value + '0'));

  std::uint16_t blank = text;
  for (std::uint16_t left = CREDIT_BLANKS; left != 0 && _guest.Byte(blank) == '0'; --left)
  {
    _guest.SetByte(blank, ' ');
    ++blank;
  }
  // The tenths move one place right for the point.
  const auto tenths = static_cast<std::uint16_t>(text + CREDIT_DIGITS - 1);
  _guest.SetByte(static_cast<std::uint16_t>(tenths + 1), _guest.Byte(tenths));
  _guest.SetByte(tenths, '.');
  regs.si = text;
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
    StoreWords(_guest, regs.ax);
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
  FormatDecimal5(_guest);
  regs.di = digits;
  regs.cx = TENTHS_BLANKS;
  BlankLeadingZeros(_guest);
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

void PrintTextModeString(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = Guest::VIDEO_SEGMENT;
  regs.ax = WithHigh(regs.ax, _guest.Get(DS.textAttribute));
  for (;;)
  {
    regs.ax = WithLow(regs.ax, _guest.Byte(regs.si));
    if (Low(regs.ax) == 0)
    {
      return;
    }
    ++regs.si;
    _guest.SetFarWord(regs.es, regs.di, regs.ax);
    regs.di = static_cast<std::uint16_t>(regs.di + 2);
  }
}

void ToggleMenuRowHighlight(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SwapTextAttributeNibbles(_guest);
  std::uint16_t attribute = regs.si;
  for (std::uint16_t cell = 0; cell < MENU_ROW_ATTRIBUTES; ++cell)
  {
    _guest.SetFarByte(regs.es, attribute, Low(regs.ax));
    attribute = static_cast<std::uint16_t>(attribute + 2);
  }
}

void ClearDockedMessageLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = DOCKED_MESSAGE_OFFSET;
  SetLow(regs.ax, ' ');
  for (regs.cx = DOCKED_MESSAGE_CHARACTERS; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarByte(regs.es, regs.di, Low(regs.ax));
    regs.di = Offset(regs.di, 2);
  }
}

void SwapTextAttributeNibbles(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t attribute = _guest.Get(DS.textAttribute);
  const auto swapped = static_cast<std::uint8_t>((attribute >> 4) | (attribute << 4));
  regs.ax = WithLow(regs.ax, swapped);
  _guest.Set(DS.textAttribute, swapped);
}

void PrintCountedTextLines(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = _guest.Byte(regs.si);
  ++regs.si;
  do
  {
    const std::uint16_t line = regs.di;
    PrintTextModeString(_guest);
    regs.di = static_cast<std::uint16_t>(line + TEXT_ROW_BYTES);
    ++regs.si;
    --regs.cx;
  } while (regs.cx != 0);
}

void FormatTenths(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = DS.priceText.offset;
  FormatDecimal5(_guest);
  regs.di = DS.priceText.offset;
  regs.cx = TENTHS_BLANKS;
  BlankLeadingZeros(_guest);
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
    PrintTextModeString(_guest);
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
      JumpBack(_guest, INPUT_RESTART_BLINK);
      RestartInputBlink(_guest);
      continue;
    }
    JumpBack(_guest, INPUT_READ_KEY);
    _guest.Call(WAIT_FOR_TIMER_TICK);
    _guest.Call(GET_KEY);
    if (_guest.Flag(Machine::FLAG_ZERO))
    {
      continue;
    }
    if (High(regs.ax) >= FIRST_UNMAPPED_SCAN)
    {
      JumpBack(_guest, INPUT_COUNT_BLINK);
      continue;
    }
    // The key's character: inc dl / je skips FFh, a key with none, and leaves DL zero.
    regs.cx = DS.scanCodeToAscii.At(High(regs.ax));
    SetLow(regs.dx, static_cast<std::uint8_t>(_guest.Byte(regs.cx) + 1));
    if (Low(regs.dx) == 0)
    {
      JumpBack(_guest, INPUT_COUNT_BLINK);
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
      JumpBack(_guest, INPUT_COUNT_BLINK);
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
      JumpBack(_guest, INPUT_REDRAW);
    }
    RedrawTypedLine(_guest);
  }
}

void ToggleInputCursor(Guest& _guest)
{
  const std::uint16_t cursor = DS.inputCursorText.offset;
  const std::uint8_t toggle = _guest.Get(DS.screenLayout) == TEXT_LAYOUT ? CURSOR_TEXT_TOGGLE : CURSOR_GRAPHICS_TOGGLE;
  _guest.SetByte(cursor, static_cast<std::uint8_t>(_guest.Byte(cursor) ^ toggle));
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
    PrintTextModeString(_guest);
    return;
  }
  DrawScreenString(_guest);
}

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BP{REGISTER_AX | REGISTER_BP, 0};
constexpr Machine::NativeContract CLOBBERS_AX_SI{REGISTER_AX | REGISTER_SI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX{REGISTER_AX | REGISTER_CX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DX{REGISTER_AX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_ALL_BUT_ES{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0};

constexpr std::array ENTRIES = {
  NativeEntry{0x3130, "DrawViewChar", &DrawViewChar, CLOBBERS_AX_BP},
  NativeEntry{0x31EC, "DrawViewString", &DrawViewString, PRESERVES_ALL},
  NativeEntry{0x31F9, "DrawScreenChar", &DrawScreenChar, CLOBBERS_AX_SI},
  NativeEntry{0x32D8, "DrawScreenString", &DrawScreenString, PRESERVES_ALL},
  NativeEntry{0x3407, "FormatDecimal5", &FormatDecimal5, CLOBBERS_AX},
  NativeEntry{0x3432, "BlankLeadingZeros", &BlankLeadingZeros, PRESERVES_ALL},
  NativeEntry{0x349A, "DrawSmallViewChar", &DrawSmallViewChar, PRESERVES_ALL},
  NativeEntry{0x3527, "DrawSmallViewString", &DrawSmallViewString, PRESERVES_ALL},
  NativeEntry{0x3543, "FormatCredits", &FormatCredits, PRESERVES_ALL},
  NativeEntry{0x35A3, "UpdateMessageLine", &UpdateMessageLine, CLOBBERS_ALL_BUT_ES},
  NativeEntry{0x3609, "ClearMessageLine", &ClearMessageLine, CLOBBERS_ALL_BUT_ES},
  NativeEntry{0x3626, "ShowBountyMessage", &ShowBountyMessage, PRESERVES_ALL},
  NativeEntry{0x364D, "ShowShipIdentity", &ShowShipIdentity, PRESERVES_ALL},
  NativeEntry{0x60D2, "PrintTextModeString", &PrintTextModeString, CLOBBERS_AX},
  NativeEntry{0x6328, "ToggleMenuRowHighlight", &ToggleMenuRowHighlight, PRESERVES_ALL},
  NativeEntry{0x6553, "ClearDockedMessageLine", &ClearDockedMessageLine, CLOBBERS_AX_CX_DI},
  NativeEntry{0x6580, "SwapTextAttributeNibbles", &SwapTextAttributeNibbles, PRESERVES_ALL},
  NativeEntry{0x65FA, "PrintCountedTextLines", &PrintCountedTextLines, CLOBBERS_AX_CX},
  NativeEntry{0x69B3, "FormatTenths", &FormatTenths, PRESERVES_ALL},
  NativeEntry{0x6DDE, "PrintTextLines", &PrintTextLines, PRESERVES_ALL},
  // ReadTextLine waits for keys as a rule.
  NativeEntry{0x7694, "ReadTextLine", &ReadTextLine, CLOBBERS_AX_CX_DX, Machine::NativeReturn::Near, 0, Machine::NativeWait::Always},
  NativeEntry{0x7727, "ToggleInputCursor", &ToggleInputCursor, PRESERVES_ALL},
  NativeEntry{0x773A, "RedrawInputLine", &RedrawInputLine, PRESERVES_ALL},
  NativeEntry{0x7750, "PrintStringForLayout", &PrintStringForLayout, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> TextEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
