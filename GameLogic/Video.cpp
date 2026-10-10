#include "pch.h"

#include "Video.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Maths.h"
#include "SaveLoad.h"
#include "StartUp.h"
#include "Timer.h"

#include <optional>
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
// Where the copy leaves SI: past both banks of the image.
constexpr std::uint16_t COCKPIT_IMAGE_BYTES = 2 * CGA_BANK_WORDS * 2;

// screenLayout: 0 the cockpit, 1 the chart frame; bit 1 set while in text mode.
constexpr std::uint8_t CHART_LAYOUT = 1;
constexpr std::uint8_t TEXT_LAYOUT_BIT = 2;

// DrawChartFrame's box. The horizontals at y=9, 39, 168 and 199 from x=32, as offsets in B800h, each 32 words of colour 3.
constexpr std::array<std::uint16_t, 4> CHART_FRAME_LINES = {0x2148, 0x25F8, 0x1A48, 0x3EF8};
constexpr std::uint16_t CHART_FRAME_LINE_WORDS = 0x20;
constexpr std::uint16_t CHART_FRAME_COLOR_WORD = 0xFFFF;
// The verticals, 192 lines from line 9: the byte that holds x=31 there, and the one 41h on that holds x=288. A line's step to the
// next is to the even bank's next line, then to the odd bank's, in turn.
constexpr std::uint16_t CHART_FRAME_LEFT_START = 0x2147;
constexpr std::uint16_t CHART_FRAME_RIGHT_BYTES = 0x41;
constexpr std::uint16_t CHART_FRAME_TO_EVEN_LINE = 0xE050;
constexpr std::uint16_t CHART_FRAME_VERTICAL_LINES = 0xC0;
constexpr std::uint8_t CHART_FRAME_LEFT_BYTE = 0x03;  // MOV AX,03C0h: AH, x=31 in colour 3
constexpr std::uint8_t CHART_FRAME_RIGHT_BYTE = 0xC0; // AL, x=288

constexpr std::uint8_t GRAPHICS_MODE = 4;         // BIOS mode 4: 320x200 in four colours
constexpr std::uint8_t GRAPHICS_PALETTE = 0;      // green, red and brown
constexpr std::uint8_t BRIGHT_ON_BLACK = 0x10;    // the colour select register: the bright palette, a black background
constexpr std::uint8_t TEXT_MODE = 1;             // BIOS mode 1: 40x25 colour text
constexpr std::uint8_t CURSOR_OFF_PAGE = 0x0E;    // the cursor address's high byte, past the 40x25 page
constexpr std::uint8_t VIDEO_ON_BLINK_OFF = 0x08; // the mode control register: attribute bit 7 a bright background

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
constexpr std::uint8_t STATUS_VERTICAL_RETRACE = 0x08;
// What SetGraphicsMode leaves in BX and DX: MOV BX,100h for int 10h AH=0Bh, which keeps it, and MOV DX,3D9h, the colour select
// register's port.
constexpr std::uint16_t SELECT_PALETTE_BX = 0x0100;
constexpr std::uint16_t COLOR_SELECT_PORT = 0x03D9;
// What they call, through the hooks.
constexpr std::uint16_t DRAW_LASER_SIGHTS = 0x0630;
constexpr std::uint16_t PRESENT_SPACE_VIEW = 0x0599;
constexpr std::uint16_t CLEAR_DRAW_BUFFER = 0x060D;
// Where their loops jump back to: PresentSpaceView's wait for the frame's time is to its start, PRESENT_SPACE_VIEW.
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

// SaveScreenshot puts CriticalErrorInterrupt on int 24h, at 0000:0090, while it writes.
constexpr std::uint16_t VECTOR_TABLE_SEGMENT = 0;
constexpr std::uint16_t CRITICAL_ERROR_VECTOR_OFFSET = 0x0090;
constexpr std::uint16_t CRITICAL_ERROR_VECTOR_SEGMENT = 0x0092;
constexpr std::uint16_t CRITICAL_ERROR_INTERRUPT = 0x02F0;
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

// AND [_offset],_keep / OR [_offset],_color: the pixels _keep clears, then _color's set, two writes.
void Plot(GameState& _state, std::uint16_t _offset, std::uint8_t _keep, std::uint8_t _color)
{
  const auto kept = static_cast<std::uint8_t>(_state.Byte(_offset) & _keep);
  _state.SetByte(_offset, kept);
  _state.SetByte(_offset, static_cast<std::uint8_t>(kept | _color));
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
[[nodiscard]] std::uint16_t StringStep(bool _backward, std::uint16_t _bytes) noexcept
{
  return _backward ? Negate(_bytes) : _bytes;
}

// STOSB: _value at _segment:_offset. Returns the offset _step on, what DI becomes.
[[nodiscard]] std::uint16_t StoreByte(GameState& _state, std::uint16_t _segment, std::uint16_t _offset, std::uint8_t _value,
                                      std::uint16_t _step)
{
  _state.SetFarByte(_segment, _offset, _value);
  return static_cast<std::uint16_t>(_offset + _step);
}

// REP STOSW: _words words of _value from _segment:_offset, _step apart. Returns the offset after the last, what DI
// becomes; CX ends at 0.
[[nodiscard]] std::uint16_t RepeatStoreWords(GameState& _state, std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _words,
                                             std::uint16_t _value, std::uint16_t _step)
{
  std::uint16_t offset = _offset;
  for (std::uint16_t words = _words; words != 0; --words)
  {
    _state.SetFarWord(_segment, offset, _value);
    offset = static_cast<std::uint16_t>(offset + _step);
  }
  return offset;
}

// Where a string instruction leaves SI and DI.
struct StringOffsets
{
  std::uint16_t source;
  std::uint16_t destination;
};

// REP MOVSW: _words words from _sourceSegment:_source to _segment:_destination, _step apart. CX ends at 0.
[[nodiscard]] StringOffsets RepeatMoveWords(GameState& _state, std::uint16_t _sourceSegment, std::uint16_t _source, std::uint16_t _segment,
                                            std::uint16_t _destination, std::uint16_t _words, std::uint16_t _step)
{
  StringOffsets offsets{_source, _destination};
  for (std::uint16_t words = _words; words != 0; --words)
  {
    _state.SetFarWord(_segment, offsets.destination, _state.FarWord(_sourceSegment, offsets.source));
    offsets.source = static_cast<std::uint16_t>(offsets.source + _step);
    offsets.destination = static_cast<std::uint16_t>(offsets.destination + _step);
  }
  return offsets;
}

// CWD.
void ConvertToDoubleWord(Machine::Registers& _regs) noexcept
{
  _regs.dx = static_cast<std::uint16_t>(Negative(_regs.ax) ? 0xFFFF : 0);
}

// ---- Lines ----

// DrawLine's working state: the registers it draws with, as bytes where it uses them so, which it loads before its
// line routines and stores after them.
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
// bytes with REP STOSB, then the pixels left. Returns whether it ran REP STOSB, after MOV BX,DS / MOV ES,BX / CLD:
// the caller sets ES to DS and clears the direction flag.
[[nodiscard]] bool DrawHorizontalLine(GameState& _state, LineState& _line)
{
  if (_line.bl == 0)
  {
    Plot(_state, _line.di, _line.dh, _line.dl);
    return false;
  }
  _line.cx = static_cast<std::uint16_t>(_line.bl + 1);
  if ((_line.dh & LEFT_PIXEL_MASK) != 0)
  {
    for (;;)
    {
      _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
      if (!NextPixel(_line.dl, _line.dh))
      {
        _state.SetByte(_line.di, _line.al);
        ++_line.di;
        if (--_line.cx == 0)
        {
          return false;
        }
        break;
      }
      if (--_line.cx == 0)
      {
        _state.SetByte(_line.di, _line.al);
        return false;
      }
    }
  }
  _line.al = _line.ah;
  _line.ah = Low(_line.cx);
  _line.cx = static_cast<std::uint16_t>(_line.cx >> 2);
  const bool storesBytes = _line.cx != 0;
  if (storesBytes)
  {
    _line.bl = Low(_state.DataSegment());
    _line.bh = High(_state.DataSegment());
    for (; _line.cx != 0; --_line.cx)
    {
      _state.SetByte(_line.di, _line.al);
      ++_line.di;
    }
  }
  _line.ah = static_cast<std::uint8_t>(_line.ah & 3);
  if (_line.ah == 0)
  {
    return storesBytes;
  }
  _line.cx = _line.ah;
  _line.al = _state.Byte(_line.di);
  do
  {
    _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
    (void)NextPixel(_line.dl, _line.dh);
  } while (--_line.cx != 0);
  _state.SetByte(_line.di, _line.al);
  return storesBytes;
}

// DrawLineVertical (CS:17AF).
void DrawVerticalLine(GameState& _state, LineState& _line)
{
  _line.cx = _line.bh;
  do
  {
    Plot(_state, _line.di, _line.dh, _line.dl);
    _line.di = static_cast<std::uint16_t>(_line.di + _line.bp);
  } while (--_line.cx != 0);
  Plot(_state, _line.di, _line.dh, _line.dl);
}

// DrawLineSteep (CS:177E): a row a pixel, AH the error term.
void DrawSteepLine(GameState& _state, LineState& _line)
{
  _line.cx = _line.bh;
  ++_line.bh;
  ++_line.bl;
  _line.ah = _line.bh;
  do
  {
    const bool borrow = _line.ah < _line.bl;
    _line.ah = static_cast<std::uint8_t>(_line.ah - _line.bl);
    Plot(_state, _line.di, _line.dh, _line.dl);
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
  Plot(_state, _line.di, _line.dh, _line.dl);
}

// DrawLineShallow (CS:172F): a pixel a step, the byte kept in AL until the line leaves it.
void DrawShallowLine(GameState& _state, LineState& _line)
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
      _state.SetByte(_line.di, _line.al);
      _line.di = static_cast<std::uint16_t>(_line.di + _line.bp + (sameByte ? 0 : 1));
      _line.al = _state.Byte(_line.di);
    }
    else if (!NextPixel(_line.dl, _line.dh))
    {
      _state.SetByte(_line.di, _line.al);
      ++_line.di;
      _line.al = _state.Byte(_line.di);
    }
    else
    {
      if (--_line.cx == 0)
      {
        _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
        _state.SetByte(_line.di, _line.al);
        return;
      }
      continue;
    }
    if (--_line.cx == 0)
    {
      Plot(_state, _line.di, _line.dh, _line.dl);
      return;
    }
  }
}

