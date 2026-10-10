#include "pch.h"

#include "Video.h"

#include "Arithmetic.h"
#include "Combat.h"
#include "DataOverlay.h"
#include "Maths.h"
#include "SaveLoad.h"
#include "StartUp.h"
#include "Timer.h"

#include <initializer_list>
#include <utility>
#include <vector>

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
// Where their loops jump back to: PresentSpaceView's wait for the frame's time is to its start.
constexpr std::uint16_t PRESENT_SPACE_VIEW = 0x0599;
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

// LeaveEndpoints (CS:16B5): the ends stay where they are, and the line is still to draw: MOV SI,0 / INC SI / STC, which leave
// ZF clear from the INC and CF set.
[[nodiscard]] constexpr ClipStep LeaveEndpoints(ClipLine _line) noexcept
{
  return ClipStep{_line, true, false};
}

// MoveEndpointToEdge (CS:1691): the first end (CX, AX) onto the edge at 0 along the line to the second (DX, BX): MOV SI,DX, then
// unless an end is on the edge already, SUB AX,BX / SUB CX,DX / NEG DX / IMUL DX / IDIV CX / ADD AX,BX / MOV CX,0 / MOV DX,SI. The
// divide saves BX, the second end's other coordinate, when it overflows. The moved end inside 0-255 counts off BP, DEC BP setting
// ZF once none is outside; outside it, the ends stay as they now are (LeaveEndpoints).
[[nodiscard]] ClipStep MoveEndpointToEdge(GameState& _state, ClipLine _line)
{
  if (_line.secondCut == 0 || _line.firstCut == 0)
  {
    return LeaveEndpoints(_line);
  }
  const auto along = static_cast<std::uint16_t>(_line.firstAlong - _line.secondAlong);
  const auto cut = static_cast<std::uint16_t>(_line.firstCut - _line.secondCut);
  const auto product = static_cast<std::uint32_t>(std::int32_t{Signed(along)} * Signed(Negate(_line.secondCut)));
  const std::uint16_t moved = DivideSignedWord(_state, product, cut, _line.secondAlong).quotient;
  _line.firstAlong = static_cast<std::uint16_t>(moved + _line.secondAlong);
  _line.firstCut = 0;
  if (High(_line.firstAlong) != 0)
  {
    return LeaveEndpoints(_line);
  }
  _line.outside = static_cast<std::uint16_t>(_line.outside - 1);
  return ClipStep{_line, true, _line.outside == 0};
}

// SwapThenCutLine (CS:168E): XCHG CX,DX / XCHG BX,AX, then MoveEndpointToEdge: the second end moved, the ends left swapped.
[[nodiscard]] ClipStep SwapThenCutLine(GameState& _state, ClipLine _line)
{
  std::swap(_line.firstCut, _line.secondCut);
  std::swap(_line.firstAlong, _line.secondAlong);
  return MoveEndpointToEdge(_state, _line);
}

// XCHG CX,AX / XCHG DX,BX: the other axis cut.
[[nodiscard]] constexpr ClipLine Transpose(ClipLine _line) noexcept
{
  std::swap(_line.firstCut, _line.firstAlong);
  std::swap(_line.secondCut, _line.secondAlong);
  return _line;
}

