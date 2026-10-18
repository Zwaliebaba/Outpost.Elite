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

// screenLayout: 0 the cockpit, 1 the chart frame; bit 1 set while in text mode.
constexpr std::uint8_t CHART_LAYOUT = 1;
constexpr std::uint8_t TEXT_LAYOUT_BIT = 2;

constexpr std::uint8_t BRIGHT_ON_BLACK = 0x10;         // the colour select: the bright palette, a black background
constexpr std::uint8_t COLOR_SELECT_AFTER_MODE = 0x30; // the colour select a mode set leaves: palette 1, bright

// The triangle fillers choose each edge's step, an add or a subtract, by rewriting the step instructions in their own code from
// tables of opcodes after them (triangleStepOpcodes, and clippedTriangleStepOpcodes). Native code holds the choice with the
// edge instead, and the code segment is not the game's memory. One rewrite reaches a picture: FillClippedGeneral's last moves
// the two bytes, opcode and ModRM, of clippedTriangleStepOpcodes' subtract-with-borrow step for edge B through the long edge's
// fraction, and leaves them there.
constexpr std::uint16_t SBB_BX_OPCODES = 0x1E1B;
// clippedFirstRow and clippedLastRow before a row is drawn.
constexpr std::uint16_t NO_ROW = 0x8000;
// A row's offset, row * 64, as DrawStackedSpans works it out: bit 6 of its low byte is the row's lowest bit.
constexpr std::uint8_t ODD_ROW_BIT = 0x40;

constexpr std::uint16_t SMALL_DISC_RADIUS = 5; // below it, DrawDisc draws a sprite
constexpr std::uint16_t SMALL_DISC_ROWS = 4;
constexpr std::uint8_t CLIP_SPRITE_LEFT = 0x55;  // only the sprite's right byte is drawn
constexpr std::uint8_t CLIP_SPRITE_RIGHT = 0xAA; // only its left byte is drawn
constexpr std::uint16_t CIRCLE_CHORDS = 32;
static_assert(std::size(VideoState{}.circlePoints) == CIRCLE_CHORDS, "circlePoints holds the 32 points");

// The drawing buffer, as in the original: a row, the space view's 126, and a row.
static_assert(sizeof(DrawingState::drawingBuffer) == DRAWING_BUFFER_BYTES);
// The space view's first byte in the drawing buffer, a row in: where PresentSpaceView's copy starts (spaceViewBuffer).

// The word's two bytes exchanged.
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

// colorFillBytes[_color], the colour added to the low byte of the table's address alone, the original reading on past the table
// for a colour past 3.
// drawColor is 0-3 wherever it is written: the code's colours, the laser beams' (BEAM_COLOR_MASK, and the toggle's 3 and 1), the
// hyperspace rings' table, an edge's colour bits (EDGE_COLOR_BITS), and the sun's and the planet's, 1 or 2; it is 0 in the
// image. One past 3 throws std::out_of_range.
[[nodiscard]] std::uint8_t ColorFillByte(std::uint8_t _color)
{
  return ByteWithin(std::span<const std::uint8_t>(COLOR_FILL_BYTES), _color);
}

// The byte of the drawing buffer at _offset, where a drawing routine writes. Every place they draw at is on it: lines,
// spans, discs and triangles are clipped to its 256x128 pixels, and the direction flag is clear wherever the original runs them
// (the one place it is set, it is cleared again with no call between), so no string store or copy runs down past its start. A
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

// The byte at _offset: the pixels _keep clears, then _color's set, two writes.
void Plot(GameState& _state, std::uint16_t _offset, std::uint8_t _keep, std::uint8_t _color)
{
  std::uint8_t& pixels = BufferByte(_state, _offset);
  const auto kept = static_cast<std::uint8_t>(pixels & _keep);
  pixels = kept;
  pixels = static_cast<std::uint8_t>(kept | _color);
}

// The pixel's color and keep-mask, each rotated right a bit twice: one pixel right. The result
// is the bit the mask's last rotate carries out, clear when the pixel has moved into the next byte.
[[nodiscard]] bool NextPixel(std::uint8_t& _color, std::uint8_t& _keep) noexcept
{
  _color = RotateRight(_color, 2);
  _keep = RotateRight(_keep, 2);
  return Negative8(_keep);
}

// A byte store: _value at the drawing buffer's byte _offset. Returns the offset _step on, where the next store goes.
[[nodiscard]] std::uint16_t StoreBufferByte(GameState& _state, std::uint16_t _offset, std::uint8_t _value, std::uint16_t _step)
{
  BufferByte(_state, _offset) = _value;
  return static_cast<std::uint16_t>(_offset + _step);
}

// A repeated word store: _words words of _value into the drawing buffer from its byte _offset, _step apart.
// Returns the offset after the last, where the next store goes.
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

// ---- Lines ----