// DrawLineDiagonal (CS:180E).
void DrawDiagonalLine(GameState& _state, LineState& _line)
{
  _line.cx = _line.bl;
  do
  {
    Plot(_state, _line.di, _line.dh, _line.dl);
    const bool sameByte = NextPixel(_line.dl, _line.dh);
    _line.di = static_cast<std::uint16_t>(_line.di + _line.bp + (sameByte ? 0 : 1));
  } while (--_line.cx != 0);
  Plot(_state, _line.di, _line.dh, _line.dl);
}

// CS:1623: the rows were doubled to clip; DrawLine takes them halved (MOV DH,BL / MOV CH,AL / SHR DH,1 / SHR CH,1). Returns
// what DrawLine returns.
bool DrawHalvedRows(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromDoubledRow, std::uint8_t _toX, std::uint8_t _toDoubledRow)
{
  return DrawLine(_state, _fromX, static_cast<std::uint8_t>(_fromDoubledRow >> 1), _toX, static_cast<std::uint8_t>(_toDoubledRow >> 1));
}

// The clip routines' result: CF and ZF, the flags their contract names.
void SetClipResult(Machine::Registers& _regs, bool _carry, bool _zero) noexcept
{
  const auto flags = static_cast<std::uint16_t>(_regs.flags & ~(FLAG_CARRY | FLAG_ZERO));
  _regs.flags = static_cast<std::uint16_t>(flags | (_carry ? FLAG_CARRY : 0) | (_zero ? FLAG_ZERO : 0));
}

// What a clip routine leaves for DrawClippedLine when it is done with one edge: CF set while the segment is still to
// draw, ZF set once BP's count of endpoints outside reaches 0, and SI, the cut's scratch.
struct ClipOutcome
{
  bool draw;
  bool allInside;
  std::uint16_t scratch;
};

void ClipOutcomeOut(Machine::Registers& _regs, ClipOutcome _outcome) noexcept
{
  _regs.si = _outcome.scratch;
  SetClipResult(_regs, _outcome.draw, _outcome.allInside);
}

// LeaveEndpoints (CS:16B5): the endpoints stay where they are, and the segment is still to draw: MOV SI,0 / INC SI / STC,
// which leave SI = 1, ZF clear from the INC and CF set.
[[nodiscard]] constexpr ClipOutcome LeaveEndpoints() noexcept
{
  return ClipOutcome{true, false, 1};
}

// MoveEndpointToEdge (CS:1691): endpoint A (CX, AX) onto the edge along the line to B (DX, BX).
void MoveEndpointToEdge(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = regs.dx;
  if (regs.dx == 0 || regs.cx == 0)
  {
    ClipOutcomeOut(regs, LeaveEndpoints());
    return;
  }
  regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bx);
  regs.cx = static_cast<std::uint16_t>(regs.cx - regs.dx);
  regs.dx = Negate(regs.dx);
  const auto product = static_cast<std::uint32_t>(std::int32_t{Signed(regs.ax)} * Signed(regs.dx));
  regs.ax = static_cast<std::uint16_t>(product);
  regs.dx = static_cast<std::uint16_t>(product >> 16);
  DivideSignedWordOnRegisters(_guest, regs.cx);
  regs.ax = static_cast<std::uint16_t>(regs.ax + regs.bx);
  regs.cx = 0;
  regs.dx = regs.si;
  if (High(regs.ax) != 0)
  {
    ClipOutcomeOut(regs, LeaveEndpoints());
    return;
  }
  --regs.bp;
  SetClipResult(regs, true, regs.bp == 0);
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

// CS:18F2-1924: the half-width of a disc's row, the profile byte at DS:_profile scaled by discRadiusRows and
// rounded, plus the fringe while sunFringeMask is set. The DX the fringe leaves is dead: DiscRowSpan's first
// instruction loads DX.
[[nodiscard]] std::uint16_t DiscHalfWidth(GameState& _state, std::uint16_t _profile)
{
  auto profile = static_cast<std::uint8_t>(_state.Byte(_profile) + 1);
  if (profile == 0)
  {
    --profile; // INC AL that stops at FFh
  }
  const auto product = static_cast<std::uint16_t>(profile * _state.Get(DS.discRadiusRows));
  const auto halfWidth = static_cast<std::uint8_t>(High(product) + (Low(product) >> 7));
  const std::uint8_t fringeMask = _state.Get(DS.sunFringeMask);
  if (fringeMask == 0)
  {
    return halfWidth;
  }
  // (a, b) becomes (b, a+b), and the new b's high byte, masked, widens the row.
  const std::uint16_t previousB = _state.Get(DS.fringeRandomB);
  const std::uint16_t previousA = _state.Get(DS.fringeRandomA);
  _state.Set(DS.fringeRandomA, previousB);
  const auto sum = static_cast<std::uint16_t>(previousA + previousB);
  _state.Set(DS.fringeRandomB, sum);
  return static_cast<std::uint16_t>(halfWidth + (High(sum) & fringeMask));
}

// What DiscRowSpan finds of a row: its left and right x, as words.
struct DiscRowEdges
{
  std::uint16_t left;  // discCenterX less the half-width, 0 where that is negative
  std::uint16_t right; // the half-width plus discCenterX, its low byte FFh past 255; the half-width alone when left
                       // is past 255 already
  bool visible;        // false when the row lies wholly off the buffer
};

// CS:1926-1940: the edges of a row _halfWidth either side of discCenterX. DX holds left and AX right, and on screen
// DH is right's low byte.
[[nodiscard]] DiscRowEdges DiscRowSpan(const GameState& _state, std::uint16_t _halfWidth)
{
  const std::uint16_t centerX = _state.Get(DS.discCenterX);
  DiscRowEdges edges{static_cast<std::uint16_t>(centerX - _halfWidth), _halfWidth, false};
  if (Negative(edges.left))
  {
    edges.left = 0;
  }
  if (High(edges.left) != 0)
  {
    return edges;
  }
  edges.right = static_cast<std::uint16_t>(_halfWidth + centerX);
  if (High(edges.right) != 0)
  {
    if (Negative8(High(edges.right)))
    {
      return edges;
    }
    SetLow(edges.right, 0xFF);
  }
  edges.visible = true;
  return edges;
}

// What a disc's row routine finds of a row: its edges, and, when it drew, the offset of the last byte FillSpan wrote.
struct DiscRow
{
  DiscRowEdges edges;
  std::optional<std::uint16_t> lastByte;
};

// CS:18EE: the row _upperRow (2*row, moving up) alone, from the profile byte at DS:_profile, while it is on the buffer.
[[nodiscard]] std::optional<DiscRow> DrawDiscUpperRow(GameState& _state, std::uint16_t _profile, std::uint16_t _upperRow, bool _backward)
{
  if (High(_upperRow) != 0)
  {
    return std::nullopt;
  }
  const DiscRowEdges edges = DiscRowSpan(_state, DiscHalfWidth(_state, _profile));
  if (!edges.visible)
  {
    return DiscRow{edges, std::nullopt};
  }
  return DiscRow{edges, FillSpan(_state, Low(edges.left), Low(edges.right), Low(_upperRow), _backward)};
}

// CS:187B: the rows _lowerRow (moving down) and _upperRow (moving up), as 2*row, one half-width for both; the upper while it
// is on the buffer.
[[nodiscard]] DiscRow DrawDiscRowPair(GameState& _state, std::uint16_t _profile, std::uint16_t _lowerRow, std::uint16_t _upperRow,
                                      bool _backward)
{
  const DiscRowEdges edges = DiscRowSpan(_state, DiscHalfWidth(_state, _profile));
  if (!edges.visible)
  {
    return DiscRow{edges, std::nullopt};
  }
  std::uint16_t lastByte = FillSpan(_state, Low(edges.left), Low(edges.right), Low(_lowerRow), _backward);
  if (High(_upperRow) == 0)
  {
    lastByte = FillSpan(_state, Low(edges.left), Low(edges.right), Low(_upperRow), _backward);
  }
  return DiscRow{edges, lastByte};
}