// SUB CX,0FFh / SUB DX,0FFh, or ADD with _bytes 0FFh: the cut coordinates moved so that the edge at 255 is at 0, or back.
[[nodiscard]] constexpr ClipLine ShiftCut(ClipLine _line, std::uint16_t _bytes) noexcept
{
  _line.firstCut = static_cast<std::uint16_t>(_line.firstCut + _bytes);
  _line.secondCut = static_cast<std::uint16_t>(_line.secondCut + _bytes);
  return _line;
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

// CS:18EE: the row _upperRow (2*row, moving up) alone, from the profile byte at DS:_profile, while it is on the buffer.
void DrawDiscUpperRow(GameState& _state, std::uint16_t _profile, std::uint16_t _upperRow, bool _backward)
{
  if (High(_upperRow) != 0)
  {
    return;
  }
  const DiscRowEdges edges = DiscRowSpan(_state, DiscHalfWidth(_state, _profile));
  if (edges.visible)
  {
    (void)FillSpan(_state, Low(edges.left), Low(edges.right), Low(_upperRow), _backward);
  }
}

// CS:187B: the rows _lowerRow (moving down) and _upperRow (moving up), as 2*row, one half-width for both; the upper while it
// is on the buffer.
void DrawDiscRowPair(GameState& _state, std::uint16_t _profile, std::uint16_t _lowerRow, std::uint16_t _upperRow, bool _backward)
{
  const DiscRowEdges edges = DiscRowSpan(_state, DiscHalfWidth(_state, _profile));
  if (!edges.visible)
  {
    return;
  }
  (void)FillSpan(_state, Low(edges.left), Low(edges.right), Low(_lowerRow), _backward);
  if (High(_upperRow) == 0)
  {
    (void)FillSpan(_state, Low(edges.left), Low(edges.right), Low(_upperRow), _backward);
  }
}

// Where DrawSmallDisc (CS:194B) puts a disc of radius 0-4: a 4-row sprite from smallDiscSprites, clipped a byte at a
// time at the left and right edges and a row at a time at the top and bottom.
struct SmallDiscPlace
{
  std::uint16_t x;      // the sprite's left x: the centre's less half the radius, 4 on when that is left of the buffer
  std::uint16_t row;    // its top row likewise, 0 once clipped at the top
  std::uint16_t sprite; // its first byte to draw in smallDiscSprites
  std::uint8_t rows;    // the rows to draw, 4 less those off the top or the bottom
  std::uint8_t clip;    // a bit pair a row: bit 0 when only the sprite's right byte is drawn, bit 1 when only its left
  bool onBuffer;        // false when the sprite is wholly off the buffer, and nothing is drawn
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
void DrawSmallDisc(GameState& _state, const SmallDiscPlace& _place)
{
  std::uint16_t at = static_cast<std::uint16_t>(Join(Low(_place.row), Low(_place.x)) >> 2);
  std::uint16_t sprite = _place.sprite;
  std::uint8_t clip = _place.clip;
  // The sprite's shift into its first byte, 2 bits a pixel.
  const auto shift = static_cast<std::uint8_t>((Low(_place.x) & 3) << 1);
  const std::uint8_t fill = _state.Get(DS.discFillByte);
  for (std::uint8_t rows = _place.rows; rows != 0; --rows)
  {
    const auto bits = static_cast<std::uint16_t>(Join(_state.Byte(sprite), 0) >> shift);
    sprite = Offset(sprite, 1);
    const bool rightOnly = (clip & 1) != 0;
    clip = RotateRight(clip, 1);
    if (rightOnly)
    {
      clip = RotateRight(clip, 1);
      Plot(_state, at, static_cast<std::uint8_t>(~Low(bits)), static_cast<std::uint8_t>(Low(bits) & fill));
    }
    else
    {
      const bool leftOnly = (clip & 1) != 0;
      clip = RotateRight(clip, 1);
      if (leftOnly)
      {
        Plot(_state, at, static_cast<std::uint8_t>(~High(bits)), static_cast<std::uint8_t>(High(bits) & fill));
      }
      else
      {
        // Both bytes, as a word, the sprite's left byte at the lower address: AND WORD [DI] with the bits' complement,
        // then OR WORD [DI] with the bits in the fill.
        const std::uint16_t word = SwapBytes(bits);
        const auto kept = static_cast<std::uint16_t>(_state.Word(at) & ~word);
        _state.SetWord(at, kept);
        _state.SetWord(at, static_cast<std::uint16_t>(kept | (word & Join(fill, fill))));
      }
    }
    at = static_cast<std::uint16_t>(at + ROW_BYTES);
  }
}

// ---- Triangles ----

// MOV transfer,CS:[_source] / MOV CS:[_site],transfer: a step instruction rewritten from the opcodes
// kept after the routine's RET. Returns the word moved, which the original leaves in the transfer register.
std::uint16_t PatchStep(GameState& _state, std::uint16_t _source, std::uint16_t _site)
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

// The spans a triangle's walk pushes for DrawStackedSpansFromRow, each DL the left x and DH the right, in the order the original
// pushes them: from the top row down. DrawStackedSpansFromRow pops them, so the last pushed is drawn first, on the bottom row.
using TriangleSpans = std::vector<std::uint16_t>;

// DrawStackedSpansFromRow (CS:1CD5): the spans of _spans popped and each filled (FillTriangleSpan), the first on the row
// _bottomRow and each next a row up, with triangleFillPattern's low byte on even rows and its high on odd ones. It pops CX
// spans, and every walk sets CX to the count it pushed.
void DrawStackedSpansFromRow(GameState& _state, TriangleSpans& _spans, std::uint8_t _bottomRow, bool _backward)
{
  // XOR AL,AL / SHR AX,1 twice: SI, the row's offset, row * 64, with bit 6 of AL the row's lowest bit.
  auto row = static_cast<std::uint16_t>(Join(_bottomRow, 0) >> 2);
  std::uint16_t pattern = _state.Get(DS.triangleFillPattern);
  if ((Low(row) & ODD_ROW_BIT) != 0)
  {
    pattern = SwapBytes(pattern);
  }
  while (!_spans.empty())
  {
    const std::uint16_t span = _spans.back();
    _spans.pop_back();
    FillTriangleSpan(_state, row, Low(span), High(span), Low(pattern), _backward);
    row = static_cast<std::uint16_t>(row - ROW_BYTES);
    pattern = SwapBytes(pattern);
  }
}

// DrawStackedSpans (CS:1CD1): DrawStackedSpansFromRow from triangleBottomRow.
void DrawStackedSpans(GameState& _state, TriangleSpans& _spans, bool _backward)
{
  DrawStackedSpansFromRow(_state, _spans, _state.Get(DS.triangleBottomRow), _backward);
}

// A corner of a triangle wholly on the buffer: x and row, as FillOnScreenTriangle holds A in AL and AH, B in BX and C in CX.
struct OnScreenCorner
{
  std::uint8_t x;
  std::uint8_t row;
};

// An edge of the on-screen walk: x as 8.8, in AX or BX, its slope, in SI or DI, and whether the step patched in at its site is
// SUB rather than ADD.
struct WalkEdge
{
  std::uint16_t x;
  std::uint16_t slope;
  bool subtract;

  // ADD AX,SI or SUB AX,SI, and likewise BX by DI.
  void Step() noexcept
  {
    x = static_cast<std::uint16_t>(subtract ? x - slope : x + slope);
  }
};

// MOV DH,AH / MOV DL,BH / CMP DL,DH / JB / XCHG DH,DL: the span between edges A and B's whole parts, DL the smaller.
[[nodiscard]] std::uint16_t EdgeSpan(const WalkEdge& _a, const WalkEdge& _b) noexcept
{
  std::uint8_t right = High(_a.x);
  std::uint8_t left = High(_b.x);
  if (!(left < right))
  {
    std::swap(left, right);
  }
  return Join(right, left);
}

// XOR AH,AH / CWD / XCHG AH,AL / DIV CX: the 8.8 slope of an edge _pixels across _rows rows. Every caller's rows are at least 1
// and the dividend's high word is 0, so the divide never overflows into the trap.
[[nodiscard]] std::uint16_t EdgeSlope(std::uint8_t _pixels, std::uint8_t _rows) noexcept
{
  return static_cast<std::uint16_t>((std::uint32_t{_pixels} << 8) / _rows);
}

// SUB r8,r8 / JAE / NEG r8, with the patch a borrow makes: _to less _from as a magnitude, and whether the edge steps back.
struct ByteRun
{
  std::uint8_t magnitude;
  bool backward;
};

[[nodiscard]] ByteRun RunBetween(std::uint8_t _from, std::uint8_t _to) noexcept
{
  const auto difference = static_cast<std::uint8_t>(_to - _from);
  return _to < _from ? ByteRun{Negate8(difference), true} : ByteRun{difference, false};
}

// TraceTwoEdges (CS:1CBE): the spans of _rows rows pushed, edges A and B stepped after each by the steps patched in at
// CS:1CC9 and CS:1CCB, then DrawStackedSpans. LOOP counts the rows from CX, the rows + 1.
void TraceTwoEdges(GameState& _state, WalkEdge _a, WalkEdge _b, std::uint8_t _rows, bool _backward)
{
  TriangleSpans spans;
  std::uint16_t rows = _rows;
  do
  {
    spans.push_back(EdgeSpan(_a, _b));
    _a.Step();
    _b.Step();
  } while (--rows != 0);
  DrawStackedSpans(_state, spans, _backward);
}

// FillFlatBottomTriangle (CS:1C62): A on top, B and C on the bottom row; edge A from A to C, edge B from A to B.
void FillFlatBottomTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c, bool _backward)
{
  _state.Set(DS.triangleEdgeStartX, _a.x);
  _state.Set(DS.triangleEdgeStartX2, _a.x);
  _state.Set(DS.triangleBottomRow, _b.row);
  const auto rows = static_cast<std::uint8_t>(_c.row - _a.row);
  _state.Set(DS.triangleUpperRows, rows);
  const ByteRun runA = RunBetween(_a.x, _c.x);
  if (runA.backward)
  {
    (void)PatchStep(_state, SUB_AX_SI, STEP_EDGE_A);
  }
  WalkEdge a{0, EdgeSlope(runA.magnitude, rows), runA.backward};
  // MOV BH,AL / MOV AX,BX / SUB AL,AH: B's x less A's.
  const ByteRun runB = RunBetween(_a.x, _b.x);
  if (runB.backward)
  {
    (void)PatchStep(_state, SUB_BX_DI, STEP_EDGE_B);
  }
  WalkEdge b{0, EdgeSlope(runB.magnitude, rows), runB.backward};
  a.x = Join(_state.Get(DS.triangleEdgeStartX), 0);
  b.x = Join(_state.Get(DS.triangleEdgeStartX2), 0);
  TraceTwoEdges(_state, a, b, static_cast<std::uint8_t>(rows + 1), _backward);
}

// FillFlatTopTriangle (CS:1CFB): B and C on the top row, A at the bottom; edge A from C to A, edge B from B to A.
void FillFlatTopTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c, bool _backward)
{
  _state.Set(DS.triangleEdgeStartX, _b.x);
  _state.Set(DS.triangleEdgeStartX2, _c.x);
  _state.Set(DS.triangleBottomRow, _a.row);
  _state.Set(DS.triangleUpperRows, static_cast<std::uint8_t>(_a.row - _c.row));
  // MOV BH,AL / SUB AL,CL: A's x less C's.
  const ByteRun runA = RunBetween(_c.x, _a.x);
  if (runA.backward)
  {
    (void)PatchStep(_state, SUB_AX_SI, STEP_EDGE_A);
  }
  const std::uint8_t rows = _state.Get(DS.triangleUpperRows);
  WalkEdge a{0, EdgeSlope(runA.magnitude, rows), runA.backward};
  // MOV AX,BX / SUB AH,AL: A's x less B's, then XOR AL,AL / XOR DX,DX / DIV CX.
  const ByteRun runB = RunBetween(_b.x, _a.x);
  if (runB.backward)
  {
    (void)PatchStep(_state, SUB_BX_DI, STEP_EDGE_B);
  }
  WalkEdge b{0, EdgeSlope(runB.magnitude, rows), runB.backward};
  a.x = Join(_state.Get(DS.triangleEdgeStartX2), 0);
  b.x = Join(_state.Get(DS.triangleEdgeStartX), 0);
  TraceTwoEdges(_state, a, b, static_cast<std::uint8_t>(rows + 1), _backward);
}

// FillOneRowTriangle (CS:1D5B): all three on one row, one span from the least x to the greatest, the x sorted by exchanges.
void FillOneRowTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c, bool _backward)
{
  _state.Set(DS.triangleBottomRow, _a.row);
  std::uint8_t least = _a.x;
  std::uint8_t middle = _b.x;
  std::uint8_t greatest = _c.x;
  if (!(least < middle))
  {
    std::swap(least, middle);
  }
  if (!(least < greatest))
  {
    std::swap(least, greatest);
  }
  if (!(middle < greatest))
  {
    std::swap(middle, greatest);
  }
  // MOV DL,CL / MOV DH,AL / CMP DL,DH / JB / XCHG DH,DL.
  std::uint8_t left = greatest;
  std::uint8_t right = least;
  if (!(left < right))
  {
    std::swap(left, right);
  }
  TriangleSpans spans{Join(right, left)};
  DrawStackedSpans(_state, spans, _backward);
}

