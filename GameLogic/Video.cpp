#include "pch.h"

#include "Video.h"

#include "Arithmetic.h"
#include "Combat.h"
#include "Maths.h"
#include "SaveLoad.h"
#include "SharedTables.h"
#include "StartUp.h"
#include "Text.h"
#include "Timer.h"
#include "VideoTables.h"

#include <initializer_list>
#include <utility>
#include <vector>

namespace Elite
{

namespace
{

constexpr std::uint16_t ROW_BYTES = 0x40;      // a row of the drawing buffer, 256 2-bit pixels
constexpr std::uint16_t BUFFER_ROWS = 0x80;    // rows 0-127
constexpr std::uint8_t LEFT_PIXEL_MASK = 0xC0; // the leftmost of a byte's four pixels
constexpr std::uint16_t DRAW_BUFFER_WORDS = 0x1000;

constexpr std::uint16_t CGA_BANK_WORDS = 0xFA0; // 8000 bytes: one bank of mode 4's 200 lines
constexpr std::uint16_t CGA_ODD_BANK = 0x2000;  // the odd lines' bank
constexpr std::uint16_t TEXT_CELLS = 1000;      // 40x25
constexpr std::uint8_t SPACE = 0x20;

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

// The triangle fillers choose each edge's step, ADD or SUB, by rewriting the step instructions in their own code from tables of
// opcodes after them (triangleStepOpcodes, CS:1CF3, and clippedTriangleStepOpcodes, CS:1FF8). Native code holds the choice with the
// edge instead, and the code segment is not the game's memory. One rewrite reaches a picture: FillClippedGeneral's last moves
// clippedTriangleStepOpcodes' SBB BX,[m] (CS:2014), its opcode and ModRM byte, through SI, the long edge's fraction, and leaves
// it there.
constexpr std::uint16_t SBB_BX_OPCODES = 0x1E1B;
// clippedFirstRow and clippedLastRow before a row is drawn.
constexpr std::uint16_t NO_ROW = 0x8000;
// Row 64 * row, in AL after DrawStackedSpans shifts it: bit 6 is the row's lowest bit.
constexpr std::uint8_t ODD_ROW_BIT = 0x40;

// The frame routines wait on the CGA's status port, whose bit 3 is the vertical retrace.
constexpr std::uint8_t STATUS_VERTICAL_RETRACE = 0x08;
// The drawing buffer's 64-byte lines onto the screen, a pair of lines (one in each bank) a turn.
constexpr std::uint16_t BUFFER_LINE_WORDS = 0x20;
constexpr std::uint16_t TO_ODD_LINE = 0x1FC0;              // from the end of an even line to the odd line below it
constexpr std::uint16_t TO_NEXT_EVEN_LINE = 0x1FF0;        // from the end of that odd line back to the next even line
constexpr std::uint16_t SPACE_VIEW_SCREEN_OFFSET = 0x01E8; // x=32, line 12
constexpr std::uint16_t SPACE_VIEW_LINE_PAIRS = 0x3F;
constexpr std::uint16_t CHART_SCREEN_OFFSET = 0x0648; // x=32, line 40
constexpr std::uint8_t CHART_LINE_PAIRS = 0x40;

constexpr std::uint8_t FIRST_DIGIT = '0';
constexpr std::uint8_t PAST_DIGITS = ':';               // the character after '9'
constexpr std::size_t SCREENSHOT_NAME_DIGITS = 5;       // a screenshot's name's digits, after its 'elite'
constexpr std::uint16_t TEXT_PAGE_BYTES = 0x07D0;       // 40x25 cells of character and attribute
constexpr std::uint16_t GRAPHICS_SCREEN_BYTES = 0x3F40; // both of mode 4's banks, the gap between them included

constexpr std::uint16_t SMALL_DISC_RADIUS = 5; // below it, DrawDisc draws a sprite
constexpr std::uint16_t SMALL_DISC_ROWS = 4;
constexpr std::uint8_t CLIP_SPRITE_LEFT = 0x55;  // only the sprite's right byte is drawn
constexpr std::uint8_t CLIP_SPRITE_RIGHT = 0xAA; // only its left byte is drawn
constexpr std::uint16_t CIRCLE_CHORDS = 32;
static_assert(std::size(VideoState{}.circlePoints) == CIRCLE_CHORDS, "circlePoints holds the 32 points");

// The drawing buffer, DS:0000-1FFF as in 1987: a row, the space view's 126, and a row.
static_assert(sizeof(DrawingState::drawingBuffer) == DRAWING_BUFFER_BYTES);
// The space view's first byte in the drawing buffer, a row in: where PresentSpaceView's copy starts (MOV SI,spaceViewBuffer).
constexpr std::uint16_t SPACE_VIEW_BUFFER_START = 64;

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

// MOV BX,colorFillBytes / ADD BL,[drawColor]: colorFillBytes[_color], the original reading on past the table for a colour past 3.
// drawColor is 0-3 wherever it is written: the code's colours, the laser beams' (BEAM_COLOR_MASK, and the toggle's 3 and 1), the
// hyperspace rings' table, an edge's colour bits (EDGE_COLOR_BITS), and the sun's and the planet's, 1 or 2; it is 0 in the
// image. One past 3 throws std::out_of_range.
[[nodiscard]] std::uint8_t ColorFillByte(std::uint8_t _color)
{
  return ByteWithin(std::span<const std::uint8_t>(COLOR_FILL_BYTES), _color);
}

// The byte of the drawing buffer at _offset, where a drawing routine's DI points. Every place they draw at is on it: lines,
// spans, discs and triangles are clipped to its 256x128 pixels, and the direction flag is clear wherever the original runs them
// (its one STD, at CS:2E4A, is cleared at CS:2E5C with no call between), so no string instruction runs down past its start. A
// byte off it, which the original would reach in the data segment around the buffer, throws std::out_of_range.
[[nodiscard]] std::uint8_t& BufferByte(GameState& _state, std::uint16_t _offset)
{
  return ByteWithin(DrawingBuffer(_state), _offset);
}

// The word of the drawing buffer at _offset, as BufferByte finds its bytes.
[[nodiscard]] std::span<std::uint8_t> BufferWord(GameState& _state, std::uint16_t _offset)
{
  return BytesWithin(DrawingBuffer(_state), _offset, 2);
}

// AND [_offset],_keep / OR [_offset],_color: the pixels _keep clears, then _color's set, two writes.
void Plot(GameState& _state, std::uint16_t _offset, std::uint8_t _keep, std::uint8_t _color)
{
  std::uint8_t& pixels = BufferByte(_state, _offset);
  const auto kept = static_cast<std::uint8_t>(pixels & _keep);
  pixels = kept;
  pixels = static_cast<std::uint8_t>(kept | _color);
}

// ROR DL,1 twice and ROR DH,1 twice: the pixel's color and keep-mask move one pixel right. The result
// is the carry the mask's last ROR leaves, clear when the pixel has moved into the next byte.
[[nodiscard]] bool NextPixel(std::uint8_t& _color, std::uint8_t& _keep) noexcept
{
  _color = RotateRight(_color, 2);
  _keep = RotateRight(_keep, 2);
  return Negative8(_keep);
}

// STOSB with ES on the data segment: _value at the drawing buffer's byte _offset. Returns the offset _step on, what DI becomes.
[[nodiscard]] std::uint16_t StoreBufferByte(GameState& _state, std::uint16_t _offset, std::uint8_t _value, std::uint16_t _step)
{
  BufferByte(_state, _offset) = _value;
  return static_cast<std::uint16_t>(_offset + _step);
}

// REP STOSW with ES on the data segment: _words words of _value into the drawing buffer from its byte _offset, _step apart.
// Returns the offset after the last, what DI becomes; CX ends at 0.
[[nodiscard]] std::uint16_t RepeatStoreBufferWords(GameState& _state, std::uint16_t _offset, std::uint16_t _words, std::uint16_t _value,
                                                   std::uint16_t _step)
{
  std::uint16_t offset = _offset;
  for (std::uint16_t words = _words; words != 0; --words)
  {
    SetWordIn(BufferWord(_state, offset), 0, _value);
    offset = static_cast<std::uint16_t>(offset + _step);
  }
  return offset;
}

// REP STOSW with ES on the CGA's memory: _words words of _value from B800:_offset, _step apart.
void RepeatStoreVideoWords(GameState& _state, std::uint16_t _offset, std::uint16_t _words, std::uint16_t _value, std::uint16_t _step)
{
  std::uint16_t offset = _offset;
  for (std::uint16_t words = _words; words != 0; --words)
  {
    _state.SetVideoWord(offset, _value);
    offset = static_cast<std::uint16_t>(offset + _step);
  }
}

// Where a string instruction leaves SI and DI.
struct StringOffsets
{
  std::uint16_t source;
  std::uint16_t destination;
};

// REP MOVSW from the data segment to the CGA's memory: _words words from the drawing buffer's byte _source to B800:_destination,
// _step apart. CX ends at 0.
[[nodiscard]] StringOffsets RepeatMoveBufferWords(GameState& _state, std::uint16_t _source, std::uint16_t _destination,
                                                  std::uint16_t _words, std::uint16_t _step)
{
  StringOffsets offsets{_source, _destination};
  for (std::uint16_t words = _words; words != 0; --words)
  {
    _state.SetVideoWord(offsets.destination, WordIn(BufferWord(_state, offsets.source), 0));
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
// bytes with REP STOSB, then the pixels left.
void DrawHorizontalLine(GameState& _state, LineState& _line)
{
  if (_line.bl == 0)
  {
    Plot(_state, _line.di, _line.dh, _line.dl);
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
        BufferByte(_state, _line.di) = _line.al;
        ++_line.di;
        if (--_line.cx == 0)
        {
          return;
        }
        break;
      }
      if (--_line.cx == 0)
      {
        BufferByte(_state, _line.di) = _line.al;
        return;
      }
    }
  }
  _line.al = _line.ah;
  _line.ah = Low(_line.cx);
  _line.cx = static_cast<std::uint16_t>(_line.cx >> 2);
  if (_line.cx != 0)
  {
    for (; _line.cx != 0; --_line.cx)
    {
      BufferByte(_state, _line.di) = _line.al;
      ++_line.di;
    }
  }
  _line.ah = static_cast<std::uint8_t>(_line.ah & 3);
  if (_line.ah == 0)
  {
    return;
  }
  _line.cx = _line.ah;
  _line.al = BufferByte(_state, _line.di);
  do
  {
    _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
    (void)NextPixel(_line.dl, _line.dh);
  } while (--_line.cx != 0);
  BufferByte(_state, _line.di) = _line.al;
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
      BufferByte(_state, _line.di) = _line.al;
      _line.di = static_cast<std::uint16_t>(_line.di + _line.bp + (sameByte ? 0 : 1));
      _line.al = BufferByte(_state, _line.di);
    }
    else if (!NextPixel(_line.dl, _line.dh))
    {
      BufferByte(_state, _line.di) = _line.al;
      ++_line.di;
      _line.al = BufferByte(_state, _line.di);
    }
    else
    {
      if (--_line.cx == 0)
      {
        _line.al = static_cast<std::uint8_t>((_line.al & _line.dh) | _line.dl);
        BufferByte(_state, _line.di) = _line.al;
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

// CS:1623: the rows were doubled to clip; DrawLine takes them halved (MOV DH,BL / MOV CH,AL / SHR DH,1 / SHR CH,1).
void DrawHalvedRows(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromDoubledRow, std::uint8_t _toX, std::uint8_t _toDoubledRow)
{
  DrawLine(_state, _fromX, static_cast<std::uint8_t>(_fromDoubledRow >> 1), _toX, static_cast<std::uint8_t>(_toDoubledRow >> 1));
}

// LeaveEndpoints (CS:16B5): the ends stay where they are, and the line is still to draw: MOV SI,0 / INC SI / STC, which leave
// ZF clear from the INC and CF set.
[[nodiscard]] constexpr ClipStep LeaveEndpoints(ClipLine _line) noexcept
{
  return ClipStep{_line, true, false};
}

// MoveEndpointToEdge (CS:1691): the first end (CX, AX) onto the edge at 0 along the line to the second (DX, BX): MOV SI,DX, then
// unless an end is on the edge already, SUB AX,BX / SUB CX,DX / NEG DX / IMUL DX / IDIV CX / ADD AX,BX / MOV CX,0 / MOV DX,SI. The
// moved end inside 0-255 counts off BP, DEC BP setting ZF once none is outside; outside it, the ends stay as they now are
// (LeaveEndpoints).
[[nodiscard]] ClipStep MoveEndpointToEdge(ClipLine _line)
{
  if (_line.secondCut == 0 || _line.firstCut == 0)
  {
    return LeaveEndpoints(_line);
  }
  const auto along = static_cast<std::uint16_t>(_line.firstAlong - _line.secondAlong);
  const auto cut = static_cast<std::uint16_t>(_line.firstCut - _line.secondCut);
  const auto product = static_cast<std::uint32_t>(std::int32_t{Signed(along)} * Signed(Negate(_line.secondCut)));
  const std::uint16_t moved = DivideSignedWord(product, cut).quotient;
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
[[nodiscard]] ClipStep SwapThenCutLine(ClipLine _line)
{
  std::swap(_line.firstCut, _line.secondCut);
  std::swap(_line.firstAlong, _line.secondAlong);
  return MoveEndpointToEdge(_line);
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

// CS:18F2-1924: the half-width of a disc's row, byte _profile of circleProfile scaled by discRadiusRows and rounded, plus the
// fringe while sunFringeMask is set. The DX the fringe leaves is dead: DiscRowSpan's first instruction loads DX.
[[nodiscard]] std::uint16_t DiscHalfWidth(GameState& _state, std::size_t _profile)
{
  auto profile = static_cast<std::uint8_t>(CIRCLE_PROFILE[_profile] + 1);
  if (profile == 0)
  {
    --profile; // INC AL that stops at FFh
  }
  const auto product = static_cast<std::uint16_t>(profile * _state.Data().video.discRadiusRows);
  const auto halfWidth = static_cast<std::uint8_t>(High(product) + (Low(product) >> 7));
  const std::uint8_t fringeMask = _state.Data().drawing.sunFringeMask;
  if (fringeMask == 0)
  {
    return halfWidth;
  }
  // (a, b) becomes (b, a+b), and the new b's high byte, masked, widens the row.
  const std::uint16_t previousB = _state.Data().video.fringeRandomB;
  const std::uint16_t previousA = _state.Data().video.fringeRandomA;
  _state.Data().video.fringeRandomA = previousB;
  const auto sum = static_cast<std::uint16_t>(previousA + previousB);
  _state.Data().video.fringeRandomB = sum;
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
  const std::uint16_t centerX = _state.Data().video.discCenterX;
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

// CS:18EE: the row _upperRow (2*row, moving up) alone, from byte _profile of circleProfile, while it is on the buffer.
void DrawDiscUpperRow(GameState& _state, std::size_t _profile, std::uint16_t _upperRow)
{
  if (High(_upperRow) != 0)
  {
    return;
  }
  const DiscRowEdges edges = DiscRowSpan(_state, DiscHalfWidth(_state, _profile));
  if (edges.visible)
  {
    FillSpan(_state, Low(edges.left), Low(edges.right), Low(_upperRow));
  }
}

// CS:187B: the rows _lowerRow (moving down) and _upperRow (moving up), as 2*row, one half-width for both; the upper while it
// is on the buffer.
void DrawDiscRowPair(GameState& _state, std::size_t _profile, std::uint16_t _lowerRow, std::uint16_t _upperRow)
{
  const DiscRowEdges edges = DiscRowSpan(_state, DiscHalfWidth(_state, _profile));
  if (!edges.visible)
  {
    return;
  }
  FillSpan(_state, Low(edges.left), Low(edges.right), Low(_lowerRow));
  if (High(_upperRow) == 0)
  {
    FillSpan(_state, Low(edges.left), Low(edges.right), Low(_upperRow));
  }
}

// Where DrawSmallDisc (CS:194B) puts a disc of radius 0-4: a 4-row sprite from smallDiscSprites, clipped a byte at a
// time at the left and right edges and a row at a time at the top and bottom.
struct SmallDiscPlace
{
  std::uint16_t x;    // the sprite's left x: the centre's less half the radius, 4 on when that is left of the buffer
  std::uint16_t row;  // its top row likewise, 0 once clipped at the top
  std::size_t sprite; // its first byte to draw in smallDiscSprites
  std::uint8_t rows;  // the rows to draw, 4 less those off the top or the bottom
  std::uint8_t clip;  // a bit pair a row: bit 0 when only the sprite's right byte is drawn, bit 1 when only its left
  bool onBuffer;      // false when the sprite is wholly off the buffer, and nothing is drawn
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
                       std::size_t{static_cast<std::uint8_t>((radius - 1) << 2)},
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
    // The rows above the buffer are skipped: AND clears CF, so CMC / ADC adds one more, to the sprite's low byte, which the 16
    // bytes of smallDiscSprites never carry out of. XOR CL,CL starts at row 0.
    place.rows = Low(place.row);
    place.sprite += static_cast<std::size_t>((~Low(place.row) & 3) + 1);
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
  const std::span<const std::uint8_t> sprite(SMALL_DISC_SPRITES);
  std::size_t row = _place.sprite;
  std::uint8_t clip = _place.clip;
  // The sprite's shift into its first byte, 2 bits a pixel.
  const auto shift = static_cast<std::uint8_t>((Low(_place.x) & 3) << 1);
  const std::uint8_t fill = _state.Data().video.discFillByte;
  for (std::uint8_t rows = _place.rows; rows != 0; --rows)
  {
    const auto bits = static_cast<std::uint16_t>(Join(sprite[row], 0) >> shift);
    ++row;
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
        const std::span<std::uint8_t> pixels = BufferWord(_state, at);
        const auto kept = static_cast<std::uint16_t>(WordIn(pixels, 0) & ~word);
        SetWordIn(pixels, 0, kept);
        SetWordIn(pixels, 0, static_cast<std::uint16_t>(kept | (word & Join(fill, fill))));
      }
    }
    at = static_cast<std::uint16_t>(at + ROW_BYTES);
  }
}

// ---- Triangles ----

// MOV BP,DX / AND BP,3 / SHL BP,1, then MOV AX,[BP] for a span's left end or [BP+8] for its right: triangleEdgeMasks' word for
// an end at x = _x, its fill bits in the low byte and the bits it keeps in the high. BP addresses the stack segment, and
// SS:0000 is DS:AD60, where the table is.
[[nodiscard]] std::uint16_t TriangleEdgeMask(std::uint8_t _x, bool _right) noexcept
{
  return TRIANGLE_EDGE_MASKS[(_right ? 4u : 0u) + (_x & 3u)];
}

// The spans a triangle's walk pushes for DrawStackedSpansFromRow, each DL the left x and DH the right, in the order the original
// pushes them: from the top row down. DrawStackedSpansFromRow pops them, so the last pushed is drawn first, on the bottom row.
using TriangleSpans = std::vector<std::uint16_t>;

// DrawStackedSpansFromRow (CS:1CD5): the spans of _spans popped and each filled (FillTriangleSpan), the first on the row
// _bottomRow and each next a row up, with triangleFillPattern's low byte on even rows and its high on odd ones. It pops CX
// spans, and every walk sets CX to the count it pushed.
void DrawStackedSpansFromRow(GameState& _state, TriangleSpans& _spans, std::uint8_t _bottomRow)
{
  // XOR AL,AL / SHR AX,1 twice: SI, the row's offset, row * 64, with bit 6 of AL the row's lowest bit.
  auto row = static_cast<std::uint16_t>(Join(_bottomRow, 0) >> 2);
  std::uint16_t pattern = _state.Data().scene.triangleFillPattern;
  if ((Low(row) & ODD_ROW_BIT) != 0)
  {
    pattern = SwapBytes(pattern);
  }
  while (!_spans.empty())
  {
    const std::uint16_t span = _spans.back();
    _spans.pop_back();
    FillTriangleSpan(_state, row, Low(span), High(span), Low(pattern));
    row = static_cast<std::uint16_t>(row - ROW_BYTES);
    pattern = SwapBytes(pattern);
  }
}

// DrawStackedSpans (CS:1CD1): DrawStackedSpansFromRow from triangleBottomRow.
void DrawStackedSpans(GameState& _state, TriangleSpans& _spans)
{
  DrawStackedSpansFromRow(_state, _spans, _state.Data().video.triangleBottomRow);
}

// A corner of a triangle wholly on the buffer: x and row, as FillOnScreenTriangle holds A in AL and AH, B in BX and C in CX.
struct OnScreenCorner
{
  std::uint8_t x;
  std::uint8_t row;
};

// An edge of the on-screen walk: x as 8.8, in AX or BX, its slope, in SI or DI, and whether the step the original patches in at
// its site is SUB rather than ADD.
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

// TraceTwoEdges (CS:1CBE): the spans of _rows rows pushed, edges A and B stepped after each by the steps the original patches
// in at CS:1CC9 and CS:1CCB, then DrawStackedSpans. LOOP counts the rows from CX, the rows + 1.
void TraceTwoEdges(GameState& _state, WalkEdge _a, WalkEdge _b, std::uint8_t _rows)
{
  TriangleSpans spans;
  std::uint16_t rows = _rows;
  do
  {
    spans.push_back(EdgeSpan(_a, _b));
    _a.Step();
    _b.Step();
  } while (--rows != 0);
  DrawStackedSpans(_state, spans);
}

// FillFlatBottomTriangle (CS:1C62): A on top, B and C on the bottom row; edge A from A to C, edge B from A to B.
void FillFlatBottomTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c)
{
  _state.Data().video.triangleEdgeStartX = _a.x;
  _state.Data().video.triangleEdgeStartX2 = _a.x;
  _state.Data().video.triangleBottomRow = _b.row;
  const auto rows = static_cast<std::uint8_t>(_c.row - _a.row);
  _state.Data().video.triangleUpperRows = rows;
  const ByteRun runA = RunBetween(_a.x, _c.x);
  WalkEdge a{0, EdgeSlope(runA.magnitude, rows), runA.backward};
  // MOV BH,AL / MOV AX,BX / SUB AL,AH: B's x less A's.
  const ByteRun runB = RunBetween(_a.x, _b.x);
  WalkEdge b{0, EdgeSlope(runB.magnitude, rows), runB.backward};
  a.x = Join(_state.Data().video.triangleEdgeStartX, 0);
  b.x = Join(_state.Data().video.triangleEdgeStartX2, 0);
  TraceTwoEdges(_state, a, b, static_cast<std::uint8_t>(rows + 1));
}

// FillFlatTopTriangle (CS:1CFB): B and C on the top row, A at the bottom; edge A from C to A, edge B from B to A.
void FillFlatTopTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c)
{
  _state.Data().video.triangleEdgeStartX = _b.x;
  _state.Data().video.triangleEdgeStartX2 = _c.x;
  _state.Data().video.triangleBottomRow = _a.row;
  _state.Data().video.triangleUpperRows = static_cast<std::uint8_t>(_a.row - _c.row);
  // MOV BH,AL / SUB AL,CL: A's x less C's.
  const ByteRun runA = RunBetween(_c.x, _a.x);
  const std::uint8_t rows = _state.Data().video.triangleUpperRows;
  WalkEdge a{0, EdgeSlope(runA.magnitude, rows), runA.backward};
  // MOV AX,BX / SUB AH,AL: A's x less B's, then XOR AL,AL / XOR DX,DX / DIV CX.
  const ByteRun runB = RunBetween(_b.x, _a.x);
  WalkEdge b{0, EdgeSlope(runB.magnitude, rows), runB.backward};
  a.x = Join(_state.Data().video.triangleEdgeStartX2, 0);
  b.x = Join(_state.Data().video.triangleEdgeStartX, 0);
  TraceTwoEdges(_state, a, b, static_cast<std::uint8_t>(rows + 1));
}

// FillOneRowTriangle (CS:1D5B): all three on one row, one span from the least x to the greatest, the x sorted by exchanges.
void FillOneRowTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c)
{
  _state.Data().video.triangleBottomRow = _a.row;
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
  DrawStackedSpans(_state, spans);
}

// FillGeneralTriangle (CS:1D82): A on top, B the middle row, C the bottom; the long edge A-C in AX by SI, the short edges A-B and
// then B-C in BX by DI, each step one the original patches in at the upper and the lower walk's sites.
void FillGeneralTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c)
{
  if (!(_b.row < _c.row))
  {
    std::swap(_b, _c);
  }
  _state.Data().video.triangleBottomRow = _c.row;
  _state.Data().video.triangleMiddleX = _b.x;
  _state.Data().video.triangleBottomX = _c.x;
  _state.Data().video.triangleUpperRows = static_cast<std::uint8_t>(_b.row - _a.row);
  _state.Data().video.triangleTotalRows = static_cast<std::uint8_t>(_c.row - _a.row);
  _state.Data().video.triangleEdgeStartX = _a.x;
  _state.Data().video.triangleEdgeStartX2 = _a.x;
  const ByteRun runLong = RunBetween(_a.x, _c.x);
  WalkEdge longEdge{0, EdgeSlope(runLong.magnitude, _state.Data().video.triangleTotalRows), runLong.backward};
  // MOV BH,AL / MOV AX,BX / SUB AL,AH: B's x less A's.
  const ByteRun runUpper = RunBetween(_a.x, _b.x);
  const std::uint8_t upperRows = _state.Data().video.triangleUpperRows;
  WalkEdge shortEdge{0, EdgeSlope(runUpper.magnitude, upperRows), runUpper.backward};
  longEdge.x = Join(_state.Data().video.triangleEdgeStartX, 0);
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
  rows = static_cast<std::uint8_t>(_state.Data().video.triangleTotalRows - _state.Data().video.triangleUpperRows);
  const ByteRun runLower = RunBetween(_state.Data().video.triangleMiddleX, _state.Data().video.triangleBottomX);
  shortEdge = WalkEdge{Join(_state.Data().video.triangleMiddleX, 0), EdgeSlope(runLower.magnitude, Low(rows)), runLower.backward};
  // TraceLowerEdges (CS:1E54): the short edge steps before each span is pushed, the long edge after.
  do
  {
    shortEdge.Step();
    spans.push_back(EdgeSpan(longEdge, shortEdge));
    longEdge.Step();
  } while (--rows != 0);
  DrawStackedSpans(_state, spans);
}

// FillOnScreenTriangle (CS:1C12): A, B and C wholly on the buffer, sorted by row into the four cases by exchanges, which the
// original starts with edge A's and edge B's steps patched to their ADD forms. MOV DX,DS / MOV ES,DX before them.
void FillOnScreenTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c)
{
  if (_a.row == _b.row)
  {
    // CMP AH,CH / XCHG CX,AX, then JE or JAE on the compare.
    const std::uint8_t topRow = _a.row;
    std::swap(_a, _c);
    if (topRow == _a.row)
    {
      FillOneRowTriangle(_state, _a, _b, _c);
    }
    else if (topRow > _a.row)
    {
      FillFlatBottomTriangle(_state, _a, _b, _c);
    }
    else
    {
      FillFlatTopTriangle(_state, _a, _b, _c);
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
    FillFlatTopTriangle(_state, _a, _b, _c);
    return;
  }
  if (_a.row > _c.row)
  {
    std::swap(_a, _c);
  }
  if (_b.row == _c.row)
  {
    FillFlatBottomTriangle(_state, _a, _b, _c);
  }
  else
  {
    FillGeneralTriangle(_state, _a, _b, _c);
  }
}

// ---- Clipped triangles ----

// A 16.16 edge of the clipped walk: x's whole part, signed, and its fraction: edge A in AX and SI, edge B in BX and DI.
struct ClippedEdge
{
  std::int16_t whole;
  std::uint16_t fraction;
};

// ADD fraction,[m] / ADC whole,[m], or SUB / SBB: _edge stepped by the slope at _wholeSlope and _fractionSlope.
[[nodiscard]] ClippedEdge StepClippedEdge(ClippedEdge _edge, const std::uint16_t& _wholeSlope, const std::uint16_t& _fractionSlope,
                                          bool _subtract)
{
  const std::uint16_t slopeFraction = _fractionSlope;
  const std::uint16_t slopeWhole = _wholeSlope;
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
  return StepClippedEdge(_edge, _state.Data().video.clippedSlopeAWhole, _state.Data().video.clippedSlopeAFraction, _subtract);
}

// Edge B stepped by clippedSlopeB.
[[nodiscard]] ClippedEdge StepClippedEdgeB(const GameState& _state, ClippedEdge _edge, bool _subtract)
{
  return StepClippedEdge(_edge, _state.Data().video.clippedSlopeBWhole, _state.Data().video.clippedSlopeBFraction, _subtract);
}

// XOR DX,DX / DIV _rows / store / XOR AX,AX / DIV _rows / store: _pixels over _rows rows as 16.16, the whole part stored first.
// Every caller's rows are at least 1, and the first divide's high word is 0 and the second's the remainder, so neither
// overflows into the trap.
void ClippedSlope(std::uint16_t _pixels, std::uint16_t _rows, std::uint16_t& _whole, std::uint16_t& _fraction)
{
  _whole = static_cast<std::uint16_t>(_pixels / _rows);
  _fraction = static_cast<std::uint16_t>((std::uint32_t{static_cast<std::uint16_t>(_pixels % _rows)} << 16) / _rows);
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
    return _state.Data().video.clippedFirstRow == NO_ROW;
  }
  if (_state.Data().video.clippedFirstRow == NO_ROW)
  {
    _state.Data().video.clippedFirstRow = _row;
  }
  _spans.push_back(ClippedSpan(_a, _b));
  if (_counted)
  {
    _state.Data().video.clippedRowCount = static_cast<std::uint8_t>(_state.Data().video.clippedRowCount + 1);
  }
  return true;
}