// What DrawDisc's row routines leave in the registers: AX the right edge and DX the left, with DH the right's low byte once
// the row is on the buffer (MOV DH,AL), and DI the last byte FillSpan wrote.
void DiscRowOut(Machine::Registers& _regs, const DiscRow& _row) noexcept
{
  _regs.ax = _row.edges.right;
  _regs.dx = _row.edges.visible ? Join(Low(_row.edges.right), Low(_row.edges.left)) : _row.edges.left;
  if (_row.lastByte)
  {
    _regs.di = *_row.lastByte;
  }
}

// Where DrawSmallDisc (CS:194B) puts a disc of radius 0-4: a 4-row sprite from smallDiscSprites, clipped a byte at a
// time at the left and right edges and a row at a time at the top and bottom. Its fields hold what the original works out
// before its row loop, as far as it gets: AX is rows and clip, BX sprite, CX row and DX x when the sprite is wholly off
// the buffer.
struct SmallDiscPlace
{
  std::uint16_t x;      // the sprite's left x: the centre's less half the radius, 4 on when that is left of the buffer
  std::uint16_t row;    // its top row likewise, 0 once clipped at the top
  std::uint16_t sprite; // its first byte to draw in smallDiscSprites
  std::uint8_t rows;    // the rows to draw, 4 less those off the top or the bottom
  std::uint8_t clip;    // a bit pair a row: bit 0 when only the sprite's right byte is drawn, bit 1 when only its left
  bool onBuffer;        // false when the sprite is wholly off the buffer, and nothing is drawn
};

// Where DrawSmallDisc's row loop stops, which the original leaves in the registers.
struct SmallDiscEnd
{
  std::uint16_t next;   // DI: the buffer offset below the last row drawn
  std::uint16_t sprite; // BX: the sprite byte after the last row drawn
  std::uint16_t bits;   // AX: the last row's bits, shifted into place, and in the fill the bytes it drew of them
  std::uint8_t clip;    // DH: the clip bits after their last turn
  std::uint8_t shift;   // DL: the sprite's shift into its first byte, 2 bits a pixel
};

// CS:194B-19AA: where the sprite of a disc of radius _radius goes for the centre (_centerX, _centerRow).
[[nodiscard]] SmallDiscPlace PlaceSmallDisc(std::uint8_t _radius, std::int16_t _centerX, std::int16_t _centerRow) noexcept
{
  // CMP BL,BH / JNE / INC BL: radius 0, with BH 0, draws radius 1's sprite. Its corner is half the radius up and left of
  // the centre.
  const std::uint8_t radius = _radius == 0 ? 1 : _radius;
  const auto half = static_cast<std::uint16_t>(radius >> 1);
  SmallDiscPlace place{static_cast<std::uint16_t>(static_cast<std::uint16_t>(_centerX) - half),
                       static_cast<std::uint16_t>(static_cast<std::uint16_t>(_centerRow) - half),
                       Offset(DS.smallDiscSprites.offset, static_cast<std::uint8_t>((radius - 1) << 2)),
                       SMALL_DISC_ROWS,
                       0,
                       false};
  if (Negative(place.x))
  {
    place.x = static_cast<std::uint16_t>(place.x + 4);
    if (Negative(place.x) || place.x == 0)
    {
      return place;
    }
    place.clip = CLIP_SPRITE_LEFT;
  }
  else
  {
    if (High(place.x) != 0)
    {
      return place;
    }
    if (place.x >= 0xFC)
    {
      place.clip = CLIP_SPRITE_RIGHT;
    }
  }
  if (Negative(place.row))
  {
    place.row = static_cast<std::uint16_t>(place.row + 4);
    if (Negative(place.row) || place.row == 0)
    {
      return place;
    }
    // The rows above the buffer are skipped: AND clears CF, so CMC / ADC adds one more. XOR CL,CL starts at row 0.
    place.rows = Low(place.row);
    place.sprite = WithLow(place.sprite, static_cast<std::uint8_t>(Low(place.sprite) + (~Low(place.row) & 3) + 1));
    place.row = 0;
  }
  else
  {
    if (place.row >= BUFFER_ROWS)
    {
      return place;
    }
    if (Low(place.row) >= BUFFER_ROWS - 3)
    {
      place.rows = static_cast<std::uint8_t>(Negate8(Low(place.row)) & 3);
    }
  }
  place.onBuffer = true;
  return place;
}

// DrawSmallDisc (CS:19AB-1A06): the sprite _place gives, in discFillByte, a row at a time from its top: each byte ANDed
// out and then ORed in, the both bytes of a row as words, as the original writes them.
[[nodiscard]] SmallDiscEnd DrawSmallDisc(GameState& _state, const SmallDiscPlace& _place)
{
  SmallDiscEnd end{static_cast<std::uint16_t>(Join(Low(_place.row), Low(_place.x)) >> 2), _place.sprite, Join(_place.clip, _place.rows),
                   _place.clip, static_cast<std::uint8_t>((Low(_place.x) & 3) << 1)};
  const std::uint8_t fill = _state.Get(DS.discFillByte);
  for (std::uint8_t rows = _place.rows; rows != 0; --rows)
  {
    end.bits = static_cast<std::uint16_t>(Join(_state.Byte(end.sprite), 0) >> end.shift);
    end.sprite = Offset(end.sprite, 1);
    const bool rightOnly = (end.clip & 1) != 0;
    end.clip = RotateRight(end.clip, 1);
    if (rightOnly)
    {
      end.clip = RotateRight(end.clip, 1);
      Plot(_state, end.next, static_cast<std::uint8_t>(~Low(end.bits)), static_cast<std::uint8_t>(Low(end.bits) & fill));
      SetLow(end.bits, static_cast<std::uint8_t>(Low(end.bits) & fill));
    }
    else
    {
      const bool leftOnly = (end.clip & 1) != 0;
      end.clip = RotateRight(end.clip, 1);
      if (leftOnly)
      {
        Plot(_state, end.next, static_cast<std::uint8_t>(~High(end.bits)), static_cast<std::uint8_t>(High(end.bits) & fill));
        SetHigh(end.bits, static_cast<std::uint8_t>(High(end.bits) & fill));
      }
      else
      {
        // Both bytes, as a word, the sprite's left byte at the lower address: AND WORD [DI] with the bits' complement,
        // then OR WORD [DI] with the bits in the fill.
        const std::uint16_t word = SwapBytes(end.bits);
        end.bits = static_cast<std::uint16_t>(word & Join(fill, fill));
        const auto kept = static_cast<std::uint16_t>(_state.Word(end.next) & ~word);
        _state.SetWord(end.next, kept);
        _state.SetWord(end.next, static_cast<std::uint16_t>(kept | end.bits));
      }
    }
    end.next = static_cast<std::uint16_t>(end.next + ROW_BYTES);
  }
  return end;
}

// ---- Triangles ----

// MOV transfer,CS:[_source] / MOV CS:[_site],transfer: a step instruction rewritten from the opcodes
// kept after the routine's RET. Returns the word moved, which the original leaves in the transfer register.
[[nodiscard]] std::uint16_t PatchStep(GameState& _state, std::uint16_t _source, std::uint16_t _site)
{
  const std::uint16_t opcodes = _state.CodeWord(_source);
  _state.SetCodeWord(_site, opcodes);
  return opcodes;
}

