#include "pch.h"

#include "Video.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Maths.h"

#include <utility>

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_DIRECTION;
using Machine::FLAG_ZERO;

constexpr std::uint16_t ROW_BYTES = 0x40;      // a row of the drawing buffer, 256 2-bit pixels
constexpr std::uint16_t BUFFER_ROWS = 0x80;    // rows 0-127
constexpr std::uint8_t LEFT_PIXEL_MASK = 0xC0; // the leftmost of a byte's four pixels
constexpr std::uint16_t DRAW_BUFFER_WORDS = 0x1000;

constexpr std::uint16_t CGA_BANK_WORDS = 0xFA0; // 8000 bytes: one bank of mode 4's 200 lines
constexpr std::uint16_t CGA_ODD_BANK = 0x2000;  // the odd lines' bank
constexpr std::uint16_t TEXT_CELLS = 1000;      // 40x25
constexpr std::uint8_t SPACE = 0x20;
// The cockpit image's segment, relative to the load segment (DS:B160, cockpitImage).
constexpr std::uint16_t COCKPIT_PARAGRAPH = 0x140A;

// screenLayout: 0 the cockpit, 1 the chart frame; bit 1 set while in text mode.
constexpr std::uint8_t CHART_LAYOUT = 1;
constexpr std::uint8_t TEXT_LAYOUT_BIT = 2;

constexpr std::uint8_t BIOS_VIDEO = 0x10;
constexpr std::uint16_t CRTC_INDEX_PORT = 0x3D4;
constexpr std::uint16_t CGA_MODE_PORT = 0x3D8;
constexpr std::uint16_t CGA_COLOR_PORT = 0x3D9;

// triangleStepOpcodes (CS:1CF3), after FillTriangle's RET: SUB AX,SI; SUB BX,DI; ADD AX,SI; ADD BX,DI.
constexpr std::uint16_t SUB_AX_SI = 0x1CF3;
constexpr std::uint16_t SUB_BX_DI = 0x1CF5;
constexpr std::uint16_t ADD_AX_SI = 0x1CF7;
constexpr std::uint16_t ADD_BX_DI = 0x1CF9;
// The step instructions FillTriangle rewrites from them.
constexpr std::uint16_t STEP_EDGE_A = 0x1CC9;
constexpr std::uint16_t STEP_EDGE_B = 0x1CCB;
constexpr std::uint16_t STEP_LONG_EDGE_UPPER = 0x1E20;
constexpr std::uint16_t STEP_SHORT_EDGE_UPPER = 0x1E22;
constexpr std::uint16_t STEP_SHORT_EDGE_LOWER = 0x1E54;
constexpr std::uint16_t STEP_LONG_EDGE_LOWER = 0x1E61;

// clippedTriangleStepOpcodes (CS:1FF8): ADD SI,[m]; ADC AX,[m]; ADD DI,[m]; ADC BX,[m], then the
// SUB/SBB forms. Only each opcode and ModRM word is copied, at the site and 4 bytes on.
constexpr std::uint16_t ADD_TO_EDGE_A = 0x1FF8;
constexpr std::uint16_t ADD_TO_EDGE_B = 0x2000;
constexpr std::uint16_t SUBTRACT_FROM_EDGE_A = 0x2008;
constexpr std::uint16_t SUBTRACT_FROM_EDGE_B = 0x2010;
constexpr std::uint16_t CARRY_STEP_BYTES = 4;
// The step sites FillClippedTriangle rewrites from them.
constexpr std::uint16_t STEP_CLIPPED_EDGE_A = 0x1FC8;
constexpr std::uint16_t STEP_CLIPPED_EDGE_B = 0x1FD0;
constexpr std::uint16_t STEP_CLIPPED_UPPER_A = 0x2200;
constexpr std::uint16_t STEP_CLIPPED_UPPER_B = 0x2208;
constexpr std::uint16_t STEP_CLIPPED_LOWER_B = 0x224E;
constexpr std::uint16_t STEP_CLIPPED_LOWER_A = 0x22AE;
// clippedFirstRow and clippedLastRow before a row is drawn.
constexpr std::uint16_t NO_ROW = 0x8000;
// Row 64 * row, in AL after DrawStackedSpans shifts it: bit 6 is the row's lowest bit.
constexpr std::uint8_t ODD_ROW_BIT = 0x40;

// The frame routines wait on the CGA's status port, whose bit 3 is the vertical retrace.
constexpr std::uint16_t CGA_STATUS_PORT = 0x3DA;
constexpr std::uint8_t STATUS_VERTICAL_RETRACE = 0x08;
// What they call, through the hooks.
constexpr std::uint16_t DRAW_LASER_SIGHTS = 0x0630;
constexpr std::uint16_t PRESENT_SPACE_VIEW = 0x0599;
constexpr std::uint16_t COPY_CHART_BUFFER_TO_SCREEN = 0x05CC;
constexpr std::uint16_t CLEAR_DRAW_BUFFER = 0x060D;
constexpr std::uint16_t WAIT_RETRACE_THEN_DELAY = 0x45FF;
// Where their loops jump back to.
constexpr std::uint16_t SPACE_VIEW_COPY_LOOP = 0x05BC;
constexpr std::uint16_t CHART_RETRACE_LOOP = 0x05D0;
constexpr std::uint16_t CHART_DELAY_LOOP = 0x05D8;
constexpr std::uint16_t CHART_COPY_LOOP = 0x05FD;
constexpr std::uint16_t SPACE_VIEW_RETRACE_LOOP = 0x4602;
constexpr std::uint16_t SPACE_VIEW_DELAY_LOOP = 0x460A;
// The delays after a retrace, in DEC/JNZ turns.
constexpr std::uint16_t CHART_DELAY_TURNS = 0x2BC;
constexpr std::uint16_t SPACE_VIEW_DELAY_TURNS = 0x7D0;
// The drawing buffer's 64-byte lines onto the screen, a pair of lines (one in each bank) a turn.
constexpr std::uint16_t BUFFER_LINE_WORDS = 0x20;
constexpr std::uint16_t TO_ODD_LINE = 0x1FC0;              // from the end of an even line to the odd line below it
constexpr std::uint16_t TO_NEXT_EVEN_LINE = 0x1FF0;        // from the end of that odd line back to the next even line
constexpr std::uint16_t SPACE_VIEW_SCREEN_OFFSET = 0x01E8; // x=32, line 12
constexpr std::uint16_t SPACE_VIEW_LINE_PAIRS = 0x3F;
constexpr std::uint16_t CHART_SCREEN_OFFSET = 0x0648; // x=32, line 40
constexpr std::uint8_t CHART_LINE_PAIRS = 0x40;

// What SaveScreenshot calls, through the hooks. Reprogramming the PIT raises IRQ 0 when the timer's output is low, and
// the original's CPU takes it as soon as interrupts are on again, with whichever handler is installed then: a call
// through a hook takes it at the next entry (Pc::CallNear), as near to that as native code gets, where a C++ call
// would leave it to the game's own handler after SaveScreenshot.
constexpr std::uint16_t INSTALL_TIMER_INTERRUPT = 0x00C6;
constexpr std::uint16_t INSTALL_DIVIDE_AND_KEYBOARD_INTERRUPTS = 0x0105;
constexpr std::uint16_t RESTORE_DIVIDE_AND_KEYBOARD_INTERRUPTS = 0x0148;
constexpr std::uint16_t RESTORE_TIMER_INTERRUPT = 0x016B;
constexpr std::uint16_t WRITE_SCREENSHOT_FILE = 0x03FD;
constexpr std::uint16_t SHOW_DISK_ERROR = 0x0470;
// It puts CriticalErrorInterrupt on int 24h, at 0000:0090, while it writes.
constexpr std::uint16_t CRITICAL_ERROR_VECTOR_OFFSET = 0x0090;
constexpr std::uint16_t CRITICAL_ERROR_VECTOR_SEGMENT = 0x0092;
constexpr std::uint16_t CRITICAL_ERROR_INTERRUPT = 0x02F0;
constexpr std::uint8_t DOS_VECTOR = 0x21;
constexpr std::uint8_t DOS_SET_TRANSFER_AREA = 0x1A;
constexpr std::uint8_t DOS_CREATE = 0x3C;
constexpr std::uint8_t DOS_CLOSE = 0x3E;
constexpr std::uint8_t DOS_WRITE = 0x40;
constexpr std::uint8_t FIRST_DIGIT = '0';
constexpr std::uint8_t PAST_DIGITS = ':';               // the character after '9'
constexpr std::uint16_t TEXT_PAGE_BYTES = 0x07D0;       // 40x25 cells of character and attribute
constexpr std::uint16_t GRAPHICS_SCREEN_BYTES = 0x3F40; // both of mode 4's banks, the gap between them included

constexpr std::uint16_t SMALL_DISC_RADIUS = 5; // below it, DrawDisc draws a sprite
constexpr std::uint16_t SMALL_DISC_ROWS = 4;
constexpr std::uint8_t CLIP_SPRITE_LEFT = 0x55;  // only the sprite's right byte is drawn
constexpr std::uint8_t CLIP_SPRITE_RIGHT = 0xAA; // only its left byte is drawn
constexpr std::uint16_t CIRCLE_CHORDS = 32;

// XCHG AH,AL.
[[nodiscard]] constexpr std::uint16_t SwapBytes(std::uint16_t _word) noexcept
{
  return Join(Low(_word), High(_word));
}

[[nodiscard]] constexpr bool Negative(std::uint16_t _word) noexcept
{
  return (_word & 0x8000) != 0;
}

[[nodiscard]] constexpr bool Negative8(std::uint8_t _byte) noexcept
{
  return (_byte & 0x80) != 0;
}

[[nodiscard]] constexpr std::int16_t Signed(std::uint16_t _word) noexcept
{
  return static_cast<std::int16_t>(_word);
}

[[nodiscard]] constexpr std::int8_t Signed8(std::uint8_t _byte) noexcept
{
  return static_cast<std::int8_t>(_byte);
}

[[nodiscard]] constexpr std::uint8_t Negate8(std::uint8_t _byte) noexcept
{
  return static_cast<std::uint8_t>(0u - _byte);
}

[[nodiscard]] constexpr std::uint8_t RotateRight(std::uint8_t _byte, unsigned _count) noexcept
{
  const unsigned count = _count & 7u;
  return static_cast<std::uint8_t>((_byte >> count) | (_byte << ((8u - count) & 7u)));
}

[[nodiscard]] bool Flag(const Machine::Registers& _regs, std::uint16_t _flag) noexcept
{
  return (_regs.flags & _flag) != 0;
}

// MOV BX,colorFillBytes / ADD BL,[drawColor]: the ADD carries nothing into BH.
[[nodiscard]] std::uint16_t ColorFillByteOffset(std::uint8_t _color) noexcept
{
  const std::uint16_t table = DS.colorFillBytes.offset;
  return Join(High(table), static_cast<std::uint8_t>(Low(table) + _color));
}

// AND [_offset],_keep / OR [_offset],_color.
void Plot(Guest& _guest, std::uint16_t _offset, std::uint8_t _keep, std::uint8_t _color)
{
  _guest.SetByte(_offset, static_cast<std::uint8_t>((_guest.Byte(_offset) & _keep) | _color));
}

// ROR DL,1 twice and ROR DH,1 twice: the pixel's color and keep-mask move one pixel right. The result
// is the carry the mask's last ROR leaves, clear when the pixel has moved into the next byte.
[[nodiscard]] bool NextPixel(std::uint8_t& _color, std::uint8_t& _keep) noexcept
{
  _color = RotateRight(_color, 2);
  _keep = RotateRight(_keep, 2);
  return Negative8(_keep);
}

// What STOSB, STOSW and MOVSW add to DI (and SI): forward, or back when DF is set.
[[nodiscard]] std::uint16_t StringStep(const Machine::Registers& _regs, std::uint16_t _bytes) noexcept
{
  return Flag(_regs, FLAG_DIRECTION) ? Negate(_bytes) : _bytes;
}

// STOSB.
void StoreByte(Guest& _guest, std::uint8_t _value)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetFarByte(regs.es, regs.di, _value);
  regs.di = static_cast<std::uint16_t>(regs.di + StringStep(regs, 1));
}

// REP STOSW.
void RepeatStoreWords(Guest& _guest, std::uint16_t _value)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t step = StringStep(regs, 2);
  for (; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarWord(regs.es, regs.di, _value);
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
}