// TraceClippedFlatEdges (CS:1F6C) and FinishClippedFlat (CS:1FDB): _rows rows from _row (LOOP from CX), edge A from _a and B
// from _b, stepped by the steps patched in at CS:1FC8 and CS:1FD0; then, if any row showed, clippedLastRow and
// DrawStackedSpansFromRow from it.
void TraceClippedFlatEdges(GameState& _state, ClippedEdge _a, bool _subtractA, ClippedEdge _b, bool _subtractB, std::uint16_t _row,
                           std::uint16_t _rows)
{
  _state.Data().video.clippedFirstRow = NO_ROW;
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
  if (_state.Data().video.clippedFirstRow == NO_ROW)
  {
    return;
  }
  --row;
  DrawStackedSpansFromRow(_state, spans, Low(row));
}

// FillClippedFlatBottom (CS:1EFD): A on top, B and C on the bottom row, signed words; edge A from A to C, edge B from A to B.
void FillClippedFlatBottom(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c)
{
  _state.Data().video.clippedEdgeStartX = static_cast<std::uint16_t>(_a.x);
  _state.Data().video.clippedEdgeStartX2 = static_cast<std::uint16_t>(_a.x);
  const auto rows = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_c.y) - static_cast<std::uint16_t>(_a.y));
  _state.Data().video.clippedUpperRows = rows;
  const WordRun runA = RunBetween(_a.x, _c.x, false);
  ClippedSlope(runA.magnitude, rows, _state.Data().video.clippedSlopeAWhole, _state.Data().video.clippedSlopeAFraction);
  const WordRun runB = RunBetween(_a.x, _b.x, false);
  ClippedSlope(runB.magnitude, rows, _state.Data().video.clippedSlopeBWhole, _state.Data().video.clippedSlopeBFraction);
  const ClippedEdge a{Signed(_state.Data().video.clippedEdgeStartX), 0};
  const ClippedEdge b{Signed(_state.Data().video.clippedEdgeStartX2), 0};
  TraceClippedFlatEdges(_state, a, runA.backward, b, runB.backward, static_cast<std::uint16_t>(_a.y), static_cast<std::uint16_t>(rows + 1));
}