// MOV BP,DX / AND BP,3 / SHL BP,1, then MOV AX,[BP] for a span's left end or [BP+8] for its right: triangleEdgeMasks' word for
// an end at x = _x, its fill bits in the low byte and the bits it keeps in the high. BP addresses the stack segment, and
// SS:0000 is DS:AD60, where the table is.
[[nodiscard]] std::uint16_t TriangleEdgeMask(const GameState& _state, std::uint8_t _x, bool _right) noexcept
{
  return _state.Word(DS.triangleEdgeMasks.At((_right ? 4u : 0u) + (_x & 3u)));
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
    FillTriangleSpanEntry(_guest);
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
  DivideWordOnRegisters(_guest, regs.cx);
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
    regs.dx = PatchStep(_guest.State(), SUB_AX_SI, STEP_EDGE_A);
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
    regs.dx = PatchStep(_guest.State(), SUB_BX_DI, STEP_EDGE_B);
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
    regs.dx = PatchStep(_guest.State(), SUB_AX_SI, STEP_EDGE_A);
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
    regs.dx = PatchStep(_guest.State(), SUB_BX_DI, STEP_EDGE_B);
    SetHigh(regs.ax, Negate8(High(regs.ax)));
  }
  SetLow(regs.ax, 0);
  regs.dx = 0;
  DivideWordOnRegisters(_guest, regs.cx);
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
  regs.dx = PatchStep(_guest.State(), ADD_AX_SI, STEP_LONG_EDGE_UPPER);
  _guest.SetCodeWord(STEP_LONG_EDGE_LOWER, regs.dx);
  regs.dx = PatchStep(_guest.State(), ADD_BX_DI, STEP_SHORT_EDGE_UPPER);
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
    regs.dx = PatchStep(_guest.State(), SUB_AX_SI, STEP_LONG_EDGE_UPPER);
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
    regs.dx = PatchStep(_guest.State(), SUB_BX_DI, STEP_SHORT_EDGE_UPPER);
    SetLow(regs.ax, Negate8(Low(regs.ax)));
  }
  SetHigh(regs.ax, 0);
  ConvertToDoubleWord(regs);
  regs.ax = SwapBytes(regs.ax);
  SetLow(regs.cx, _guest.Get(DS.triangleUpperRows));
  DivideWordOnRegisters(_guest, regs.cx);
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
    regs.dx = PatchStep(_guest.State(), SUB_BX_DI, STEP_SHORT_EDGE_LOWER);
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
  regs.dx = PatchStep(_guest.State(), ADD_AX_SI, STEP_EDGE_A);
  regs.dx = PatchStep(_guest.State(), ADD_BX_DI, STEP_EDGE_B);
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