// REP MOVSW from _sourceSegment:SI.
void RepeatMoveWords(Guest& _guest, std::uint16_t _sourceSegment)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t step = StringStep(regs, 2);
  for (; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarWord(regs.es, regs.di, _guest.FarWord(_sourceSegment, regs.si));
    regs.si = static_cast<std::uint16_t>(regs.si + step);
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
}

// CWD.
void ConvertToDoubleWord(Machine::Registers& _regs) noexcept
{
  _regs.dx = static_cast<std::uint16_t>(Negative(_regs.ax) ? 0xFFFF : 0);
}

// ---- Lines ----

// The registers DrawLine works in, as bytes where it uses them so.
struct LineState
{
  std::uint8_t al = 0;
  std::uint8_t ah = 0;
  std::uint8_t bl = 0;
  std::uint8_t bh = 0;
  std::uint8_t dl = 0;
  std::uint8_t dh = 0;
  std::uint16_t cx = 0;
  std::uint16_t di = 0;
  std::uint16_t bp = 0;
};

// DrawLineHorizontal (CS:17BE): BL pixels right of the first, pixel by pixel to a byte boundary, whole
// bytes with REP STOSB, then the pixels left.
void DrawHorizontalLine(Guest& _guest, LineState& _line)
{
  if (_line.bl == 0)
  {
    Plot(_guest, _line.di, _line.dh, _line.dl);
    return;
  }
  _line.cx = static_cast<std::uint16_t>(_line.bl + 1);
  if ((_line.dh & LEFT_PIXEL_MASK) != 0)
  {
    for (;;)
    {
      _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
      if (!NextPixel(_line.dl, _line.dh))
      {
        _guest.SetByte(_line.di, _line.al);
        ++_line.di;
        if (--_line.cx == 0)
        {
          return;
        }
        break;
      }
      if (--_line.cx == 0)
      {
        _guest.SetByte(_line.di, _line.al);
        return;
      }
    }
  }
  _line.al = _line.ah;
  _line.ah = Low(_line.cx);
  _line.cx = static_cast<std::uint16_t>(_line.cx >> 2);
  if (_line.cx != 0)
  {
    Machine::Registers& regs = _guest.Regs();
    _line.bl = Low(regs.ds);
    _line.bh = High(regs.ds);
    regs.es = regs.ds;
    _guest.SetFlag(FLAG_DIRECTION, false);
    for (; _line.cx != 0; --_line.cx)
    {
      _guest.SetFarByte(regs.es, _line.di, _line.al);
      ++_line.di;
    }
  }
  _line.ah = static_cast<std::uint8_t>(_line.ah & 3);
  if (_line.ah == 0)
  {
    return;
  }
  _line.cx = _line.ah;
  _line.al = _guest.Byte(_line.di);
  do
  {
    _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
    (void)NextPixel(_line.dl, _line.dh);
  } while (--_line.cx != 0);
  _guest.SetByte(_line.di, _line.al);
}

// DrawLineVertical (CS:17AF).
void DrawVerticalLine(Guest& _guest, LineState& _line)
{
  _line.cx = _line.bh;
  do
  {
    Plot(_guest, _line.di, _line.dh, _line.dl);
    _line.di = static_cast<std::uint16_t>(_line.di + _line.bp);
  } while (--_line.cx != 0);
  Plot(_guest, _line.di, _line.dh, _line.dl);
}

// DrawLineSteep (CS:177E): a row a pixel, AH the error term.
void DrawSteepLine(Guest& _guest, LineState& _line)
{
  _line.cx = _line.bh;
  ++_line.bh;
  ++_line.bl;
  _line.ah = _line.bh;
  do
  {
    const bool borrow = _line.ah < _line.bl;
    _line.ah = static_cast<std::uint8_t>(_line.ah - _line.bl);
    Plot(_guest, _line.di, _line.dh, _line.dl);
    if (borrow)
    {
      _line.ah = static_cast<std::uint8_t>(_line.ah + _line.bh);
      const bool sameByte = NextPixel(_line.dl, _line.dh);
      _line.di = static_cast<std::uint16_t>(_line.di + _line.bp + (sameByte ? 0 : 1));
    }
    else
    {
      _line.di = static_cast<std::uint16_t>(_line.di + _line.bp);
    }
  } while (--_line.cx != 0);
  Plot(_guest, _line.di, _line.dh, _line.dl);
}

// DrawLineShallow (CS:172F): a pixel a step, the byte kept in AL until the line leaves it.
void DrawShallowLine(Guest& _guest, LineState& _line)
{
  _line.cx = _line.bl;
  if (_line.bl != 0xFF)
  {
    ++_line.bl; // ADD BL,1 / SBB BL,0: an increment that stops at FFh
  }
  ++_line.bh;
  _line.ah = _line.bl;
  for (;;)
  {
    const bool borrow = _line.ah < _line.bh;
    _line.ah = static_cast<std::uint8_t>(_line.ah - _line.bh);
    _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
    if (borrow)
    {
      _line.ah = static_cast<std::uint8_t>(_line.ah + _line.bl);
      const bool sameByte = NextPixel(_line.dl, _line.dh);
      _guest.SetByte(_line.di, _line.al);
      _line.di = static_cast<std::uint16_t>(_line.di + _line.bp + (sameByte ? 0 : 1));
      _line.al = _guest.Byte(_line.di);
    }
    else if (!NextPixel(_line.dl, _line.dh))
    {
      _guest.SetByte(_line.di, _line.al);
      ++_line.di;
      _line.al = _guest.Byte(_line.di);
    }
    else
    {
      if (--_line.cx == 0)
      {
        _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
        _guest.SetByte(_line.di, _line.al);
        return;
      }
      continue;
    }
    if (--_line.cx == 0)
    {
      Plot(_guest, _line.di, _line.dh, _line.dl);
      return;
    }
  }
}

// DrawLineDiagonal (CS:180E).
void DrawDiagonalLine(Guest& _guest, LineState& _line)
{
  _line.cx = _line.bl;
  do
  {
    Plot(_guest, _line.di, _line.dh, _line.dl);
    const bool sameByte = NextPixel(_line.dl, _line.dh);
    _line.di = static_cast<std::uint16_t>(_line.di + _line.bp + (sameByte ? 0 : 1));
  } while (--_line.cx != 0);
  Plot(_guest, _line.di, _line.dh, _line.dl);
}

// CS:1623: the rows were doubled to clip; DrawLine takes them halved, in DH and CH.
void DrawHalvedRows(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetHigh(regs.dx, static_cast<std::uint8_t>(Low(regs.bx) >> 1));
  SetHigh(regs.cx, static_cast<std::uint8_t>(Low(regs.ax) >> 1));
  DrawLine(_guest);
}

void SetClipResult(Guest& _guest, bool _carry, bool _zero)
{
  _guest.SetFlag(FLAG_CARRY, _carry);
  _guest.SetFlag(FLAG_ZERO, _zero);
}

// CS:16B5: the endpoints stay where they are; SI=1 and ZF clear from its INC, CF set.
void LeaveEndpoints(Guest& _guest)
{
  _guest.Regs().si = 1;
  SetClipResult(_guest, true, false);
}

// MoveEndpointToEdge (CS:1691): endpoint A (CX, AX) onto the edge along the line to B (DX, BX).
void MoveEndpointToEdge(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = regs.dx;
  if (regs.dx == 0 || regs.cx == 0)
  {
    LeaveEndpoints(_guest);
    return;
  }
  regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bx);
  regs.cx = static_cast<std::uint16_t>(regs.cx - regs.dx);
  regs.dx = Negate(regs.dx);
  const auto product = static_cast<std::uint32_t>(std::int32_t{Signed(regs.ax)} * Signed(regs.dx));
  regs.ax = static_cast<std::uint16_t>(product);
  regs.dx = static_cast<std::uint16_t>(product >> 16);
  DivideSignedWord(_guest, regs.cx);
  regs.ax = static_cast<std::uint16_t>(regs.ax + regs.bx);
  regs.cx = 0;
  regs.dx = regs.si;
  if (High(regs.ax) != 0)
  {
    LeaveEndpoints(_guest);
    return;
  }
  --regs.bp;
  SetClipResult(_guest, true, regs.bp == 0);
}

// SwapThenCutLine (CS:168E).
void SwapThenCutLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::swap(regs.cx, regs.dx);
  std::swap(regs.bx, regs.ax);
  MoveEndpointToEdge(_guest);
}

// ---- Discs ----

// CS:18F2-1924: AX = the profile at SI scaled by discRadiusRows and rounded, plus the fringe while
// sunFringeMask is set.
void DiscHalfWidth(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  auto profile = static_cast<std::uint8_t>(_guest.Byte(regs.si) + 1);
  if (profile == 0)
  {
    --profile; // INC AL that stops at FFh
  }
  const auto product = static_cast<std::uint16_t>(profile * _guest.Get(DS.discRadiusRows));
  regs.ax = static_cast<std::uint8_t>(High(product) + (Low(product) >> 7));
  const std::uint8_t fringeMask = _guest.Get(DS.sunFringeMask);
  if (fringeMask == 0)
  {
    return;
  }
  // (a, b) becomes (b, a+b), and the new b's high byte, masked, widens the row.
  regs.dx = _guest.Get(DS.fringeRandomB);
  const std::uint16_t previous = _guest.Get(DS.fringeRandomA);
  _guest.Set(DS.fringeRandomA, regs.dx);
  regs.dx = static_cast<std::uint16_t>(previous + regs.dx);
  _guest.Set(DS.fringeRandomB, regs.dx);
  regs.dx = static_cast<std::uint8_t>(High(regs.dx) & fringeMask);
  regs.ax = static_cast<std::uint16_t>(regs.ax + regs.dx);
}

// CS:1926-1940: DL, DH = the row's left and right x from discCenterX and the half-width in AX, clamped
// to 0-255. False when the row lies wholly off the buffer.
[[nodiscard]] bool DiscRowSpan(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t centerX = _guest.Get(DS.discCenterX);
  regs.dx = static_cast<std::uint16_t>(centerX - regs.ax);
  if (Negative(regs.dx))
  {
    regs.dx = 0;
  }
  if (High(regs.dx) != 0)
  {
    return false;
  }
  regs.ax = static_cast<std::uint16_t>(regs.ax + centerX);
  if (High(regs.ax) != 0)
  {
    if (Negative8(High(regs.ax)))
    {
      return false;
    }
    SetLow(regs.ax, 0xFF);
  }
  SetHigh(regs.dx, Low(regs.ax));
  return true;
}

// CS:18EE: the row at BX (2*row, moving up) alone.
void DrawDiscUpperRow(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (High(regs.bx) != 0)
  {
    return;
  }
  DiscHalfWidth(_guest);
  if (!DiscRowSpan(_guest))
  {
    return;
  }
  std::swap(regs.cx, regs.bx);
  FillSpan(_guest);
  std::swap(regs.cx, regs.bx);
}

// CS:187B: the rows at CX (moving down) and BX (moving up), one half-width for both.
void DrawDiscRowPair(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  DiscHalfWidth(_guest);
  if (!DiscRowSpan(_guest))
  {
    return;
  }
  FillSpan(_guest);
  if (High(regs.bx) != 0)
  {
    return;
  }
  std::swap(regs.cx, regs.bx);
  FillSpan(_guest);
  std::swap(regs.cx, regs.bx);
}