// FillGeneralTriangle (CS:1D82): A on top, B the middle row, C the bottom; the long edge A-C in AX by SI, the short edges A-B and
// then B-C in BX by DI, each step patched in at the upper and the lower walk's sites.
void FillGeneralTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c, bool _backward)
{
  const std::uint16_t addLong = PatchStep(_state, ADD_AX_SI, STEP_LONG_EDGE_UPPER);
  _state.SetCodeWord(STEP_LONG_EDGE_LOWER, addLong);
  const std::uint16_t addShort = PatchStep(_state, ADD_BX_DI, STEP_SHORT_EDGE_UPPER);
  _state.SetCodeWord(STEP_SHORT_EDGE_LOWER, addShort);
  if (!(_b.row < _c.row))
  {
    std::swap(_b, _c);
  }
  _state.Set(DS.triangleBottomRow, _c.row);
  _state.Set(DS.triangleMiddleX, _b.x);
  _state.Set(DS.triangleBottomX, _c.x);
  _state.Set(DS.triangleUpperRows, static_cast<std::uint8_t>(_b.row - _a.row));
  _state.Set(DS.triangleTotalRows, static_cast<std::uint8_t>(_c.row - _a.row));
  _state.Set(DS.triangleEdgeStartX, _a.x);
  _state.Set(DS.triangleEdgeStartX2, _a.x);
  const ByteRun runLong = RunBetween(_a.x, _c.x);
  if (runLong.backward)
  {
    const std::uint16_t subtractLong = PatchStep(_state, SUB_AX_SI, STEP_LONG_EDGE_UPPER);
    _state.SetCodeWord(STEP_LONG_EDGE_LOWER, subtractLong);
  }
  WalkEdge longEdge{0, EdgeSlope(runLong.magnitude, _state.Get(DS.triangleTotalRows)), runLong.backward};
  // MOV BH,AL / MOV AX,BX / SUB AL,AH: B's x less A's.
  const ByteRun runUpper = RunBetween(_a.x, _b.x);
  if (runUpper.backward)
  {
    (void)PatchStep(_state, SUB_BX_DI, STEP_SHORT_EDGE_UPPER);
  }
  const std::uint8_t upperRows = _state.Get(DS.triangleUpperRows);
  WalkEdge shortEdge{0, EdgeSlope(runUpper.magnitude, upperRows), runUpper.backward};
  longEdge.x = Join(_state.Get(DS.triangleEdgeStartX), 0);
  shortEdge.x = longEdge.x;
  // TraceUpperEdges (CS:1E15): rows A to B inclusive, each span pushed before the edges step.
  TriangleSpans spans;
  std::uint16_t rows = static_cast<std::uint8_t>(upperRows + 1);
  do
  {
    spans.push_back(EdgeSpan(longEdge, shortEdge));
    longEdge.Step();
    shortEdge.Step();
  } while (--rows != 0);
  // StartLowerShortEdge (CS:1E26): the rows below B, the short edge restarting from B's x. PUSH AX / POP AX round its slope keep
  // the long edge.
  rows = static_cast<std::uint8_t>(_state.Get(DS.triangleTotalRows) - _state.Get(DS.triangleUpperRows));
  const ByteRun runLower = RunBetween(_state.Get(DS.triangleMiddleX), _state.Get(DS.triangleBottomX));
  if (runLower.backward)
  {
    (void)PatchStep(_state, SUB_BX_DI, STEP_SHORT_EDGE_LOWER);
  }
  shortEdge = WalkEdge{Join(_state.Get(DS.triangleMiddleX), 0), EdgeSlope(runLower.magnitude, Low(rows)), runLower.backward};
  // TraceLowerEdges (CS:1E54): the short edge steps before each span is pushed, the long edge after.
  do
  {
    shortEdge.Step();
    spans.push_back(EdgeSpan(longEdge, shortEdge));
    longEdge.Step();
  } while (--rows != 0);
  DrawStackedSpans(_state, spans, _backward);
}

// FillOnScreenTriangle (CS:1C12): A, B and C wholly on the buffer, sorted by row into the four cases by exchanges, with edge A's
// and edge B's steps patched to their ADD forms first. MOV DX,DS / MOV ES,DX before them.
void FillOnScreenTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c, bool _backward)
{
  (void)PatchStep(_state, ADD_AX_SI, STEP_EDGE_A);
  (void)PatchStep(_state, ADD_BX_DI, STEP_EDGE_B);
  if (_a.row == _b.row)
  {
    // CMP AH,CH / XCHG CX,AX, then JE or JAE on the compare.
    const std::uint8_t topRow = _a.row;
    std::swap(_a, _c);
    if (topRow == _a.row)
    {
      FillOneRowTriangle(_state, _a, _b, _c, _backward);
    }
    else if (topRow > _a.row)
    {
      FillFlatBottomTriangle(_state, _a, _b, _c, _backward);
    }
    else
    {
      FillFlatTopTriangle(_state, _a, _b, _c, _backward);
    }
    return;
  }
  if (_a.row > _b.row)
  {
    std::swap(_a, _b);
  }
  if (_a.row == _c.row)
  {
    std::swap(_a, _b);
    FillFlatTopTriangle(_state, _a, _b, _c, _backward);
    return;
  }
  if (_a.row > _c.row)
  {
    std::swap(_a, _c);
  }
  if (_b.row == _c.row)
  {
    FillFlatBottomTriangle(_state, _a, _b, _c, _backward);
  }
  else
  {
    FillGeneralTriangle(_state, _a, _b, _c, _backward);
  }
}

// ---- Clipped triangles ----

// The step pair at _site (ADD/ADC or SUB/SBB) rewritten from the pair at _source, a word at the site and a word 4 bytes on.
// Returns the second word moved, which the original leaves in the transfer register.
std::uint16_t PatchClippedStep(GameState& _state, std::uint16_t _source, std::uint16_t _site)
{
  (void)PatchStep(_state, _source, _site);
  return PatchStep(_state, static_cast<std::uint16_t>(_source + CARRY_STEP_BYTES), static_cast<std::uint16_t>(_site + CARRY_STEP_BYTES));
}

// A 16.16 edge of the clipped walk: x's whole part, signed, and its fraction: edge A in AX and SI, edge B in BX and DI.
struct ClippedEdge
{
  std::int16_t whole;
  std::uint16_t fraction;
};

// ADD fraction,[m] / ADC whole,[m], or SUB / SBB: _edge stepped by the slope at _wholeSlope and _fractionSlope.
[[nodiscard]] ClippedEdge StepClippedEdge(const GameState& _state, ClippedEdge _edge, DataField<std::uint16_t> _wholeSlope,
                                          DataField<std::uint16_t> _fractionSlope, bool _subtract)
{
  const std::uint16_t slopeFraction = _state.Get(_fractionSlope);
  const std::uint16_t slopeWhole = _state.Get(_wholeSlope);
  auto whole = static_cast<std::uint16_t>(_edge.whole);
  if (_subtract)
  {
    const bool borrow = _edge.fraction < slopeFraction;
    _edge.fraction = static_cast<std::uint16_t>(_edge.fraction - slopeFraction);
    whole = static_cast<std::uint16_t>(whole - slopeWhole - (borrow ? 1 : 0));
  }
  else
  {
    const std::uint32_t sum = std::uint32_t{_edge.fraction} + slopeFraction;
    _edge.fraction = static_cast<std::uint16_t>(sum);
    whole = static_cast<std::uint16_t>(whole + slopeWhole + (sum >> 16));
  }
  return ClippedEdge{Signed(whole), _edge.fraction};
}

// Edge A stepped by clippedSlopeA.
[[nodiscard]] ClippedEdge StepClippedEdgeA(const GameState& _state, ClippedEdge _edge, bool _subtract)
{
  return StepClippedEdge(_state, _edge, DS.clippedSlopeAWhole, DS.clippedSlopeAFraction, _subtract);
}