// FillClippedFlatTop (CS:2018): B and C on the top row, A at the bottom; edge A from C to A, edge B from B to A. PUSH AX / POP AX
// round edge A's slope keep A's x.
void FillClippedFlatTop(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c)
{
  _state.Data().video.clippedEdgeStartX = static_cast<std::uint16_t>(_b.x);
  _state.Data().video.clippedEdgeStartX2 = static_cast<std::uint16_t>(_c.x);
  const auto rows = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_a.y) - static_cast<std::uint16_t>(_c.y));
  _state.Data().video.clippedUpperRows = rows;
  const WordRun runA = RunBetween(_c.x, _a.x, false);
  ClippedSlope(runA.magnitude, rows, _state.Data().video.clippedSlopeAWhole, _state.Data().video.clippedSlopeAFraction);
  const WordRun runB = RunBetween(_b.x, _a.x, false);
  ClippedSlope(runB.magnitude, rows, _state.Data().video.clippedSlopeBWhole, _state.Data().video.clippedSlopeBFraction);
  const ClippedEdge a{Signed(_state.Data().video.clippedEdgeStartX2), 0};
  const ClippedEdge b{Signed(_state.Data().video.clippedEdgeStartX), 0};
  // The walk starts from BP, B's row, the top.
  TraceClippedFlatEdges(_state, a, runA.backward, b, runB.backward, static_cast<std::uint16_t>(_b.y), static_cast<std::uint16_t>(rows + 1));
}