// DrawSmallDisc (CS:194B): radius 0-4 as a 4-row sprite from smallDiscSprites, clipped a byte at a
// time at the left and right edges and a row at a time at the top and bottom.
void DrawSmallDisc(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (Low(regs.bx) == High(regs.bx))
  {
    SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + 1));
  }
  regs.ax = regs.bx;
  SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) - 1));
  regs.ax = static_cast<std::uint16_t>(regs.ax >> 1);
  regs.cx = static_cast<std::uint16_t>(regs.cx - regs.ax);
  regs.dx = static_cast<std::uint16_t>(regs.dx - regs.ax);
  SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) << 2));
  regs.bx = static_cast<std::uint16_t>(regs.bx + DS.smallDiscSprites.offset);
  regs.ax = SMALL_DISC_ROWS;
  if (Negative(regs.dx))
  {
    regs.dx = static_cast<std::uint16_t>(regs.dx + 4);
    if (Negative(regs.dx) || regs.dx == 0)
    {
      return;
    }
    SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) | CLIP_SPRITE_LEFT));
  }
  else
  {
    if (High(regs.dx) != 0)
    {
      return;
    }
    if (regs.dx >= 0xFC)
    {
      SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) | CLIP_SPRITE_RIGHT));
    }
  }
  if (Negative(regs.cx))
  {
    regs.cx = static_cast<std::uint16_t>(regs.cx + 4);
    if (Negative(regs.cx) || regs.cx == 0)
    {
      return;
    }
    // The rows above the buffer are skipped: AND clears CF, so CMC / ADC adds one more.
    SetLow(regs.ax, Low(regs.cx));
    SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + (~Low(regs.cx) & 3) + 1));
    SetLow(regs.cx, 0);
  }
  else
  {
    if (regs.cx >= BUFFER_ROWS)
    {
      return;
    }
    if (Low(regs.cx) >= BUFFER_ROWS - 3)
    {
      SetLow(regs.ax, static_cast<std::uint8_t>(Negate8(Low(regs.cx)) & 3));
    }
  }
  SetHigh(regs.dx, Low(regs.cx));
  regs.di = static_cast<std::uint16_t>(regs.dx >> 2);
  regs.cx = Low(regs.ax);
  SetHigh(regs.dx, High(regs.ax));
  SetLow(regs.dx, static_cast<std::uint8_t>((Low(regs.dx) & 3) << 1));
  const std::uint8_t fill = _guest.Get(DS.discFillByte);
  do
  {
    const std::uint16_t rows = regs.cx;
    regs.ax = Join(_guest.Byte(regs.bx), 0);
    ++regs.bx;
    SetLow(regs.cx, Low(regs.dx));
    regs.ax = static_cast<std::uint16_t>(regs.ax >> Low(regs.cx));
    std::uint8_t clip = High(regs.dx);
    bool carry = (clip & 1) != 0;
    clip = RotateRight(clip, 1);
    if (carry)
    {
      clip = RotateRight(clip, 1);
      Plot(_guest, regs.di, static_cast<std::uint8_t>(~Low(regs.ax)), static_cast<std::uint8_t>(Low(regs.ax) & fill));
      SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & fill));
    }
    else
    {
      carry = (clip & 1) != 0;
      clip = RotateRight(clip, 1);
      if (carry)
      {
        Plot(_guest, regs.di, static_cast<std::uint8_t>(~High(regs.ax)), static_cast<std::uint8_t>(High(regs.ax) & fill));
        SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) & fill));
      }
      else
      {
        // Both bytes, as a word: the sprite's left byte at DI.
        const std::uint16_t sprite = SwapBytes(regs.ax);
        regs.ax = static_cast<std::uint16_t>(sprite & Join(fill, fill));
        _guest.SetWord(regs.di, static_cast<std::uint16_t>((_guest.Word(regs.di) & ~sprite) | regs.ax));
      }
    }
    SetHigh(regs.dx, clip);
    regs.di = static_cast<std::uint16_t>(regs.di + ROW_BYTES);
    regs.cx = rows;
  } while (--regs.cx != 0);
}

// ---- Triangles ----

// MOV transfer,CS:[_source] / MOV CS:[_site],transfer: a step instruction rewritten from the opcodes
// kept after the routine's RET.
void PatchStep(Guest& _guest, std::uint16_t& _transfer, std::uint16_t _source, std::uint16_t _site)
{
  _transfer = _guest.CodeWord(_source);
  _guest.SetCodeWord(_site, _transfer);
}

// FillTriangleSpan's row walk (DrawStackedSpansFromRow, CS:1CD5), with AH the bottom row and CX the
// spans pushed: pops each and fills it, bottom row first, the pattern's bytes alternating by row.
void DrawStackedSpansFromRow(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, 0);
  regs.ax = static_cast<std::uint16_t>(regs.ax >> 2);
  regs.si = regs.ax;
  regs.bx = _guest.Get(DS.triangleFillPattern);
  if ((Low(regs.ax) & ODD_ROW_BIT) != 0)
  {
    regs.bx = SwapBytes(regs.bx);
  }
  do
  {
    regs.dx = _guest.Pop();
    FillTriangleSpan(_guest);
    regs.si = static_cast<std::uint16_t>(regs.si - ROW_BYTES);
    regs.bx = SwapBytes(regs.bx);
  } while (--regs.cx != 0);
}

// DrawStackedSpans (CS:1CD1).
void DrawStackedSpans(Guest& _guest)
{
  SetHigh(_guest.Regs().ax, _guest.Get(DS.triangleBottomRow));
  DrawStackedSpansFromRow(_guest);
}

// The span between the two edges' integer parts, AH and BH: DL the smaller, DH the larger.
[[nodiscard]] std::uint16_t EdgeSpan(const Machine::Registers& _regs) noexcept
{
  std::uint8_t right = High(_regs.ax);
  std::uint8_t left = High(_regs.bx);
  if (!(left < right))
  {
    std::swap(left, right);
  }
  return Join(right, left);
}

// ADD AX,SI or SUB AX,SI: an 8.8 edge stepped by its slope.
[[nodiscard]] std::uint16_t StepEdge(std::uint16_t _edge, std::uint16_t _slope, bool _subtract) noexcept
{
  return static_cast<std::uint16_t>(_subtract ? _edge - _slope : _edge + _slope);
}

// TraceTwoEdges (CS:1CBE): pushes the span of each of CX rows, stepping edge A (AX by SI) and B (BX by
// DI), then draws them.
void TraceTwoEdges(Guest& _guest, bool _subtractA, bool _subtractB)
{
  Machine::Registers& regs = _guest.Regs();
  do
  {
    regs.dx = EdgeSpan(regs);
    _guest.Push(regs.dx);
    regs.ax = StepEdge(regs.ax, regs.si, _subtractA);
    regs.bx = StepEdge(regs.bx, regs.di, _subtractB);
  } while (--regs.cx != 0);
  regs.cx = regs.bp;
  DrawStackedSpans(_guest);
}

// XOR AH,AH / CWD / XCHG AH,AL / DIV CX: the 8.8 slope of an edge AL pixels across CX rows.
void EdgeSlope(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetHigh(regs.ax, 0);
  ConvertToDoubleWord(regs);
  regs.ax = SwapBytes(regs.ax);
  DivideWord(_guest, regs.cx);
}

// FillFlatBottomTriangle (CS:1C62): A on top, B and C on the bottom row.
void FillFlatBottomTriangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.triangleEdgeStartX, Low(regs.ax));
  _guest.Set(DS.triangleEdgeStartX2, Low(regs.ax));
  _guest.Set(DS.triangleBottomRow, High(regs.bx));
  SetHigh(regs.cx, static_cast<std::uint8_t>(High(regs.cx) - High(regs.ax)));
  _guest.Set(DS.triangleUpperRows, High(regs.cx));
  const bool subtractA = Low(regs.cx) < Low(regs.ax);
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) - Low(regs.ax)));
  if (subtractA)
  {
    PatchStep(_guest, regs.dx, SUB_AX_SI, STEP_EDGE_A);
    SetLow(regs.cx, Negate8(Low(regs.cx)));
  }
  SetHigh(regs.bx, Low(regs.ax));
  SetLow(regs.ax, Low(regs.cx));
  regs.cx = High(regs.cx);
  EdgeSlope(_guest);
  regs.si = regs.ax;
  regs.ax = regs.bx;
  const bool subtractB = Low(regs.ax) < High(regs.ax);
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) - High(regs.ax)));
  if (subtractB)
  {
    PatchStep(_guest, regs.dx, SUB_BX_DI, STEP_EDGE_B);
    SetLow(regs.ax, Negate8(Low(regs.ax)));
  }
  EdgeSlope(_guest);
  regs.di = regs.ax;
  regs.ax = Join(_guest.Get(DS.triangleEdgeStartX), 0);
  regs.bx = Join(_guest.Get(DS.triangleEdgeStartX2), 0);
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) + 1));
  regs.bp = regs.cx;
  TraceTwoEdges(_guest, subtractA, subtractB);
}

// FillFlatTopTriangle (CS:1CFB): B and C on the top row, A at the bottom.
void FillFlatTopTriangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.triangleEdgeStartX, Low(regs.bx));
  _guest.Set(DS.triangleEdgeStartX2, Low(regs.cx));
  _guest.Set(DS.triangleBottomRow, High(regs.ax));
  SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) - High(regs.cx)));
  _guest.Set(DS.triangleUpperRows, High(regs.ax));
  SetHigh(regs.bx, Low(regs.ax));
  const bool subtractA = Low(regs.ax) < Low(regs.cx);
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) - Low(regs.cx)));
  if (subtractA)
  {
    PatchStep(_guest, regs.dx, SUB_AX_SI, STEP_EDGE_A);
    SetLow(regs.ax, Negate8(Low(regs.ax)));
  }
  regs.cx = _guest.Get(DS.triangleUpperRows);
  EdgeSlope(_guest);
  regs.si = regs.ax;
  regs.ax = regs.bx;
  const bool subtractB = High(regs.ax) < Low(regs.ax);
  SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) - Low(regs.ax)));
  if (subtractB)
  {
    PatchStep(_guest, regs.dx, SUB_BX_DI, STEP_EDGE_B);
    SetHigh(regs.ax, Negate8(High(regs.ax)));
  }
  SetLow(regs.ax, 0);
  regs.dx = 0;
  DivideWord(_guest, regs.cx);
  regs.di = regs.ax;
  regs.ax = Join(_guest.Get(DS.triangleEdgeStartX2), 0);
  regs.bx = Join(_guest.Get(DS.triangleEdgeStartX), 0);
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) + 1));
  regs.bp = regs.cx;
  TraceTwoEdges(_guest, subtractA, subtractB);
}

// FillOneRowTriangle (CS:1D5B): all three on one row, one span from the least x to the greatest.
void FillOneRowTriangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.triangleBottomRow, High(regs.ax));
  std::uint8_t al = Low(regs.ax);
  std::uint8_t bl = Low(regs.bx);
  std::uint8_t cl = Low(regs.cx);
  if (!(al < bl))
  {
    std::swap(al, bl);
  }
  if (!(al < cl))
  {
    std::swap(al, cl);
  }
  if (!(bl < cl))
  {
    std::swap(bl, cl);
  }
  SetLow(regs.ax, al);
  SetLow(regs.bx, bl);
  std::uint8_t dl = cl;
  std::uint8_t dh = al;
  if (!(dl < dh))
  {
    std::swap(dl, dh);
  }
  regs.dx = Join(dh, dl);
  _guest.Push(regs.dx);
  regs.cx = 1;
  DrawStackedSpans(_guest);
}