// The step pair at _site (ADD/ADC or SUB/SBB) rewritten from the pair at _source, a word at the site and a word 4 bytes on.
// Returns the second word moved, which the original leaves in the transfer register.
[[nodiscard]] std::uint16_t PatchClippedStep(GameState& _state, std::uint16_t _source, std::uint16_t _site)
{
  (void)PatchStep(_state, _source, _site);
  return PatchStep(_state, static_cast<std::uint16_t>(_source + CARRY_STEP_BYTES), static_cast<std::uint16_t>(_site + CARRY_STEP_BYTES));
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

// A 16.16 edge of the clipped walk: x's whole part, signed, and its fraction.
struct ClippedEdge
{
  std::int16_t whole;
  std::uint16_t fraction;
};

// Edge A stepped by clippedSlopeA.
[[nodiscard]] ClippedEdge StepClippedEdgeA(const GameState& _state, ClippedEdge _edge, bool _subtract)
{
  auto whole = static_cast<std::uint16_t>(_edge.whole);
  StepClippedEdge(whole, _edge.fraction, _state.Get(DS.clippedSlopeAWhole), _state.Get(DS.clippedSlopeAFraction), _subtract);
  return ClippedEdge{Signed(whole), _edge.fraction};
}

// Edge B stepped by clippedSlopeB.
[[nodiscard]] ClippedEdge StepClippedEdgeB(const GameState& _state, ClippedEdge _edge, bool _subtract)
{
  auto whole = static_cast<std::uint16_t>(_edge.whole);
  StepClippedEdge(whole, _edge.fraction, _state.Get(DS.clippedSlopeBWhole), _state.Get(DS.clippedSlopeBFraction), _subtract);
  return ClippedEdge{Signed(whole), _edge.fraction};
}

// Edge A as the clipped walk holds it, in AX:SI, and edge B, in BX:DI.
[[nodiscard]] ClippedEdge EdgeA(const Machine::Registers& _regs) noexcept
{
  return ClippedEdge{Signed(_regs.ax), _regs.si};
}

void SetEdgeA(Machine::Registers& _regs, ClippedEdge _edge) noexcept
{
  _regs.ax = static_cast<std::uint16_t>(_edge.whole);
  _regs.si = _edge.fraction;
}

[[nodiscard]] ClippedEdge EdgeB(const Machine::Registers& _regs) noexcept
{
  return ClippedEdge{Signed(_regs.bx), _regs.di};
}

void SetEdgeB(Machine::Registers& _regs, ClippedEdge _edge) noexcept
{
  _regs.bx = static_cast<std::uint16_t>(_edge.whole);
  _regs.di = _edge.fraction;
}

// XOR DX,DX / DIV _rows / store / XOR AX,AX / DIV _rows / store: AX pixels over _rows rows as 16.16.
void ClippedSlope(Guest& _guest, std::uint16_t _rows, DataField<std::uint16_t> _whole, DataField<std::uint16_t> _fraction)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = 0;
  DivideWordOnRegisters(_guest, _rows);
  _guest.Set(_whole, regs.ax);
  regs.ax = 0;
  DivideWordOnRegisters(_guest, _rows);
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
    SetEdgeA(regs, StepClippedEdgeA(_guest.State(), EdgeA(regs), _subtractA));
    SetEdgeB(regs, StepClippedEdgeB(_guest.State(), EdgeB(regs), _subtractB));
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
    regs.dx = PatchClippedStep(_guest.State(), SUBTRACT_FROM_EDGE_A, STEP_CLIPPED_EDGE_A);
    regs.cx = Negate(regs.cx);
  }
  std::swap(regs.cx, regs.ax);
  ClippedSlope(_guest, regs.di, DS.clippedSlopeAWhole, DS.clippedSlopeAFraction);
  const bool subtractB = Signed(regs.bx) < Signed(regs.cx);
  regs.bx = static_cast<std::uint16_t>(regs.bx - regs.cx);
  if (subtractB)
  {
    regs.dx = PatchClippedStep(_guest.State(), SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_EDGE_B);
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
    regs.dx = PatchClippedStep(_guest.State(), SUBTRACT_FROM_EDGE_A, STEP_CLIPPED_EDGE_A);
    regs.ax = Negate(regs.ax);
  }
  ClippedSlope(_guest, regs.di, DS.clippedSlopeAWhole, DS.clippedSlopeAFraction);
  regs.ax = bottomX;
  const bool subtractB = Signed(regs.ax) < Signed(regs.bx);
  regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bx);
  if (subtractB)
  {
    regs.dx = PatchClippedStep(_guest.State(), SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_EDGE_B);
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
  regs.si = PatchStep(_guest.State(), ADD_TO_EDGE_A, STEP_CLIPPED_UPPER_A);
  _guest.SetCodeWord(STEP_CLIPPED_LOWER_A, regs.si);
  regs.si = PatchStep(_guest.State(), ADD_TO_EDGE_A + CARRY_STEP_BYTES, STEP_CLIPPED_UPPER_A + CARRY_STEP_BYTES);
  _guest.SetCodeWord(STEP_CLIPPED_LOWER_A + CARRY_STEP_BYTES, regs.si);
  regs.si = PatchStep(_guest.State(), ADD_TO_EDGE_B, STEP_CLIPPED_UPPER_B);
  _guest.SetCodeWord(STEP_CLIPPED_LOWER_B, regs.si);
  regs.si = PatchStep(_guest.State(), ADD_TO_EDGE_B + CARRY_STEP_BYTES, STEP_CLIPPED_UPPER_B + CARRY_STEP_BYTES);
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
    regs.si = PatchStep(_guest.State(), SUBTRACT_FROM_EDGE_A, STEP_CLIPPED_UPPER_A);
    _guest.SetCodeWord(STEP_CLIPPED_LOWER_A, regs.si);
    regs.si = PatchStep(_guest.State(), SUBTRACT_FROM_EDGE_A + CARRY_STEP_BYTES, STEP_CLIPPED_UPPER_A + CARRY_STEP_BYTES);
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
    regs.si = PatchClippedStep(_guest.State(), SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_UPPER_B);
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
    SetEdgeA(regs, StepClippedEdgeA(_guest.State(), EdgeA(regs), subtractLong));
    SetEdgeB(regs, StepClippedEdgeB(_guest.State(), EdgeB(regs), subtractUpper));
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
      regs.si = PatchClippedStep(_guest.State(), SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_LOWER_B);
      regs.ax = Negate(regs.ax);
    }
    ClippedSlope(_guest, regs.cx, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
    regs.bx = _guest.Get(DS.clippedMiddleX);
    regs.ax = longEdge;
    // ClippedLowerRowLoop (CS:224E).
    do
    {
      SetEdgeB(regs, StepClippedEdgeB(_guest.State(), EdgeB(regs), subtractLower));
      if (!TakeClippedRow(_guest, true))
      {
        break;
      }
      SetEdgeA(regs, StepClippedEdgeA(_guest.State(), EdgeA(regs), subtractLong));
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
// that sees a retrace uses it up (Cga::SetRetraceSeenOnce), so these are the original's reads, one a turn. Each turn
// carries the bit AL holds at the jump, what it read.
void WaitForRetrace(Hardware& _hardware, std::uint16_t _loop)
{
  for (;;)
  {
    const auto retrace = static_cast<std::uint8_t>(_hardware.CgaStatus() & STATUS_VERTICAL_RETRACE);
    if (retrace != 0)
    {
      return;
    }
    _hardware.LoopTurn(_loop, {retrace});
  }
}

// MOV AX,_turns / DEC AX / JNZ at CS:_loop: a delay the 8088's speed made. Each turn carries AX, the count, so paced
// time never idles in it.
void SpinDelay(Hardware& _hardware, std::uint16_t _turns, std::uint16_t _loop)
{
  std::uint16_t count = _turns;
  while (--count != 0)
  {
    _hardware.LoopTurn(_loop, {count});
  }
}

// The copy loop at CS:_loop: _linePairs line pairs of a 64-byte buffer line from DS:_source to B800:_destination, the even
// line, then the odd one below it, then back to the next even line: MOV CX,BP / REP MOVSW / ADD DI,AX / MOV CX,BP /
// REP MOVSW / SUB DI,DX / DEC BX / JNZ, with BP, AX and DX the steps SetLinePairSteps gives. Each turn carries what the
// next reads and changes: SI, DI and BX. Returns where SI and DI end.
StringOffsets CopyLinePairs(GameState& _state, Hardware& _hardware, std::uint16_t _loop, std::uint16_t _source, std::uint16_t _destination,
                            std::uint16_t _linePairs, bool _backward)
{
  StringOffsets at{_source, _destination};
  std::uint16_t pairs = _linePairs;
  for (;;)
  {
    at = RepeatMoveWords(_state, _state.DataSegment(), at.source, GameState::VIDEO_SEGMENT, at.destination, BUFFER_LINE_WORDS,
                         StringStep(_backward, 2));
    at.destination = static_cast<std::uint16_t>(at.destination + TO_ODD_LINE);
    at = RepeatMoveWords(_state, _state.DataSegment(), at.source, GameState::VIDEO_SEGMENT, at.destination, BUFFER_LINE_WORDS,
                         StringStep(_backward, 2));
    at.destination = static_cast<std::uint16_t>(at.destination - TO_NEXT_EVEN_LINE);
    if (--pairs == 0)
    {
      return at;
    }
    _hardware.LoopTurn(_loop, {at.source, at.destination, pairs});
  }
}

// ---- Screens ----

// TEST [screenLayout],2 (CS:7BC7, CS:7C2C), for a screen that is not the one wanted: from text, mode 4 through the BIOS
// (SetGraphicsMode); from graphics, both banks cleared in the direction the routine finds (ClearCgaScreen).
ScreenChange MakeGraphicsScreen(GameState& _state, Hardware& _hardware, std::uint8_t _layout, bool _backward)
{
  if ((_layout & TEXT_LAYOUT_BIT) != 0)
  {
    SetGraphicsMode(_hardware);
    return ScreenChange::ModeSet;
  }
  ClearCgaScreen(_state, _backward);
  return ScreenChange::Cleared;
}

} // namespace

void SaveScreenshot(GameState& _state, Hardware& _hardware)
{
  RestoreDivideAndKeyboardInterrupts(_state, _hardware);
  (void)RestoreTimerInterrupt(_state, _hardware);
  // The IRQ 0 its PIT reprogramming raises when the timer's output was low, taken by the BIOS's handler, back on int 8. The
  // original's CPU takes it after RestoreTimerInterrupt's STI, before the clock is read; here it is taken once it returns, where
  // the register code's call of WriteScreenshotFile through its hook took it.
  _hardware.TakeDueInterrupts();
  _state.Set(DS.diskError, 0);
  // Int 24h's vector, pushed round the write and popped back after it.
  const std::uint16_t errorOffset = _state.FarWord(VECTOR_TABLE_SEGMENT, CRITICAL_ERROR_VECTOR_OFFSET);
  const std::uint16_t errorSegment = _state.FarWord(VECTOR_TABLE_SEGMENT, CRITICAL_ERROR_VECTOR_SEGMENT);
  _state.SetFarWord(VECTOR_TABLE_SEGMENT, CRITICAL_ERROR_VECTOR_OFFSET, CRITICAL_ERROR_INTERRUPT);
  _state.SetFarWord(VECTOR_TABLE_SEGMENT, CRITICAL_ERROR_VECTOR_SEGMENT, _state.CodeSegment());
  WriteScreenshotFile(_state, _hardware);
  if (_state.Get(DS.diskError) != 0)
  {
    ShowDiskError(_state);
  }
  _state.SetFarWord(VECTOR_TABLE_SEGMENT, CRITICAL_ERROR_VECTOR_SEGMENT, errorSegment);
  _state.SetFarWord(VECTOR_TABLE_SEGMENT, CRITICAL_ERROR_VECTOR_OFFSET, errorOffset);
  // InstallTimerInterrupt leaves ES = 0, the interrupt table, which InstallDivideAndKeyboardInterrupts takes.
  InstallTimerInterrupt(_state, _hardware);
  // Its PIT reprogramming likewise, the IRQ 0 taken by the game's handler after its RET, as the original's CPU takes it.
  _hardware.TakeDueInterrupts();
  InstallDivideAndKeyboardInterrupts(_state, _hardware, VECTOR_TABLE_SEGMENT);
}

void WriteScreenshotFile(GameState& _state, Hardware& _hardware)
{
  _hardware.SetDiskTransferArea(_state.DataSegment(), DS.diskTransferArea.offset);
  // The two digits, the second counting fastest: '00', '01' ... '99', '00'.
  std::uint16_t digits = _state.Get(DS.screenshotNumber);
  SetHigh(digits, static_cast<std::uint8_t>(High(digits) + 1));
  if (High(digits) == PAST_DIGITS)
  {
    SetHigh(digits, FIRST_DIGIT);
    SetLow(digits, static_cast<std::uint8_t>(Low(digits) + 1));
    if (Low(digits) == PAST_DIGITS)
    {
      SetLow(digits, FIRST_DIGIT);
    }
  }
  _state.Set(DS.screenshotNumber, digits);
  _state.Set(DS.screenshotTextFileDigits, digits);
  _state.Set(DS.screenshotGraphicsFileDigits, digits);
  // eliteNN.lo for the text page, eliteNN.hi for the graphics screen.
  const std::uint16_t name =
    (_state.Get(DS.screenLayout) & TEXT_LAYOUT_BIT) != 0 ? DS.screenshotTextFileName.offset : DS.screenshotGraphicsFileName.offset;
  const DosAnswer created = _hardware.CreateFile(_state.DataSegment(), name, 0);
  if (created.failed)
  {
    _state.Set(DS.diskError, 1);
    return;
  }
  _state.Set(DS.fileHandle, created.value);
  // From B800:0000: the text page, or both graphics banks and the gap between them.
  const std::uint16_t bytes = (_state.Get(DS.screenLayout) & TEXT_LAYOUT_BIT) != 0 ? TEXT_PAGE_BYTES : GRAPHICS_SCREEN_BYTES;
  if (_hardware.WriteFile(_state.Get(DS.fileHandle), GameState::VIDEO_SEGMENT, 0, bytes).failed)
  {
    _state.Set(DS.diskError, 1);
  }
  if (_hardware.CloseFile(_state.Get(DS.fileHandle)).failed)
  {
    _state.Set(DS.diskError, 1);
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

void PresentChartFrame(GameState& _state, Hardware& _hardware)
{
  // MOV AX,B800h / MOV ES,AX / CLD: AL, the bands CopyChartBufferToScreen skips, is 0 whatever the caller passed, and both
  // copy forwards.
  CopyChartBufferToScreen(_state, _hardware, 0, false);
  ClearDrawBuffer(_state, false);
}

void PresentSpaceView(GameState& _state, Hardware& _hardware, bool _backward)
{
  // MOV AL,[msSinceFrame] / CMP AL,[minimumFrameMs] / JB back to the start, while the timer interrupt counts msSinceFrame up.
  // Each turn loads AL before it reads it, so the loop carries nothing.
  while (_state.Get(DS.msSinceFrame) < _state.Get(DS.minimumFrameMs))
  {
    _hardware.LoopTurn(PRESENT_SPACE_VIEW, {});
  }
  _state.Set(DS.msSinceFrame, 0);
  WaitRetraceThenDelay(_hardware);
  (void)CopyLinePairs(_state, _hardware, SPACE_VIEW_COPY_LOOP, DS.spaceViewBuffer.offset, SPACE_VIEW_SCREEN_OFFSET, SPACE_VIEW_LINE_PAIRS,
                      _backward);
}

void CopyChartBufferToScreen(GameState& _state, Hardware& _hardware, std::uint8_t _bandsSkipped, bool _backward)
{
  // MOV DX,3DAh / PUSH AX, the retrace's loop at 05D0 and the delay's at 05D8, then POP AX: the bands are a value, not a word on
  // the stack.
  WaitForRetrace(_hardware, CHART_RETRACE_LOOP);
  SpinDelay(_hardware, CHART_DELAY_TURNS, CHART_DELAY_LOOP);
  // _bandsSkipped bands of 8 lines: SHL BL,1 twice / SUB BL,40h / NEG BL, so 64 - 4 * the bands line pairs as a byte, from
  // DS:the bands * 512, as SHL AH,1 leaves the bands doubled in AH.
  const auto linePairs = static_cast<std::uint8_t>(CHART_LINE_PAIRS - static_cast<std::uint8_t>(_bandsSkipped << 2));
  const std::uint16_t source = Join(static_cast<std::uint8_t>(_bandsSkipped << 1), 0);
  (void)CopyLinePairs(_state, _hardware, CHART_COPY_LOOP, source, CHART_SCREEN_OFFSET, linePairs, _backward);
}

void ClearDrawBuffer(GameState& _state, bool _backward)
{
  // MOV AX,<the data segment> / MOV ES,AX, then REP STOSW of 0 from DI = 0.
  (void)RepeatStoreWords(_state, _state.DataSegment(), 0, DRAW_BUFFER_WORDS, 0, StringStep(_backward, 2));
}

void PlotPixel(GameState& _state, std::uint8_t _x, std::uint8_t _row)
{
  // The pixel's two bits in the colour's fill byte, kept from the byte at row * 64 + x / 4 by the AND, set by the OR.
  const std::uint8_t pixel = RotateRight(LEFT_PIXEL_MASK, static_cast<unsigned>((_x & 3) << 1));
  const auto color = static_cast<std::uint8_t>(pixel & _state.Byte(ColorFillByteOffset(_state.Get(DS.drawColor))));
  Plot(_state, static_cast<std::uint16_t>(Join(_row, _x) >> 2), static_cast<std::uint8_t>(~pixel), color);
}

void DrawClippedLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // CS:1623 on the endpoints as they stand, DL, BL and CL, AL, and what DrawLine leaves of ES and the direction flag.
  const auto drawHalvedRows = [&_guest, &regs]
  { DrawLineOut(_guest, DrawHalvedRows(_guest.State(), Low(regs.dx), Low(regs.bx), Low(regs.cx), Low(regs.ax))); };
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
      drawHalvedRows();
      return;
    }
    ++regs.bp;
  }
  regs.ax = regs.si;
  ClipLineToLowEdge(_guest);
  if (Flag(regs, FLAG_ZERO))
  {
    drawHalvedRows();
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
    drawHalvedRows();
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
    drawHalvedRows();
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
  drawHalvedRows();
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
    SetClipResult(_guest.Regs(), false, false);
    return;
  }
  if (!Negative8(High(regs.dx)))
  {
    ClipOutcomeOut(_guest.Regs(), LeaveEndpoints());
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
    SetClipResult(_guest.Regs(), true, false);
    return;
  }
  if (Negative8(High(regs.cx)))
  {
    SwapThenCutLine(_guest);
    return;
  }
  // Both beyond 255: CF clear, ZF from AND CH,CH.
  SetClipResult(_guest.Regs(), false, High(regs.cx) == 0);
}