// FillClippedOneRow (CS:208B): all three on row _a's, its low byte triangleBottomRow; the x sorted by signed exchanges, and one
// span from the least to the greatest, held to 0-255, unless all lie left of 0 or right of 255.
void FillClippedOneRow(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c)
{
  _state.Data().video.triangleBottomRow = Low(static_cast<std::uint16_t>(_a.y));
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
  DrawStackedSpans(_state, spans);
}

// FillClippedGeneral (CS:20C2): A on top, B the middle row, C the bottom; the long edge A-C in AX:SI, the short edges in BX:DI,
// every row from A's walked and counted in clippedRowCount, each step one the original patches in at the upper and the lower
// walk's sites.
void FillClippedGeneral(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c)
{
  if (!(_b.y < _c.y))
  {
    std::swap(_b, _c);
  }
  // PUSH DX / POP BP keep the top row for the walk.
  const auto topRow = static_cast<std::uint16_t>(_a.y);
  _state.Data().video.clippedMiddleX = static_cast<std::uint16_t>(_b.x);
  _state.Data().video.clippedBottomX = static_cast<std::uint16_t>(_c.x);
  _state.Data().video.clippedUpperRows = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_b.y) - topRow);
  _state.Data().video.clippedTotalRows = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_c.y) - topRow);
  _state.Data().video.clippedEdgeStartX = static_cast<std::uint16_t>(_a.x);
  _state.Data().video.clippedEdgeStartX2 = static_cast<std::uint16_t>(_a.x);
  const WordRun runLong = RunBetween(_a.x, _c.x, true);
  // PUSH AX / POP AX round the long edge's slope keep A's x.
  ClippedSlope(runLong.magnitude, _state.Data().video.clippedTotalRows, _state.Data().video.clippedSlopeAWhole,
               _state.Data().video.clippedSlopeAFraction);
  const WordRun runUpper = RunBetween(_a.x, _b.x, true);
  const std::uint16_t upperRows = _state.Data().video.clippedUpperRows;
  ClippedSlope(runUpper.magnitude, upperRows, _state.Data().video.clippedSlopeBWhole, _state.Data().video.clippedSlopeBFraction);
  ClippedEdge longEdge{Signed(_state.Data().video.clippedEdgeStartX), 0};
  ClippedEdge shortEdge = longEdge;
  std::uint16_t row = topRow;
  _state.Data().video.clippedFirstRow = NO_ROW;
  _state.Data().video.clippedRowCount = 0;
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
    // StartClippedLowerEdge (CS:2213): the short edge restarts from B's x, its fraction carrying on. The original patches the
    // SUB/SBB form in through SI, which leaves the long edge's fraction the second word moved. PUSH AX / POP AX keep the long
    // edge's x.
    rows = static_cast<std::uint16_t>(_state.Data().video.clippedTotalRows - _state.Data().video.clippedUpperRows);
    const WordRun runLower = RunBetween(Signed(_state.Data().video.clippedMiddleX), Signed(_state.Data().video.clippedBottomX), true);
    if (runLower.backward)
    {
      longEdge.fraction = SBB_BX_OPCODES;
    }
    ClippedSlope(runLower.magnitude, rows, _state.Data().video.clippedSlopeBWhole, _state.Data().video.clippedSlopeBFraction);
    shortEdge.whole = Signed(_state.Data().video.clippedMiddleX);
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
  if (_state.Data().video.clippedRowCount == 0)
  {
    return;
  }
  --row;
  if (row >= BUFFER_ROWS)
  {
    return;
  }
  DrawStackedSpansFromRow(_state, spans, Low(row));
}