// A line as DrawLine draws it: where it is in the drawing buffer, how far it has to go, and its pixel.
struct LinePen
{
  std::uint16_t at;      // the buffer's byte the pixel is in
  std::uint16_t rowStep; // a row down, or up when the line rises
  std::uint8_t across;   // its pixels across, right of the first
  std::uint8_t down;     // its rows down or up, past the first
  std::uint8_t keep;     // the mask that keeps the rest of the pixel's byte
  std::uint8_t pixel;    // the pixel's colour, in place in its byte
  std::uint8_t held;     // the byte being drawn, read from the buffer, until it is written back
  std::uint8_t fill;     // the colour's fill byte, for whole bytes of a horizontal line
};

// The pen's pixel into the byte it holds.
void PaintHeld(LinePen& _pen) noexcept
{
  _pen.held = static_cast<std::uint8_t>((_pen.held & _pen.keep) | _pen.pixel);
}

// DrawLineHorizontal: the pixels across right of the first, pixel by pixel to a byte boundary, then whole bytes, then the
// pixels left.
void DrawHorizontalLine(GameState& _state, LinePen& _pen)
{
  if (_pen.across == 0)
  {
    Plot(_state, _pen.at, _pen.keep, _pen.pixel);
    return;
  }
  std::uint16_t count = static_cast<std::uint16_t>(_pen.across + 1);
  if ((_pen.keep & LEFT_PIXEL_MASK) != 0)
  {
    for (;;)
    {
      PaintHeld(_pen);
      if (!NextPixel(_pen.pixel, _pen.keep))
      {
        BufferByte(_state, _pen.at) = _pen.held;
        ++_pen.at;
        if (--count == 0)
        {
          return;
        }
        break;
      }
      if (--count == 0)
      {
        BufferByte(_state, _pen.at) = _pen.held;
        return;
      }
    }
  }
  const auto rest = static_cast<std::uint8_t>(count & 3);
  for (std::uint16_t bytes = static_cast<std::uint16_t>(count >> 2); bytes != 0; --bytes)
  {
    BufferByte(_state, _pen.at) = _pen.fill;
    ++_pen.at;
  }
  if (rest == 0)
  {
    return;
  }
  _pen.held = BufferByte(_state, _pen.at);
  for (std::uint8_t left = rest; left != 0; --left)
  {
    PaintHeld(_pen);
    (void)NextPixel(_pen.pixel, _pen.keep);
  }
  BufferByte(_state, _pen.at) = _pen.held;
}

// DrawLineVertical.
void DrawVerticalLine(GameState& _state, LinePen& _pen)
{
  for (std::uint8_t rows = _pen.down; rows != 0; --rows)
  {
    Plot(_state, _pen.at, _pen.keep, _pen.pixel);
    _pen.at = static_cast<std::uint16_t>(_pen.at + _pen.rowStep);
  }
  Plot(_state, _pen.at, _pen.keep, _pen.pixel);
}

// DrawLineSteep: a row a pixel, stepping across when the error term borrows.
void DrawSteepLine(GameState& _state, LinePen& _pen)
{
  const auto down = static_cast<std::uint8_t>(_pen.down + 1);
  const auto across = static_cast<std::uint8_t>(_pen.across + 1);
  std::uint8_t error = down;
  for (std::uint8_t rows = _pen.down; rows != 0; --rows)
  {
    const bool borrow = error < across;
    error = static_cast<std::uint8_t>(error - across);
    Plot(_state, _pen.at, _pen.keep, _pen.pixel);
    if (borrow)
    {
      error = static_cast<std::uint8_t>(error + down);
      const bool sameByte = NextPixel(_pen.pixel, _pen.keep);
      _pen.at = static_cast<std::uint16_t>(_pen.at + _pen.rowStep + (sameByte ? 0 : 1));
    }
    else
    {
      _pen.at = static_cast<std::uint16_t>(_pen.at + _pen.rowStep);
    }
  }
  Plot(_state, _pen.at, _pen.keep, _pen.pixel);
}

// DrawLineShallow: a pixel a step, stepping a row when the error term borrows, the byte held until the line leaves it.
void DrawShallowLine(GameState& _state, LinePen& _pen)
{
  // An increment of the pixels across that stops at FFh.
  const auto across = static_cast<std::uint8_t>(_pen.across != 0xFF ? _pen.across + 1 : _pen.across);
  const auto down = static_cast<std::uint8_t>(_pen.down + 1);
  std::uint8_t error = across;
  for (std::uint8_t pixels = _pen.across;;)
  {
    const bool borrow = error < down;
    error = static_cast<std::uint8_t>(error - down);
    PaintHeld(_pen);
    if (borrow)
    {
      error = static_cast<std::uint8_t>(error + across);
      const bool sameByte = NextPixel(_pen.pixel, _pen.keep);
      BufferByte(_state, _pen.at) = _pen.held;
      _pen.at = static_cast<std::uint16_t>(_pen.at + _pen.rowStep + (sameByte ? 0 : 1));
      _pen.held = BufferByte(_state, _pen.at);
    }
    else if (!NextPixel(_pen.pixel, _pen.keep))
    {
      BufferByte(_state, _pen.at) = _pen.held;
      ++_pen.at;
      _pen.held = BufferByte(_state, _pen.at);
    }
    else
    {
      if (--pixels == 0)
      {
        PaintHeld(_pen);
        BufferByte(_state, _pen.at) = _pen.held;
        return;
      }
      continue;
    }
    if (--pixels == 0)
    {
      Plot(_state, _pen.at, _pen.keep, _pen.pixel);
      return;
    }
  }
}