// Edge B stepped by clippedSlopeB.
[[nodiscard]] ClippedEdge StepClippedEdgeB(const GameState& _state, ClippedEdge _edge, bool _subtract)
{
  return StepClippedEdge(_state, _edge, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction, _subtract);
}

// XOR DX,DX / DIV _rows / store / XOR AX,AX / DIV _rows / store: _pixels over _rows rows as 16.16, the whole part stored first.
// Every caller's rows are at least 1, and the first divide's high word is 0 and the second's the remainder, so neither
// overflows into the trap.
void ClippedSlope(GameState& _state, std::uint16_t _pixels, std::uint16_t _rows, DataField<std::uint16_t> _whole,
                  DataField<std::uint16_t> _fraction)
{
  _state.Set(_whole, static_cast<std::uint16_t>(_pixels / _rows));
  _state.Set(_fraction, static_cast<std::uint16_t>((std::uint32_t{static_cast<std::uint16_t>(_pixels % _rows)} << 16) / _rows));
}

// SUB r16,r16 / JGE or JG / NEG r16, with the patch a step back makes: _to less _from as a magnitude, and whether the edge steps
// back: JGE steps back only below, JG at or below.
struct WordRun
{
  std::uint16_t magnitude;
  bool backward;
};

[[nodiscard]] WordRun RunBetween(std::int16_t _from, std::int16_t _to, bool _backAtZero) noexcept
{
  const auto difference = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_to) - static_cast<std::uint16_t>(_from));
  const bool backward = _backAtZero ? !(_to > _from) : _to < _from;
  return WordRun{backward ? Negate(difference) : difference, backward};
}

// Whether row _row, between edges _a and _b, shows (CS:1F78): CMP BP,80h / JAE, TEST BH,AH / JS, AND AH,AH / JLE, AND BH,BH / JG:
// 0-127, and not both edges left of 0 or both right of 255.
[[nodiscard]] bool ClippedRowVisible(std::uint16_t _row, ClippedEdge _a, ClippedEdge _b) noexcept
{
  if (_row >= BUFFER_ROWS)
  {
    return false;
  }
  const std::uint8_t highA = High(static_cast<std::uint16_t>(_a.whole));
  const std::uint8_t highB = High(static_cast<std::uint16_t>(_b.whole));
  if (Negative8(static_cast<std::uint8_t>(highB & highA)))
  {
    return false;
  }
  return !(Signed8(highA) > 0 && Signed8(highB) > 0);
}

// PushClippedSpan (CS:1F96): a shown row's span, held to 0-255, DL the left x and DH the right.
[[nodiscard]] std::uint16_t ClippedSpan(ClippedEdge _a, ClippedEdge _b) noexcept
{
  const auto a = static_cast<std::uint16_t>(_a.whole);
  const auto b = static_cast<std::uint16_t>(_b.whole);
  const bool aFirst = _a.whole <= _b.whole;
  const std::uint16_t left = aFirst ? a : b;
  const std::uint16_t right = aFirst ? b : a;
  return Join(High(right) != 0 ? std::uint8_t{0xFF} : Low(right), High(left) != 0 ? std::uint8_t{0} : Low(left));
}

// A row of the clipped walk: when it shows, its span pushed, the first such row noted in clippedFirstRow, and, _counted,
// clippedRowCount stepped. False when the walk is over: a hidden row after a shown one.
[[nodiscard]] bool TakeClippedRow(GameState& _state, TriangleSpans& _spans, std::uint16_t _row, ClippedEdge _a, ClippedEdge _b,
                                  bool _counted)
{
  if (!ClippedRowVisible(_row, _a, _b))
  {
    return _state.Get(DS.clippedFirstRow) == NO_ROW;
  }
  if (_state.Get(DS.clippedFirstRow) == NO_ROW)
  {
    _state.Set(DS.clippedFirstRow, _row);
  }
  _spans.push_back(ClippedSpan(_a, _b));
  if (_counted)
  {
    _state.Set(DS.clippedRowCount, static_cast<std::uint8_t>(_state.Get(DS.clippedRowCount) + 1));
  }
  return true;
}

// TraceClippedFlatEdges (CS:1F6C) and FinishClippedFlat (CS:1FDB): _rows rows from _row (LOOP from CX), edge A from _a and B
// from _b, stepped by the steps patched in at CS:1FC8 and CS:1FD0; then, if any row showed, clippedLastRow and
// DrawStackedSpansFromRow from it.
void TraceClippedFlatEdges(GameState& _state, ClippedEdge _a, bool _subtractA, ClippedEdge _b, bool _subtractB, std::uint16_t _row,
                           std::uint16_t _rows, bool _backward)
{
  _state.Set(DS.clippedLastRow, NO_ROW);
  _state.Set(DS.clippedFirstRow, NO_ROW);
  TriangleSpans spans;
  std::uint16_t row = _row;
  std::uint16_t rows = _rows;
  do
  {
    if (!TakeClippedRow(_state, spans, row, _a, _b, false))
    {
      break;
    }
    _a = StepClippedEdgeA(_state, _a, _subtractA);
    _b = StepClippedEdgeB(_state, _b, _subtractB);
    ++row;
  } while (--rows != 0);
  if (_state.Get(DS.clippedFirstRow) == NO_ROW)
  {
    return;
  }
  --row;
  _state.Set(DS.clippedLastRow, row);
  DrawStackedSpansFromRow(_state, spans, Low(row), _backward);
}

// FillClippedFlatBottom (CS:1EFD): A on top, B and C on the bottom row, signed words; edge A from A to C, edge B from A to B.
void FillClippedFlatBottom(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c, bool _backward)
{
  _state.Set(DS.clippedEdgeStartX, static_cast<std::uint16_t>(_a.x));
  _state.Set(DS.clippedEdgeStartX2, static_cast<std::uint16_t>(_a.x));
  const auto rows = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_c.y) - static_cast<std::uint16_t>(_a.y));
  _state.Set(DS.clippedUpperRows, rows);
  const WordRun runA = RunBetween(_a.x, _c.x, false);
  if (runA.backward)
  {
    (void)PatchClippedStep(_state, SUBTRACT_FROM_EDGE_A, STEP_CLIPPED_EDGE_A);
  }
  ClippedSlope(_state, runA.magnitude, rows, DS.clippedSlopeAWhole, DS.clippedSlopeAFraction);
  const WordRun runB = RunBetween(_a.x, _b.x, false);
  if (runB.backward)
  {
    (void)PatchClippedStep(_state, SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_EDGE_B);
  }
  ClippedSlope(_state, runB.magnitude, rows, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
  const ClippedEdge a{Signed(_state.Get(DS.clippedEdgeStartX)), 0};
  const ClippedEdge b{Signed(_state.Get(DS.clippedEdgeStartX2)), 0};
  TraceClippedFlatEdges(_state, a, runA.backward, b, runB.backward, static_cast<std::uint16_t>(_a.y), static_cast<std::uint16_t>(rows + 1),
                        _backward);
}

// FillClippedFlatTop (CS:2018): B and C on the top row, A at the bottom; edge A from C to A, edge B from B to A. PUSH AX / POP AX
// round edge A's slope keep A's x.
void FillClippedFlatTop(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c, bool _backward)
{
  _state.Set(DS.clippedEdgeStartX, static_cast<std::uint16_t>(_b.x));
  _state.Set(DS.clippedEdgeStartX2, static_cast<std::uint16_t>(_c.x));
  const auto rows = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_a.y) - static_cast<std::uint16_t>(_c.y));
  _state.Set(DS.clippedUpperRows, rows);
  const WordRun runA = RunBetween(_c.x, _a.x, false);
  if (runA.backward)
  {
    (void)PatchClippedStep(_state, SUBTRACT_FROM_EDGE_A, STEP_CLIPPED_EDGE_A);
  }
  ClippedSlope(_state, runA.magnitude, rows, DS.clippedSlopeAWhole, DS.clippedSlopeAFraction);
  const WordRun runB = RunBetween(_b.x, _a.x, false);
  if (runB.backward)
  {
    (void)PatchClippedStep(_state, SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_EDGE_B);
  }
  ClippedSlope(_state, runB.magnitude, rows, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
  const ClippedEdge a{Signed(_state.Get(DS.clippedEdgeStartX2)), 0};
  const ClippedEdge b{Signed(_state.Get(DS.clippedEdgeStartX)), 0};
  // The walk starts from BP, B's row, the top.
  TraceClippedFlatEdges(_state, a, runA.backward, b, runB.backward, static_cast<std::uint16_t>(_b.y), static_cast<std::uint16_t>(rows + 1),
                        _backward);
}