// ---- Frames ----

// IN AL,DX / AND AL,8 / JZ: the CGA's status until it reports a vertical retrace. A read that sees a retrace uses it up
// (Cga::SetRetraceSeenOnce), so these are the original's reads, one a poll, and a poll that sees none waits (ADR-039).
void WaitForRetrace(Hardware& _hardware)
{
  for (;;)
  {
    const auto retrace = static_cast<std::uint8_t>(_hardware.CgaStatus() & STATUS_VERTICAL_RETRACE);
    if (retrace != 0)
    {
      return;
    }
    _hardware.Wait();
  }
}

// The copy loop at 05BC or 05FD: _linePairs line pairs of a 64-byte buffer line from DS:_source to B800:_destination, the even
// line, then the odd one below it, then back to the next even line: MOV CX,BP / REP MOVSW / ADD DI,AX / MOV CX,BP /
// REP MOVSW / SUB DI,DX / DEC BX / JNZ, with BP, AX and DX the steps SetLinePairSteps gives.
void CopyLinePairs(GameState& _state, std::uint16_t _source, std::uint16_t _destination, std::uint16_t _linePairs)
{
  StringOffsets at{_source, _destination};
  std::uint16_t pairs = _linePairs;
  for (;;)
  {
    at = RepeatMoveBufferWords(_state, at.source, at.destination, BUFFER_LINE_WORDS, 2);
    at.destination = static_cast<std::uint16_t>(at.destination + TO_ODD_LINE);
    at = RepeatMoveBufferWords(_state, at.source, at.destination, BUFFER_LINE_WORDS, 2);
    at.destination = static_cast<std::uint16_t>(at.destination - TO_NEXT_EVEN_LINE);
    if (--pairs == 0)
    {
      return;
    }
  }
}

// ---- Screens ----