// FillGeneralTriangle (CS:1D82): A on top, B the middle row, C the bottom; the long edge A-C in AX by
// SI, the short edges A-B and then B-C in BX by DI.
void FillGeneralTriangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PatchStep(_guest, regs.dx, ADD_AX_SI, STEP_LONG_EDGE_UPPER);
  _guest.SetCodeWord(STEP_LONG_EDGE_LOWER, regs.dx);
  PatchStep(_guest, regs.dx, ADD_BX_DI, STEP_SHORT_EDGE_UPPER);
  _guest.SetCodeWord(STEP_SHORT_EDGE_LOWER, regs.dx);
  if (!(High(regs.bx) < High(regs.cx)))
  {
    std::swap(regs.cx, regs.bx);
  }
  _guest.Set(DS.triangleBottomRow, High(regs.cx));
  _guest.Set(DS.triangleMiddleX, Low(regs.bx));
  _guest.Set(DS.triangleBottomX, Low(regs.cx));
  SetHigh(regs.bx, static_cast<std::uint8_t>(High(regs.bx) - High(regs.ax)));
  _guest.Set(DS.triangleUpperRows, High(regs.bx));
  SetHigh(regs.cx, static_cast<std::uint8_t>(High(regs.cx) - High(regs.ax)));
  _guest.Set(DS.triangleTotalRows, High(regs.cx));
  _guest.Set(DS.triangleEdgeStartX, Low(regs.ax));
  _guest.Set(DS.triangleEdgeStartX2, Low(regs.ax));
  const bool subtractLong = Low(regs.cx) < Low(regs.ax);
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) - Low(regs.ax)));
  if (subtractLong)
  {
    PatchStep(_guest, regs.dx, SUB_AX_SI, STEP_LONG_EDGE_UPPER);
    _guest.SetCodeWord(STEP_LONG_EDGE_LOWER, regs.dx);
    SetLow(regs.cx, Negate8(Low(regs.cx)));
  }
  SetHigh(regs.bx, Low(regs.ax));
  SetLow(regs.ax, Low(regs.cx));
  regs.cx = _guest.Get(DS.triangleTotalRows);
  EdgeSlope(_guest);
  regs.si = regs.ax;
  regs.ax = regs.bx;
  const bool subtractUpper = Low(regs.ax) < High(regs.ax);
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) - High(regs.ax)));
  if (subtractUpper)
  {
    PatchStep(_guest, regs.dx, SUB_BX_DI, STEP_SHORT_EDGE_UPPER);
    SetLow(regs.ax, Negate8(Low(regs.ax)));
  }
  SetHigh(regs.ax, 0);
  ConvertToDoubleWord(regs);
  regs.ax = SwapBytes(regs.ax);
  SetLow(regs.cx, _guest.Get(DS.triangleUpperRows));
  DivideWord(_guest, regs.cx);
  regs.di = regs.ax;
  regs.ax = Join(_guest.Get(DS.triangleEdgeStartX), 0);
  regs.bx = regs.ax;
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) + 1));
  // TraceUpperEdges (CS:1E15): rows A to B inclusive.
  do
  {
    regs.dx = EdgeSpan(regs);
    _guest.Push(regs.dx);
    regs.ax = StepEdge(regs.ax, regs.si, subtractLong);
    regs.bx = StepEdge(regs.bx, regs.di, subtractUpper);
  } while (--regs.cx != 0);
  // StartLowerShortEdge (CS:1E26): the rows below B, the short edge restarting from B.
  SetLow(regs.cx, static_cast<std::uint8_t>(_guest.Get(DS.triangleTotalRows) - _guest.Get(DS.triangleUpperRows)));
  const std::uint16_t longEdge = regs.ax;
  const std::uint8_t middleX = _guest.Get(DS.triangleMiddleX);
  const std::uint8_t bottomX = _guest.Get(DS.triangleBottomX);
  const bool subtractLower = bottomX < middleX;
  SetLow(regs.ax, static_cast<std::uint8_t>(bottomX - middleX));
  if (subtractLower)
  {
    PatchStep(_guest, regs.dx, SUB_BX_DI, STEP_SHORT_EDGE_LOWER);
    SetLow(regs.ax, Negate8(Low(regs.ax)));
  }
  EdgeSlope(_guest);
  regs.di = regs.ax;
  regs.bx = Join(_guest.Get(DS.triangleMiddleX), 0);
  regs.ax = longEdge;
  do
  {
    regs.bx = StepEdge(regs.bx, regs.di, subtractLower);
    regs.dx = EdgeSpan(regs);
    _guest.Push(regs.dx);
    regs.ax = StepEdge(regs.ax, regs.si, subtractLong);
  } while (--regs.cx != 0);
  SetLow(regs.cx, static_cast<std::uint8_t>(_guest.Get(DS.triangleTotalRows) + 1));
  DrawStackedSpans(_guest);
}

// FillOnScreenTriangle (CS:1C12): every coordinate in the buffer. Sorts by row into the four cases.
void FillOnScreenTriangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // AL, AH = A's x and row; BL, BH = B's; CL, CH = C's.
  regs.ax = Join(Low(regs.dx), Low(regs.si));
  regs.dx = regs.bp;
  SetHigh(regs.bx, Low(regs.dx));
  regs.dx = regs.di;
  SetHigh(regs.cx, Low(regs.dx));
  SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) >> 1));
  SetHigh(regs.bx, static_cast<std::uint8_t>(High(regs.bx) >> 1));
  SetHigh(regs.cx, static_cast<std::uint8_t>(High(regs.cx) >> 1));
  regs.dx = regs.ds;
  regs.es = regs.dx;
  PatchStep(_guest, regs.dx, ADD_AX_SI, STEP_EDGE_A);
  PatchStep(_guest, regs.dx, ADD_BX_DI, STEP_EDGE_B);
  if (High(regs.ax) == High(regs.bx))
  {
    const std::uint8_t topRow = High(regs.ax);
    const std::uint8_t otherRow = High(regs.cx);
    std::swap(regs.cx, regs.ax);
    if (topRow == otherRow)
    {
      FillOneRowTriangle(_guest);
    }
    else if (topRow > otherRow)
    {
      FillFlatBottomTriangle(_guest);
    }
    else
    {
      FillFlatTopTriangle(_guest);
    }
    return;
  }
  if (High(regs.ax) > High(regs.bx))
  {
    std::swap(regs.bx, regs.ax);
  }
  if (High(regs.ax) == High(regs.cx))
  {
    std::swap(regs.bx, regs.ax);
    FillFlatTopTriangle(_guest);
    return;
  }
  if (High(regs.ax) > High(regs.cx))
  {
    std::swap(regs.cx, regs.ax);
  }
  if (High(regs.bx) == High(regs.cx))
  {
    FillFlatBottomTriangle(_guest);
  }
  else
  {
    FillGeneralTriangle(_guest);
  }
}

// ---- Clipped triangles ----

// Rewrites the step pair at _site (ADD/ADC or SUB/SBB) from the pair at _source, through _transfer.
void PatchClippedStep(Guest& _guest, std::uint16_t& _transfer, std::uint16_t _source, std::uint16_t _site)
{
  PatchStep(_guest, _transfer, _source, _site);
  PatchStep(_guest, _transfer, static_cast<std::uint16_t>(_source + CARRY_STEP_BYTES),
            static_cast<std::uint16_t>(_site + CARRY_STEP_BYTES));
}

// ADD fraction,[m] / ADC whole,[m], or SUB / SBB: a 16.16 edge stepped by its slope.
void StepClippedEdge(std::uint16_t& _whole, std::uint16_t& _fraction, std::uint16_t _slopeWhole, std::uint16_t _slopeFraction,
                     bool _subtract) noexcept
{
  if (_subtract)
  {
    const bool borrow = _fraction < _slopeFraction;
    _fraction = static_cast<std::uint16_t>(_fraction - _slopeFraction);
    _whole = static_cast<std::uint16_t>(_whole - _slopeWhole - (borrow ? 1 : 0));
    return;
  }
  const std::uint32_t sum = std::uint32_t{_fraction} + _slopeFraction;
  _fraction = static_cast<std::uint16_t>(sum);
  _whole = static_cast<std::uint16_t>(_whole + _slopeWhole + (sum >> 16));
}

// Edge A: AX:SI by clippedSlopeA.
void StepClippedEdgeA(Guest& _guest, bool _subtract)
{
  Machine::Registers& regs = _guest.Regs();
  StepClippedEdge(regs.ax, regs.si, _guest.Get(DS.clippedSlopeAWhole), _guest.Get(DS.clippedSlopeAFraction), _subtract);
}

// Edge B: BX:DI by clippedSlopeB.
void StepClippedEdgeB(Guest& _guest, bool _subtract)
{
  Machine::Registers& regs = _guest.Regs();
  StepClippedEdge(regs.bx, regs.di, _guest.Get(DS.clippedSlopeBWhole), _guest.Get(DS.clippedSlopeBFraction), _subtract);
}

// XOR DX,DX / DIV _rows / store / XOR AX,AX / DIV _rows / store: AX pixels over _rows rows as 16.16.
void ClippedSlope(Guest& _guest, std::uint16_t _rows, DataField<std::uint16_t> _whole, DataField<std::uint16_t> _fraction)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = 0;
  DivideWord(_guest, _rows);
  _guest.Set(_whole, regs.ax);
  regs.ax = 0;
  DivideWord(_guest, _rows);
  _guest.Set(_fraction, regs.ax);
}

// Whether row BP, between edges AX and BX, shows: 0-127, and not both edges left of 0 or right of 255.
[[nodiscard]] bool ClippedRowVisible(const Machine::Registers& _regs) noexcept
{
  if (_regs.bp >= BUFFER_ROWS)
  {
    return false;
  }
  if (Negative8(static_cast<std::uint8_t>(High(_regs.bx) & High(_regs.ax))))
  {
    return false;
  }
  return !(Signed8(High(_regs.ax)) > 0 && Signed8(High(_regs.bx)) > 0);
}

// A visible row's span, clamped to 0-255, pushed for DrawStackedSpans: DL the left x, DH the right.
void PushClippedSpan(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint8_t left = 0;
  std::uint8_t right = 0;
  if (Signed(regs.ax) <= Signed(regs.bx))
  {
    right = High(regs.bx) != 0 ? std::uint8_t{0xFF} : Low(regs.bx);
    left = High(regs.ax) != 0 ? std::uint8_t{0} : Low(regs.ax);
  }
  else
  {
    right = High(regs.ax) != 0 ? std::uint8_t{0xFF} : Low(regs.ax);
    left = High(regs.bx) != 0 ? std::uint8_t{0} : Low(regs.bx);
  }
  regs.dx = Join(right, left);
  _guest.Push(regs.dx);
}

// A row of the clipped walk: pushed if visible, noting the first. False when the walk is over: a hidden
// row after a drawn one.
[[nodiscard]] bool TakeClippedRow(Guest& _guest, bool _counted)
{
  Machine::Registers& regs = _guest.Regs();
  if (!ClippedRowVisible(regs))
  {
    return _guest.Get(DS.clippedFirstRow) == NO_ROW;
  }
  if (_guest.Get(DS.clippedFirstRow) == NO_ROW)
  {
    _guest.Set(DS.clippedFirstRow, regs.bp);
  }
  PushClippedSpan(_guest);
  if (_counted)
  {
    _guest.Set(DS.clippedRowCount, static_cast<std::uint8_t>(_guest.Get(DS.clippedRowCount) + 1));
  }
  return true;
}

// TraceClippedFlatEdges (CS:1F6C) and FinishClippedFlat (CS:1FDB): CX rows from BP, edge A in AX:SI and
// B in BX:DI.
void TraceClippedFlatEdges(Guest& _guest, bool _subtractA, bool _subtractB)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.clippedLastRow, NO_ROW);
  _guest.Set(DS.clippedFirstRow, NO_ROW);
  do
  {
    if (!TakeClippedRow(_guest, false))
    {
      break;
    }
    StepClippedEdgeA(_guest, _subtractA);
    StepClippedEdgeB(_guest, _subtractB);
    ++regs.bp;
  } while (--regs.cx != 0);
  const std::uint16_t firstRow = _guest.Get(DS.clippedFirstRow);
  if (firstRow == NO_ROW)
  {
    return;
  }
  --regs.bp;
  _guest.Set(DS.clippedLastRow, regs.bp);
  regs.cx = static_cast<std::uint16_t>(regs.bp - firstRow);
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) + 1));
  regs.ax = SwapBytes(regs.bp);
  DrawStackedSpansFromRow(_guest);
}

// FillClippedFlatBottom (CS:1EFD): A on top, B and C on the bottom row.
void FillClippedFlatBottom(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.clippedEdgeStartX, regs.ax);
  _guest.Set(DS.clippedEdgeStartX2, regs.ax);
  regs.bp = regs.dx;
  regs.di = static_cast<std::uint16_t>(regs.di - regs.dx);
  _guest.Set(DS.clippedUpperRows, regs.di);
  const bool subtractA = Signed(regs.cx) < Signed(regs.ax);
  regs.cx = static_cast<std::uint16_t>(regs.cx - regs.ax);
  if (subtractA)
  {
    PatchClippedStep(_guest, regs.dx, SUBTRACT_FROM_EDGE_A, STEP_CLIPPED_EDGE_A);
    regs.cx = Negate(regs.cx);
  }
  std::swap(regs.cx, regs.ax);
  ClippedSlope(_guest, regs.di, DS.clippedSlopeAWhole, DS.clippedSlopeAFraction);
  const bool subtractB = Signed(regs.bx) < Signed(regs.cx);
  regs.bx = static_cast<std::uint16_t>(regs.bx - regs.cx);
  if (subtractB)
  {
    PatchClippedStep(_guest, regs.dx, SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_EDGE_B);
    regs.bx = Negate(regs.bx);
  }
  regs.ax = regs.bx;
  ClippedSlope(_guest, regs.di, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
  regs.ax = _guest.Get(DS.clippedEdgeStartX);
  regs.si = 0;
  regs.bx = _guest.Get(DS.clippedEdgeStartX2);
  regs.cx = regs.di;
  regs.di = 0;
  ++regs.cx;
  TraceClippedFlatEdges(_guest, subtractA, subtractB);
}