// FillClippedOneRow (CS:208B): all three on row _a's, its low byte triangleBottomRow; the x sorted by signed exchanges, and one
// span from the least to the greatest, held to 0-255, unless all lie left of 0 or right of 255.
void FillClippedOneRow(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c, bool _backward)
{
  _state.Set(DS.triangleBottomRow, Low(static_cast<std::uint16_t>(_a.y)));
  std::int16_t least = _a.x;
  std::int16_t middle = _b.x;
  std::int16_t greatest = _c.x;
  if (least > middle)
  {
    std::swap(least, middle);
  }
  if (least > greatest)
  {
    std::swap(least, greatest);
  }
  if (middle > greatest)
  {
    std::swap(middle, greatest);
  }
  if (greatest < 0 || least >= 0x100)
  {
    return;
  }
  const std::uint8_t left = least < 0 ? std::uint8_t{0} : Low(static_cast<std::uint16_t>(least));
  const std::uint8_t right = greatest >= 0x100 ? std::uint8_t{0xFF} : Low(static_cast<std::uint16_t>(greatest));
  TriangleSpans spans{Join(right, left)};
  DrawStackedSpans(_state, spans, _backward);
}

// FillClippedGeneral (CS:20C2): A on top, B the middle row, C the bottom; the long edge A-C in AX:SI, the short edges in BX:DI,
// every row from A's walked and counted in clippedRowCount, each step patched in at the upper and the lower walk's sites.
void FillClippedGeneral(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c, bool _backward)
{
  // The ADD/ADC forms through SI, to both walks' sites.
  for (const std::uint16_t word : {std::uint16_t{0}, CARRY_STEP_BYTES})
  {
    const std::uint16_t addA = PatchStep(_state, Offset(ADD_TO_EDGE_A, word), Offset(STEP_CLIPPED_UPPER_A, word));
    _state.SetCodeWord(Offset(STEP_CLIPPED_LOWER_A, word), addA);
  }
  for (const std::uint16_t word : {std::uint16_t{0}, CARRY_STEP_BYTES})
  {
    const std::uint16_t addB = PatchStep(_state, Offset(ADD_TO_EDGE_B, word), Offset(STEP_CLIPPED_UPPER_B, word));
    _state.SetCodeWord(Offset(STEP_CLIPPED_LOWER_B, word), addB);
  }
  if (!(_b.y < _c.y))
  {
    std::swap(_b, _c);
  }
  // PUSH DX / POP BP keep the top row for the walk.
  const auto topRow = static_cast<std::uint16_t>(_a.y);
  _state.Set(DS.clippedMiddleX, static_cast<std::uint16_t>(_b.x));
  _state.Set(DS.clippedBottomX, static_cast<std::uint16_t>(_c.x));
  _state.Set(DS.clippedUpperRows, static_cast<std::uint16_t>(static_cast<std::uint16_t>(_b.y) - topRow));
  _state.Set(DS.clippedTotalRows, static_cast<std::uint16_t>(static_cast<std::uint16_t>(_c.y) - topRow));
  _state.Set(DS.clippedEdgeStartX, static_cast<std::uint16_t>(_a.x));
  _state.Set(DS.clippedEdgeStartX2, static_cast<std::uint16_t>(_a.x));
  const WordRun runLong = RunBetween(_a.x, _c.x, true);
  if (runLong.backward)
  {
    for (const std::uint16_t word : {std::uint16_t{0}, CARRY_STEP_BYTES})
    {
      const std::uint16_t subtractA = PatchStep(_state, Offset(SUBTRACT_FROM_EDGE_A, word), Offset(STEP_CLIPPED_UPPER_A, word));
      _state.SetCodeWord(Offset(STEP_CLIPPED_LOWER_A, word), subtractA);
    }
  }
  // PUSH AX / POP AX round the long edge's slope keep A's x.
  ClippedSlope(_state, runLong.magnitude, _state.Get(DS.clippedTotalRows), DS.clippedSlopeAWhole, DS.clippedSlopeAFraction);
  const WordRun runUpper = RunBetween(_a.x, _b.x, true);
  if (runUpper.backward)
  {
    (void)PatchClippedStep(_state, SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_UPPER_B);
  }
  const std::uint16_t upperRows = _state.Get(DS.clippedUpperRows);
  ClippedSlope(_state, runUpper.magnitude, upperRows, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
  ClippedEdge longEdge{Signed(_state.Get(DS.clippedEdgeStartX)), 0};
  ClippedEdge shortEdge = longEdge;
  std::uint16_t row = topRow;
  _state.Set(DS.clippedLastRow, NO_ROW);
  _state.Set(DS.clippedFirstRow, NO_ROW);
  _state.Set(DS.clippedRowCount, 0);
  TriangleSpans spans;
  // ClippedUpperRowLoop (CS:21A5): rows A to B inclusive.
  bool walking = true;
  std::uint16_t rows = static_cast<std::uint16_t>(upperRows + 1);
  do
  {
    walking = TakeClippedRow(_state, spans, row, longEdge, shortEdge, true);
    if (!walking)
    {
      break;
    }
    longEdge = StepClippedEdgeA(_state, longEdge, runLong.backward);
    shortEdge = StepClippedEdgeB(_state, shortEdge, runUpper.backward);
    ++row;
  } while (--rows != 0);
  if (walking)
  {
    // StartClippedLowerEdge (CS:2213): the short edge restarts from B's x, its fraction carrying on. The SUB/SBB form goes to its
    // sites through SI, which leaves the long edge's fraction the second word moved. PUSH AX / POP AX keep the long edge's x.
    rows = static_cast<std::uint16_t>(_state.Get(DS.clippedTotalRows) - _state.Get(DS.clippedUpperRows));
    const WordRun runLower = RunBetween(Signed(_state.Get(DS.clippedMiddleX)), Signed(_state.Get(DS.clippedBottomX)), true);
    if (runLower.backward)
    {
      longEdge.fraction = PatchClippedStep(_state, SUBTRACT_FROM_EDGE_B, STEP_CLIPPED_LOWER_B);
    }
    ClippedSlope(_state, runLower.magnitude, rows, DS.clippedSlopeBWhole, DS.clippedSlopeBFraction);
    shortEdge.whole = Signed(_state.Get(DS.clippedMiddleX));
    // ClippedLowerRowLoop (CS:224E): the short edge steps before the row is taken, the long edge after.
    do
    {
      shortEdge = StepClippedEdgeB(_state, shortEdge, runLower.backward);
      if (!TakeClippedRow(_state, spans, row, longEdge, shortEdge, true))
      {
        break;
      }
      longEdge = StepClippedEdgeA(_state, longEdge, runLong.backward);
      ++row;
    } while (--rows != 0);
  }
  // FinishClippedGeneral (CS:22B9). The guard on the last row never holds: a row is pushed only below 128.
  if (_state.Get(DS.clippedRowCount) == 0)
  {
    return;
  }
  --row;
  _state.Set(DS.clippedLastRow, row);
  if (row >= BUFFER_ROWS)
  {
    return;
  }
  DrawStackedSpansFromRow(_state, spans, Low(row), _backward);
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

void FinishSpaceViewFrame(GameState& _state, Hardware& _hardware)
{
  // MOV AX,B800h / MOV ES,AX, the sights, then XOR AL,AL / CLD: the copy and the clear run forwards.
  DrawLaserSights(_state);
  PresentSpaceView(_state, _hardware, false);
  ClearDrawBuffer(_state, false);
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

bool DrawClippedLine(GameState& _state, std::uint16_t _fromX, std::uint16_t _fromRow, std::uint16_t _toX, std::uint16_t _toRow)
{
  // ADD BX,BX / ADD AX,AX: the rows doubled, so that 0-127 spans 0-255 as x does. The line is held as its ends (CX, AX), the to
  // end, and (DX, BX), the from end, x the coordinate cut first; MOV SI,AX keeps the doubled row while BP counts the ends with a
  // coordinate outside 0-255.
  ClipLine line{_toX, static_cast<std::uint16_t>(_toRow << 1), _fromX, static_cast<std::uint16_t>(_fromRow << 1), 0};
  const bool firstOutside = (High(line.firstAlong) | High(line.firstCut)) != 0;
  const bool secondOutside = (High(line.secondAlong) | High(line.secondCut)) != 0;
  line.outside = static_cast<std::uint16_t>((firstOutside ? 1 : 0) + (secondOutside ? 1 : 0));
  // CS:1623: DrawLine on the ends as they stand.
  const auto draw = [&_state](const ClipLine& _ends)
  { return DrawHalvedRows(_state, Low(_ends.secondCut), Low(_ends.secondAlong), Low(_ends.firstCut), Low(_ends.firstAlong)); };
  if (line.outside == 0)
  {
    return draw(line);
  }
  // x below 0, then the row below 0, each through ClipLineToLowEdge.
  ClipStep step = ClipLineToLowEdge(_state, line);
  if (step.allInside)
  {
    return draw(step.line);
  }
  if (!step.draw)
  {
    return false;
  }
  step = ClipLineToLowEdge(_state, Transpose(step.line));
  line = Transpose(step.line);
  if (step.allInside)
  {
    return draw(line);
  }
  if (!step.draw)
  {
    return false;
  }
  // x beyond 255, then the row, each through ClipLineToHighEdge with 0FFh taken off the cut coordinates and put back.
  constexpr std::uint16_t HIGH_EDGE = 0xFF;
  step = ClipLineToHighEdge(_state, ShiftCut(line, Negate(HIGH_EDGE)));
  if (!step.draw)
  {
    return false;
  }
  line = ShiftCut(step.line, HIGH_EDGE);
  if (step.allInside)
  {
    return draw(line);
  }
  step = ClipLineToHighEdge(_state, ShiftCut(Transpose(line), Negate(HIGH_EDGE)));
  if (!step.draw || !step.allInside)
  {
    return false;
  }
  return draw(Transpose(ShiftCut(step.line, HIGH_EDGE)));
}

ClipStep ClipLineToLowEdge(GameState& _state, ClipLine _line)
{
  // AND CH,CH / JS, then AND DH,DH / JNS.
  if (Negative(_line.firstCut))
  {
    if (!Negative(_line.secondCut))
    {
      return MoveEndpointToEdge(_state, _line);
    }
    // Both below the edge: CLC, with ZF clear from AND DH,DH.
    return ClipStep{_line, false, false};
  }
  if (!Negative(_line.secondCut))
  {
    return LeaveEndpoints(_line);
  }
  return SwapThenCutLine(_state, _line);
}

ClipStep ClipLineToHighEdge(GameState& _state, ClipLine _line)
{
  // AND DH,DH / JS, then AND CH,CH / JNS or JS.
  if (Negative(_line.secondCut))
  {
    if (!Negative(_line.firstCut))
    {
      return MoveEndpointToEdge(_state, _line);
    }
    // Both inside: STC, with ZF clear from AND CH,CH.
    return ClipStep{_line, true, false};
  }
  if (Negative(_line.firstCut))
  {
    return SwapThenCutLine(_state, _line);
  }
  // Both beyond 255: CLC, with ZF from AND CH,CH.
  return ClipStep{_line, false, High(_line.firstCut) == 0};
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

void DrawDisc(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow, bool _backward)
{
  _state.Set(DS.discFillByte, _state.Byte(DS.colorFillBytes.At(_state.Get(DS.drawColor) & 3u)));
  if (_radius < SMALL_DISC_RADIUS)
  {
    const SmallDiscPlace place = PlaceSmallDisc(Low(_radius), Signed(_centerX), Signed(_centerRow));
    if (place.onBuffer)
    {
      DrawSmallDisc(_state, place);
    }
    return;
  }
  const auto rows = static_cast<std::uint16_t>(_radius >> 1);
  _state.Set(DS.discRadiusRows, Low(rows));
  _state.Set(DS.discRowsLeft, Low(rows));
  _state.Set(DS.discCenterX, _centerX);
  // The step through circleProfile a row, 20000h/rows as 8.8 (FFFFh for 2 rows, which would overflow), by XCHG AH,AL: the
  // fraction's low byte keeps the quotient's high byte, and the whole part is that byte alone.
  constexpr std::uint32_t PROFILE_BYTES_8_8 = 0x20000;
  const std::uint16_t perRow = rows == 2 ? std::uint16_t{0xFFFF} : DivideWord(_state, PROFILE_BYTES_8_8, rows, rows).quotient;
  const std::uint16_t step = SwapBytes(perRow);
  _state.Set(DS.discProfileStepFraction, step);
  _state.Set(DS.discProfileStepWhole, Low(step));
  // CX walks down from the centre row and BX up, both as 2*row; the centre row is drawn once. BP holds the fraction and SI the
  // profile byte.
  std::uint16_t fraction = 0;
  std::uint16_t profile = DS.circleProfile.offset;
  auto lowerRow = static_cast<std::uint16_t>(_centerRow << 1);
  std::uint16_t upperRow = lowerRow;
  DrawDiscUpperRow(_state, profile, upperRow, _backward);
  for (;;)
  {
    const std::uint32_t sum = std::uint32_t{fraction} + _state.Get(DS.discProfileStepFraction);
    fraction = static_cast<std::uint16_t>(sum);
    profile = static_cast<std::uint16_t>(profile + _state.Get(DS.discProfileStepWhole) + (sum >> 16));
    upperRow = static_cast<std::uint16_t>(upperRow - 2);
    lowerRow = static_cast<std::uint16_t>(lowerRow + 2);
    const auto rowsLeft = static_cast<std::uint8_t>(_state.Get(DS.discRowsLeft) - 1);
    _state.Set(DS.discRowsLeft, rowsLeft);
    if (rowsLeft == 0)
    {
      return;
    }
    if (High(lowerRow) != 0)
    {
      DrawDiscUpperRow(_state, profile, upperRow, _backward);
    }
    else
    {
      DrawDiscRowPair(_state, profile, lowerRow, upperRow, _backward);
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

bool DrawCircle(GameState& _state, std::uint8_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow)
{
  // PUSH CX / PUSH DX keep the centre. The first octant, circleOctant's eight bytes scaled by the radius / 128 (MUL BL / SHL AX,1 /
  // MOV AL,AH), a byte of 0 staying 0, as four points of words.
  std::uint16_t point = DS.circlePoints.offset;
  for (std::uint16_t octantByte = 0; octantByte != 8; ++octantByte)
  {
    const std::uint8_t unit = _state.Byte(Offset(DS.circleOctant.offset, octantByte));
    const std::uint8_t scaled = unit == 0 ? std::uint8_t{0} : High(static_cast<std::uint16_t>((unit * _radius) << 1));
    _state.SetWord(point, scaled);
    point = Offset(point, 2);
  }
  // The second octant mirrors the first across the diagonal, from its last point back.
  std::uint16_t from = DS.circlePoints.offset;
  std::uint16_t to = DS.circlePoints.At(7);
  for (std::uint16_t points = 4; points != 0; --points)
  {
    _state.SetWord(Offset(to, 2), _state.Word(from));
    _state.SetWord(to, _state.Word(Offset(from, 2)));
    from = Offset(from, 4);
    to = static_cast<std::uint16_t>(to - 4);
  }
  // The next quarter turns the first by 90 degrees, and the half after that by 180.
  from = DS.circlePoints.offset;
  to = DS.circlePoints.At(8);
  for (std::uint16_t points = 8; points != 0; --points)
  {
    _state.SetWord(Offset(to, 2), _state.Word(from));
    _state.SetWord(to, Negate(_state.Word(Offset(from, 2))));
    from = Offset(from, 4);
    to = Offset(to, 4);
  }
  from = DS.circlePoints.offset;
  to = DS.circlePoints.At(16);
  for (std::uint16_t points = 16; points != 0; --points)
  {
    _state.SetWord(to, Negate(_state.Word(from)));
    _state.SetWord(Offset(to, 2), Negate(_state.Word(Offset(from, 2))));
    from = Offset(from, 4);
    to = Offset(to, 4);
  }
  // POP BX / POP AX: every point moved to the centre.
  point = DS.circlePoints.offset;
  for (std::uint16_t points = CIRCLE_CHORDS; points != 0; --points)
  {
    _state.SetWord(point, Offset(_state.Word(point), _centerX));
    _state.SetWord(Offset(point, 2), Offset(_state.Word(Offset(point, 2)), _centerRow));
    point = Offset(point, 4);
  }
  // The chord from the last point to the first, then each to the next, x and row each.
  const std::uint16_t last = DS.circlePoints.At(CIRCLE_CHORDS - 1);
  bool filled =
    DrawClippedLine(_state, _state.Word(last), _state.Word(Offset(last, 2)), _state.Word(DS.circlePoints.offset), _state.Get(DS.data2EFE));
  point = DS.circlePoints.offset;
  for (std::uint16_t chords = CIRCLE_CHORDS - 1; chords != 0; --chords)
  {
    const bool chordFilled = DrawClippedLine(_state, _state.Word(Offset(point, 4)), _state.Word(Offset(point, 6)), _state.Word(point),
                                             _state.Word(Offset(point, 2)));
    filled = filled || chordFilled;
    point = Offset(point, 4);
  }
  return filled;
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

bool FillTriangle(GameState& _state, Triangle _triangle, bool _backward)
{
  // SHL DX,1 / SHL BP,1 / SHL DI,1: the rows doubled, so that a row on the buffer, 0-127, has a high byte of 0 as an x does.
  const auto doubled = [](ScreenPoint _point)
  { return ScreenPoint{_point.x, static_cast<std::int16_t>(static_cast<std::uint16_t>(_point.y) << 1)}; };
  const Triangle rowsDoubled{doubled(_triangle.first), doubled(_triangle.second), doubled(_triangle.third)};
  const auto word = [](std::int16_t _value) { return static_cast<std::uint16_t>(_value); };
  // MOV SI,AX / OR AX,BP / OR AX,DI / OR AH,CH / OR AH,BH / OR AH,DH: a high byte set in any of them is off the buffer.
  const auto any = static_cast<std::uint16_t>(word(rowsDoubled.first.x) | word(rowsDoubled.second.y) | word(rowsDoubled.third.y));
  const auto high = static_cast<std::uint8_t>(High(any) | High(word(rowsDoubled.third.x)) | High(word(rowsDoubled.second.x)) |
                                              High(word(rowsDoubled.first.y)));
  if (high != 0)
  {
    return FillClippedTriangle(_state, rowsDoubled, _backward);
  }
  // CS:1C12: A in AL and AH, B in BL and BH, C in CL and CH, the rows halved back (SHR).
  const auto corner = [&word](ScreenPoint _point)
  { return OnScreenCorner{Low(word(_point.x)), static_cast<std::uint8_t>(Low(word(_point.y)) >> 1)}; };
  FillOnScreenTriangle(_state, corner(rowsDoubled.first), corner(rowsDoubled.second), corner(rowsDoubled.third), _backward);
  return true;
}

bool FillClippedTriangle(GameState& _state, Triangle _doubled, bool _backward)
{
  const auto word = [](std::int16_t _value) { return static_cast<std::uint16_t>(_value); };
  const std::uint16_t ax = word(_doubled.first.x);
  const std::uint16_t bx = word(_doubled.second.x);
  const std::uint16_t cx = word(_doubled.third.x);
  const std::uint16_t dx = word(_doubled.first.y);
  const std::uint16_t bp = word(_doubled.second.y);
  const std::uint16_t di = word(_doubled.third.y);
  // MOV AX,SI / AND AX,BX / AND AX,CX / JS: every x left of 0; then every doubled row above 0.
  if (Negative(static_cast<std::uint16_t>(ax & bx & cx)) || Negative(static_cast<std::uint16_t>(dx & bp & di)))
  {
    return false;
  }
  // AND AH,AH / JLE, three times: every x right of 255.
  if (Signed8(High(ax)) > 0 && Signed8(High(bx)) > 0 && Signed8(High(cx)) > 0)
  {
    return false;
  }
  // AND DH,DH / JLE, CMP BP,100h / JL, CMP DI,100h / JGE: every doubled row below 255.
  if (Signed8(High(dx)) > 0 && Signed(bp) >= 0x100 && Signed(di) >= 0x100)
  {
    return false;
  }
  // PrepareClippedTriangle (CS:1E9D): SAR DX, BP and DI: the rows halved, keeping their signs. MOV SI,DS / MOV ES,SI, then the
  // ADD/ADC forms of both edges' steps through SI.
  const auto halved = [](ScreenPoint _point) { return ScreenPoint{_point.x, static_cast<std::int16_t>(_point.y >> 1)}; };
  ScreenPoint a = halved(_doubled.first);
  ScreenPoint b = halved(_doubled.second);
  ScreenPoint c = halved(_doubled.third);
  (void)PatchClippedStep(_state, ADD_TO_EDGE_A, STEP_CLIPPED_EDGE_A);
  (void)PatchClippedStep(_state, ADD_TO_EDGE_B, STEP_CLIPPED_EDGE_B);
  // Sorted by row into the four cases by exchanges, A and B, then A and C, as signed words.
  if (a.y == b.y)
  {
    // CMP DX,DI / XCHG CX,AX / XCHG DI,DX, then JE or JG on the compare.
    const std::int16_t topRow = a.y;
    std::swap(a, c);
    if (topRow == a.y)
    {
      FillClippedOneRow(_state, a, b, c, _backward);
    }
    else if (topRow > a.y)
    {
      FillClippedFlatBottom(_state, a, b, c, _backward);
    }
    else
    {
      FillClippedFlatTop(_state, a, b, c, _backward);
    }
    return true;
  }
  if (a.y > b.y)
  {
    std::swap(a, b);
  }
  if (a.y == c.y)
  {
    std::swap(a, b);
    FillClippedFlatTop(_state, a, b, c, _backward);
    return true;
  }
  if (a.y > c.y)
  {
    std::swap(a, c);
  }
  if (b.y == c.y)
  {
    FillClippedFlatBottom(_state, a, b, c, _backward);
  }
  else
  {
    FillClippedGeneral(_state, a, b, c, _backward);
  }
  return true;
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

void DrawTitlePlanet(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow, bool _backward)
{
  _state.Set(DS.sunFringeMask, 1);
  DrawDisc(_state, _radius, _centerX, _centerRow, _backward);
  _state.Set(DS.sunFringeMask, 0);
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
// FillTriangleSpan's: AX and BP as the original leaves them, which RenderBlueprintBody's contract compared until the triangle filler
// and RenderBlueprintBody were de-assembled at level 5 (FillTriangleSpanEntry). The hook now has no caller in the game.
constexpr Machine::NativeContract FILLS_TRIANGLE_SPAN{REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
// FinishSpaceViewFrame's: DX as the original leaves it (FinishSpaceViewFrameEntry).
constexpr Machine::NativeContract FINISHES_SPACE_VIEW_FRAME{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0};
// ShowCockpitScreen's: SI and ES as the original leaves them (ShowCockpitScreenEntry).
constexpr Machine::NativeContract SHOWS_COCKPIT{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
// DrawChartFrame's: ES as the original leaves it (DrawChartFrameEntry).
constexpr Machine::NativeContract DRAWS_CHART_FRAME{GENERAL_REGISTERS, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_DX{REGISTER_AX | REGISTER_BX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_DX{REGISTER_AX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract SAVES_SCREENSHOT{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI, 0};
// DrawTitlePlanet's, widened from every register to DrawDisc's (ADR-012 item 6): its one caller, the title screen's loop (CS:7DFF),
// loads DI, BL, AX, SI and BX before it reads them, and DrawScreenString (CS:31EC) takes SI, DI, BX and ES, so of what DrawDisc
// leaves only ES = DS is read.
constexpr Machine::NativeContract DRAWS_TITLE_PLANET = CLOBBERS_GENERAL;

} // namespace

// ── The entries of the routines de-assembled so far ──

namespace
{

// A line as the clip routines take it in the registers: (CX, AX), (DX, BX) and BP.
[[nodiscard]] ClipLine ClipLineIn(const Machine::Registers& _regs) noexcept
{
  return ClipLine{_regs.cx, _regs.ax, _regs.dx, _regs.bx, _regs.bp};
}

// What a clip routine leaves in the registers and the flags its contract names, CF and ZF.
void ClipStepOut(Guest& _guest, const ClipStep& _step) noexcept
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = _step.line.firstCut;
  regs.ax = _step.line.firstAlong;
  regs.dx = _step.line.secondCut;
  regs.bx = _step.line.secondAlong;
  regs.bp = _step.line.outside;
  _guest.SetFlag(FLAG_CARRY, _step.draw);
  _guest.SetFlag(FLAG_ZERO, _step.allInside);
}

} // namespace

void DrawClippedLineEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  DrawLineOut(_guest, DrawClippedLine(_guest.State(), regs.dx, regs.bx, regs.cx, regs.ax));
  _guest.Clobber(CLOBBERS_GENERAL);
}

void ClipLineToLowEdgeEntry(Guest& _guest)
{
  ClipStepOut(_guest, ClipLineToLowEdge(_guest.State(), ClipLineIn(_guest.Regs())));
  _guest.Clobber(CLIPS_LINE);
}

void ClipLineToHighEdgeEntry(Guest& _guest)
{
  ClipStepOut(_guest, ClipLineToHighEdge(_guest.State(), ClipLineIn(_guest.Regs())));
  _guest.Clobber(CLIPS_LINE);
}

void DrawCircleEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  DrawLineOut(_guest, DrawCircle(_guest.State(), Low(regs.bx), regs.cx, regs.dx));
  _guest.Clobber(CLOBBERS_GENERAL);
}

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

void DrawDiscEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  DrawDisc(_guest.State(), regs.bx, regs.dx, regs.cx, Flag(regs, FLAG_DIRECTION));
  // MOV AX,DS / MOV ES,AX, before anything is drawn: the contract compares ES, and the chart and title screens write through it.
  regs.es = regs.ds;
  _guest.Clobber(CLOBBERS_GENERAL);
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

void FinishSpaceViewFrameEntry(Guest& _guest)
{
  FinishSpaceViewFrame(_guest.State(), _guest.Devices());
  // CLD, and MOV AX,B800h / MOV ES,AX after ClearDrawBuffer's ES = DS. DX is what PresentSpaceView leaves, MOV DX,1FF0h, the copy's
  // step back to the next even line, which the contract compares: a caller reads it.
  Machine::Registers& regs = _guest.Regs();
  _guest.SetFlag(FLAG_DIRECTION, false);
  regs.es = GameState::VIDEO_SEGMENT;
  regs.dx = TO_NEXT_EVEN_LINE;
  _guest.Clobber(FINISHES_SPACE_VIEW_FRAME);
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
  _guest.Clobber(CLOBBERS_GENERAL);
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
  // returned them to RenderBlueprintBody, whose contract compared them. BP indexes the right end's mask word; AX is the right
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

void FillTriangleEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto point = [](std::uint16_t _x, std::uint16_t _row) { return ScreenPoint{Signed(_x), Signed(_row)}; };
  // MOV DX,DS / MOV ES,DX, or MOV SI,DS / MOV ES,SI on the clipped path, once the triangle is not wholly off the buffer: the
  // contract compares ES.
  if (FillTriangle(_guest.State(), Triangle{point(regs.ax, regs.dx), point(regs.bx, regs.bp), point(regs.cx, regs.di)},
                   Flag(regs, FLAG_DIRECTION)))
  {
    regs.es = regs.ds;
  }
  _guest.Clobber(CLOBBERS_GENERAL);
}

void FillClippedTriangleEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto point = [](std::uint16_t _x, std::uint16_t _row) { return ScreenPoint{Signed(_x), Signed(_row)}; };
  if (FillClippedTriangle(_guest.State(), Triangle{point(regs.si, regs.dx), point(regs.bx, regs.bp), point(regs.cx, regs.di)},
                          Flag(regs, FLAG_DIRECTION)))
  {
    regs.es = regs.ds;
  }
  _guest.Clobber(CLOBBERS_GENERAL);
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

void DrawTitlePlanetEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  DrawTitlePlanet(_guest.State(), regs.bx, regs.dx, regs.cx, Flag(regs, FLAG_DIRECTION));
  // DrawDisc's ES = DS, which the title screen's caller (CS:7E37) draws its text through.
  regs.es = regs.ds;
  _guest.Clobber(CLOBBERS_GENERAL);
}

namespace
{

// The frame routines wait for the timer and the CGA's retrace as a rule: they run on the native thread, and the
// digests accept them (ADR-010 items 5 and 8).
constexpr Machine::NativeWait WAITS = Machine::NativeWait::Always;

constexpr std::array ENTRIES = {
  NativeEntry{0x01B7, "SaveScreenshot", &SaveScreenshotEntry, SAVES_SCREENSHOT},
  NativeEntry{0x03FD, "WriteScreenshotFile", &WriteScreenshotFileEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x0570, "FinishSpaceViewFrame", &FinishSpaceViewFrameEntry, FINISHES_SPACE_VIEW_FRAME, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x0587, "PresentChartFrame", &PresentChartFrameEntry, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x0599, "PresentSpaceView", &PresentSpaceViewEntry, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x05CC, "CopyChartBufferToScreen", &CopyChartBufferToScreenEntry, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x060D, "ClearDrawBuffer", &ClearDrawBufferEntry, PRESERVES_ALL},
  NativeEntry{0x15E0, "PlotPixel", &PlotPixelEntry, CLOBBERS_BX_CX},
  NativeEntry{0x1603, "DrawClippedLine", &DrawClippedLineEntry, CLOBBERS_GENERAL},
  NativeEntry{0x1686, "ClipLineToLowEdge", &ClipLineToLowEdgeEntry, CLIPS_LINE},
  NativeEntry{0x16C1, "ClipLineToHighEdge", &ClipLineToHighEdgeEntry, CLIPS_LINE},
  NativeEntry{0x16D1, "DrawLine", &DrawLineEntry, DRAWS_LINE},
  NativeEntry{0x1826, "DrawDisc", &DrawDiscEntry, CLOBBERS_GENERAL},
  NativeEntry{0x1A07, "FillSpan", &FillSpanEntry, PRESERVES_ALL},
  NativeEntry{0x1AC1, "DrawCircle", &DrawCircleEntry, CLOBBERS_GENERAL},
  NativeEntry{0x1B7A, "FillTriangleSpan", &FillTriangleSpanEntry, FILLS_TRIANGLE_SPAN},
  NativeEntry{0x1BFB, "FillTriangle", &FillTriangleEntry, CLOBBERS_GENERAL},
  NativeEntry{0x1E6E, "FillClippedTriangle", &FillClippedTriangleEntry, CLOBBERS_GENERAL},
  NativeEntry{0x45FF, "WaitRetraceThenDelay", &WaitRetraceThenDelayEntry, CLOBBERS_AX_DX, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x7BC0, "ShowCockpitScreen", &ShowCockpitScreenEntry, SHOWS_COCKPIT},
  NativeEntry{0x7BFB, "ClearCgaScreen", &ClearCgaScreenEntry, CLOBBERS_AX_CX_DI},
  NativeEntry{0x7C12, "ClearTextScreen", &ClearTextScreenEntry, CLOBBERS_AX_CX_DI},
  NativeEntry{0x7C25, "DrawChartFrame", &DrawChartFrameEntry, DRAWS_CHART_FRAME},
  NativeEntry{0x7CFE, "SetGraphicsMode", &SetGraphicsModeEntry, CLOBBERS_AX_BX_DX},
  NativeEntry{0x7D11, "SetTextMode", &SetTextModeEntry, CLOBBERS_AX_DX},
  NativeEntry{0x7D4E, "DrawTitlePlanet", &DrawTitlePlanetEntry, DRAWS_TITLE_PLANET},
};

} // namespace

std::span<const NativeEntry> VideoEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