// TEST [screenLayout],2 (CS:7BC7, CS:7C2C), for a screen that is not the one wanted: from text, mode 4 through the BIOS
// (SetGraphicsMode); from graphics, both banks cleared in the direction the routine finds (ClearCgaScreen).
ScreenChange MakeGraphicsScreen(GameState& _state, Hardware& _hardware, std::uint8_t _layout)
{
  if ((_layout & TEXT_LAYOUT_BIT) != 0)
  {
    SetGraphicsMode(_hardware);
    return ScreenChange::ModeSet;
  }
  ClearCgaScreen(_state);
  return ScreenChange::Cleared;
}

} // namespace

std::span<std::uint8_t, DRAWING_BUFFER_BYTES> DrawingBuffer(GameState& _state) noexcept
{
  return BytesOf(_state.Data().drawing.drawingBuffer);
}

void SaveScreenshot(GameState& _state, Hardware& _hardware)
{
  RestoreDivideAndKeyboardInterrupts(_hardware);
  (void)RestoreTimerInterrupt(_state, _hardware);
  _state.Data().disk.diskError = 0;
  // The original puts its own critical-error handler, CS:02F0, on int 24h round the write, and the handler that was there back
  // after it. Nothing raises int 24h: DOS answers a failed call with CF set and an error.
  WriteScreenshotFile(_state, _hardware);
  if (_state.Data().disk.diskError != 0)
  {
    ShowDiskError(_state);
  }
  InstallTimerInterrupt(_state, _hardware);
  InstallDivideAndKeyboardInterrupts(_state, _hardware);
}

void WriteScreenshotFile(GameState& _state, Hardware& _hardware)
{
  _hardware.SetDiskTransferArea(_state.Data().saveLoad.diskTransferArea);
  // The two digits, the second counting fastest: '00', '01' ... '99', '00'.
  std::uint16_t digits = _state.Data().video.screenshotNumber;
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
  _state.Data().video.screenshotNumber = digits;
  // The two digits into both names, after their 'elite'.
  const auto writeDigits = [digits](std::uint8_t(&_name)[11])
  {
    _name[SCREENSHOT_NAME_DIGITS] = Low(digits);
    _name[SCREENSHOT_NAME_DIGITS + 1] = High(digits);
  };
  writeDigits(_state.Data().video.screenshotTextFileName);
  writeDigits(_state.Data().video.screenshotGraphicsFileName);
  // eliteNN.lo for the text page, eliteNN.hi for the graphics screen: DS:DX the name, up to the NUL each ends with, which
  // nothing writes.
  const std::string_view name = (_state.Data().drawing.screenLayout & TEXT_LAYOUT_BIT) != 0
                                  ? TextIn(_state.Data().video.screenshotTextFileName)
                                  : TextIn(_state.Data().video.screenshotGraphicsFileName);
  const DosAnswer created = _hardware.CreateFile(name, 0);
  if (created.failed)
  {
    _state.Data().disk.diskError = 1;
    return;
  }
  _state.Data().disk.fileHandle = created.value;
  // From B800:0000: the text page, or both graphics banks and the gap between them.
  const std::uint16_t bytes = (_state.Data().drawing.screenLayout & TEXT_LAYOUT_BIT) != 0 ? TEXT_PAGE_BYTES : GRAPHICS_SCREEN_BYTES;
  if (_hardware.WriteFile(_state.Data().disk.fileHandle, _state.VideoBytes().first(bytes)).failed)
  {
    _state.Data().disk.diskError = 1;
  }
  if (_hardware.CloseFile(_state.Data().disk.fileHandle).failed)
  {
    _state.Data().disk.diskError = 1;
  }
}

void FinishSpaceViewFrame(GameState& _state, Hardware& _hardware)
{
  // MOV AX,B800h / MOV ES,AX, the sights, then XOR AL,AL / CLD: the copy and the clear run forwards. The sights are watched for
  // the space view, which the renderer draws in their place (ADR-035).
  _hardware.BeginSpaceViewObject();
  DrawLaserSights(_state);
  _hardware.EndSpaceViewRendered();
  PresentSpaceView(_state, _hardware);
  ClearDrawBuffer(_state);
}

void PresentChartFrame(GameState& _state, Hardware& _hardware)
{
  // MOV AX,B800h / MOV ES,AX / CLD: AL, the bands CopyChartBufferToScreen skips, is 0 whatever the caller passed, and both
  // copy forwards.
  CopyChartBufferToScreen(_state, _hardware, 0);
  ClearDrawBuffer(_state);
}

void PresentSpaceView(GameState& _state, Hardware& _hardware)
{
  // MOV AL,[msSinceFrame] / CMP AL,[minimumFrameMs] / JB back to the start at 0599h, while the timer interrupt counts
  // msSinceFrame up.
  while (_state.Data().flow.msSinceFrame < _state.Data().flight.minimumFrameMs)
  {
    _hardware.Wait();
  }
  _state.Data().flow.msSinceFrame = 0;
  WaitRetraceThenDelay(_hardware);
  CopyLinePairs(_state, SPACE_VIEW_BUFFER_START, SPACE_VIEW_SCREEN_OFFSET, SPACE_VIEW_LINE_PAIRS);
}

void CopyChartBufferToScreen(GameState& _state, Hardware& _hardware, std::uint8_t _bandsSkipped)
{
  // MOV DX,3DAh / PUSH AX, the retrace's loop at 05D0 and the delay's at 05D8, then POP AX: the bands are a value, not a word on
  // the stack. The delay, MOV AX,2BCh / DEC AX / JNZ, is the 8088's speed and takes no paced time.
  WaitForRetrace(_hardware);
  // _bandsSkipped bands of 8 lines: SHL BL,1 twice / SUB BL,40h / NEG BL, so 64 - 4 * the bands line pairs as a byte, from
  // DS:the bands * 512, as SHL AH,1 leaves the bands doubled in AH.
  const auto linePairs = static_cast<std::uint8_t>(CHART_LINE_PAIRS - static_cast<std::uint8_t>(_bandsSkipped << 2));
  const std::uint16_t source = Join(static_cast<std::uint8_t>(_bandsSkipped << 1), 0);
  CopyLinePairs(_state, source, CHART_SCREEN_OFFSET, linePairs);
}

void ClearDrawBuffer(GameState& _state)
{
  // MOV AX,<the data segment> / MOV ES,AX, then REP STOSW of 0 from DI = 0.
  (void)RepeatStoreBufferWords(_state, 0, DRAW_BUFFER_WORDS, 0, 2);
}

void PlotPixel(GameState& _state, std::uint8_t _x, std::uint8_t _row)
{
  // The pixel's two bits in the colour's fill byte, kept from the byte at row * 64 + x / 4 by the AND, set by the OR.
  const std::uint8_t pixel = RotateRight(LEFT_PIXEL_MASK, static_cast<unsigned>((_x & 3) << 1));
  const auto color = static_cast<std::uint8_t>(pixel & ColorFillByte(_state.Data().drawing.drawColor));
  Plot(_state, static_cast<std::uint16_t>(Join(_row, _x) >> 2), static_cast<std::uint8_t>(~pixel), color);
}

void DrawClippedLine(GameState& _state, std::uint16_t _fromX, std::uint16_t _fromRow, std::uint16_t _toX, std::uint16_t _toRow)
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
  { DrawHalvedRows(_state, Low(_ends.secondCut), Low(_ends.secondAlong), Low(_ends.firstCut), Low(_ends.firstAlong)); };
  if (line.outside == 0)
  {
    draw(line);
    return;
  }
  // x below 0, then the row below 0, each through ClipLineToLowEdge.
  ClipStep step = ClipLineToLowEdge(line);
  if (step.allInside)
  {
    draw(step.line);
    return;
  }
  if (!step.draw)
  {
    return;
  }
  step = ClipLineToLowEdge(Transpose(step.line));
  line = Transpose(step.line);
  if (step.allInside)
  {
    draw(line);
    return;
  }
  if (!step.draw)
  {
    return;
  }
  // x beyond 255, then the row, each through ClipLineToHighEdge with 0FFh taken off the cut coordinates and put back.
  constexpr std::uint16_t HIGH_EDGE = 0xFF;
  step = ClipLineToHighEdge(ShiftCut(line, Negate(HIGH_EDGE)));
  if (!step.draw)
  {
    return;
  }
  line = ShiftCut(step.line, HIGH_EDGE);
  if (step.allInside)
  {
    draw(line);
    return;
  }
  step = ClipLineToHighEdge(ShiftCut(Transpose(line), Negate(HIGH_EDGE)));
  if (!step.draw || !step.allInside)
  {
    return;
  }
  draw(Transpose(ShiftCut(step.line, HIGH_EDGE)));
  return;
}