// FillClippedFlatTop (CS:2018): B and C on the top row, A at the bottom.
void FillClippedFlatTop(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.clippedEdgeStartX, regs.bx);
  _guest.Set(DS.clippedEdgeStartX2, regs.cx);
  regs.dx = static_cast<std::uint16_t>(regs.dx - regs.di);
  _guest.Set(DS.clippedUpperRows, regs.dx);
  regs.di = regs.dx;
  const std::uint16_t bottomX = regs.ax;
  const bool subtractA = Signed(regs.ax) < Signed(regs.cx);
  regs.ax = static_cast<std::uint16_t>(regs.ax - regs.cx);
  if (subtractA)
  {
    PatchClippedStep(_guest, regs.dx, SUBTRACT_FROM_EDGE_A, STEP_CLIPPED_EDGE_A);
    regs.ax = Negate(regs.ax);
  }
  ClippedSlope(_guest, regs.di, DS.clippedSlopeAWhole, DS.clippedSlopeAFraction);
  regs.ax = bottomX;
  const bool subtractB = Signed(regs.ax) < Signed(regs.bx);
  regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bx);
  if (subtractB)
  {
    PatchClippedStep(_guest, regs.dx, SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_EDGE_B);
    regs.ax = Negate(regs.ax);
  }
  ClippedSlope(_guest, regs.di, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
  regs.ax = _guest.Get(DS.clippedEdgeStartX2);
  regs.si = 0;
  regs.bx = _guest.Get(DS.clippedEdgeStartX);
  regs.cx = regs.di;
  regs.di = 0;
  ++regs.cx;
  TraceClippedFlatEdges(_guest, subtractA, subtractB);
}

// FillClippedOneRow (CS:208B): all three on one row.
void FillClippedOneRow(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.triangleBottomRow, Low(regs.dx));
  if (Signed(regs.ax) > Signed(regs.bx))
  {
    std::swap(regs.bx, regs.ax);
  }
  if (Signed(regs.ax) > Signed(regs.cx))
  {
    std::swap(regs.cx, regs.ax);
  }
  if (Signed(regs.bx) > Signed(regs.cx))
  {
    std::swap(regs.cx, regs.bx);
  }
  if (Negative(regs.cx) || Signed(regs.ax) >= 0x100)
  {
    return;
  }
  std::uint8_t left = Low(regs.ax);
  std::uint8_t right = Low(regs.cx);
  if (Negative(regs.ax))
  {
    left = 0;
  }
  if (Signed(regs.cx) >= 0x100)
  {
    right = 0xFF;
  }
  regs.dx = Join(right, left);
  _guest.Push(regs.dx);
  regs.cx = 1;
  DrawStackedSpans(_guest);
}

// FillClippedGeneral (CS:20C2): A on top, B the middle row, C the bottom; the long edge A-C in AX:SI,
// the short edges in BX:DI, every row from A's walked, counted in clippedRowCount.
void FillClippedGeneral(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PatchStep(_guest, regs.si, ADD_TO_EDGE_A, STEP_CLIPPED_UPPER_A);
  _guest.SetCodeWord(STEP_CLIPPED_LOWER_A, regs.si);
  PatchStep(_guest, regs.si, ADD_TO_EDGE_A + CARRY_STEP_BYTES, STEP_CLIPPED_UPPER_A + CARRY_STEP_BYTES);
  _guest.SetCodeWord(STEP_CLIPPED_LOWER_A + CARRY_STEP_BYTES, regs.si);
  PatchStep(_guest, regs.si, ADD_TO_EDGE_B, STEP_CLIPPED_UPPER_B);
  _guest.SetCodeWord(STEP_CLIPPED_LOWER_B, regs.si);
  PatchStep(_guest, regs.si, ADD_TO_EDGE_B + CARRY_STEP_BYTES, STEP_CLIPPED_UPPER_B + CARRY_STEP_BYTES);
  _guest.SetCodeWord(STEP_CLIPPED_LOWER_B + CARRY_STEP_BYTES, regs.si);
  if (!(Signed(regs.bp) < Signed(regs.di)))
  {
    std::swap(regs.cx, regs.bx);
    std::swap(regs.di, regs.bp);
  }
  const std::uint16_t topRow = regs.dx;
  _guest.Set(DS.clippedMiddleX, regs.bx);
  _guest.Set(DS.clippedBottomX, regs.cx);
  regs.bp = static_cast<std::uint16_t>(regs.bp - regs.dx);
  _guest.Set(DS.clippedUpperRows, regs.bp);
  regs.di = static_cast<std::uint16_t>(regs.di - regs.dx);
  _guest.Set(DS.clippedTotalRows, regs.di);
  _guest.Set(DS.clippedEdgeStartX, regs.ax);
  _guest.Set(DS.clippedEdgeStartX2, regs.ax);
  const bool subtractLong = !(Signed(regs.cx) > Signed(regs.ax));
  regs.cx = static_cast<std::uint16_t>(regs.cx - regs.ax);
  if (subtractLong)
  {
    PatchStep(_guest, regs.si, SUBTRACT_FROM_EDGE_A, STEP_CLIPPED_UPPER_A);
    _guest.SetCodeWord(STEP_CLIPPED_LOWER_A, regs.si);
    PatchStep(_guest, regs.si, SUBTRACT_FROM_EDGE_A + CARRY_STEP_BYTES, STEP_CLIPPED_UPPER_A + CARRY_STEP_BYTES);
    _guest.SetCodeWord(STEP_CLIPPED_LOWER_A + CARRY_STEP_BYTES, regs.si);
    regs.cx = Negate(regs.cx);
  }
  const std::uint16_t startX = regs.ax;
  regs.ax = regs.cx;
  regs.cx = _guest.Get(DS.clippedTotalRows);
  ClippedSlope(_guest, regs.cx, DS.clippedSlopeAWhole, DS.clippedSlopeAFraction);
  regs.ax = startX;
  const bool subtractUpper = !(Signed(regs.bx) > Signed(regs.ax));
  regs.bx = static_cast<std::uint16_t>(regs.bx - regs.ax);
  if (subtractUpper)
  {
    PatchClippedStep(_guest, regs.si, SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_UPPER_B);
    regs.bx = Negate(regs.bx);
  }
  regs.ax = regs.bx;
  regs.cx = _guest.Get(DS.clippedUpperRows);
  ClippedSlope(_guest, regs.cx, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
  regs.ax = _guest.Get(DS.clippedEdgeStartX);
  regs.si = 0;
  regs.bx = regs.ax;
  regs.di = 0;
  ++regs.cx;
  regs.bp = topRow;
  _guest.Set(DS.clippedLastRow, NO_ROW);
  _guest.Set(DS.clippedFirstRow, NO_ROW);
  _guest.Set(DS.clippedRowCount, 0);
  // ClippedUpperRowLoop (CS:21A5): rows A to B inclusive.
  bool walking = true;
  do
  {
    walking = TakeClippedRow(_guest, true);
    if (!walking)
    {
      break;
    }
    StepClippedEdgeA(_guest, subtractLong);
    StepClippedEdgeB(_guest, subtractUpper);
    ++regs.bp;
  } while (--regs.cx != 0);
  if (walking)
  {
    // StartClippedLowerEdge (CS:2213): the short edge restarts from B; its fraction carries on.
    regs.cx = static_cast<std::uint16_t>(_guest.Get(DS.clippedTotalRows) - _guest.Get(DS.clippedUpperRows));
    const std::uint16_t longEdge = regs.ax;
    const std::uint16_t middleX = _guest.Get(DS.clippedMiddleX);
    const std::uint16_t bottomX = _guest.Get(DS.clippedBottomX);
    const bool subtractLower = !(Signed(bottomX) > Signed(middleX));
    regs.ax = static_cast<std::uint16_t>(bottomX - middleX);
    if (subtractLower)
    {
      PatchClippedStep(_guest, regs.si, SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_LOWER_B);
      regs.ax = Negate(regs.ax);
    }
    ClippedSlope(_guest, regs.cx, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
    regs.bx = _guest.Get(DS.clippedMiddleX);
    regs.ax = longEdge;
    // ClippedLowerRowLoop (CS:224E).
    do
    {
      StepClippedEdgeB(_guest, subtractLower);
      if (!TakeClippedRow(_guest, true))
      {
        break;
      }
      StepClippedEdgeA(_guest, subtractLong);
      ++regs.bp;
    } while (--regs.cx != 0);
  }
  // FinishClippedGeneral (CS:22B9).
  if (_guest.Get(DS.clippedRowCount) == 0)
  {
    return;
  }
  --regs.bp;
  _guest.Set(DS.clippedLastRow, regs.bp);
  regs.cx = static_cast<std::uint16_t>(regs.bp - _guest.Get(DS.clippedFirstRow));
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) + 1));
  regs.ax = regs.bp;
  if (regs.ax >= BUFFER_ROWS)
  {
    return;
  }
  regs.ax = SwapBytes(regs.ax);
  regs.cx = _guest.Get(DS.clippedRowCount);
  DrawStackedSpansFromRow(_guest);
}

// ---- Frames ----

// IN AL,DX / AND AL,8 / JZ: the CGA's status until it reports a vertical retrace, the loop at CS:_loop. A read
// that sees a retrace uses it up (Cga::SetRetraceSeenOnce), so these are the original's reads, one a turn.
void WaitForRetrace(Guest& _guest, std::uint16_t _loop)
{
  Machine::Registers& regs = _guest.Regs();
  for (;;)
  {
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(regs.dx) & STATUS_VERTICAL_RETRACE));
    if (Low(regs.ax) != 0)
    {
      return;
    }
    _guest.JumpBack(_loop);
  }
}

// MOV AX,_turns / DEC AX / JNZ at CS:_loop: a delay the 8088's speed made. Each turn changes AX, so paced time
// never idles in it.
void SpinDelay(Guest& _guest, std::uint16_t _turns, std::uint16_t _loop)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _turns;
  for (;;)
  {
    --regs.ax;
    if (regs.ax == 0)
    {
      return;
    }
    _guest.JumpBack(_loop);
  }
}

// BX line pairs of BP words from DS:SI to ES:DI, the even line and then the odd one AX on, DI back by DX for the
// next pair: the copy loop at CS:_loop.
void CopyLinePairs(Guest& _guest, std::uint16_t _loop)
{
  Machine::Registers& regs = _guest.Regs();
  for (;;)
  {
    regs.cx = regs.bp;
    RepeatMoveWords(_guest, regs.ds);
    regs.di = static_cast<std::uint16_t>(regs.di + regs.ax);
    regs.cx = regs.bp;
    RepeatMoveWords(_guest, regs.ds);
    regs.di = static_cast<std::uint16_t>(regs.di - regs.dx);
    --regs.bx;
    if (regs.bx == 0)
    {
      return;
    }
    _guest.JumpBack(_loop);
  }
}

// The registers CopyLinePairs takes for a 64-byte buffer line onto the screen: 32 words, then the odd bank,
// then back to the next even line.
void SetLinePairSteps(Machine::Registers& _regs) noexcept
{
  _regs.bp = BUFFER_LINE_WORDS;
  _regs.ax = TO_ODD_LINE;
  _regs.dx = TO_NEXT_EVEN_LINE;
}

} // namespace