// DrawLineDiagonal.
void DrawDiagonalLine(GameState& _state, LinePen& _pen)
{
  for (std::uint8_t pixels = _pen.across; pixels != 0; --pixels)
  {
    Plot(_state, _pen.at, _pen.keep, _pen.pixel);
    const bool sameByte = NextPixel(_pen.pixel, _pen.keep);
    _pen.at = static_cast<std::uint16_t>(_pen.at + _pen.rowStep + (sameByte ? 0 : 1));
  }
  Plot(_state, _pen.at, _pen.keep, _pen.pixel);
}

// the rows were doubled to clip; DrawLine takes them halved.
void DrawHalvedRows(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromDoubledRow, std::uint8_t _toX, std::uint8_t _toDoubledRow)
{
  DrawLine(_state, _fromX, static_cast<std::uint8_t>(_fromDoubledRow >> 1), _toX, static_cast<std::uint8_t>(_toDoubledRow >> 1));
}

// LeaveEndpoints: the ends stay where they are, and the line is still to draw, not yet all inside.
[[nodiscard]] constexpr ClipStep LeaveEndpoints(ClipLine _line) noexcept
{
  return ClipStep{_line, true, false};
}

// MoveEndpointToEdge: the first end onto the edge at 0 along the line to the second: unless an end is on the edge already,
// firstAlong becomes secondAlong + (firstAlong - secondAlong) * -secondCut / (firstCut - secondCut), a signed multiply and
// divide of words, and firstCut 0. The moved end inside 0-255 counts off outside, all inside once none is left; outside it,
// the ends stay as they now are (LeaveEndpoints).
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

// SwapThenCutLine: the ends exchanged, then MoveEndpointToEdge: the second end moved, the ends left swapped.
[[nodiscard]] ClipStep SwapThenCutLine(ClipLine _line)
{
  std::swap(_line.firstCut, _line.secondCut);
  std::swap(_line.firstAlong, _line.secondAlong);
  return MoveEndpointToEdge(_line);
}

// Each end's two coordinates exchanged: the other axis cut.
[[nodiscard]] constexpr ClipLine Transpose(ClipLine _line) noexcept
{
  std::swap(_line.firstCut, _line.firstAlong);
  std::swap(_line.secondCut, _line.secondAlong);
  return _line;
}

// Both cut coordinates moved by _bytes: 0FFh less, so that the edge at 255 is at 0, or 0FFh more, back.
[[nodiscard]] constexpr ClipLine ShiftCut(ClipLine _line, std::uint16_t _bytes) noexcept
{
  _line.firstCut = static_cast<std::uint16_t>(_line.firstCut + _bytes);
  _line.secondCut = static_cast<std::uint16_t>(_line.secondCut + _bytes);
  return _line;
}

// ---- Discs ----