bool DrawLine(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromRow, std::uint8_t _toX, std::uint8_t _toRow)
{
  std::uint8_t x = _fromX;
  std::uint8_t row = _fromRow;
  std::uint8_t endX = _toX;
  std::uint8_t endRow = _toRow;
  LineState line;
  // BL = |dx| and BH = |drow|, with the endpoints swapped so that the line runs right; BP the row step.
  line.al = static_cast<std::uint8_t>(endX - x);
  if (endX < x)
  {
    line.al = Negate8(line.al);
    std::swap(x, endX);
    std::swap(row, endRow);
  }
  line.bl = line.al;
  line.bp = ROW_BYTES;
  line.al = static_cast<std::uint8_t>(endRow - row);
  if (endRow < row)
  {
    line.al = Negate8(line.al);
    line.bp = Negate(line.bp);
  }
  line.bh = line.al;
  line.di = static_cast<std::uint16_t>(Join(row, x) >> 2);
  line.al = _state.Byte(ColorFillByteOffset(_state.Get(DS.drawColor)));
  line.ah = line.al;
  // DL the pixel's color in place, DH the mask that keeps the rest of its byte.
  const auto shift = static_cast<std::uint8_t>((x & 3) << 1);
  const std::uint8_t pixel = RotateRight(LEFT_PIXEL_MASK, shift);
  line.dh = static_cast<std::uint8_t>(~pixel);
  line.al = static_cast<std::uint8_t>(line.al & pixel);
  line.dl = line.al;
  line.al = _state.Byte(line.di);
  line.cx = shift;
  if (line.bh == 0)
  {
    return DrawHorizontalLine(_state, line);
  }
  if (line.bl == 0)
  {
    DrawVerticalLine(_state, line);
  }
  else if (line.bl < line.bh)
  {
    DrawSteepLine(_state, line);
  }
  else if (line.bl != line.bh)
  {
    DrawShallowLine(_state, line);
  }
  else
  {
    DrawDiagonalLine(_state, line);
  }
  return false;
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
    // The registers as the original leaves them, which DrawSunOrPlanet's and DrawDistantStation's contracts compare: the
    // place as far as it got when the sprite is off the buffer, else where its row loop stopped, with CX = 0 from LOOP.
    const SmallDiscPlace place = PlaceSmallDisc(Low(regs.bx), Signed(regs.dx), Signed(regs.cx));
    regs.ax = Join(place.clip, place.rows);
    regs.bx = place.sprite;
    regs.cx = place.row;
    regs.dx = place.x;
    if (place.onBuffer)
    {
      const SmallDiscEnd end = DrawSmallDisc(_guest.State(), place);
      regs.ax = end.bits;
      regs.bx = end.sprite;
      regs.cx = 0;
      regs.dx = Join(end.clip, end.shift);
      regs.di = end.next;
    }
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
    DivideWordOnRegisters(_guest, regs.bx);
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
  if (const std::optional<DiscRow> row = DrawDiscUpperRow(_guest.State(), regs.si, regs.bx, Flag(regs, FLAG_DIRECTION)))
  {
    DiscRowOut(regs, *row);
  }
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
      if (const std::optional<DiscRow> row = DrawDiscUpperRow(_guest.State(), regs.si, regs.bx, Flag(regs, FLAG_DIRECTION)))
      {
        DiscRowOut(regs, *row);
      }
    }
    else
    {
      DiscRowOut(regs, DrawDiscRowPair(_guest.State(), regs.si, regs.cx, regs.bx, Flag(regs, FLAG_DIRECTION)));
    }
  }
}