void SaveScreenshot(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(RESTORE_DIVIDE_AND_KEYBOARD_INTERRUPTS);
  _guest.Call(RESTORE_TIMER_INTERRUPT);
  _guest.Set(DS.diskError, 0);
  regs.bx = 0;
  regs.es = regs.bx;
  _guest.Push(_guest.FarWord(regs.es, CRITICAL_ERROR_VECTOR_OFFSET));
  _guest.Push(_guest.FarWord(regs.es, CRITICAL_ERROR_VECTOR_SEGMENT));
  regs.bx = CRITICAL_ERROR_INTERRUPT;
  _guest.SetFarWord(regs.es, CRITICAL_ERROR_VECTOR_OFFSET, regs.bx);
  regs.bx = regs.cs;
  _guest.SetFarWord(regs.es, CRITICAL_ERROR_VECTOR_SEGMENT, regs.bx);
  _guest.Call(WRITE_SCREENSHOT_FILE);
  if (_guest.Get(DS.diskError) != 0)
  {
    _guest.Call(SHOW_DISK_ERROR);
  }
  regs.ax = 0;
  regs.es = regs.ax;
  _guest.SetFarWord(regs.es, CRITICAL_ERROR_VECTOR_SEGMENT, _guest.Pop());
  _guest.SetFarWord(regs.es, CRITICAL_ERROR_VECTOR_OFFSET, _guest.Pop());
  _guest.Call(INSTALL_TIMER_INTERRUPT);
  _guest.Call(INSTALL_DIVIDE_AND_KEYBOARD_INTERRUPTS);
}

void WriteScreenshotFile(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetHigh(regs.ax, DOS_SET_TRANSFER_AREA);
  regs.dx = DS.diskTransferArea.offset;
  _guest.Interrupt(DOS_VECTOR);
  // The two digits, the second counting fastest: '00', '01' ... '99', '00'.
  regs.ax = _guest.Get(DS.screenshotNumber);
  SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) + 1));
  if (High(regs.ax) == PAST_DIGITS)
  {
    SetHigh(regs.ax, FIRST_DIGIT);
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) + 1));
    if (Low(regs.ax) == PAST_DIGITS)
    {
      SetLow(regs.ax, FIRST_DIGIT);
    }
  }
  _guest.Set(DS.screenshotNumber, regs.ax);
  _guest.Set(DS.screenshotTextFileDigits, regs.ax);
  _guest.Set(DS.screenshotGraphicsFileDigits, regs.ax);
  SetHigh(regs.ax, DOS_CREATE);
  regs.dx = DS.screenshotTextFileName.offset;
  if ((_guest.Get(DS.screenLayout) & TEXT_LAYOUT_BIT) == 0)
  {
    regs.dx = DS.screenshotGraphicsFileName.offset;
  }
  regs.cx = 0;
  _guest.Interrupt(DOS_VECTOR);
  if (Flag(regs, FLAG_CARRY))
  {
    _guest.Set(DS.diskError, 1);
    return;
  }
  _guest.Set(DS.fileHandle, regs.ax);
  SetHigh(regs.ax, DOS_WRITE);
  regs.bx = _guest.Get(DS.fileHandle);
  regs.cx = TEXT_PAGE_BYTES;
  if ((_guest.Get(DS.screenLayout) & TEXT_LAYOUT_BIT) == 0)
  {
    regs.cx = GRAPHICS_SCREEN_BYTES;
  }
  // From B800:0000, with DS pointing there for the call.
  _guest.Push(regs.ds);
  regs.dx = Guest::VIDEO_SEGMENT;
  regs.ds = regs.dx;
  regs.dx = 0;
  _guest.Interrupt(DOS_VECTOR);
  regs.ds = _guest.Pop();
  if (Flag(regs, FLAG_CARRY))
  {
    _guest.Set(DS.diskError, 1);
  }
  SetHigh(regs.ax, DOS_CLOSE);
  regs.bx = _guest.Get(DS.fileHandle);
  _guest.Interrupt(DOS_VECTOR);
  if (Flag(regs, FLAG_CARRY))
  {
    _guest.Set(DS.diskError, 1);
  }
}

void FinishSpaceViewFrame(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  _guest.Call(DRAW_LASER_SIGHTS);
  SetLow(regs.ax, 0);
  _guest.SetFlag(FLAG_DIRECTION, false);
  _guest.Call(PRESENT_SPACE_VIEW);
  _guest.Call(CLEAR_DRAW_BUFFER);
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
}

void PresentChartFrame(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // MOV AX,B800h: AL, the bands CopyChartBufferToScreen skips, is 0 whatever the caller passed.
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  _guest.SetFlag(FLAG_DIRECTION, false);
  _guest.Call(COPY_CHART_BUFFER_TO_SCREEN);
  _guest.Call(CLEAR_DRAW_BUFFER);
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
}

void PresentSpaceView(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The timer interrupt counts msSinceFrame up while this idles.
  for (;;)
  {
    SetLow(regs.ax, _guest.Get(DS.msSinceFrame));
    if (Low(regs.ax) >= _guest.Get(DS.minimumFrameMs))
    {
      break;
    }
    _guest.JumpBack(PRESENT_SPACE_VIEW);
  }
  _guest.Set(DS.msSinceFrame, 0);
  _guest.Call(WAIT_RETRACE_THEN_DELAY);
  regs.si = DS.spaceViewBuffer.offset;
  regs.di = SPACE_VIEW_SCREEN_OFFSET;
  SetLinePairSteps(regs);
  regs.bx = SPACE_VIEW_LINE_PAIRS;
  CopyLinePairs(_guest, SPACE_VIEW_COPY_LOOP);
}

void CopyChartBufferToScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = CGA_STATUS_PORT;
  _guest.Push(regs.ax);
  WaitForRetrace(_guest, CHART_RETRACE_LOOP);
  SpinDelay(_guest, CHART_DELAY_TURNS, CHART_DELAY_LOOP);
  regs.ax = _guest.Pop();
  // AL bands of 8 lines skipped: 64 - 4*AL line pairs from DS:AL*512.
  SetLow(regs.bx, Low(regs.ax));
  SetHigh(regs.ax, Low(regs.bx));
  SetLow(regs.bx, Negate(static_cast<std::uint8_t>(static_cast<std::uint8_t>(Low(regs.bx) << 2) - CHART_LINE_PAIRS)));
  SetHigh(regs.bx, 0);
  SetLow(regs.ax, 0);
  SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) << 1));
  regs.si = regs.ax;
  regs.di = CHART_SCREEN_OFFSET;
  SetLinePairSteps(regs);
  CopyLinePairs(_guest, CHART_COPY_LOOP);
}

void ClearDrawBuffer(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.DataSegment();
  regs.es = regs.ax;
  regs.cx = DRAW_BUFFER_WORDS;
  regs.ax = 0;
  regs.di = 0;
  RepeatStoreWords(_guest, regs.ax);
}

void PlotPixel(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = ColorFillByteOffset(_guest.Get(DS.drawColor));
  const std::uint8_t pixel = RotateRight(LEFT_PIXEL_MASK, static_cast<unsigned>((Low(regs.dx) & 3) << 1));
  const auto color = static_cast<std::uint8_t>(pixel & _guest.Byte(regs.bx));
  regs.cx = Join(color, static_cast<std::uint8_t>(~pixel));
  regs.bx = static_cast<std::uint16_t>(regs.dx >> 2);
  Plot(_guest, regs.bx, Low(regs.cx), color);
}

void DrawClippedLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bp = 0;
  regs.bx = static_cast<std::uint16_t>(regs.bx << 1);
  regs.ax = static_cast<std::uint16_t>(regs.ax << 1);
  regs.si = regs.ax;
  // BP counts the endpoints with a coordinate outside 0-255.
  SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) | High(regs.cx)));
  if (High(regs.ax) != 0)
  {
    ++regs.bp;
    SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.bx) | High(regs.dx)));
    if (High(regs.ax) != 0)
    {
      ++regs.bp;
    }
  }
  else
  {
    SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.bx) | High(regs.dx)));
    if (High(regs.ax) == 0)
    {
      DrawHalvedRows(_guest);
      return;
    }
    ++regs.bp;
  }
  regs.ax = regs.si;
  ClipLineToLowEdge(_guest);
  if (Flag(regs, FLAG_ZERO))
  {
    DrawHalvedRows(_guest);
    return;
  }
  if (!Flag(regs, FLAG_CARRY))
  {
    return;
  }
  std::swap(regs.cx, regs.ax);
  std::swap(regs.dx, regs.bx);
  ClipLineToLowEdge(_guest);
  std::swap(regs.cx, regs.ax);
  std::swap(regs.dx, regs.bx);
  if (Flag(regs, FLAG_ZERO))
  {
    DrawHalvedRows(_guest);
    return;
  }
  if (!Flag(regs, FLAG_CARRY))
  {
    return;
  }
  constexpr std::uint16_t HIGH_EDGE = 0xFF;
  regs.cx = static_cast<std::uint16_t>(regs.cx - HIGH_EDGE);
  regs.dx = static_cast<std::uint16_t>(regs.dx - HIGH_EDGE);
  ClipLineToHighEdge(_guest);
  if (!Flag(regs, FLAG_CARRY))
  {
    return;
  }
  regs.cx = static_cast<std::uint16_t>(regs.cx + HIGH_EDGE);
  regs.dx = static_cast<std::uint16_t>(regs.dx + HIGH_EDGE);
  if (Flag(regs, FLAG_ZERO))
  {
    DrawHalvedRows(_guest);
    return;
  }
  std::swap(regs.cx, regs.ax);
  std::swap(regs.dx, regs.bx);
  regs.cx = static_cast<std::uint16_t>(regs.cx - HIGH_EDGE);
  regs.dx = static_cast<std::uint16_t>(regs.dx - HIGH_EDGE);
  ClipLineToHighEdge(_guest);
  if (!Flag(regs, FLAG_CARRY) || !Flag(regs, FLAG_ZERO))
  {
    return;
  }
  regs.cx = static_cast<std::uint16_t>(regs.cx + HIGH_EDGE);
  regs.dx = static_cast<std::uint16_t>(regs.dx + HIGH_EDGE);
  std::swap(regs.cx, regs.ax);
  std::swap(regs.dx, regs.bx);
  DrawHalvedRows(_guest);
}

void ClipLineToLowEdge(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  if (Negative8(High(regs.cx)))
  {
    if (!Negative8(High(regs.dx)))
    {
      MoveEndpointToEdge(_guest);
      return;
    }
    // Both below the edge: CF clear, and ZF clear from AND DH,DH.
    SetClipResult(_guest, false, false);
    return;
  }
  if (!Negative8(High(regs.dx)))
  {
    LeaveEndpoints(_guest);
    return;
  }
  SwapThenCutLine(_guest);
}

void ClipLineToHighEdge(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  if (Negative8(High(regs.dx)))
  {
    if (!Negative8(High(regs.cx)))
    {
      MoveEndpointToEdge(_guest);
      return;
    }
    // Both inside: CF set, and ZF clear from AND CH,CH.
    SetClipResult(_guest, true, false);
    return;
  }
  if (Negative8(High(regs.cx)))
  {
    SwapThenCutLine(_guest);
    return;
  }
  // Both beyond 255: CF clear, ZF from AND CH,CH.
  SetClipResult(_guest, false, High(regs.cx) == 0);
}

void DrawLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint8_t dl = Low(regs.dx);
  std::uint8_t dh = High(regs.dx);
  std::uint8_t cl = Low(regs.cx);
  std::uint8_t ch = High(regs.cx);
  LineState line;
  // BL = |dx| and BH = |drow|, with the endpoints swapped so that the line runs right; BP the row step.
  line.al = static_cast<std::uint8_t>(cl - dl);
  if (cl < dl)
  {
    line.al = Negate8(line.al);
    std::swap(dl, cl);
    std::swap(dh, ch);
  }
  line.bl = line.al;
  line.bp = ROW_BYTES;
  line.al = static_cast<std::uint8_t>(ch - dh);
  if (ch < dh)
  {
    line.al = Negate8(line.al);
    line.bp = Negate(line.bp);
  }
  line.bh = line.al;
  line.di = static_cast<std::uint16_t>(Join(dh, dl) >> 2);
  line.al = _guest.Byte(ColorFillByteOffset(_guest.Get(DS.drawColor)));
  line.ah = line.al;
  // DL the pixel's color in place, DH the mask that keeps the rest of its byte.
  const auto shift = static_cast<std::uint8_t>((dl & 3) << 1);
  const std::uint8_t pixel = RotateRight(LEFT_PIXEL_MASK, shift);
  line.dh = static_cast<std::uint8_t>(~pixel);
  line.al = static_cast<std::uint8_t>(line.al & pixel);
  line.dl = line.al;
  line.al = _guest.Byte(line.di);
  line.cx = shift;
  if (line.bh == 0)
  {
    DrawHorizontalLine(_guest, line);
  }
  else if (line.bl == 0)
  {
    DrawVerticalLine(_guest, line);
  }
  else if (line.bl < line.bh)
  {
    DrawSteepLine(_guest, line);
  }
  else if (line.bl != line.bh)
  {
    DrawShallowLine(_guest, line);
  }
  else
  {
    DrawDiagonalLine(_guest, line);
  }
  regs.ax = Join(line.ah, line.al);
  regs.bx = Join(line.bh, line.bl);
  regs.cx = line.cx;
  regs.dx = Join(line.dh, line.dl);
  regs.di = line.di;
  regs.bp = line.bp;
}