ClipStep ClipLineToLowEdge(ClipLine _line)
{
  // AND CH,CH / JS, then AND DH,DH / JNS.
  if (Negative(_line.firstCut))
  {
    if (!Negative(_line.secondCut))
    {
      return MoveEndpointToEdge(_line);
    }
    // Both below the edge: CLC, with ZF clear from AND DH,DH.
    return ClipStep{_line, false, false};
  }
  if (!Negative(_line.secondCut))
  {
    return LeaveEndpoints(_line);
  }
  return SwapThenCutLine(_line);
}

ClipStep ClipLineToHighEdge(ClipLine _line)
{
  // AND DH,DH / JS, then AND CH,CH / JNS or JS.
  if (Negative(_line.secondCut))
  {
    if (!Negative(_line.firstCut))
    {
      return MoveEndpointToEdge(_line);
    }
    // Both inside: STC, with ZF clear from AND CH,CH.
    return ClipStep{_line, true, false};
  }
  if (Negative(_line.firstCut))
  {
    return SwapThenCutLine(_line);
  }
  // Both beyond 255: CLC, with ZF from AND CH,CH.
  return ClipStep{_line, false, High(_line.firstCut) == 0};
}

void DrawLine(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromRow, std::uint8_t _toX, std::uint8_t _toRow)
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
  line.al = ColorFillByte(_state.Data().drawing.drawColor);
  line.ah = line.al;
  // DL the pixel's color in place, DH the mask that keeps the rest of its byte.
  const auto shift = static_cast<std::uint8_t>((x & 3) << 1);
  const std::uint8_t pixel = RotateRight(LEFT_PIXEL_MASK, shift);
  line.dh = static_cast<std::uint8_t>(~pixel);
  line.al = static_cast<std::uint8_t>(line.al & pixel);
  line.dl = line.al;
  line.al = BufferByte(_state, line.di);
  line.cx = shift;
  if (line.bh == 0)
  {
    DrawHorizontalLine(_state, line);
    return;
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
}

void DrawDisc(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow)
{
  _state.Data().video.discFillByte = std::uint8_t{COLOR_FILL_BYTES[_state.Data().drawing.drawColor & 3u]};
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
  _state.Data().video.discRadiusRows = Low(rows);
  _state.Data().video.discRowsLeft = Low(rows);
  _state.Data().video.discCenterX = _centerX;
  // The step through circleProfile a row, 20000h/rows as 8.8 (FFFFh for 2 rows, which would overflow), by XCHG AH,AL: the
  // fraction's low byte keeps the quotient's high byte, and the whole part is that byte alone.
  constexpr std::uint32_t PROFILE_BYTES_8_8 = 0x20000;
  const std::uint16_t perRow = rows == 2 ? std::uint16_t{0xFFFF} : DivideWord(PROFILE_BYTES_8_8, rows).quotient;
  const std::uint16_t step = SwapBytes(perRow);
  _state.Data().video.discProfileStepFraction = step;
  _state.Data().video.discProfileStepWhole = Low(step);
  // CX walks down from the centre row and BX up, both as 2*row; the centre row is drawn once. BP holds the fraction and SI the
  // profile byte.
  std::uint16_t fraction = 0;
  std::size_t profile = 0;
  auto lowerRow = static_cast<std::uint16_t>(_centerRow << 1);
  std::uint16_t upperRow = lowerRow;
  DrawDiscUpperRow(_state, profile, upperRow);
  for (;;)
  {
    const std::uint32_t sum = std::uint32_t{fraction} + _state.Data().video.discProfileStepFraction;
    fraction = static_cast<std::uint16_t>(sum);
    profile += _state.Data().video.discProfileStepWhole + (sum >> 16);
    upperRow = static_cast<std::uint16_t>(upperRow - 2);
    lowerRow = static_cast<std::uint16_t>(lowerRow + 2);
    const auto rowsLeft = static_cast<std::uint8_t>(_state.Data().video.discRowsLeft - 1);
    _state.Data().video.discRowsLeft = rowsLeft;
    if (rowsLeft == 0)
    {
      return;
    }
    if (High(lowerRow) != 0)
    {
      DrawDiscUpperRow(_state, profile, upperRow);
    }
    else
    {
      DrawDiscRowPair(_state, profile, lowerRow, upperRow);
    }
  }
}

void FillSpan(GameState& _state, std::uint8_t _left, std::uint8_t _right, std::uint8_t _doubledRow)
{
  const auto row = static_cast<std::uint8_t>(_doubledRow >> 1);
  const std::uint8_t fill = _state.Data().video.discFillByte;
  const auto leftByte = static_cast<std::uint8_t>(_left >> 2);
  const auto rightByte = static_cast<std::uint8_t>(_right >> 2);
  const std::uint16_t leftMask = SPAN_LEFT_MASKS[_left & 3u];
  const std::uint16_t rightMask = SPAN_RIGHT_MASKS[_right & 3u];
  const auto bytes = static_cast<std::uint8_t>(rightByte - leftByte);
  if (bytes == 0)
  {
    // One byte holds the whole span: both masks at once.
    const auto at = static_cast<std::uint16_t>(Join(row, _left) >> 2);
    Plot(_state, at, static_cast<std::uint8_t>(High(rightMask) | High(leftMask)),
         static_cast<std::uint8_t>(Low(rightMask) & Low(leftMask) & fill));
    return;
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
      at = StoreBufferByte(_state, at, fill, 1);
    }
    at = RepeatStoreBufferWords(_state, at, static_cast<std::uint16_t>(middle >> 1), Join(fill, fill), 2);
  }
  Plot(_state, at, High(rightMask), static_cast<std::uint8_t>(Low(rightMask) & fill));
}

void DrawCircle(GameState& _state, std::uint8_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow)
{
  // circlePoints: 32 points, x and row.
  const std::span<VideoState::CirclePoint> points(_state.Data().video.circlePoints);
  // PUSH CX / PUSH DX keep the centre. The first octant, circleOctant's eight bytes scaled by the radius / 128 (MUL BL / SHL AX,1 /
  // MOV AL,AH), a byte of 0 staying 0, as four points, x then row.
  const std::span<const std::uint8_t> octant(CIRCLE_OCTANT);
  for (std::size_t octantByte = 0; octantByte != 8; ++octantByte)
  {
    const std::uint8_t unit = octant[octantByte];
    const std::uint8_t scaled = unit == 0 ? std::uint8_t{0} : High(static_cast<std::uint16_t>((unit * _radius) << 1));
    VideoState::CirclePoint& point = points[octantByte / 2];
    (octantByte % 2 == 0 ? point.x : point.row) = scaled;
  }
  // The second octant mirrors the first across the diagonal, from its last point back.
  for (std::size_t from = 0, to = 7; from != 4; ++from, --to)
  {
    points[to].row = std::uint16_t{points[from].x};
    points[to].x = std::uint16_t{points[from].row};
  }
  // The next quarter turns the first by 90 degrees, and the half after that by 180.
  for (std::size_t from = 0; from != 8; ++from)
  {
    points[from + 8].row = std::uint16_t{points[from].x};
    points[from + 8].x = Negate(std::uint16_t{points[from].row});
  }
  for (std::size_t from = 0; from != 16; ++from)
  {
    points[from + 16].x = Negate(std::uint16_t{points[from].x});
    points[from + 16].row = Negate(std::uint16_t{points[from].row});
  }
  // POP BX / POP AX: every point moved to the centre.
  for (VideoState::CirclePoint& point : points)
  {
    point.x = Offset(std::uint16_t{point.x}, _centerX);
    point.row = Offset(std::uint16_t{point.row}, _centerRow);
  }
  // The chord from the last point to the first, then each to the next, x and row each.
  DrawClippedLine(_state, points[CIRCLE_CHORDS - 1].x, points[CIRCLE_CHORDS - 1].row, points[0].x, points[0].row);
  for (std::size_t chord = 0; chord + 1 != CIRCLE_CHORDS; ++chord)
  {
    DrawClippedLine(_state, points[chord + 1].x, points[chord + 1].row, points[chord].x, points[chord].row);
  }
}