std::uint16_t FillSpan(GameState& _state, std::uint8_t _left, std::uint8_t _right, std::uint8_t _doubledRow, bool _backward)
{
  const auto row = static_cast<std::uint8_t>(_doubledRow >> 1);
  const std::uint8_t fill = _state.Get(DS.discFillByte);
  const auto leftByte = static_cast<std::uint8_t>(_left >> 2);
  const auto rightByte = static_cast<std::uint8_t>(_right >> 2);
  const std::uint16_t leftMask = _state.Word(DS.spanLeftMasks.At(_left & 3u));
  const std::uint16_t rightMask = _state.Word(DS.spanRightMasks.At(_right & 3u));
  const auto bytes = static_cast<std::uint8_t>(rightByte - leftByte);
  if (bytes == 0)
  {
    // One byte holds the whole span: both masks at once.
    const auto at = static_cast<std::uint16_t>(Join(row, _left) >> 2);
    Plot(_state, at, static_cast<std::uint8_t>(High(rightMask) | High(leftMask)),
         static_cast<std::uint8_t>(Low(rightMask) & Low(leftMask) & fill));
    return at;
  }
  std::uint16_t at = static_cast<std::uint16_t>(row * ROW_BYTES + leftByte);
  Plot(_state, at, High(leftMask), static_cast<std::uint8_t>(Low(leftMask) & fill));
  ++at;
  // The bytes between the ends: STOSB for an odd one, then REP STOSW, into ES = DS.
  const auto middle = static_cast<std::uint16_t>(bytes - 1);
  if (middle != 0)
  {
    if ((middle & 1) != 0)
    {
      at = StoreByte(_state, _state.DataSegment(), at, fill, StringStep(_backward, 1));
    }
    at = RepeatStoreWords(_state, _state.DataSegment(), at, static_cast<std::uint16_t>(middle >> 1), Join(fill, fill),
                          StringStep(_backward, 2));
  }
  Plot(_state, at, High(rightMask), static_cast<std::uint8_t>(Low(rightMask) & fill));
  return at;
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

void FillTriangleSpan(GameState& _state, std::uint16_t _row, std::uint8_t _left, std::uint8_t _right, std::uint8_t _fill, bool _backward)
{
  // MOV AL,DL / SHR AL,1 twice / CBW / ADD DI,AX: the left x over 4 is below 40h, so AH is 0.
  const auto leftByte = static_cast<std::uint8_t>(_left >> 2);
  auto at = static_cast<std::uint16_t>(_row + leftByte);
  const auto bytes = static_cast<std::uint8_t>((_right >> 2) - leftByte);
  const std::uint16_t leftMask = TriangleEdgeMask(_state, _left, false);
  if (bytes == 0)
  {
    // FillTriangleSpanOneByte (CS:1BD5): both edges in one byte.
    const std::uint16_t rightMask = TriangleEdgeMask(_state, _right, true);
    Plot(_state, at, static_cast<std::uint8_t>(High(leftMask) | High(rightMask)),
         static_cast<std::uint8_t>(Low(leftMask) & Low(rightMask) & _fill));
    return;
  }
  Plot(_state, at, High(leftMask), static_cast<std::uint8_t>(Low(leftMask) & _fill));
  ++at;
  // The bytes between, into ES = DS: STOSB to reach an even DI, then REP STOSW, then STOSB for an odd one left.
  auto middle = static_cast<std::uint16_t>(bytes - 1);
  if (middle != 0 && (at & 1) != 0)
  {
    at = StoreByte(_state, _state.DataSegment(), at, _fill, StringStep(_backward, 1));
    --middle;
  }
  if (middle != 0)
  {
    const auto words = static_cast<std::uint16_t>(middle >> 1);
    if (words != 0)
    {
      at = RepeatStoreWords(_state, _state.DataSegment(), at, words, Join(_fill, _fill), StringStep(_backward, 2));
    }
    if ((middle & 1) != 0)
    {
      at = StoreByte(_state, _state.DataSegment(), at, _fill, StringStep(_backward, 1));
    }
  }
  const std::uint16_t rightMask = TriangleEdgeMask(_state, _right, true);
  Plot(_state, at, High(rightMask), static_cast<std::uint8_t>(Low(rightMask) & _fill));
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
  regs.si = PatchClippedStep(_guest.State(), ADD_TO_EDGE_A, STEP_CLIPPED_EDGE_A);
  regs.si = PatchClippedStep(_guest.State(), ADD_TO_EDGE_B, STEP_CLIPPED_EDGE_B);
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

void WaitRetraceThenDelay(Hardware& _hardware)
{
  // MOV DX,3DAh and the retrace's loop at 4602, then MOV AX,7D0h and the delay's at 460A, which counts AX down to 0.
  WaitForRetrace(_hardware, SPACE_VIEW_RETRACE_LOOP);
  SpinDelay(_hardware, SPACE_VIEW_DELAY_TURNS, SPACE_VIEW_DELAY_LOOP);
}

ScreenChange ShowCockpitScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  const std::uint8_t layout = _state.Get(DS.screenLayout);
  if (layout == 0)
  {
    return ScreenChange::None;
  }
  const ScreenChange change = MakeGraphicsScreen(_state, _hardware, layout, _backward);
  _state.Set(DS.screenLayout, 0);
  // PUSH DS / MOV DS,<cockpitImage's segment>, then from its start both banks, after CLD: the even lines' 8000 bytes and the odd
  // lines' after them.
  const auto cockpit = static_cast<std::uint16_t>(_state.CodeSegment() + COCKPIT_PARAGRAPH);
  const StringOffsets even = RepeatMoveWords(_state, cockpit, 0, GameState::VIDEO_SEGMENT, 0, CGA_BANK_WORDS, StringStep(false, 2));
  (void)RepeatMoveWords(_state, cockpit, even.source, GameState::VIDEO_SEGMENT, CGA_ODD_BANK, CGA_BANK_WORDS, StringStep(false, 2));
  return change;
}

void ClearCgaScreen(GameState& _state, bool _backward)
{
  (void)RepeatStoreWords(_state, GameState::VIDEO_SEGMENT, 0, CGA_BANK_WORDS, 0, StringStep(_backward, 2));
  (void)RepeatStoreWords(_state, GameState::VIDEO_SEGMENT, CGA_ODD_BANK, CGA_BANK_WORDS, 0, StringStep(_backward, 2));
}

void ClearTextScreen(GameState& _state, bool _backward)
{
  (void)RepeatStoreWords(_state, GameState::VIDEO_SEGMENT, 0, TEXT_CELLS, Join(_state.Get(DS.textAttribute), SPACE),
                         StringStep(_backward, 2));
}

ScreenChange DrawChartFrame(GameState& _state, Hardware& _hardware, bool _backward)
{
  const std::uint8_t layout = _state.Get(DS.screenLayout);
  if (layout == CHART_LAYOUT)
  {
    return ScreenChange::None;
  }
  const ScreenChange change = MakeGraphicsScreen(_state, _hardware, layout, _backward);
  _state.Set(DS.screenLayout, CHART_LAYOUT);
  // The horizontals at y=9, 39, 168 and 199, x 32-287: 32 words of colour 3 each into ES = B800h, by REP STOSW in the direction
  // the routine finds (MOV CX,20h, then MOV CL,20h over the 0 the last left).
  for (const std::uint16_t line : CHART_FRAME_LINES)
  {
    (void)RepeatStoreWords(_state, GameState::VIDEO_SEGMENT, line, CHART_FRAME_LINE_WORDS, CHART_FRAME_COLOR_WORD,
                           StringStep(_backward, 2));
  }
  // PUSH DS / MOV DS,ES, then the verticals at x=31 and x=288 from line 9, a line a turn, alternating banks: the step to the next
  // line and the one after it swap each turn (XCHG BP,DX), and LOOP counts the lines.
  std::uint16_t at = CHART_FRAME_LEFT_START;
  std::uint16_t step = CHART_FRAME_TO_EVEN_LINE;
  std::uint16_t nextStep = CGA_ODD_BANK;
  for (std::uint16_t lines = CHART_FRAME_VERTICAL_LINES; lines != 0; --lines)
  {
    _state.SetVideoByte(at, CHART_FRAME_LEFT_BYTE);
    _state.SetVideoByte(static_cast<std::uint16_t>(at + CHART_FRAME_RIGHT_BYTES), CHART_FRAME_RIGHT_BYTE);
    at = static_cast<std::uint16_t>(at + step);
    std::swap(step, nextStep);
  }
  return change;
}

void SetGraphicsMode(Hardware& _hardware)
{
  _hardware.SetVideoMode(GRAPHICS_MODE);
  _hardware.SelectPalette(GRAPHICS_PALETTE);
  _hardware.SetColorSelect(BRIGHT_ON_BLACK);
}

void SetTextMode(Hardware& _hardware)
{
  _hardware.SetVideoMode(TEXT_MODE);
  _hardware.SetCursorAddressHigh(CURSOR_OFF_PAGE);
  _hardware.SetModeControl(VIDEO_ON_BLINK_OFF);
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
using Machine::REGISTER_SI;

constexpr std::uint16_t GENERAL_REGISTERS = REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP;

constexpr Machine::NativeContract CLOBBERS_BX_CX{REGISTER_BX | REGISTER_CX, 0};
// "Clobbers all": DS is left alone, and ES comes back equal to it or as it was, so both are compared.
constexpr Machine::NativeContract CLOBBERS_GENERAL{GENERAL_REGISTERS, 0};
constexpr Machine::NativeContract CLIPS_LINE{REGISTER_SI, FLAG_CARRY | FLAG_ZERO};
// DrawLine's: ES as the original leaves it, DS after a horizontal line's REP STOSB, which DrawClippedLine's, DrawLaserBeams'
// and UpdateStardust's contracts compare.
constexpr Machine::NativeContract DRAWS_LINE{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI | REGISTER_BP, 0};
// FillTriangleSpan's: AX and BP as the original leaves them, which RenderBlueprintBody's contract compares
// (FillTriangleSpanEntry).
constexpr Machine::NativeContract FILLS_TRIANGLE_SPAN{REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
// PresentSpaceView's: DX as the original leaves it (PresentSpaceViewEntry).
constexpr Machine::NativeContract PRESENTS_SPACE_VIEW{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0};
// ShowCockpitScreen's: SI and ES as the original leaves them (ShowCockpitScreenEntry).
constexpr Machine::NativeContract SHOWS_COCKPIT{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
// DrawChartFrame's: ES as the original leaves it (DrawChartFrameEntry).
constexpr Machine::NativeContract DRAWS_CHART_FRAME{GENERAL_REGISTERS, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_DX{REGISTER_AX | REGISTER_BX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_DX{REGISTER_AX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract SAVES_SCREENSHOT{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI, 0};

} // namespace

// ── The entries of the routines de-assembled so far ──

void ClearDrawBufferEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const bool backward = Flag(regs, FLAG_DIRECTION);
  ClearDrawBuffer(_guest.State(), backward);
  // The contract keeps ES, AX, CX and DI, as REP STOSW leaves them.
  regs.es = _guest.DataSegment();
  regs.ax = 0;
  regs.cx = 0;
  regs.di = static_cast<std::uint16_t>(DRAW_BUFFER_WORDS * StringStep(backward, 2));
  _guest.Clobber(PRESERVES_ALL);
}

void PlotPixelEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  PlotPixel(_guest.State(), Low(regs.dx), High(regs.dx));
  _guest.Clobber(CLOBBERS_BX_CX);
}

void DrawLineEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  DrawLineOut(_guest, DrawLine(_guest.State(), Low(regs.dx), High(regs.dx), Low(regs.cx), High(regs.cx)));
  _guest.Clobber(DRAWS_LINE);
}

void DrawLineOut(Guest& _guest, bool _filled)
{
  if (_filled)
  {
    // MOV BX,DS / MOV ES,BX / CLD, before the horizontal line's REP STOSB.
    Machine::Registers& regs = _guest.Regs();
    regs.es = regs.ds;
    _guest.SetFlag(FLAG_DIRECTION, false);
  }
}

void FillSpanEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The contract keeps DI, the span's last byte, which DrawDisc leaves for DrawTitlePlanet and DrawSunOrPlanet to
  // compare.
  regs.di = FillSpan(_guest.State(), Low(regs.dx), High(regs.dx), Low(regs.cx), Flag(regs, FLAG_DIRECTION));
  _guest.Clobber(PRESERVES_ALL);
}

void ClearCgaScreenEntry(Guest& _guest)
{
  ClearCgaScreen(_guest.State(), _guest.Flag(FLAG_DIRECTION));
  _guest.Regs().es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_AX_CX_DI);
}

void ClearTextScreenEntry(Guest& _guest)
{
  ClearTextScreen(_guest.State(), _guest.Flag(FLAG_DIRECTION));
  _guest.Regs().es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_AX_CX_DI);
}