// the half-width of a disc's row, byte _profile of circleProfile scaled by discRadiusRows and rounded, plus the
// fringe while sunFringeMask is set. The rest of what the fringe leaves is dead: DiscRowSpan overwrites it first.
[[nodiscard]] std::uint16_t DiscHalfWidth(GameState& _state, std::size_t _profile)
{
  auto profile = static_cast<std::uint8_t>(CIRCLE_PROFILE[_profile] + 1);
  if (profile == 0)
  {
    --profile; // an increment that stops at FFh
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

// the edges of a row _halfWidth either side of discCenterX; on the buffer, their low bytes are FillSpan's ends.
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

// the row _upperRow (2*row, moving up) alone, from byte _profile of circleProfile, while it is on the buffer.
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

// the rows _lowerRow (moving down) and _upperRow (moving up), as 2*row, one half-width for both; the upper while it
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

// Where DrawSmallDisc puts a disc of radius 0-4: a 4-row sprite from smallDiscSprites, clipped a byte at a
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

// where the sprite of a disc of radius _radius goes for the centre (_centerX, _centerRow).
[[nodiscard]] SmallDiscPlace PlaceSmallDisc(std::uint8_t _radius, std::int16_t _centerX, std::int16_t _centerRow) noexcept
{
  // Radius 0 draws radius 1's sprite. Its corner is half the radius up and left of the centre.
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
    // The rows above the buffer are skipped: a carry the original adds in makes the sprite's step one more than the complemented
    // row's low two bits, added to the sprite address's low byte, which the 16 bytes of smallDiscSprites never carry out of.
    // Drawing starts at row 0.
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

// DrawSmallDisc: the sprite _place gives, in discFillByte, a row at a time from its top: each byte ANDed
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
        // Both bytes, as a word, the sprite's left byte at the lower address: the word kept where the bits are clear,
        // then the bits in the fill set in it, two word writes.
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

// triangleEdgeMasks' word for an end at x = _x, the left ends' four words first and the right ends' after: its fill bits in the
// low byte and the bits it keeps in the high. The original reads it in the stack segment, where the table is.
[[nodiscard]] std::uint16_t TriangleEdgeMask(std::uint8_t _x, bool _right) noexcept
{
  return TRIANGLE_EDGE_MASKS[(_right ? 4u : 0u) + (_x & 3u)];
}

// The spans a triangle's walk pushes for DrawStackedSpansFromRow, each the left x in its low byte and the right in its high, in
// the order the original pushes them: from the top row down. DrawStackedSpansFromRow pops them, so the last pushed is drawn
// first, on the bottom row.
using TriangleSpans = std::vector<std::uint16_t>;

// DrawStackedSpansFromRow: the spans of _spans popped and each filled (FillTriangleSpan), the first on the row
// _bottomRow and each next a row up, with triangleFillPattern's low byte on even rows and its high on odd ones. It pops a
// count of spans, and every walk sets that count to the number it pushed.
void DrawStackedSpansFromRow(GameState& _state, TriangleSpans& _spans, std::uint8_t _bottomRow)
{
  // The row's offset, row * 64, with bit 6 of its low byte the row's lowest bit.
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

// DrawStackedSpans: DrawStackedSpansFromRow from triangleBottomRow.
void DrawStackedSpans(GameState& _state, TriangleSpans& _spans)
{
  DrawStackedSpansFromRow(_state, _spans, _state.Data().video.triangleBottomRow);
}

// A corner of a triangle wholly on the buffer: x and row, a byte each.
struct OnScreenCorner
{
  std::uint8_t x;
  std::uint8_t row;
};

// An edge of the on-screen walk: x as 8.8, its slope, and whether the step the original patches in at its site subtracts
// rather than adds.
struct WalkEdge
{
  std::uint16_t x;
  std::uint16_t slope;
  bool subtract;

  // x moved on by the slope, forwards or back, in 16 bits.
  void Step() noexcept
  {
    x = static_cast<std::uint16_t>(subtract ? x - slope : x + slope);
  }
};

// The span between edges A and B's whole parts, the smaller in the low byte.
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

// The 8.8 slope of an edge _pixels across _rows rows, _pixels * 256 over _rows, unsigned. Every caller's rows are at least 1
// and the dividend's high word is 0, so the divide never overflows into the trap.
[[nodiscard]] std::uint16_t EdgeSlope(std::uint8_t _pixels, std::uint8_t _rows) noexcept
{
  return static_cast<std::uint16_t>((std::uint32_t{_pixels} << 8) / _rows);
}

// _to less _from in 8 bits as a magnitude, negated when it borrows, and whether the edge steps back: the patch a borrow makes.
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

// TraceTwoEdges: the spans of _rows rows pushed, edges A and B stepped after each by the steps the original patches
// in, then DrawStackedSpans. Every caller's _rows is the rows between the corners + 1.
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

// FillFlatBottomTriangle: A on top, B and C on the bottom row; edge A from A to C, edge B from A to B.
void FillFlatBottomTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c)
{
  _state.Data().video.triangleEdgeStartX = _a.x;
  _state.Data().video.triangleEdgeStartX2 = _a.x;
  _state.Data().video.triangleBottomRow = _b.row;
  const auto rows = static_cast<std::uint8_t>(_c.row - _a.row);
  _state.Data().video.triangleUpperRows = rows;
  const ByteRun runA = RunBetween(_a.x, _c.x);
  WalkEdge a{0, EdgeSlope(runA.magnitude, rows), runA.backward};
  // B's x less A's.
  const ByteRun runB = RunBetween(_a.x, _b.x);
  WalkEdge b{0, EdgeSlope(runB.magnitude, rows), runB.backward};
  a.x = Join(_state.Data().video.triangleEdgeStartX, 0);
  b.x = Join(_state.Data().video.triangleEdgeStartX2, 0);
  TraceTwoEdges(_state, a, b, static_cast<std::uint8_t>(rows + 1));
}

// FillFlatTopTriangle: B and C on the top row, A at the bottom; edge A from C to A, edge B from B to A.
void FillFlatTopTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c)
{
  _state.Data().video.triangleEdgeStartX = _b.x;
  _state.Data().video.triangleEdgeStartX2 = _c.x;
  _state.Data().video.triangleBottomRow = _a.row;
  _state.Data().video.triangleUpperRows = static_cast<std::uint8_t>(_a.row - _c.row);
  // A's x less C's.
  const ByteRun runA = RunBetween(_c.x, _a.x);
  const std::uint8_t rows = _state.Data().video.triangleUpperRows;
  WalkEdge a{0, EdgeSlope(runA.magnitude, rows), runA.backward};
  // A's x less B's, over the rows as for edge A.
  const ByteRun runB = RunBetween(_b.x, _a.x);
  WalkEdge b{0, EdgeSlope(runB.magnitude, rows), runB.backward};
  a.x = Join(_state.Data().video.triangleEdgeStartX2, 0);
  b.x = Join(_state.Data().video.triangleEdgeStartX, 0);
  TraceTwoEdges(_state, a, b, static_cast<std::uint8_t>(rows + 1));
}

// FillOneRowTriangle: all three on one row, one span from the least x to the greatest, the x sorted by exchanges.
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
  // The greatest and the least as a span, the smaller in the low byte.
  std::uint8_t left = greatest;
  std::uint8_t right = least;
  if (!(left < right))
  {
    std::swap(left, right);
  }
  TriangleSpans spans{Join(right, left)};
  DrawStackedSpans(_state, spans);
}

// FillGeneralTriangle: A on top, B the middle row, C the bottom; the long edge A-C, the short edges A-B and
// then B-C, each step one the original patches in at the upper and the lower walk's sites.
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
  // B's x less A's.
  const ByteRun runUpper = RunBetween(_a.x, _b.x);
  const std::uint8_t upperRows = _state.Data().video.triangleUpperRows;
  WalkEdge shortEdge{0, EdgeSlope(runUpper.magnitude, upperRows), runUpper.backward};
  longEdge.x = Join(_state.Data().video.triangleEdgeStartX, 0);
  shortEdge.x = longEdge.x;
  // TraceUpperEdges: rows A to B inclusive, each span pushed before the edges step.
  TriangleSpans spans;
  std::uint16_t rows = static_cast<std::uint8_t>(upperRows + 1);
  do
  {
    spans.push_back(EdgeSpan(longEdge, shortEdge));
    longEdge.Step();
    shortEdge.Step();
  } while (--rows != 0);
  // StartLowerShortEdge: the rows below B, the short edge restarting from B's x, the long edge
  // carrying on.
  rows = static_cast<std::uint8_t>(_state.Data().video.triangleTotalRows - _state.Data().video.triangleUpperRows);
  const ByteRun runLower = RunBetween(_state.Data().video.triangleMiddleX, _state.Data().video.triangleBottomX);
  shortEdge = WalkEdge{Join(_state.Data().video.triangleMiddleX, 0), EdgeSlope(runLower.magnitude, Low(rows)), runLower.backward};
  // TraceLowerEdges: the short edge steps before each span is pushed, the long edge after.
  do
  {
    shortEdge.Step();
    spans.push_back(EdgeSpan(longEdge, shortEdge));
    longEdge.Step();
  } while (--rows != 0);
  DrawStackedSpans(_state, spans);
}