void FillTriangleSpan(GameState& _state, std::uint16_t _row, std::uint8_t _left, std::uint8_t _right, std::uint8_t _fill)
{
  // MOV AL,DL / SHR AL,1 twice / CBW / ADD DI,AX: the left x over 4 is below 40h, so AH is 0.
  const auto leftByte = static_cast<std::uint8_t>(_left >> 2);
  auto at = static_cast<std::uint16_t>(_row + leftByte);
  const auto bytes = static_cast<std::uint8_t>((_right >> 2) - leftByte);
  const std::uint16_t leftMask = TriangleEdgeMask(_left, false);
  if (bytes == 0)
  {
    // FillTriangleSpanOneByte (CS:1BD5): both edges in one byte.
    const std::uint16_t rightMask = TriangleEdgeMask(_right, true);
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
    at = StoreBufferByte(_state, at, _fill, 1);
    --middle;
  }
  if (middle != 0)
  {
    const auto words = static_cast<std::uint16_t>(middle >> 1);
    if (words != 0)
    {
      at = RepeatStoreBufferWords(_state, at, words, Join(_fill, _fill), 2);
    }
    if ((middle & 1) != 0)
    {
      at = StoreBufferByte(_state, at, _fill, 1);
    }
  }
  const std::uint16_t rightMask = TriangleEdgeMask(_right, true);
  Plot(_state, at, High(rightMask), static_cast<std::uint8_t>(Low(rightMask) & _fill));
}

void FillTriangle(GameState& _state, Triangle _triangle)
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
    FillClippedTriangle(_state, rowsDoubled);
    return;
  }
  // CS:1C12: A in AL and AH, B in BL and BH, C in CL and CH, the rows halved back (SHR).
  const auto corner = [&word](ScreenPoint _point)
  { return OnScreenCorner{Low(word(_point.x)), static_cast<std::uint8_t>(Low(word(_point.y)) >> 1)}; };
  FillOnScreenTriangle(_state, corner(rowsDoubled.first), corner(rowsDoubled.second), corner(rowsDoubled.third));
}

void FillClippedTriangle(GameState& _state, Triangle _doubled)
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
    return;
  }
  // AND AH,AH / JLE, three times: every x right of 255.
  if (Signed8(High(ax)) > 0 && Signed8(High(bx)) > 0 && Signed8(High(cx)) > 0)
  {
    return;
  }
  // AND DH,DH / JLE, CMP BP,100h / JL, CMP DI,100h / JGE: every doubled row below 255.
  if (Signed8(High(dx)) > 0 && Signed(bp) >= 0x100 && Signed(di) >= 0x100)
  {
    return;
  }
  // PrepareClippedTriangle (CS:1E9D): SAR DX, BP and DI: the rows halved, keeping their signs. MOV SI,DS / MOV ES,SI, then the
  // ADD/ADC forms of both edges' steps, which the original patches in through SI.
  const auto halved = [](ScreenPoint _point) { return ScreenPoint{_point.x, static_cast<std::int16_t>(_point.y >> 1)}; };
  ScreenPoint a = halved(_doubled.first);
  ScreenPoint b = halved(_doubled.second);
  ScreenPoint c = halved(_doubled.third);
  // Sorted by row into the four cases by exchanges, A and B, then A and C, as signed words.
  if (a.y == b.y)
  {
    // CMP DX,DI / XCHG CX,AX / XCHG DI,DX, then JE or JG on the compare.
    const std::int16_t topRow = a.y;
    std::swap(a, c);
    if (topRow == a.y)
    {
      FillClippedOneRow(_state, a, b, c);
    }
    else if (topRow > a.y)
    {
      FillClippedFlatBottom(_state, a, b, c);
    }
    else
    {
      FillClippedFlatTop(_state, a, b, c);
    }
    return;
  }
  if (a.y > b.y)
  {
    std::swap(a, b);
  }
  if (a.y == c.y)
  {
    std::swap(a, b);
    FillClippedFlatTop(_state, a, b, c);
    return;
  }
  if (a.y > c.y)
  {
    std::swap(a, c);
  }
  if (b.y == c.y)
  {
    FillClippedFlatBottom(_state, a, b, c);
  }
  else
  {
    FillClippedGeneral(_state, a, b, c);
  }
}

void WaitRetraceThenDelay(Hardware& _hardware)
{
  // MOV DX,3DAh and the retrace's loop at 4602, then MOV AX,7D0h and the delay's at 460A, which counts AX down to 0: the
  // 8088's speed, which takes no paced time.
  WaitForRetrace(_hardware);
}

ScreenChange ShowCockpitScreen(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t layout = _state.Data().drawing.screenLayout;
  if (layout == 0)
  {
    return ScreenChange::None;
  }
  const ScreenChange change = MakeGraphicsScreen(_state, _hardware, layout);
  _state.Data().drawing.screenLayout = 0;
  // PUSH DS / MOV DS,<cockpitImage's segment>, then from its start both banks, after CLD (REP MOVSW): the even lines' 8000 bytes
  // and the odd lines' after them.
  const std::span<const std::uint8_t> cockpit(COCKPIT_IMAGE);
  for (std::uint16_t word = 0; word < CGA_BANK_WORDS; ++word)
  {
    _state.SetVideoWord(static_cast<std::uint16_t>(word * 2), WordIn(cockpit, std::size_t{word} * 2));
  }
  for (std::uint16_t word = 0; word < CGA_BANK_WORDS; ++word)
  {
    _state.SetVideoWord(static_cast<std::uint16_t>(CGA_ODD_BANK + word * 2), WordIn(cockpit, (std::size_t{CGA_BANK_WORDS} + word) * 2));
  }
  return change;
}

void ClearCgaScreen(GameState& _state)
{
  RepeatStoreVideoWords(_state, 0, CGA_BANK_WORDS, 0, 2);
  RepeatStoreVideoWords(_state, CGA_ODD_BANK, CGA_BANK_WORDS, 0, 2);
}

void ClearTextScreen(GameState& _state)
{
  RepeatStoreVideoWords(_state, 0, TEXT_CELLS, Join(_state.Data().drawing.textAttribute, SPACE), 2);
}

ScreenChange DrawChartFrame(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t layout = _state.Data().drawing.screenLayout;
  if (layout == CHART_LAYOUT)
  {
    return ScreenChange::None;
  }
  const ScreenChange change = MakeGraphicsScreen(_state, _hardware, layout);
  _state.Data().drawing.screenLayout = CHART_LAYOUT;
  // The horizontals at y=9, 39, 168 and 199, x 32-287: 32 words of colour 3 each into ES = B800h, by REP STOSW in the direction
  // the routine finds (MOV CX,20h, then MOV CL,20h over the 0 the last left).
  for (const std::uint16_t line : CHART_FRAME_LINES)
  {
    RepeatStoreVideoWords(_state, line, CHART_FRAME_LINE_WORDS, CHART_FRAME_COLOR_WORD, 2);
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

void DrawTitlePlanet(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow)
{
  _state.Data().drawing.sunFringeMask = 1;
  DrawDisc(_state, _radius, _centerX, _centerRow);
  _state.Data().drawing.sunFringeMask = 0;
}

} // namespace Elite