void SaveScreenshotEntry(Guest& _guest)
{
  SaveScreenshot(_guest.State(), _guest.Devices());
  // InstallDivideAndKeyboardInterrupts leaves ES on the CGA's memory.
  _guest.Regs().es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(SAVES_SCREENSHOT);
}

void WriteScreenshotFileEntry(Guest& _guest)
{
  WriteScreenshotFile(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX);
}

void PresentChartFrameEntry(Guest& _guest)
{
  PresentChartFrame(_guest.State(), _guest.Devices());
  // CLD, and MOV AX,B800h / MOV ES,AX after ClearDrawBuffer's ES = DS.
  _guest.SetFlag(FLAG_DIRECTION, false);
  _guest.Regs().es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_GENERAL);
}

void PresentSpaceViewEntry(Guest& _guest)
{
  PresentSpaceView(_guest.State(), _guest.Devices(), _guest.Flag(FLAG_DIRECTION));
  // MOV DX,1FF0h, the copy's step back to the next even line, which the contract compares: FinishSpaceViewFrame hands it on, and
  // a caller of that reads it (hyperspace-and-fight.replay's pirates digest moves with it poisoned).
  _guest.Regs().dx = TO_NEXT_EVEN_LINE;
  _guest.Clobber(PRESENTS_SPACE_VIEW);
}

void CopyChartBufferToScreenEntry(Guest& _guest)
{
  CopyChartBufferToScreen(_guest.State(), _guest.Devices(), Low(_guest.Regs().ax), _guest.Flag(FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_GENERAL);
}

void WaitRetraceThenDelayEntry(Guest& _guest)
{
  WaitRetraceThenDelay(_guest.Devices());
  _guest.Clobber(CLOBBERS_AX_DX);
}

void ShowCockpitScreenEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ScreenChange change = ShowCockpitScreen(_guest.State(), _guest.Devices(), _guest.Flag(FLAG_DIRECTION));
  if (change == ScreenChange::None)
  {
    _guest.Clobber(SHOWS_COCKPIT);
    return;
  }
  if (change == ScreenChange::ModeSet)
  {
    // The contract compares BX and DX, which SetGraphicsMode leaves as its own MOVs set them.
    regs.bx = SELECT_PALETTE_BX;
    regs.dx = COLOR_SELECT_PORT;
  }
  // CLD before the copy, and what the copy leaves in SI and ES, which the contract compares: RestoreFlightScreen, whose contract
  // compares SI, ends with them, and the screens drawn after it write through ES.
  _guest.SetFlag(FLAG_DIRECTION, false);
  regs.si = COCKPIT_IMAGE_BYTES;
  regs.es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(SHOWS_COCKPIT);
}

void DrawChartFrameEntry(Guest& _guest)
{
  if (DrawChartFrame(_guest.State(), _guest.Devices(), _guest.Flag(FLAG_DIRECTION)) != ScreenChange::None)
  {
    // MOV ES,AX with AX = B800h, which the contract compares: the chart screens draw their titles through it next.
    _guest.Regs().es = GameState::VIDEO_SEGMENT;
  }
  _guest.Clobber(DRAWS_CHART_FRAME);
}

void FillTriangleSpanEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t left = Low(regs.dx);
  const std::uint8_t right = High(regs.dx);
  const std::uint8_t fill = Low(regs.bx);
  FillTriangleSpan(_guest.State(), regs.si, left, right, fill, Flag(regs, FLAG_DIRECTION));
  // AX, DX and BP as the original leaves them, which the contract compares: after a face's last triangle, DrawVisibleFaces
  // returns them to RenderBlueprintBody, whose contract compares them. BP indexes the right end's mask word; AX is the right
  // end's AND and OR, with the left end's merged in when both ends are in one byte; DX is the right end's mask word then,
  // else DL = DH from MOV DL,DH.
  const std::uint16_t leftMask = TriangleEdgeMask(_guest.State(), left, false);
  const std::uint16_t rightMask = TriangleEdgeMask(_guest.State(), right, true);
  if ((left >> 2) == (right >> 2))
  {
    regs.ax =
      Join(static_cast<std::uint8_t>(High(leftMask) | High(rightMask)), static_cast<std::uint8_t>(Low(leftMask) & Low(rightMask) & fill));
    regs.dx = rightMask;
  }
  else
  {
    regs.ax = Join(High(rightMask), static_cast<std::uint8_t>(Low(rightMask) & fill));
    regs.dx = Join(right, right);
  }
  regs.bp = static_cast<std::uint16_t>((right & 3u) << 1);
  _guest.Clobber(FILLS_TRIANGLE_SPAN);
}

void SetGraphicsModeEntry(Guest& _guest)
{
  SetGraphicsMode(_guest.Devices());
  _guest.Clobber(CLOBBERS_AX_BX_DX);
}

void SetTextModeEntry(Guest& _guest)
{
  SetTextMode(_guest.Devices());
  _guest.Clobber(CLOBBERS_AX_DX);
}

namespace
{

// The frame routines wait for the timer and the CGA's retrace as a rule: they run on the native thread, and the
// digests accept them (ADR-010 items 5 and 8).
constexpr Machine::NativeWait WAITS = Machine::NativeWait::Always;

constexpr std::array ENTRIES = {
  NativeEntry{0x01B7, "SaveScreenshot", &SaveScreenshotEntry, SAVES_SCREENSHOT},
  NativeEntry{0x03FD, "WriteScreenshotFile", &WriteScreenshotFileEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x0570, "FinishSpaceViewFrame", &FinishSpaceViewFrame, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x0587, "PresentChartFrame", &PresentChartFrameEntry, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x0599, "PresentSpaceView", &PresentSpaceViewEntry, PRESENTS_SPACE_VIEW, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x05CC, "CopyChartBufferToScreen", &CopyChartBufferToScreenEntry, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x060D, "ClearDrawBuffer", &ClearDrawBufferEntry, PRESERVES_ALL},
  NativeEntry{0x15E0, "PlotPixel", &PlotPixelEntry, CLOBBERS_BX_CX},
  NativeEntry{0x1603, "DrawClippedLine", &DrawClippedLine, CLOBBERS_GENERAL},
  NativeEntry{0x1686, "ClipLineToLowEdge", &ClipLineToLowEdge, CLIPS_LINE},
  NativeEntry{0x16C1, "ClipLineToHighEdge", &ClipLineToHighEdge, CLIPS_LINE},
  NativeEntry{0x16D1, "DrawLine", &DrawLineEntry, DRAWS_LINE},
  NativeEntry{0x1826, "DrawDisc", &DrawDisc, CLOBBERS_GENERAL},
  NativeEntry{0x1A07, "FillSpan", &FillSpanEntry, PRESERVES_ALL},
  NativeEntry{0x1AC1, "DrawCircle", &DrawCircle, CLOBBERS_GENERAL},
  NativeEntry{0x1B7A, "FillTriangleSpan", &FillTriangleSpanEntry, FILLS_TRIANGLE_SPAN},
  NativeEntry{0x1BFB, "FillTriangle", &FillTriangle, CLOBBERS_GENERAL},
  NativeEntry{0x1E6E, "FillClippedTriangle", &FillClippedTriangle, CLOBBERS_GENERAL},
  NativeEntry{0x45FF, "WaitRetraceThenDelay", &WaitRetraceThenDelayEntry, CLOBBERS_AX_DX, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x7BC0, "ShowCockpitScreen", &ShowCockpitScreenEntry, SHOWS_COCKPIT},
  NativeEntry{0x7BFB, "ClearCgaScreen", &ClearCgaScreenEntry, CLOBBERS_AX_CX_DI},
  NativeEntry{0x7C12, "ClearTextScreen", &ClearTextScreenEntry, CLOBBERS_AX_CX_DI},
  NativeEntry{0x7C25, "DrawChartFrame", &DrawChartFrameEntry, DRAWS_CHART_FRAME},
  NativeEntry{0x7CFE, "SetGraphicsMode", &SetGraphicsModeEntry, CLOBBERS_AX_BX_DX},
  NativeEntry{0x7D11, "SetTextMode", &SetTextModeEntry, CLOBBERS_AX_DX},
  NativeEntry{0x7D4E, "DrawTitlePlanet", &DrawTitlePlanet, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> VideoEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