// FillOnScreenTriangle: A, B and C wholly on the buffer, sorted by row into the four cases by exchanges, which the
// original starts with edge A's and edge B's steps patched to their adding forms, its
// string stores aimed at the data segment.
void FillOnScreenTriangle(GameState& _state, OnScreenCorner _a, OnScreenCorner _b, OnScreenCorner _c)
{
  if (_a.row == _b.row)
  {
    // A's row compared with C's before A and C are exchanged, the branch on that compare after.
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

// A 16.16 edge of the clipped walk: x's whole part, signed, and its fraction.
struct ClippedEdge
{
  std::int16_t whole;
  std::uint16_t fraction;
};

// _edge stepped by the slope at _wholeSlope and _fractionSlope, forwards or back: the fraction, then its carry or borrow and
// the whole part.
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

// _pixels over _rows rows as 16.16 by two unsigned divides, the whole part stored first, then the remainder's fraction.
// Every caller's rows are at least 1, and the first divide's high word is 0 and the second's the remainder, so neither
// overflows into the trap.
void ClippedSlope(std::uint16_t _pixels, std::uint16_t _rows, std::uint16_t& _whole, std::uint16_t& _fraction)
{
  _whole = static_cast<std::uint16_t>(_pixels / _rows);
  _fraction = static_cast<std::uint16_t>((std::uint32_t{static_cast<std::uint16_t>(_pixels % _rows)} << 16) / _rows);
}

// _to less _from as a magnitude, negated when the edge steps back, and whether it does, the patch a step back makes: with
// _backAtZero it steps back when _to is at or below _from, otherwise only below.
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

// Whether row _row, between edges _a and _b, shows: 0-127, and not both edges left of 0 or both right of 255, as the high
// bytes of their whole parts say.
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

// PushClippedSpan: a shown row's span, held to 0-255, the left x in the low byte and the right in the high.
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

// TraceClippedFlatEdges and FinishClippedFlat: _rows rows from _row, edge A from _a and B
// from _b, stepped by the steps patched in; then, if any row showed, clippedLastRow and
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

// FillClippedFlatBottom: A on top, B and C on the bottom row, signed words; edge A from A to C, edge B from A to B.
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

// FillClippedFlatTop: B and C on the top row, A at the bottom; edge A from C to A, edge B from B to A.
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
  // The walk starts from B's row, the top.
  TraceClippedFlatEdges(_state, a, runA.backward, b, runB.backward, static_cast<std::uint16_t>(_b.y), static_cast<std::uint16_t>(rows + 1));
}

// FillClippedOneRow: all three on row _a's, its low byte triangleBottomRow; the x sorted by signed exchanges, and one
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

// FillClippedGeneral: A on top, B the middle row, C the bottom; the long edge A-C as edge A, the short edges as edge B,
// every row from A's walked and counted in clippedRowCount, each step one the original patches in at the upper and the lower
// walk's sites.
void FillClippedGeneral(GameState& _state, ScreenPoint _a, ScreenPoint _b, ScreenPoint _c)
{
  if (!(_b.y < _c.y))
  {
    std::swap(_b, _c);
  }
  // The top row, kept for the walk.
  const auto topRow = static_cast<std::uint16_t>(_a.y);
  _state.Data().video.clippedMiddleX = static_cast<std::uint16_t>(_b.x);
  _state.Data().video.clippedBottomX = static_cast<std::uint16_t>(_c.x);
  _state.Data().video.clippedUpperRows = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_b.y) - topRow);
  _state.Data().video.clippedTotalRows = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_c.y) - topRow);
  _state.Data().video.clippedEdgeStartX = static_cast<std::uint16_t>(_a.x);
  _state.Data().video.clippedEdgeStartX2 = static_cast<std::uint16_t>(_a.x);
  const WordRun runLong = RunBetween(_a.x, _c.x, true);
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
  // ClippedUpperRowLoop: rows A to B inclusive.
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
    // StartClippedLowerEdge: the short edge restarts from B's x, its fraction carrying on. The original patches the
    // subtracting forms in through the long edge's fraction, which they leave holding the second word moved.
    rows = static_cast<std::uint16_t>(_state.Data().video.clippedTotalRows - _state.Data().video.clippedUpperRows);
    const WordRun runLower = RunBetween(Signed(_state.Data().video.clippedMiddleX), Signed(_state.Data().video.clippedBottomX), true);
    if (runLower.backward)
    {
      longEdge.fraction = SBB_BX_OPCODES;
    }
    ClippedSlope(runLower.magnitude, rows, _state.Data().video.clippedSlopeBWhole, _state.Data().video.clippedSlopeBFraction);
    shortEdge.whole = Signed(_state.Data().video.clippedMiddleX);
    // ClippedLowerRowLoop: the short edge steps before the row is taken, the long edge after.
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
  // FinishClippedGeneral. The guard on the last row never holds: a row is pushed only below 128.
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