void DrawDisc(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = static_cast<std::uint16_t>(_guest.Get(DS.drawColor) & 3);
  regs.di = DS.colorFillBytes.At(regs.ax);
  SetLow(regs.ax, _guest.Byte(regs.di));
  _guest.Set(DS.discFillByte, Low(regs.ax));
  regs.ax = regs.ds;
  regs.es = regs.ax;
  if (regs.bx < SMALL_DISC_RADIUS)
  {
    DrawSmallDisc(_guest);
    return;
  }
  regs.bx = static_cast<std::uint16_t>(regs.bx >> 1);
  _guest.Set(DS.discRadiusRows, Low(regs.bx));
  _guest.Set(DS.discRowsLeft, Low(regs.bx));
  _guest.Set(DS.discCenterX, regs.dx);
  // The step through circleProfile a row, 20000h/rows as 8.8; the fraction's low byte keeps the
  // quotient's high byte.
  regs.dx = 2;
  regs.ax = 0;
  if (regs.bx == 2)
  {
    regs.ax = 0xFFFF;
  }
  else
  {
    DivideWord(_guest, regs.bx);
  }
  regs.ax = SwapBytes(regs.ax);
  _guest.Set(DS.discProfileStepFraction, regs.ax);
  SetHigh(regs.ax, 0);
  _guest.Set(DS.discProfileStepWhole, regs.ax);
  regs.bp = 0;
  regs.si = DS.circleProfile.offset;
  regs.cx = static_cast<std::uint16_t>(regs.cx << 1);
  regs.bx = regs.cx;
  // CX walks down from the centre row and BX up, both as 2*row; the centre row is drawn once.
  DrawDiscUpperRow(_guest);
  for (;;)
  {
    const std::uint32_t fraction = std::uint32_t{regs.bp} + _guest.Get(DS.discProfileStepFraction);
    regs.bp = static_cast<std::uint16_t>(fraction);
    regs.si = static_cast<std::uint16_t>(regs.si + _guest.Get(DS.discProfileStepWhole) + (fraction >> 16));
    regs.bx = static_cast<std::uint16_t>(regs.bx - 2);
    regs.cx = static_cast<std::uint16_t>(regs.cx + 2);
    const auto rowsLeft = static_cast<std::uint8_t>(_guest.Get(DS.discRowsLeft) - 1);
    _guest.Set(DS.discRowsLeft, rowsLeft);
    if (rowsLeft == 0)
    {
      return;
    }
    if (High(regs.cx) != 0)
    {
      DrawDiscUpperRow(_guest);
    }
    else
    {
      DrawDiscRowPair(_guest);
    }
  }
}

void FillSpan(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Machine::Registers saved = regs; // PUSH AX, BX, CX, DX
  const auto row = static_cast<std::uint8_t>(Low(regs.cx) >> 1);
  const std::uint8_t fill = _guest.Get(DS.discFillByte);
  const std::uint8_t leftX = Low(regs.dx);
  const std::uint8_t rightX = High(regs.dx);
  const auto leftByte = static_cast<std::uint8_t>(leftX >> 2);
  const auto rightByte = static_cast<std::uint8_t>(rightX >> 2);
  const std::uint16_t leftMask = _guest.Word(DS.spanLeftMasks.At(leftX & 3u));
  const std::uint16_t rightMask = _guest.Word(DS.spanRightMasks.At(rightX & 3u));
  const auto bytes = static_cast<std::uint8_t>(rightByte - leftByte);
  if (bytes == 0)
  {
    // One byte holds the whole span: both masks at once.
    regs.di = static_cast<std::uint16_t>(Join(row, leftX) >> 2);
    Plot(_guest, regs.di, static_cast<std::uint8_t>(High(rightMask) | High(leftMask)),
         static_cast<std::uint8_t>(Low(rightMask) & Low(leftMask) & fill));
  }
  else
  {
    regs.di = static_cast<std::uint16_t>(row * ROW_BYTES + leftByte);
    Plot(_guest, regs.di, High(leftMask), static_cast<std::uint8_t>(Low(leftMask) & fill));
    ++regs.di;
    regs.cx = static_cast<std::uint16_t>(bytes - 1);
    if (regs.cx != 0)
    {
      const bool odd = (regs.cx & 1) != 0;
      regs.cx = static_cast<std::uint16_t>(regs.cx >> 1);
      if (odd)
      {
        StoreByte(_guest, fill);
      }
      RepeatStoreWords(_guest, Join(fill, fill));
    }
    Plot(_guest, regs.di, High(rightMask), static_cast<std::uint8_t>(Low(rightMask) & fill));
  }
  regs.ax = saved.ax;
  regs.bx = saved.bx;
  regs.cx = saved.cx;
  regs.dx = saved.dx;
}

void DrawCircle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t centerX = regs.cx;
  const std::uint16_t centerRow = regs.dx;
  // The first octant, circleOctant scaled by BL/128, as four points.
  regs.cx = 8;
  regs.si = DS.circleOctant.offset;
  regs.di = DS.circlePoints.offset;
  do
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    if (Low(regs.ax) != 0)
    {
      regs.ax = static_cast<std::uint16_t>(Low(regs.ax) * Low(regs.bx));
      regs.ax = static_cast<std::uint16_t>(regs.ax << 1);
      SetLow(regs.ax, High(regs.ax));
    }
    SetHigh(regs.ax, 0);
    _guest.SetWord(regs.di, regs.ax);
    ++regs.si;
    regs.di = static_cast<std::uint16_t>(regs.di + 2);
  } while (--regs.cx != 0);
  // The second octant mirrors the first across the diagonal.
  regs.si = DS.circlePoints.offset;
  regs.di = DS.circlePoints.At(7);
  regs.cx = 4;
  do
  {
    regs.ax = _guest.Word(regs.si);
    _guest.SetWord(static_cast<std::uint16_t>(regs.di + 2), regs.ax);
    regs.ax = _guest.Word(static_cast<std::uint16_t>(regs.si + 2));
    _guest.SetWord(regs.di, regs.ax);
    regs.si = static_cast<std::uint16_t>(regs.si + 4);
    regs.di = static_cast<std::uint16_t>(regs.di - 4);
  } while (--regs.cx != 0);
  // The next quarter turns the first by 90 degrees, and the half after that by 180.
  regs.si = DS.circlePoints.offset;
  regs.di = DS.circlePoints.At(8);
  regs.cx = 8;
  do
  {
    regs.ax = _guest.Word(regs.si);
    _guest.SetWord(static_cast<std::uint16_t>(regs.di + 2), regs.ax);
    regs.ax = Negate(_guest.Word(static_cast<std::uint16_t>(regs.si + 2)));
    _guest.SetWord(regs.di, regs.ax);
    regs.si = static_cast<std::uint16_t>(regs.si + 4);
    regs.di = static_cast<std::uint16_t>(regs.di + 4);
  } while (--regs.cx != 0);
  regs.si = DS.circlePoints.offset;
  regs.di = DS.circlePoints.At(16);
  regs.cx = 16;
  do
  {
    regs.ax = Negate(_guest.Word(regs.si));
    _guest.SetWord(regs.di, regs.ax);
    regs.ax = Negate(_guest.Word(static_cast<std::uint16_t>(regs.si + 2)));
    _guest.SetWord(static_cast<std::uint16_t>(regs.di + 2), regs.ax);
    regs.si = static_cast<std::uint16_t>(regs.si + 4);
    regs.di = static_cast<std::uint16_t>(regs.di + 4);
  } while (--regs.cx != 0);
  regs.si = DS.circlePoints.offset;
  regs.bx = centerRow;
  regs.ax = centerX;
  regs.cx = CIRCLE_CHORDS;
  do
  {
    _guest.SetWord(regs.si, static_cast<std::uint16_t>(_guest.Word(regs.si) + regs.ax));
    const auto row = static_cast<std::uint16_t>(regs.si + 2);
    _guest.SetWord(row, static_cast<std::uint16_t>(_guest.Word(row) + regs.bx));
    regs.si = static_cast<std::uint16_t>(regs.si + 4);
  } while (--regs.cx != 0);
  // The chord from the last point to the first, then each to the next.
  regs.cx = _guest.Word(DS.circlePoints.offset);
  regs.ax = _guest.Get(DS.data2EFE);
  regs.dx = _guest.Word(static_cast<std::uint16_t>(regs.si - 4));
  regs.bx = _guest.Word(static_cast<std::uint16_t>(regs.si - 2));
  DrawClippedLine(_guest);
  regs.si = DS.circlePoints.offset;
  regs.bp = CIRCLE_CHORDS - 1;
  do
  {
    const std::uint16_t point = regs.si;
    const std::uint16_t chords = regs.bp;
    regs.cx = _guest.Word(regs.si);
    regs.ax = _guest.Word(static_cast<std::uint16_t>(regs.si + 2));
    regs.dx = _guest.Word(static_cast<std::uint16_t>(regs.si + 4));
    regs.bx = _guest.Word(static_cast<std::uint16_t>(regs.si + 6));
    DrawClippedLine(_guest);
    regs.bp = chords;
    regs.si = static_cast<std::uint16_t>(point + 4);
  } while (--regs.bp != 0);
}

void FillTriangleSpan(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t cx = regs.cx; // PUSH CX
  regs.di = regs.si;
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.dx) >> 2));
  SetHigh(regs.ax, Negative8(Low(regs.ax)) ? std::uint8_t{0xFF} : std::uint8_t{0}); // CBW
  regs.di = static_cast<std::uint16_t>(regs.di + regs.ax);
  SetLow(regs.cx, static_cast<std::uint8_t>((High(regs.dx) >> 2) - Low(regs.ax)));
  // triangleEdgeMasks at SS:0000: the left edge's mask at [BP], the right edge's at [BP+8].
  const auto edgeMask = [&](std::uint8_t _x, std::uint16_t _table)
  {
    regs.bp = static_cast<std::uint16_t>((_x & 3) << 1);
    return _guest.FarWord(regs.ss, static_cast<std::uint16_t>(regs.bp + _table));
  };
  if (Low(regs.cx) == 0)
  {
    // FillTriangleSpanOneByte (CS:1BD5): both edges in one byte.
    regs.ax = edgeMask(Low(regs.dx), 0);
    SetLow(regs.dx, High(regs.dx));
    regs.dx = edgeMask(Low(regs.dx), 8);
    SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) | High(regs.dx)));
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & Low(regs.dx) & Low(regs.bx)));
    Plot(_guest, regs.di, High(regs.ax), Low(regs.ax));
    regs.cx = cx;
    return;
  }
  SetHigh(regs.cx, 0);
  regs.ax = edgeMask(Low(regs.dx), 0);
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & Low(regs.bx)));
  Plot(_guest, regs.di, High(regs.ax), Low(regs.ax));
  ++regs.di;
  SetLow(regs.dx, High(regs.dx));
  regs.bp = static_cast<std::uint16_t>((regs.dx & 3) << 1);
  if (--regs.cx != 0)
  {
    // The bytes between: one to reach an even DI, then words.
    SetLow(regs.ax, Low(regs.bx));
    bool filled = false;
    if ((regs.di & 1) != 0)
    {
      StoreByte(_guest, Low(regs.ax));
      filled = --regs.cx == 0;
    }
    if (!filled)
    {
      const bool odd = (regs.cx & 1) != 0;
      regs.cx = static_cast<std::uint16_t>(regs.cx >> 1);
      if (regs.cx != 0)
      {
        SetHigh(regs.ax, Low(regs.ax));
        RepeatStoreWords(_guest, regs.ax);
      }
      if (odd)
      {
        StoreByte(_guest, Low(regs.ax));
      }
    }
  }
  regs.ax = edgeMask(Low(regs.dx), 8);
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & Low(regs.bx)));
  Plot(_guest, regs.di, High(regs.ax), Low(regs.ax));
  regs.cx = cx;
}

void FillTriangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = static_cast<std::uint16_t>(regs.dx << 1);
  regs.bp = static_cast<std::uint16_t>(regs.bp << 1);
  regs.di = static_cast<std::uint16_t>(regs.di << 1);
  regs.si = regs.ax;
  regs.ax = static_cast<std::uint16_t>(regs.ax | regs.bp | regs.di);
  SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) | High(regs.cx) | High(regs.bx) | High(regs.dx)));
  if (High(regs.ax) != 0)
  {
    FillClippedTriangle(_guest);
    return;
  }
  FillOnScreenTriangle(_guest);
}

void FillClippedTriangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = static_cast<std::uint16_t>(regs.si & regs.bx & regs.cx);
  if (Negative(regs.ax))
  {
    return;
  }
  regs.ax = static_cast<std::uint16_t>(regs.dx & regs.bp & regs.di);
  if (Negative(regs.ax))
  {
    return;
  }
  regs.ax = regs.si;
  if (Signed8(High(regs.ax)) > 0 && Signed8(High(regs.bx)) > 0 && Signed8(High(regs.cx)) > 0)
  {
    return;
  }
  if (Signed8(High(regs.dx)) > 0 && Signed(regs.bp) >= 0x100 && Signed(regs.di) >= 0x100)
  {
    return;
  }
  // PrepareClippedTriangle (CS:1E9D).
  regs.dx = static_cast<std::uint16_t>(Signed(regs.dx) >> 1);
  regs.bp = static_cast<std::uint16_t>(Signed(regs.bp) >> 1);
  regs.di = static_cast<std::uint16_t>(Signed(regs.di) >> 1);
  regs.si = regs.ds;
  regs.es = regs.si;
  PatchClippedStep(_guest, regs.si, ADD_TO_EDGE_A, STEP_CLIPPED_EDGE_A);
  PatchClippedStep(_guest, regs.si, ADD_TO_EDGE_B, STEP_CLIPPED_EDGE_B);
  if (regs.dx == regs.bp)
  {
    const std::int16_t topRow = Signed(regs.dx);
    const std::int16_t otherRow = Signed(regs.di);
    std::swap(regs.cx, regs.ax);
    std::swap(regs.di, regs.dx);
    if (topRow == otherRow)
    {
      FillClippedOneRow(_guest);
    }
    else if (topRow > otherRow)
    {
      FillClippedFlatBottom(_guest);
    }
    else
    {
      FillClippedFlatTop(_guest);
    }
    return;
  }
  if (Signed(regs.dx) > Signed(regs.bp))
  {
    std::swap(regs.bx, regs.ax);
    std::swap(regs.bp, regs.dx);
  }
  if (regs.dx == regs.di)
  {
    std::swap(regs.bx, regs.ax);
    std::swap(regs.bp, regs.dx);
    FillClippedFlatTop(_guest);
    return;
  }
  if (Signed(regs.dx) > Signed(regs.di))
  {
    std::swap(regs.cx, regs.ax);
    std::swap(regs.di, regs.dx);
  }
  if (regs.bp == regs.di)
  {
    FillClippedFlatBottom(_guest);
  }
  else
  {
    FillClippedGeneral(_guest);
  }
}

void WaitRetraceThenDelay(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = CGA_STATUS_PORT;
  WaitForRetrace(_guest, SPACE_VIEW_RETRACE_LOOP);
  SpinDelay(_guest, SPACE_VIEW_DELAY_TURNS, SPACE_VIEW_DELAY_LOOP);
}

void ShowCockpitScreen(Guest& _guest)
{
  const std::uint8_t layout = _guest.Get(DS.screenLayout);
  if (layout == 0)
  {
    return;
  }
  if ((layout & TEXT_LAYOUT_BIT) != 0)
  {
    SetGraphicsMode(_guest);
  }
  else
  {
    ClearCgaScreen(_guest);
  }
  _guest.Set(DS.screenLayout, 0);
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  regs.ax = static_cast<std::uint16_t>(_guest.CodeSegment() + COCKPIT_PARAGRAPH);
  regs.si = 0;
  regs.di = 0;
  regs.cx = CGA_BANK_WORDS;
  _guest.SetFlag(FLAG_DIRECTION, false);
  RepeatMoveWords(_guest, regs.ax);
  regs.di = CGA_ODD_BANK;
  regs.cx = CGA_BANK_WORDS;
  RepeatMoveWords(_guest, regs.ax);
}

void ClearCgaScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  regs.cx = CGA_BANK_WORDS;
  regs.di = 0;
  regs.ax = 0;
  RepeatStoreWords(_guest, regs.ax);
  regs.cx = CGA_BANK_WORDS;
  regs.di = CGA_ODD_BANK;
  RepeatStoreWords(_guest, regs.ax);
}

void ClearTextScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  regs.cx = TEXT_CELLS;
  regs.di = 0;
  regs.ax = Join(_guest.Get(DS.textAttribute), SPACE);
  RepeatStoreWords(_guest, regs.ax);
}

void DrawChartFrame(Guest& _guest)
{
  const std::uint8_t layout = _guest.Get(DS.screenLayout);
  if (layout == CHART_LAYOUT)
  {
    return;
  }
  if ((layout & TEXT_LAYOUT_BIT) != 0)
  {
    SetGraphicsMode(_guest);
  }
  else
  {
    ClearCgaScreen(_guest);
  }
  _guest.Set(DS.screenLayout, CHART_LAYOUT);
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  // The horizontals at y=9, 39, 168 and 199, x 32-287: 32 words of colour 3 each.
  constexpr std::uint16_t LINE_WORDS = 0x20;
  constexpr std::array<std::uint16_t, 3> LATER_LINES = {0x25F8, 0x1A48, 0x3EF8};
  regs.ax = 0xFFFF;
  regs.di = 0x2148;
  regs.cx = LINE_WORDS;
  RepeatStoreWords(_guest, regs.ax);
  for (const std::uint16_t line : LATER_LINES)
  {
    regs.di = line;
    SetLow(regs.cx, LINE_WORDS);
    RepeatStoreWords(_guest, regs.ax);
  }
  // The verticals at x=31 and x=288, lines 10-201, alternating banks: DX and BP swap each line.
  regs.si = 0x2147;
  regs.bx = 0x41;
  regs.cx = 0xC0;
  regs.bp = CGA_ODD_BANK;
  regs.dx = 0xE050;
  regs.ax = 0x03C0;
  do
  {
    _guest.SetVideoByte(regs.si, High(regs.ax));
    _guest.SetVideoByte(static_cast<std::uint16_t>(regs.bx + regs.si), Low(regs.ax));
    regs.si = static_cast<std::uint16_t>(regs.si + regs.dx);
    std::swap(regs.bp, regs.dx);
  } while (--regs.cx != 0);
}

void SetGraphicsMode(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = 4; // set mode 4: 320x200 in four colours
  _guest.Interrupt(BIOS_VIDEO);
  regs.bx = 0x0100; // palette 0
  SetHigh(regs.ax, 0x0B);
  _guest.Interrupt(BIOS_VIDEO);
  regs.dx = CGA_COLOR_PORT;
  SetLow(regs.ax, 0x10); // the bright palette, black background
  _guest.Out8(regs.dx, Low(regs.ax));
}

void SetTextMode(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = 1; // set mode 1: 40x25 colour text
  _guest.Interrupt(BIOS_VIDEO);
  regs.dx = CRTC_INDEX_PORT;
  SetLow(regs.ax, 0x0E); // CRTC register 14, the cursor address's high byte, = 0Eh: off the page
  _guest.Out8(regs.dx, Low(regs.ax));
  ++regs.dx;
  _guest.Out8(regs.dx, Low(regs.ax));
  regs.dx = CGA_MODE_PORT;
  SetLow(regs.ax, 0x08); // video on, blink off
  _guest.Out8(regs.dx, Low(regs.ax));
}

void DrawTitlePlanet(Guest& _guest)
{
  _guest.Set(DS.sunFringeMask, 1);
  DrawDisc(_guest);
  _guest.Set(DS.sunFringeMask, 0);
}

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr std::uint16_t GENERAL_REGISTERS = REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP;

constexpr Machine::NativeContract CLOBBERS_BX_CX{REGISTER_BX | REGISTER_CX, 0};
constexpr Machine::NativeContract CLOBBERS_DI{REGISTER_DI, 0};
// "Clobbers all": DS is left alone, and ES comes back equal to it or as it was, so both are compared.
constexpr Machine::NativeContract CLOBBERS_GENERAL{GENERAL_REGISTERS, 0};
constexpr Machine::NativeContract CLIPS_LINE{REGISTER_SI, FLAG_CARRY | FLAG_ZERO};
constexpr Machine::NativeContract DRAWS_LINE{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0};
constexpr Machine::NativeContract FILLS_TRIANGLE_SPAN{REGISTER_AX | REGISTER_DI | REGISTER_BP, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_SI_DI_ES{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI | REGISTER_ES, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_GENERAL_AND_ES{GENERAL_REGISTERS | REGISTER_ES, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_DX{REGISTER_AX | REGISTER_BX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_DX{REGISTER_AX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract SAVES_SCREENSHOT{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI, 0};

// The frame routines wait for the timer and the CGA's retrace as a rule: they run on the native thread, and the
// digests accept them (ADR-010 items 5 and 8).
constexpr Machine::NativeWait WAITS = Machine::NativeWait::Always;

constexpr std::array ENTRIES = {
  NativeEntry{0x01B7, "SaveScreenshot", &SaveScreenshot, SAVES_SCREENSHOT},
  NativeEntry{0x03FD, "WriteScreenshotFile", &WriteScreenshotFile, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x0570, "FinishSpaceViewFrame", &FinishSpaceViewFrame, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x0587, "PresentChartFrame", &PresentChartFrame, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x0599, "PresentSpaceView", &PresentSpaceView, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x05CC, "CopyChartBufferToScreen", &CopyChartBufferToScreen, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x060D, "ClearDrawBuffer", &ClearDrawBuffer, PRESERVES_ALL},
  NativeEntry{0x15E0, "PlotPixel", &PlotPixel, CLOBBERS_BX_CX},
  NativeEntry{0x1603, "DrawClippedLine", &DrawClippedLine, CLOBBERS_GENERAL},
  NativeEntry{0x1686, "ClipLineToLowEdge", &ClipLineToLowEdge, CLIPS_LINE},
  NativeEntry{0x16C1, "ClipLineToHighEdge", &ClipLineToHighEdge, CLIPS_LINE},
  NativeEntry{0x16D1, "DrawLine", &DrawLine, DRAWS_LINE},
  NativeEntry{0x1826, "DrawDisc", &DrawDisc, CLOBBERS_GENERAL},
  NativeEntry{0x1A07, "FillSpan", &FillSpan, CLOBBERS_DI},
  NativeEntry{0x1AC1, "DrawCircle", &DrawCircle, CLOBBERS_GENERAL},
  NativeEntry{0x1B7A, "FillTriangleSpan", &FillTriangleSpan, FILLS_TRIANGLE_SPAN},
  NativeEntry{0x1BFB, "FillTriangle", &FillTriangle, CLOBBERS_GENERAL},
  NativeEntry{0x1E6E, "FillClippedTriangle", &FillClippedTriangle, CLOBBERS_GENERAL},
  NativeEntry{0x45FF, "WaitRetraceThenDelay", &WaitRetraceThenDelay, CLOBBERS_AX_DX, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x7BC0, "ShowCockpitScreen", &ShowCockpitScreen, CLOBBERS_AX_CX_SI_DI_ES},
  NativeEntry{0x7BFB, "ClearCgaScreen", &ClearCgaScreen, CLOBBERS_AX_CX_DI},
  NativeEntry{0x7C12, "ClearTextScreen", &ClearTextScreen, CLOBBERS_AX_CX_DI},
  NativeEntry{0x7C25, "DrawChartFrame", &DrawChartFrame, CLOBBERS_GENERAL_AND_ES},
  NativeEntry{0x7CFE, "SetGraphicsMode", &SetGraphicsMode, CLOBBERS_AX_BX_DX},
  NativeEntry{0x7D11, "SetTextMode", &SetTextMode, CLOBBERS_AX_DX},
  NativeEntry{0x7D4E, "DrawTitlePlanet", &DrawTitlePlanet, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> VideoEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