// The display's frame clock, polled until it reports a vertical retrace. A poll that sees a retrace uses it up
// (Display::TakeRetrace), so these are the original's reads, one a poll, and a poll that sees none waits (ADR-039, ADR-069).
[[nodiscard]] Task<void> WaitForRetrace(Host& _host)
{
  for (;;)
  {
    if (_host.Screen().TakeRetrace())
    {
      co_return;
    }
    co_await _host.Wait();
  }
}

// ---- Screens ----

// screenLayout's text bit, for a screen that is not the one wanted: from text, the graphics mode (SetGraphicsMode); from
// graphics, the screen cleared, which the original did in video memory. Either way the space view no longer shows.
ScreenChange MakeGraphicsScreen(GameState& _state, Host& _host, std::uint8_t _layout)
{
  _host.LeaveSpaceView();
  if ((_layout & TEXT_LAYOUT_BIT) != 0)
  {
    SetGraphicsMode(_state, _host);
    return ScreenChange::ModeSet;
  }
  return ScreenChange::Cleared;
}

} // namespace

std::span<std::uint8_t, DRAWING_BUFFER_BYTES> DrawingBuffer(GameState& _state) noexcept
{
  return BytesOf(_state.Data().drawing.drawingBuffer);
}

[[nodiscard]] Task<void> FinishSpaceViewFrame(GameState& _state, Host& _host)
{
  // The sights, then the copy and the clear, both forwards. The sights are watched for
  // the space view, which the renderer draws in their place (ADR-035).
  _host.BeginSpaceViewObject();
  DrawLaserSights(_state);
  _host.EndSpaceViewRendered();
  co_await PresentSpaceView(_state, _host);
  ClearDrawBuffer(_state);
}

[[nodiscard]] Task<void> PresentSpaceView(GameState& _state, Host& _host)
{
  // msSinceFrame, compared with minimumFrameMs until it is no longer below it, while the timer interrupt counts
  // msSinceFrame up.
  while (_state.Data().flow.msSinceFrame < _state.Data().flight.minimumFrameMs)
  {
    co_await _host.Wait();
  }
  _state.Data().flow.msSinceFrame = 0;
  co_await WaitRetraceThenDelay(_host);
  // The view presented, where the original copied the buffer's line pairs to video memory (ADR-069).
  _host.PresentSpaceView(DrawingBuffer(_state));
}

void ClearDrawBuffer(GameState& _state)
{
  // Words of 0 through the whole drawing buffer from its byte 0.
  (void)RepeatStoreBufferWords(_state, 0, DRAW_BUFFER_WORDS, 0, 2);
}

void PlotPixel(GameState& _state, std::uint8_t _x, std::uint8_t _row)
{
  // The pixel's two bits in the colour's fill byte, into the byte at row * 64 + x / 4, the rest of it kept.
  const std::uint8_t pixel = RotateRight(LEFT_PIXEL_MASK, static_cast<unsigned>((_x & 3) << 1));
  const auto color = static_cast<std::uint8_t>(pixel & ColorFillByte(_state.Data().drawing.drawColor));
  Plot(_state, static_cast<std::uint16_t>(Join(_row, _x) >> 2), static_cast<std::uint8_t>(~pixel), color);
}

void DrawClippedLine(GameState& _state, std::uint16_t _fromX, std::uint16_t _fromRow, std::uint16_t _toX, std::uint16_t _toRow)
{
  // The rows doubled, so that 0-127 spans 0-255 as x does. The line is held as its ends, the first the to end and the second
  // the from end, x the coordinate cut first; outside counts the ends with a coordinate outside 0-255.
  ClipLine line{_toX, static_cast<std::uint16_t>(_toRow << 1), _fromX, static_cast<std::uint16_t>(_fromRow << 1), 0};
  const bool firstOutside = (High(line.firstAlong) | High(line.firstCut)) != 0;
  const bool secondOutside = (High(line.secondAlong) | High(line.secondCut)) != 0;
  line.outside = static_cast<std::uint16_t>((firstOutside ? 1 : 0) + (secondOutside ? 1 : 0));
  // DrawLine on the ends as they stand.
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
  // The first end's sign on the cut axis, then the second's.
  if (Negative(_line.firstCut))
  {
    if (!Negative(_line.secondCut))
    {
      return MoveEndpointToEdge(_line);
    }
    // Both below the edge: nothing to draw, and not all inside.
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
  // The second end's sign on the cut axis, then the first's.
  if (Negative(_line.secondCut))
  {
    if (!Negative(_line.firstCut))
    {
      return MoveEndpointToEdge(_line);
    }
    // Both inside: still to draw, though not counted all inside.
    return ClipStep{_line, true, false};
  }
  if (Negative(_line.firstCut))
  {
    return SwapThenCutLine(_line);
  }
  // Both beyond 255: nothing to draw, all inside when the first end's high byte is 0.
  return ClipStep{_line, false, High(_line.firstCut) == 0};
}

void DrawLine(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromRow, std::uint8_t _toX, std::uint8_t _toRow)
{
  std::uint8_t x = _fromX;
  std::uint8_t row = _fromRow;
  std::uint8_t endX = _toX;
  std::uint8_t endRow = _toRow;
  LinePen pen{};
  // The pixels across and the rows down, with the endpoints swapped so that the line runs right, and the row step.
  pen.across = static_cast<std::uint8_t>(endX - x);
  if (endX < x)
  {
    pen.across = Negate8(pen.across);
    std::swap(x, endX);
    std::swap(row, endRow);
  }
  pen.rowStep = ROW_BYTES;
  pen.down = static_cast<std::uint8_t>(endRow - row);
  if (endRow < row)
  {
    pen.down = Negate8(pen.down);
    pen.rowStep = Negate(pen.rowStep);
  }
  pen.at = static_cast<std::uint16_t>(Join(row, x) >> 2);
  pen.fill = ColorFillByte(_state.Data().drawing.drawColor);
  // The pixel's colour in place, and the mask that keeps the rest of its byte.
  const std::uint8_t pixel = RotateRight(LEFT_PIXEL_MASK, static_cast<unsigned>((x & 3) << 1));
  pen.keep = static_cast<std::uint8_t>(~pixel);
  pen.pixel = static_cast<std::uint8_t>(pen.fill & pixel);
  pen.held = BufferByte(_state, pen.at);
  if (pen.down == 0)
  {
    DrawHorizontalLine(_state, pen);
  }
  else if (pen.across == 0)
  {
    DrawVerticalLine(_state, pen);
  }
  else if (pen.across < pen.down)
  {
    DrawSteepLine(_state, pen);
  }
  else if (pen.across != pen.down)
  {
    DrawShallowLine(_state, pen);
  }
  else
  {
    DrawDiagonalLine(_state, pen);
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
  // The step through circleProfile a row, 20000h/rows as 8.8 (FFFFh for 2 rows, which would overflow), its bytes exchanged: the
  // fraction's low byte keeps the quotient's high byte, and the whole part is that byte alone.
  constexpr std::uint32_t PROFILE_BYTES_8_8 = 0x20000;
  const std::uint16_t perRow = rows == 2 ? std::uint16_t{0xFFFF} : DivideWord(PROFILE_BYTES_8_8, rows).quotient;
  const std::uint16_t step = SwapBytes(perRow);
  _state.Data().video.discProfileStepFraction = step;
  _state.Data().video.discProfileStepWhole = Low(step);
  // lowerRow walks down from the centre row and upperRow up, both as 2*row; the centre row is drawn once.
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
  // The bytes between the ends: a byte store for an odd one, then word stores.
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
  // The first octant, circleOctant's eight bytes scaled by the radius / 128 (the product doubled, its high byte), a byte of 0
  // staying 0, as four points, x then row.
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
  // Every point moved to the centre.
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
  // The row's offset plus the left x over 4, which is below 40h, so its sign extension adds nothing.
  const auto leftByte = static_cast<std::uint8_t>(_left >> 2);
  auto at = static_cast<std::uint16_t>(_row + leftByte);
  const auto bytes = static_cast<std::uint8_t>((_right >> 2) - leftByte);
  const std::uint16_t leftMask = TriangleEdgeMask(_left, false);
  if (bytes == 0)
  {
    // FillTriangleSpanOneByte: both edges in one byte.
    const std::uint16_t rightMask = TriangleEdgeMask(_right, true);
    Plot(_state, at, static_cast<std::uint8_t>(High(leftMask) | High(rightMask)),
         static_cast<std::uint8_t>(Low(leftMask) & Low(rightMask) & _fill));
    return;
  }
  Plot(_state, at, High(leftMask), static_cast<std::uint8_t>(Low(leftMask) & _fill));
  ++at;
  // The bytes between: a byte store to reach an even offset, then word stores, then a byte store for an odd one left.
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
  // The rows doubled, so that a row on the buffer, 0-127, has a high byte of 0 as an x does.
  const auto doubled = [](ScreenPoint _point)
  { return ScreenPoint{_point.x, static_cast<std::int16_t>(static_cast<std::uint16_t>(_point.y) << 1)}; };
  const Triangle rowsDoubled{doubled(_triangle.first), doubled(_triangle.second), doubled(_triangle.third)};
  const auto word = [](std::int16_t _value) { return static_cast<std::uint16_t>(_value); };
  // Every x and doubled row combined bit by bit: a high byte set in any of them is off the buffer.
  const auto any = static_cast<std::uint16_t>(word(rowsDoubled.first.x) | word(rowsDoubled.second.y) | word(rowsDoubled.third.y));
  const auto high = static_cast<std::uint8_t>(High(any) | High(word(rowsDoubled.third.x)) | High(word(rowsDoubled.second.x)) |
                                              High(word(rowsDoubled.first.y)));
  if (high != 0)
  {
    FillClippedTriangle(_state, rowsDoubled);
    return;
  }
  // Each corner's x and row as bytes, the rows halved back.
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
  // Every x left of 0, their sign bits all set; then every doubled row above 0.
  if (Negative(static_cast<std::uint16_t>(ax & bx & cx)) || Negative(static_cast<std::uint16_t>(dx & bp & di)))
  {
    return;
  }
  // Every x right of 255: each high byte positive.
  if (Signed8(High(ax)) > 0 && Signed8(High(bx)) > 0 && Signed8(High(cx)) > 0)
  {
    return;
  }
  // Every doubled row below 255: A's high byte positive, B's and C's rows, signed, at least 100h.
  if (Signed8(High(dx)) > 0 && Signed(bp) >= 0x100 && Signed(di) >= 0x100)
  {
    return;
  }
  // PrepareClippedTriangle: the rows halved, keeping their signs; the original then aims its string stores at the data segment
  // and patches in the adding forms of both edges' steps.
  const auto halved = [](ScreenPoint _point) { return ScreenPoint{_point.x, static_cast<std::int16_t>(_point.y >> 1)}; };
  ScreenPoint a = halved(_doubled.first);
  ScreenPoint b = halved(_doubled.second);
  ScreenPoint c = halved(_doubled.third);
  // Sorted by row into the four cases by exchanges, A and B, then A and C, as signed words.
  if (a.y == b.y)
  {
    // A's row compared with C's before A and C are exchanged, the branch on that compare after.
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

[[nodiscard]] Task<void> WaitRetraceThenDelay(Host& _host)
{
  // The retrace's loop on port 3DAh, then the delay's, which counts down from 7D0h to 0: the
  // 8088's speed, which takes no paced time.
  co_await WaitForRetrace(_host);
}

ScreenChange ShowCockpitScreen(GameState& _state, Host& _host)
{
  const std::uint8_t layout = _state.Data().drawing.screenLayout;
  if (layout == 0)
  {
    return ScreenChange::None;
  }
  const ScreenChange change = MakeGraphicsScreen(_state, _host, layout);
  _state.Data().drawing.screenLayout = 0;
  return change;
}

ScreenChange DrawChartFrame(GameState& _state, Host& _host)
{
  const std::uint8_t layout = _state.Data().drawing.screenLayout;
  if (layout == CHART_LAYOUT)
  {
    return ScreenChange::None;
  }
  const ScreenChange change = MakeGraphicsScreen(_state, _host, layout);
  _state.Data().drawing.screenLayout = CHART_LAYOUT;
  return change;
}

void SetGraphicsMode(GameState& _state, Host& _host)
{
  (void)_state;
  _host.Screen().SetColorSelect(BRIGHT_ON_BLACK);
}

void SetTextMode(GameState& _state, Host& _host)
{
  (void)_state;
  _host.LeaveSpaceView();
  _host.Screen().SetColorSelect(COLOR_SELECT_AFTER_MODE);
}

void DrawTitlePlanet(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow)
{
  _state.Data().drawing.sunFringeMask = 1;
  DrawDisc(_state, _radius, _centerX, _centerRow);
  _state.Data().drawing.sunFringeMask = 0;
}

} // namespace Elite
