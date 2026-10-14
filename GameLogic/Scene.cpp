#include "pch.h"

#include "Scene.h"

#include "Arithmetic.h"
#include "Combat.h"
#include "Equipment.h"
#include "Maths.h"
#include "SceneTables.h"
#include "SharedTables.h"
#include "Text.h"
#include "Video.h"

#include <algorithm>
#include <initializer_list>
#include <optional>

namespace Elite
{

namespace
{

// The ship slots DetonateEnergyBomb's loop looks at begin at firstShipSlot, after the sun, the planet and the station: its count is
// objectSlotCount less these.
constexpr std::uint8_t BOMB_FIRST_SLOT = 3;

// What DivideOverflowInterrupt makes of each divide here, by the byte two before the instruction after it. ProjectVertices'
// divides, by the vertex's z, take a memory operand, so the handler reads their ModRM byte 74h and saturates only the
// quotient's low byte; ProjectDiscCoordinate's, by the slot's view z, has ModRM byte 75h, and ProjectToScreen's, by a
// register, a word divide's opcode.
constexpr DivideTrap VERTEX_DIVIDE_TRAP = DivideTrap::LowByte;
constexpr DivideTrap DISC_DIVIDE_TRAP = DivideTrap::Word;
constexpr DivideTrap SCREEN_DIVIDE_TRAP = DivideTrap::Word;

// Byte 0 of a slot, with Ships.h's SLOT_ACTIVE.
constexpr std::uint8_t SLOT_TYPE_BITS = 0x3E;
constexpr std::uint8_t SLOT_IN_RANGE = 0x40;
constexpr std::uint8_t SLOT_VISIBLE = 0x80;
constexpr std::uint8_t SLOT_KEEP_BITS = 0x3F;
constexpr std::uint8_t SLOT_DRAWABLE = SLOT_VISIBLE | SLOT_ACTIVE;
// Byte +1Eh, the flags.
constexpr std::uint8_t STATE_DRAWN = 0x80;
constexpr std::uint8_t STATE_FLASH_OFF = 0x40;
constexpr std::uint8_t STATE_FLASHING = 0x20;
constexpr std::uint8_t STATE_BLIP_SHOWN = 0x02;
constexpr std::uint8_t FLASH_OFF_FRAMES = 0x14;
constexpr std::uint8_t FLASH_ON_FRAMES = 0x19;

// The top byte of a dividend ScaledDividend makes: the magnitude's sign.
constexpr std::uint8_t HIGH_BYTE_ONES = 0xFF;
constexpr std::uint8_t HIGH_BYTE_ZERO = 0x00;

constexpr std::uint16_t SCREEN_CENTER_X = 0x80;
constexpr std::uint16_t SCREEN_CENTER_Y = 0x40;
constexpr std::uint16_t SCREEN_RIGHT = 0xFF;
constexpr std::uint16_t SCREEN_BOTTOM = 0x7F;
constexpr std::uint16_t NEAR_VERTEX_X = 0x8000; // ProjectVertices' mark for a vertex behind nearPlaneZ
constexpr std::uint16_t OFF_SCREEN = 0x7FFF;    // what DrawVisibleFaces skips: the divide trap's saturation
constexpr std::size_t POINT_BYTES = 4;
constexpr std::size_t VERTEX_BYTES = 6;
constexpr std::uint16_t VERTEX_Z = 4;
// What a vertex program reaches: vertexBuffer's 42 vertices. Its six bits of vertex could name 64, past the buffer into the bytes
// after it, which the state no longer holds beside it (D38); TableTests checks that no blueprint's does.
constexpr std::size_t VERTEX_REACH = sizeof(SceneState::vertexBuffer);
constexpr std::uint8_t VERTEX_INDEX_BITS = 0x3F;
constexpr std::uint8_t VERTEX_OP_BITS = 0xC0;
constexpr std::uint8_t VERTEX_OP_LOAD = 0x00;
constexpr std::uint8_t VERTEX_OP_STORE = 0x40;
constexpr std::uint8_t VERTEX_OP_AVERAGE = 0x80;
constexpr std::uint8_t FACE_TRIANGLE = 0x80;
constexpr std::uint16_t TRIANGLE_ITEM_BYTES = 4;
constexpr std::uint16_t EDGE_VERTEX_BITS = 0xFCFC;
constexpr std::uint8_t EDGE_COLOR_BITS = 0x03;
constexpr std::uint8_t DOT_COLOR = 3;
constexpr std::uint16_t DOT_RADIUS = 2;
constexpr std::uint16_t DISTANT_STATION_Z = 0x1B58;
constexpr std::uint16_t SQUARED_DISTANCE_SHIFT = 6;
constexpr std::uint8_t DISTANT_DEPTH = 2;
constexpr std::uint16_t PLANET_SCALE = PLANET_DISC_SCALE;
constexpr std::uint16_t SUN_SCALE = SUN_DISC_SCALE;
constexpr std::uint8_t ALTITUDE_LIMIT = 0x7F;
constexpr std::uint16_t RADIUS_LIMIT = 0xFF;
constexpr std::uint16_t FATAL_RADIUS = 0xFD;
constexpr std::uint16_t SCOOP_RADIUS = 0xC3;
constexpr std::uint16_t FRINGE_RADIUS_LARGE = 0xB4;
constexpr std::uint16_t FRINGE_RADIUS_SMALL = 0x28;
constexpr std::uint8_t FRINGE_NONE = 0;
constexpr std::uint8_t FRINGE_SMALL = 1;
constexpr std::uint8_t FRINGE_MEDIUM = 3;
constexpr std::uint8_t FRINGE_LARGE = 7;
constexpr std::uint8_t SCOOP_FUEL = 6;
constexpr std::uint16_t SCOOP_MESSAGE_FRAMES = 5;

// DrawDistantStation: a station's disc, by its depth byte +25h.
constexpr std::uint8_t STATION_COLOR = 3;
constexpr std::uint8_t NEAR_STATION_DEPTH = 0x14;
constexpr std::uint16_t NEAR_STATION_RADIUS = 0x0E;
constexpr std::uint8_t STATION_DEPTH_BASE = 0x25;
constexpr std::uint8_t STATION_DEPTH_SMALLEST = 4;

// The blueprint handlers' vertices: the angle tables' words, a 2048-entry turn, and the vertices written.
constexpr std::uint16_t ANGLE_BITS = 0x7FF;
constexpr std::uint16_t SINE_BYTE_BITS = 0xFFF;
constexpr std::uint16_t QUARTER_TURN_BYTES = 0x400;
constexpr std::uint16_t DODO_RING_STEP_BYTES = 0x734; // 72 degrees on, from the cosine back to a sine
constexpr std::uint16_t DODO_SLOT_START_BYTES = 0x1FC;
constexpr std::uint16_t HALF_TURN_BYTES = 0x800;
constexpr std::uint16_t DODO_RING_VERTICES = 5;
constexpr std::uint16_t DODO_SLOT_VERTICES = 2;
constexpr std::uint16_t DODO_ROTATED_VERTICES = 14;
constexpr std::uint16_t DODO_REFLECTED_VERTICES = 10;
constexpr std::uint16_t DODO_SLOT_CORNERS = 4;
constexpr std::uint16_t DODO_OUTER_RING = 0x1E; // the ring of radius 3.80, five vertices on
constexpr std::uint16_t DODO_NEAR_RING_Z = 0xC4;
constexpr std::uint16_t DODO_FAR_RING_Z = 0x2E;
constexpr std::uint16_t DODO_SLOT_Z = 0xFF3C;
constexpr std::uint16_t DODO_SLOT_OPPOSITE = 0x0C; // the slot's corner two vertices on, reflected
constexpr std::size_t DODO_REFLECTED = 14 * VERTEX_BYTES;
constexpr std::uint16_t BOX_CORNERS_ROTATED = 3;
constexpr std::uint16_t BOX_FOURTH_CORNER = 0x12;
constexpr std::size_t BOX_FIRST_CORNER = 34 * VERTEX_BYTES;
constexpr std::size_t BOX_SECOND_CORNER = 35 * VERTEX_BYTES;
constexpr std::size_t BOX_FIFTH_CORNER = 38 * VERTEX_BYTES;
constexpr std::size_t BOX_SEVENTH_CORNER = 40 * VERTEX_BYTES;
constexpr std::size_t BOX_EIGHTH_CORNER = 41 * VERTEX_BYTES;
constexpr std::size_t BOX_REFLECTION_STEP = 0x18; // four vertices
constexpr std::size_t BOX_REFLECTION_SKIP = 0x12; // three vertices

// The vertices of vertexBuffer, as far as a vertex program reaches (VERTEX_REACH): a vertex is three words, x, y and z, and
// ProjectVertices makes the first ones points of two, x and y, in place. Each routine holds a vertex as the byte it starts at,
// where the original holds its address, as the blueprints do.
[[nodiscard]] std::span<std::uint16_t> Vertices(GameState& _state)
{
  return _state.Data().scene.vertexBuffer;
}

// The word of the vertices that starts at their byte _byte, which is even: every byte a blueprint holds of them is
// (TableTests), and every byte the routines step to.
[[nodiscard]] std::uint16_t VertexWord(std::span<const std::uint16_t> _words, std::size_t _byte) noexcept
{
  return _words[_byte / 2];
}

// _value into the word of the vertices that starts at their byte _byte, which is even.
void SetVertexWord(std::span<std::uint16_t> _words, std::size_t _byte, std::uint16_t _value) noexcept
{
  _words[_byte / 2] = _value;
}

// The high byte of sineTable's word at byte _angle, which is even: a signed byte.
[[nodiscard]] std::uint8_t SineHigh(std::uint16_t _angle)
{
  return High(SINE_TABLE[_angle >> 1]);
}

// The handlers' rotations by the drawn object's angles: -roll into rotation pair 3, yaw into 4, and pitch with the
// player's into 5.
void SetDrawAngles(GameState& _state)
{
  (void)SetSinCos(_state, 3, Negate(_state.Data().scene.drawRollAngle));
  (void)SetSinCos(_state, 4, _state.Data().scene.drawYawAngle);
  (void)SetSinCos(_state, 5, static_cast<std::uint16_t>(_state.Data().scene.drawPitchAngle + _state.Data().playerShip.playerPitchAngle));
}

[[nodiscard]] bool Negative(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0;
}

// The magnitude, by two's complement: 8000h stays 8000h.
[[nodiscard]] std::uint16_t Magnitude(std::uint16_t _value) noexcept
{
  return Negative(_value) ? Negate(_value) : _value;
}

// LOOP: decrements the counter, and says whether to go round again.
[[nodiscard]] bool Loop(std::uint16_t& _counter) noexcept
{
  _counter = static_cast<std::uint16_t>(_counter - 1);
  return _counter != 0;
}

// Whether a word add, or a word subtract, overflows as signed.
[[nodiscard]] bool AddOverflows(std::uint16_t _a, std::uint16_t _b) noexcept
{
  const auto sum = static_cast<std::uint16_t>(_a + _b);
  return Negative(static_cast<std::uint16_t>((_a ^ sum) & (_b ^ sum)));
}

[[nodiscard]] bool SubtractOverflows(std::uint16_t _a, std::uint16_t _b) noexcept
{
  const auto difference = static_cast<std::uint16_t>(_a - _b);
  return Negative(static_cast<std::uint16_t>((_a ^ _b) & (_a ^ difference)));
}

// A rotation loop of the blueprint handlers: _count vertices from _first, the coordinates at byte offsets _a and _b of each
// rotated by rotation pair _pair (RotateBySinCosN) and written back, the first, then the second. Nothing reads what the
// original's loop leaves behind: its place past the vertices, a count of 0, and the last vertex's two coordinates.
void RotateVertices(GameState& _state, std::size_t _first, std::uint16_t _count, std::uint16_t _a, std::uint16_t _b, std::size_t _pair)
{
  const std::span<std::uint16_t> vertices = Vertices(_state);
  std::size_t vertex = _first;
  for (std::uint16_t left = _count; left != 0; --left)
  {
    const std::size_t first = vertex + _a;
    const std::size_t second = vertex + _b;
    const Pair rotated = RotateByStoredSinCos(
      _state, _pair, Pair{static_cast<std::int16_t>(VertexWord(vertices, first)), static_cast<std::int16_t>(VertexWord(vertices, second))});
    SetVertexWord(vertices, first, static_cast<std::uint16_t>(rotated.first));
    SetVertexWord(vertices, second, static_cast<std::uint16_t>(rotated.second));
    vertex += VERTEX_BYTES;
  }
}

// The handlers' last rotation, by the view direction when it is not ahead: -viewAngle into rotation
// pair 8, and the x and z of _count vertices from _first turned by it. Nothing reads the last x and z the original leaves
// behind: the box's handler loads a fresh first coordinate next, and ReflectVertexAboutCenter loads both before reading them.
void RotateVerticesToView(GameState& _state, std::size_t _first, std::uint16_t _count)
{
  const std::uint16_t angle = _state.Data().playerShip.viewAngle;
  if (angle == 0)
  {
    return;
  }
  (void)SetSinCos(_state, 8, Negate(angle));
  RotateVertices(_state, _first, _count, 0, VERTEX_Z, 8);
}

// RenderBlueprint, what RenderBlueprintBody does with _blueprint's body: the vertex program from _accumulator, the
// projection, the edge lists and the faces. Each count is a byte written into a word's low half and stored as the whole word,
// whose high byte is 0, as the loops before it leave it, or what the edge count's doubling carried there.
void RenderBlueprint(GameState& _state, const Blueprint& _blueprint, Vector _accumulator)
{
  RunVertexProgram(_state, _blueprint.vertexProgram, _accumulator);
  std::uint16_t count = 0;
  SetLow(count, _blueprint.projectedVertices);
  _state.Data().scene.projectedVertexCount = count;
  ProjectVertices(_state);
  count = 0;
  // Fixed edges, then face edges: a count byte, then two bytes for each.
  SetLow(count, static_cast<std::uint8_t>(_blueprint.fixedEdges.size() / 2));
  count = static_cast<std::uint16_t>(count << 1);
  // The original keeps the edge lists' addresses in fixedEdgeList and faceEdgeList for its edge routines; native code hands
  // them _blueprint, and nothing reads the two.
  SetLow(count, static_cast<std::uint8_t>(_blueprint.faceEdges.size() / 2));
  count = static_cast<std::uint16_t>(count << 1);
  SetLow(count, _blueprint.faces);
  DrawVisibleFaces(_state, _blueprint, count);
}

// _magnitude * 256 as a 32-bit dividend, its top byte the word's sign: what ProjectVertices and DrawSunOrPlanetDisc divide. The
// top byte is FFh for the one magnitude negation leaves negative, 8000h, which overflows the divide.
[[nodiscard]] std::uint32_t ScaledDividend(std::uint16_t _magnitude) noexcept
{
  return (std::uint32_t{Negative(_magnitude) ? HIGH_BYTE_ONES : HIGH_BYTE_ZERO} << 24) | (std::uint32_t{_magnitude} << 8);
}

// One coordinate of a vertex: 256|_value| over _z, the vertex's z, plus one when the quotient is below twice the remainder,
// doubled in 16 bits, with _value's sign put back.
[[nodiscard]] std::uint16_t ProjectVertexCoordinate(std::uint16_t _value, std::uint16_t _z)
{
  const bool negative = Negative(_value);
  const std::uint16_t magnitude = negative ? Negate(_value) : _value;
  const WordQuotient divided = DivideUnsigned(ScaledDividend(magnitude), _z, VERTEX_DIVIDE_TRAP);
  std::uint16_t coordinate = divided.quotient;
  if (coordinate < static_cast<std::uint16_t>(divided.remainder << 1))
  {
    coordinate = Offset(coordinate, 1);
  }
  return negative ? Negate(coordinate) : coordinate;
}

// The bytes LoadTriangle takes, one a vertex.
constexpr std::uint16_t TRIANGLE_VERTEX_BYTES = 3;

// The three vertex bytes at _indices of a face list, each shifted left by _shift, as a byte, into the byte of vertexBuffer a
// point starts at, and the points there. The second's x goes through faceTestScratch, written before its y is read, as the
// original keeps it there.
[[nodiscard]] Triangle LoadTriangle(GameState& _state, std::span<const std::uint8_t> _list, std::uint16_t _indices, std::uint8_t _shift)
{
  const std::span<const std::uint16_t> points = Vertices(_state);
  const auto vertex = [_list, _indices, _shift](std::uint16_t _which)
  { return std::size_t{static_cast<std::uint8_t>(_list[Offset(_indices, _which)] << _shift)}; };
  const auto coordinate = [points](std::size_t _vertex, std::size_t _bytes)
  { return static_cast<std::int16_t>(VertexWord(points, _vertex + _bytes)); };
  const std::size_t first = vertex(0);
  const ScreenPoint firstPoint{coordinate(first, 0), coordinate(first, 2)};
  const std::size_t second = vertex(1);
  const std::int16_t secondX = coordinate(second, 0);
  const ScreenPoint secondPoint{secondX, coordinate(second, 2)};
  const std::size_t third = vertex(2);
  return Triangle{firstPoint, secondPoint, ScreenPoint{coordinate(third, 0), coordinate(third, 2)}};
}

// The two products TriangleWindingSign compares, each a whole 32-bit signed product.
struct WindingProducts
{
  std::uint32_t first;  // (x0-x1)(y2-y1)
  std::uint32_t second; // (y0-y1)(x2-x1)
};

// Each difference in 16 bits, wrapping, then their signed product.
[[nodiscard]] WindingProducts Winding(const Triangle& _triangle) noexcept
{
  const auto difference = [](std::int16_t _a, std::int16_t _b)
  { return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint16_t>(_a) - static_cast<std::uint16_t>(_b))); };
  const auto product = [](std::int16_t _a, std::int16_t _b) { return static_cast<std::uint32_t>(std::int32_t{_a} * _b); };
  return WindingProducts{product(difference(_triangle.first.x, _triangle.second.x), difference(_triangle.third.y, _triangle.second.y)),
                         product(difference(_triangle.first.y, _triangle.second.y), difference(_triangle.third.x, _triangle.second.x))};
}

// An edge item: _item, the edge's offset in _blueprint's face edges, which the original's faceEdgeList points at; the edge's colour from
// its high byte's low bits, then DrawClippedLine from its second vertex to its first, unless either's x is 7FFFh, the trap's
// saturation.
void DrawFaceEdge(GameState& _state, const Blueprint& _blueprint, std::uint8_t _item)
{
  const std::uint16_t edge = Join(_blueprint.faceEdges[_item + 1u], _blueprint.faceEdges[_item]);
  _state.Data().drawing.drawColor = static_cast<std::uint8_t>(High(edge) & EDGE_COLOR_BITS);
  const auto vertices = static_cast<std::uint16_t>(edge & EDGE_VERTEX_BITS);
  const std::span<const std::uint16_t> points = Vertices(_state);
  const std::size_t to = Low(vertices);
  const std::size_t from = High(vertices);
  const std::uint16_t toX = VertexWord(points, to);
  const std::uint16_t fromX = VertexWord(points, from);
  if (toX == OFF_SCREEN || fromX == OFF_SCREEN)
  {
    return;
  }
  DrawClippedLine(_state, fromX, VertexWord(points, from + 2), toX, VertexWord(points, to + 2));
}

// A triangle item: _item, the offset of its pattern in faceFillPatterns, into triangleFillPattern, then the three
// vertex bytes at _vertices of _list: FillTriangle unless a corner's x is 7FFFh. Returns the offset past the vertex bytes.
[[nodiscard]] std::uint16_t DrawFaceTriangle(GameState& _state, std::uint8_t _item, std::span<const std::uint8_t> _list,
                                             std::uint16_t _vertices)
{
  // _item is even, and inside the table: TableTests checks every triangle item's.
  _state.Data().scene.triangleFillPattern = FACE_FILL_PATTERNS[_item / 2];
  const Triangle triangle = LoadTriangle(_state, _list, _vertices, 1);
  const auto offScreen = [](ScreenPoint _point) { return static_cast<std::uint16_t>(_point.x) == OFF_SCREEN; };
  if (!offScreen(triangle.first) && !offScreen(triangle.second) && !offScreen(triangle.third))
  {
    FillTriangle(_state, triangle);
  }
  return Offset(_vertices, TRIANGLE_VERTEX_BYTES);
}

// A facing face's items, at _items in _blueprint's face list: a count byte, taken as a word, then the items, an edge's one byte
// counted off by one and a triangle's four by four, the count ending only when it lands on 0; one that steps past it wraps and
// runs on. Returns the offset in the list past them.
[[nodiscard]] std::uint16_t DrawFaceItems(GameState& _state, const Blueprint& _blueprint, std::uint16_t _items)
{
  const std::span<const std::uint8_t> list = _blueprint.faceList;
  std::uint16_t next = Offset(_items, 1);
  std::uint16_t left = list[_items];
  for (;;)
  {
    const std::uint8_t item = list[next];
    next = Offset(next, 1);
    // Bit 7 says a triangle, and the rest is the offset.
    const auto offset = static_cast<std::uint8_t>(item & ~FACE_TRIANGLE);
    if ((item & FACE_TRIANGLE) == 0)
    {
      DrawFaceEdge(_state, _blueprint, offset);
      if (!Loop(left))
      {
        return next;
      }
    }
    else
    {
      next = DrawFaceTriangle(_state, offset, list, next);
      left = static_cast<std::uint16_t>(left - TRIANGLE_ITEM_BYTES);
      if (left == 0)
      {
        return next;
      }
    }
  }
}

// The magnitude doubled, in 16 bits: what ClassifyViewPosition compares with z.
[[nodiscard]] std::uint16_t DoubledMagnitude(std::uint16_t _value) noexcept
{
  return static_cast<std::uint16_t>(Magnitude(_value) << 1);
}

// ClassifyViewPosition, the shared tail of TransformShip and ClassifyStationPosition: _view is _slot's view
// position. Visible, with byte 0 bit 7 set, when z is at least nearClipZ and twice |x| and twice |y| are at most z; the
// position is stored, in the slot and as drawCenter, when z passes.
void ClassifyViewPosition(GameState& _state, SpaceObject& _slot, Vector _view)
{
  const auto x = static_cast<std::uint16_t>(_view.x);
  const auto y = static_cast<std::uint16_t>(_view.y);
  const auto z = static_cast<std::uint16_t>(_view.z);
  if (Negative(z) || z < NEAR_CLIP_Z)
  {
    return;
  }
  _slot.viewX = x;
  _state.Data().scene.drawCenterX = x;
  _slot.viewY = y;
  _state.Data().scene.drawCenterY = y;
  _slot.viewZ = z;
  _state.Data().scene.drawCenterZ = z;
  if (z < DoubledMagnitude(x) || z < DoubledMagnitude(y))
  {
    return;
  }
  _slot.type = static_cast<std::uint8_t>(_slot.type | SLOT_VISIBLE);
}

// A station's compass position: the slot's words at +20h, +22h and +24h.
[[nodiscard]] Vector CompassPosition(SpaceObject& _slot) noexcept
{
  return Vector{static_cast<std::int16_t>(_slot.compassX), static_cast<std::int16_t>(_slot.compassY),
                static_cast<std::int16_t>(_slot.compassZ)};
}

// An object's view position: the slot's words at +10h, +12h and +14h.
[[nodiscard]] Vector ViewPosition(SpaceObject& _slot) noexcept
{
  return Vector{static_cast<std::int16_t>(_slot.viewX), static_cast<std::int16_t>(_slot.viewY), static_cast<std::int16_t>(_slot.viewZ)};
}

// RotateToViewDirection, the shared tail of the two TransformToView entries: _view with (x, z) rotated by -viewAngle
// through rotation pair 8, which it sets, when the view is not the front one. The position is kept round SetSinCos8 and
// comes back as it went in.
[[nodiscard]] Vector RotateToViewDirection(GameState& _state, Vector _view)
{
  const std::uint16_t viewAngle = _state.Data().playerShip.viewAngle;
  if (viewAngle == 0)
  {
    return _view;
  }
  (void)SetSinCos(_state, 8, Negate(viewAngle));
  const Pair xz = RotateByStoredSinCos(_state, 8, Pair{_view.x, _view.z});
  return Vector{xz.first, _view.y, xz.second};
}

// An unsigned word times itself: the whole 32-bit product.
[[nodiscard]] std::uint32_t UnsignedSquare(std::uint16_t _value) noexcept
{
  return std::uint32_t{_value} * _value;
}

// CheckShipInRange's test once IsObjectNear has passed: |x|, |y| and |z| below maxAxisDistance, then the high words
// of their unsigned squares, summed, below maxDistanceSquaredHigh after the second and the third. Within, the sum shifted
// right 6 is the ship's size, and byte 0 bit 6 is set.
[[nodiscard]] ShipRange ShipWithinRange(SpaceObject& _slot)
{
  ShipRange range{};
  constexpr std::array<ObjectWord, 3> POSITION = {&SpaceObject::x, &SpaceObject::y, &SpaceObject::z};
  for (std::size_t axis = 0; axis < POSITION.size(); ++axis)
  {
    range.magnitudes[axis] = Magnitude(_slot.*POSITION[axis]);
    if (range.magnitudes[axis] >= MAX_AXIS_DISTANCE)
    {
      return range;
    }
  }
  for (std::size_t axis = 0; axis < POSITION.size(); ++axis)
  {
    range.squaredHigh = Offset(range.squaredHigh, static_cast<std::uint16_t>(UnsignedSquare(range.magnitudes[axis]) >> 16));
    if (axis != 0 && range.squaredHigh >= MAX_DISTANCE_SQUARED_HIGH)
    {
      return range;
    }
  }
  range.within = true;
  _slot.size = Low(static_cast<std::uint16_t>(range.squaredHigh >> SQUARED_DISTANCE_SHIFT));
  _slot.type = static_cast<std::uint8_t>(_slot.type | SLOT_IN_RANGE);
  return range;
}

// ClassifyObject, one slot of TransformAndDrawObjects' first pass: _slot's drawn bit (+1Eh bit 7) cleared; an active
// object's visible and in-range bits cleared, and one off the scanner counted a frame further, removed (RemoveObject) on the
// 255th; then the sun or the planet transformed (TransformSunOrPlanet), a ship in range transformed (CheckShipInRange,
// TransformShip), and the station classified at its compass position (ClassifyStationPosition) when UpdateCompass has made it the
// compass's target and its depth is below 2. Returns the slot the original's pass is on after it, from which it steps on:
// UpdateCompass, once it runs, leaves it at stationSlot; every other callee keeps it.
[[nodiscard]] SlotIndex ClassifyObject(GameState& _state, SpaceObject& _slot)
{
  _slot.flags = static_cast<std::uint8_t>(_slot.flags & ~STATE_DRAWN);
  if ((_slot.type & SLOT_ACTIVE) == 0)
  {
    return _state.IndexOf(_slot);
  }
  _slot.type = static_cast<std::uint8_t>(_slot.type & SLOT_KEEP_BITS);
  if (_slot.FramesAway() != 0 && (_slot.flags & STATE_BLIP_SHOWN) == 0)
  {
    // Off the scanner: despawned on the 255th frame.
    _slot.FramesAway() = static_cast<std::uint8_t>(_slot.FramesAway() + 1);
    if (_slot.FramesAway() == 0)
    {
      (void)RemoveObject(_state, _slot);
      return _state.IndexOf(_slot);
    }
  }
  if (IsSunOrPlanet(_slot))
  {
    TransformSunOrPlanet(_state, _slot);
    return _state.IndexOf(_slot);
  }
  if (!IsStation(_slot).station)
  {
    if (CheckShipInRange(_state, _slot).InRange())
    {
      TransformShip(_state, _slot);
    }
    return _state.IndexOf(_slot);
  }
  const SlotIndex station = UpdateCompass(_state) ? SlotIndex::Station : _state.IndexOf(_slot);
  SpaceObject& target = _state.Slot(station);
  if (_state.Data().flight.compassTargetIsStation != 0 && target.depth < DISTANT_DEPTH)
  {
    ClassifyStationPosition(_state, target);
  }
  return station;
}

// The station in _slot, when it is far enough away to draw as a disc: true if it is, and DrawDistantStation drew it.
// One at depth 1 nearer than 1B58h has its compass position doubled into its view position instead.
[[nodiscard]] bool DrawStationIfDistant(GameState& _state, SpaceObject& _slot)
{
  const std::uint8_t depth = _slot.depth;
  if (depth == 0)
  {
    return false;
  }
  if (depth == 1)
  {
    const std::uint16_t z = _slot.compassZ;
    if (z < DISTANT_STATION_Z)
    {
      _slot.viewZ = static_cast<std::uint16_t>(z << 1);
      _slot.viewX = static_cast<std::uint16_t>(_slot.compassX << 1);
      _slot.viewY = static_cast<std::uint16_t>(_slot.compassY << 1);
      return false;
    }
  }
  DrawDistantStation(_state, _slot);
  return true;
}

// _slot's flashing, for an object with +1Eh bit 5, the one that carries the device: true when it is in its off frames and not
// drawn.
[[nodiscard]] bool FlashedOff(SpaceObject& _slot)
{
  if ((_slot.flags & STATE_FLASHING) == 0)
  {
    return false;
  }
  const bool off = (_slot.flags & STATE_FLASH_OFF) != 0;
  _slot.FlashFrames() = static_cast<std::uint8_t>(_slot.FlashFrames() - 1);
  if (_slot.FlashFrames() != 0)
  {
    return off;
  }
  _slot.flags = static_cast<std::uint8_t>(_slot.flags ^ STATE_FLASH_OFF);
  _slot.FlashFrames() = off ? FLASH_OFF_FRAMES : FLASH_ON_FRAMES;
  return false;
}

// DrawObjectAsDot: _slot's view position projected (ProjectToScreen), as a disc of radius 2 in colour 3.
void DrawObjectAsDot(GameState& _state, SpaceObject& _slot)
{
  const ScreenPoint at = ProjectToScreen(ViewPosition(_slot));
  _state.Data().drawing.drawColor = DOT_COLOR;
  DrawDisc(_state, DOT_RADIUS, static_cast<std::uint16_t>(at.x), static_cast<std::uint16_t>(at.y));
}

// DrawObject, the visible object in _slot, not yet drawn: the sun or the planet (DrawSunOrPlanet); a station far enough
// off as a disc (DrawStationIfDistant); nothing in a flashing object's off frames; a dot (DrawObjectAsDot) when its size (+3Eh)
// passes its level of detail (+3Fh); else its blueprint, from blueprintTable by its type (RunBlueprintHandler), at its angles and
// view position. The slot is kept round the sun's, the station's and the dot's draws, and the search the draws jump back to
// loads what it reads.
void DrawObject(GameState& _state, Host& _host, SpaceObject& _slot)
{
  if (IsSunOrPlanet(_slot))
  {
    _host.NoteSpaceViewRendered(); // the renderer draws it as a sphere (ADR-035); no byte of the state is written
    DrawSunOrPlanet(_state, _slot);
    return;
  }
  if (IsStation(_slot).station && DrawStationIfDistant(_state, _slot))
  {
    return;
  }
  if (FlashedOff(_slot))
  {
    return;
  }
  if (_slot.detail < _slot.size)
  {
    DrawObjectAsDot(_state, _slot);
    return;
  }
  // Byte 0's type bits, the type doubled, index blueprintTable. The sun and the planet, types 1Eh and 1Fh, past its 30 entries,
  // were drawn above.
  const Blueprint& blueprint = BLUEPRINTS[(_slot.type & SLOT_TYPE_BITS) >> 1];
  _state.Data().scene.drawPitchAngle = std::uint16_t{_slot.pitch};
  _state.Data().scene.drawYawAngle = std::uint16_t{_slot.yaw};
  _state.Data().scene.drawRollAngle = std::uint16_t{_slot.roll};
  _state.Data().scene.drawCenterX = std::uint16_t{_slot.viewX};
  _state.Data().scene.drawCenterY = std::uint16_t{_slot.viewY};
  _state.Data().scene.drawCenterZ = std::uint16_t{_slot.viewZ};
  _host.NoteSpaceViewModel(); // for a renderer that draws the model itself (ADR-033); no byte of the state is written
  RunBlueprintHandler(_state, blueprint);
}

// DrawFarthestObject, TransformAndDrawObjects' second pass, to which every draw jumps back: of shipSlotCount slots
// (a byte count, 65,536 for 0), the visible object not yet drawn with the largest depth class (+3Dh), then
// the largest view z, marked drawn (+1Eh bit 7) and drawn (DrawObject). Returns false once no object is left.
bool DrawFarthestObject(GameState& _state, Host& _host)
{
  // The slot found, none at first; its depth and its z, from 0.
  std::optional<SlotIndex> farthest;
  std::uint8_t farthestDepth = 0;
  std::uint16_t farthestZ = 0;
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint32_t count = LoopCount(_state.Data().world.shipSlotCount); count != 0; --count)
  {
    const SpaceObject& object = _state.Slot(slot);
    if ((object.type & SLOT_DRAWABLE) == SLOT_DRAWABLE && (object.flags & STATE_DRAWN) == 0)
    {
      const std::uint8_t depth = object.depth;
      if (farthestDepth < depth || (farthestDepth == depth && farthestZ < object.viewZ))
      {
        farthestDepth = depth;
        farthestZ = object.viewZ;
        farthest = slot;
      }
    }
    slot = SlotAfter(slot);
  }
  if (!farthest)
  {
    return false;
  }
  SpaceObject& object = _state.Slot(*farthest);
  object.flags = static_cast<std::uint8_t>(object.flags | STATE_DRAWN);
  // What the object draws, watched for the space view's layers (ADR-033), which read the buffer and write nothing.
  _host.BeginSpaceViewObject();
  DrawObject(_state, _host, object);
  _host.EndSpaceViewObject(static_cast<std::uint8_t>(Number(*farthest)));
  return true;
}

// One coordinate of the sun's or planet's centre: 256|_value| over _slot's view z, rounded down, with _value's sign put back.
[[nodiscard]] std::uint16_t ProjectDiscCoordinate(const SpaceObject& _slot, std::uint16_t _value)
{
  const bool negative = Negative(_value);
  const std::uint16_t magnitude = negative ? Negate(_value) : _value;
  const std::uint16_t coordinate = DivideUnsigned(ScaledDividend(magnitude), _slot.viewZ, DISC_DIVIDE_TRAP).quotient;
  return negative ? Negate(coordinate) : coordinate;
}

// _center + _radius must not overflow or be negative, and _center - _radius must not overflow or pass _limit: the sum first, then
// the radius taken off it twice, only the second subtraction tested for overflow, then the signed comparison with _limit.
[[nodiscard]] bool DiscWithin(std::uint16_t _center, std::uint16_t _radius, std::uint16_t _limit) noexcept
{
  const auto upperEdge = static_cast<std::uint16_t>(_center + _radius);
  if (AddOverflows(_center, _radius) || Negative(upperEdge))
  {
    return false;
  }
  const auto once = static_cast<std::uint16_t>(upperEdge - _radius);
  const auto lowerEdge = static_cast<std::uint16_t>(once - _radius);
  return !SubtractOverflows(once, _radius) && static_cast<std::int16_t>(lowerEdge) <= static_cast<std::int16_t>(_limit);
}

// DrawSunOrPlanetDisc: a disc of radius _radius at _slot's view position projected, in its colour (+0Bh), drawn only
// when the disc's whole box is on the 256x128 view: DrawDisc with twice the radius.
void DrawSunOrPlanetDisc(GameState& _state, SpaceObject& _slot, std::uint16_t _radius)
{
  const auto row = Offset(ProjectDiscCoordinate(_slot, _slot.viewY), SCREEN_CENTER_Y);
  const auto x = Offset(ProjectDiscCoordinate(_slot, _slot.viewX), SCREEN_CENTER_X);
  if (!DiscWithin(x, _radius, SCREEN_RIGHT) || !DiscWithin(row, _radius, SCREEN_BOTTOM))
  {
    return;
  }
  _state.Data().drawing.drawColor = High(_slot.pitch); // the disc's colour
  DrawDisc(_state, static_cast<std::uint16_t>(_radius << 1), x, row);
}

// What SupernovaHeat makes of the sun once supernovaFrames has counted down to 1.
struct SupernovaGlow
{
  std::uint16_t heat; // a byte: the heat, which stands for the sun's radius
  SlotIndex slot;     // the slot after it: the sun's, or past the ship slots once an energy bomb went off and looked at them
};

// SupernovaHeat: once supernovaFrames has counted down to 1, where it is held, the sun's heat is supernovaHeat, first the
// cabin temperature, growing by a quarter of itself (at least 1) a frame; when it overflows a byte, the player is killed (KillPlayer, with
// StartPlayerDeathSound) and an energy bomb goes off (DetonateEnergyBomb), and the heat is FFh. None while the countdown runs, or
// when it is 0. _slot is the sun's slot.
[[nodiscard]] std::optional<SupernovaGlow> SupernovaHeat(GameState& _state, SlotIndex _slot)
{
  if (_state.Data().commander.supernovaFrames == 0)
  {
    return std::nullopt;
  }
  _state.Data().commander.supernovaFrames = static_cast<std::uint16_t>(_state.Data().commander.supernovaFrames - 1);
  if (_state.Data().commander.supernovaFrames != 0)
  {
    return std::nullopt;
  }
  _state.Data().commander.supernovaFrames = 1;
  if (_state.Data().commander.supernovaHeat == 0)
  {
    _state.Data().commander.supernovaHeat = std::uint8_t{_state.Data().playerShip.cabinTemperature};
  }
  auto step = static_cast<std::uint8_t>(_state.Data().commander.supernovaHeat >> 2);
  if (step == 0)
  {
    step = 1;
  }
  const unsigned sum = unsigned{step} + _state.Data().commander.supernovaHeat;
  SupernovaGlow glow{static_cast<std::uint8_t>(sum), _slot};
  if (sum > 0xFF)
  {
    static_cast<void>(KillPlayer(_state));
    // DetonateEnergyBomb's loop leaves the original's slot offset past the ship slots, by the count it loads: objectSlotCount
    // less 3, a byte, 0 for 65,536, which the offset's 64 KiB wrap brings back to the first ship's slot: the slot that many on
    // from the first ship's. In flight objectSlotCount is 14h, as SetUpLocalSpace sets it and nothing else writes it, so that
    // is the first debris slot, 17 on.
    const auto ships = static_cast<std::uint8_t>(_state.Data().flight.objectSlotCount - BOMB_FIRST_SLOT);
    DetonateEnergyBomb(_state);
    glow.slot = SlotAfter(SlotIndex::FirstShip, ships);
    glow.heat = 0xFF;
  }
  _state.Data().commander.supernovaHeat = Low(glow.heat);
  return glow;
}

// The sun's disc, as SizeSun leaves it for DrawSunOrPlanetDisc.
struct SunDisc
{
  std::uint16_t radius; // the disc's radius
  SlotIndex slot;       // the slot whose view z is tested: the sun's, or where a supernova's energy bomb left it
};

// SizeSun: the sun's radius, SupernovaHeat's heat or else 100 scaled to _slot's distance (ScaleByInverseDistance),
// into the cabin temperature. Behind the view, by bit 7 of the view z's high byte in that slot, nothing more; in
// front, sunFringeMask 1, 3 or 7 below radii 28h, B4h and C3h; from C3h, with fuel scoops fitted, 6 more fuel, written, then FFh
// over it on a carry, with FUEL SCOOPS ACTIVE for 5 frames; from FDh, KillPlayer, whose StartPlayerDeathSound leaves its step length
// over the radius's low byte, as nothing here keeps the radius round it, and a radius past a byte held to FFh. Returns the disc
// in front of the view.
[[nodiscard]] std::optional<SunDisc> SizeSun(GameState& _state, SpaceObject& _slot)
{
  SunDisc disc{0, _state.IndexOf(_slot)};
  if (const std::optional<SupernovaGlow> glow = SupernovaHeat(_state, _state.IndexOf(_slot)))
  {
    disc = SunDisc{glow->heat, glow->slot};
  }
  else
  {
    // 100 * 65536.
    disc.radius = ScaleByInverseDistance(_slot, std::uint32_t{SUN_SCALE} << 16);
  }
  _state.Data().playerShip.cabinTemperature = Low(disc.radius);
  if ((High(_state.Slot(disc.slot).viewZ) & 0x80) != 0)
  {
    return std::nullopt;
  }
  _state.Data().drawing.sunFringeMask = FRINGE_SMALL;
  if (disc.radius < FRINGE_RADIUS_SMALL)
  {
    return disc;
  }
  _state.Data().drawing.sunFringeMask = FRINGE_MEDIUM;
  if (disc.radius < FRINGE_RADIUS_LARGE)
  {
    return disc;
  }
  _state.Data().drawing.sunFringeMask = FRINGE_LARGE;
  if (disc.radius < SCOOP_RADIUS)
  {
    return disc;
  }
  if (_state.Data().commander.fuelScoopsFitted == 1)
  {
    const unsigned fuel = unsigned{_state.Data().commander.fuel} + SCOOP_FUEL;
    _state.Data().commander.fuel = static_cast<std::uint8_t>(fuel);
    if (fuel > 0xFF)
    {
      _state.Data().commander.fuel = 0xFF;
      _state.Data().messages.messagePointer = Message::FuelScoopsActive;
      _state.Data().messages.messageFrames = SCOOP_MESSAGE_FRAMES;
    }
  }
  if (disc.radius >= FATAL_RADIUS)
  {
    if (const std::optional<std::uint8_t> stepLength = KillPlayer(_state))
    {
      SetLow(disc.radius, *stepLength);
    }
    if (High(disc.radius) != 0)
    {
      disc.radius = RADIUS_LIMIT;
    }
  }
  return disc;
}

// SizePlanet: sunFringeMask cleared, the radius ScaleByInverseDistance makes of 50 at _slot's distance, and the
// altitude, twice 255 less that radius held to 127. In front of the view, a radius of FDh or more kills the player, with
// StartPlayerDeathSound, and one past a byte draws as FFh. Returns the disc's radius in front of the view.
std::optional<std::uint16_t> SizePlanet(GameState& _state, SpaceObject& _slot)
{
  _state.Data().drawing.sunFringeMask = FRINGE_NONE;
  // 50 * 65536.
  const std::uint16_t scaled = ScaleByInverseDistance(_slot, std::uint32_t{PLANET_SCALE} << 16);
  auto altitude = static_cast<std::uint8_t>(~Low(scaled));
  if (altitude > ALTITUDE_LIMIT)
  {
    altitude = ALTITUDE_LIMIT;
  }
  altitude = static_cast<std::uint8_t>(altitude << 1);
  _state.Data().playerShip.altitude = altitude;
  if ((High(_slot.viewZ) & 0x80) != 0)
  {
    return std::nullopt;
  }
  std::uint16_t radius = scaled;
  if (radius >= FATAL_RADIUS)
  {
    // The radius is kept round KillPlayer.
    static_cast<void>(KillPlayer(_state));
    if (High(radius) != 0)
    {
      radius = RADIUS_LIMIT;
    }
  }
  return radius;
}

// The most vertices a blueprint has ProjectVertices project: a count of vertexBuffer's, which holds 42.
[[nodiscard]] consteval std::size_t MostProjectedVertices()
{
  std::size_t most = 0;
  for (const Blueprint& blueprint : BLUEPRINTS)
  {
    most = std::max<std::size_t>(most, blueprint.projectedVertices);
  }
  return most;
}

static_assert(MostProjectedVertices() * VERTEX_BYTES <= VERTEX_REACH, "a blueprint projects vertexBuffer's vertices alone");

} // namespace

void ProjectVertices(GameState& _state)
{
  // A count of the whole word, 0 none. One place walks the vertices and another the points they become, in place.
  // The count is a blueprint's (RenderBlueprint), at most 37 of vertexBuffer's 42 vertices (MostProjectedVertices).
  const std::uint16_t count = _state.Data().scene.projectedVertexCount;
  const std::span<std::uint16_t> vertices = BytesWithin(Vertices(_state), 0, std::size_t{count} * VERTEX_BYTES / 2);
  std::size_t vertex = 0;
  std::size_t point = 0;
  for (std::uint16_t left = count; left != 0; --left)
  {
    const std::size_t z = vertex + VERTEX_Z;
    if (static_cast<std::int16_t>(VertexWord(vertices, z)) < static_cast<std::int16_t>(NEAR_PLANE_Z))
    {
      SetVertexWord(vertices, point, NEAR_VERTEX_X);
    }
    else
    {
      const std::uint16_t x = ProjectVertexCoordinate(VertexWord(vertices, vertex), VertexWord(vertices, z));
      SetVertexWord(vertices, point, Offset(x, SCREEN_CENTER_X));
      const std::uint16_t y = ProjectVertexCoordinate(VertexWord(vertices, vertex + 2), VertexWord(vertices, z));
      SetVertexWord(vertices, point + 2, Offset(y, SCREEN_CENTER_Y));
    }
    point += POINT_BYTES;
    vertex += VERTEX_BYTES;
  }
}

void ReflectVertexAboutCenter(GameState& _state, std::size_t _vertex, std::size_t _reflection)
{
  const std::span<std::uint16_t> vertices = Vertices(_state);
  std::array<std::uint16_t, 3> center{};
  constexpr std::array<std::uint16_t SceneState::*, 3> CENTER = {&SceneState::drawCenterX, &SceneState::drawCenterY,
                                                                 &SceneState::drawCenterZ};
  for (std::size_t axis = 0; axis < CENTER.size(); ++axis)
  {
    const std::size_t bytes = 2 * axis;
    center[axis] = _state.Data().scene.*CENTER[axis];
    SetVertexWord(vertices, _reflection + bytes, static_cast<std::uint16_t>(center[axis] - VertexWord(vertices, _vertex + bytes)));
    SetVertexWord(vertices, _vertex + bytes, Offset(VertexWord(vertices, _vertex + bytes), center[axis]));
  }
}

void OffsetVertexByCenter(GameState& _state, std::size_t _vertex)
{
  const std::span<std::uint16_t> vertices = Vertices(_state);
  constexpr std::array<std::uint16_t SceneState::*, 3> CENTER = {&SceneState::drawCenterX, &SceneState::drawCenterY,
                                                                 &SceneState::drawCenterZ};
  for (std::size_t axis = 0; axis < CENTER.size(); ++axis)
  {
    const std::size_t bytes = 2 * axis;
    SetVertexWord(vertices, _vertex + bytes, Offset(VertexWord(vertices, _vertex + bytes), _state.Data().scene.*CENTER[axis]));
  }
}

void BuildBoxCornerVertices(GameState& _state, std::uint16_t _extents)
{
  const std::uint16_t extents = _extents;
  SetDrawAngles(_state);
  // q, the half-height, from the low byte; h, the half-length, from the high, negated; p is boxHalfWidth.
  const std::uint16_t height = Low(extents);
  const std::uint16_t length = Negate(std::uint16_t{High(extents)});
  // boxCornerVertices, vertex 34 on, the box's corners.
  const std::span<std::uint16_t> vertices = Vertices(_state);
  const std::size_t corners = BOX_FIRST_CORNER;
  SetVertexWord(vertices, corners + VERTEX_Z, length);
  SetVertexWord(vertices, corners + VERTEX_BYTES + VERTEX_Z, length);
  SetVertexWord(vertices, corners + 2 * VERTEX_BYTES + VERTEX_Z, length);
  // (p, q) by -roll, and its opposite, (-p, -q); then (p, -q).
  const Pair upper =
    RotateByStoredSinCos(_state, 3, Pair{static_cast<std::int16_t>(_state.Data().scene.boxHalfWidth), static_cast<std::int16_t>(height)});
  SetVertexWord(vertices, corners, static_cast<std::uint16_t>(upper.first));
  SetVertexWord(vertices, corners + 2, static_cast<std::uint16_t>(upper.second));
  SetVertexWord(vertices, corners + 2 * VERTEX_BYTES, Negate(static_cast<std::uint16_t>(upper.first)));
  SetVertexWord(vertices, corners + 2 * VERTEX_BYTES + 2, Negate(static_cast<std::uint16_t>(upper.second)));
  const Pair lower = RotateByStoredSinCos(
    _state, 3, Pair{static_cast<std::int16_t>(_state.Data().scene.boxHalfWidth), static_cast<std::int16_t>(Negate(height))});
  SetVertexWord(vertices, corners + VERTEX_BYTES, static_cast<std::uint16_t>(lower.first));
  SetVertexWord(vertices, corners + VERTEX_BYTES + 2, static_cast<std::uint16_t>(lower.second));
  RotateVertices(_state, corners, BOX_CORNERS_ROTATED, 0, VERTEX_Z, 4);
  RotateVertices(_state, corners, BOX_CORNERS_ROTATED, 2, VERTEX_Z, 5);
  RotateVertices(_state, corners, BOX_CORNERS_ROTATED, 0, VERTEX_Z, 1);
  RotateVertices(_state, corners, BOX_CORNERS_ROTATED, 0, 2, 2);
  RotateVerticesToView(_state, corners, BOX_CORNERS_ROTATED);

  // The fourth corner completes the parallelogram, v2 - v1 + v0; the fifth and eighth are the second and third
  // again, and four reflections through the centre make the far face.
  for (std::size_t word = corners; word != corners + std::size_t{2} * BOX_CORNERS_ROTATED; word += 2)
  {
    SetVertexWord(vertices, word + BOX_FOURTH_CORNER,
                  static_cast<std::uint16_t>(VertexWord(vertices, word + 2 * VERTEX_BYTES) - VertexWord(vertices, word + VERTEX_BYTES) +
                                             VertexWord(vertices, word)));
  }
  std::size_t source = BOX_SECOND_CORNER;
  for (const std::size_t copy : {BOX_FIFTH_CORNER, BOX_EIGHTH_CORNER})
  {
    for (std::size_t word = 0; word != std::size_t{2} * BOX_CORNERS_ROTATED; word += 2)
    {
      SetVertexWord(vertices, copy + word, VertexWord(vertices, source));
      source += 2;
    }
  }
  std::size_t vertex = corners;
  std::size_t reflection = BOX_SEVENTH_CORNER;
  ReflectVertexAboutCenter(_state, vertex, reflection);
  vertex += BOX_REFLECTION_STEP;
  reflection -= BOX_REFLECTION_STEP;
  ReflectVertexAboutCenter(_state, vertex, reflection);
  vertex += BOX_REFLECTION_SKIP;
  reflection -= VERTEX_BYTES;
  ReflectVertexAboutCenter(_state, vertex, reflection);
  vertex -= BOX_REFLECTION_STEP;
  reflection += BOX_REFLECTION_STEP;
  ReflectVertexAboutCenter(_state, vertex, reflection);
}

void BuildDodoVertices(GameState& _state)
{
  SetDrawAngles(_state);
  // From the roll angle, in 72-degree steps: the sine and cosine high bytes of each, scaled to the two rings.
  // The angle a byte offset into sineTable, always even.
  const auto ringRadii = [](std::uint16_t _angle) { return ScaleDodoRadii(static_cast<std::int8_t>(SineHigh(_angle))); };
  auto angle = static_cast<std::uint16_t>((_state.Data().scene.drawRollAngle & ANGLE_BITS) << 1);
  const std::span<std::uint16_t> vertices = Vertices(_state);
  std::size_t vertex = 0;
  for (std::uint16_t left = DODO_RING_VERTICES; left != 0; --left)
  {
    const DodoRadii sine = ringRadii(angle);
    SetVertexWord(vertices, vertex, static_cast<std::uint16_t>(sine.inner));
    SetVertexWord(vertices, vertex + DODO_OUTER_RING, static_cast<std::uint16_t>(sine.outer));
    SetVertexWord(vertices, vertex + VERTEX_Z, DODO_NEAR_RING_Z);
    SetVertexWord(vertices, vertex + DODO_OUTER_RING + VERTEX_Z, DODO_FAR_RING_Z);
    angle = static_cast<std::uint16_t>((angle - QUARTER_TURN_BYTES) & SINE_BYTE_BITS);
    const DodoRadii cosine = ringRadii(angle);
    SetVertexWord(vertices, vertex + 2, static_cast<std::uint16_t>(cosine.inner));
    SetVertexWord(vertices, vertex + DODO_OUTER_RING + 2, static_cast<std::uint16_t>(cosine.outer));
    angle = static_cast<std::uint16_t>((angle + DODO_RING_STEP_BYTES) & SINE_BYTE_BITS);
    vertex += VERTEX_BYTES;
  }
  // The slot: two corners at half the high bytes, and their opposites.
  const auto slotHalf = [](std::uint16_t _angle) { return Sar(SignExtend(SineHigh(_angle)), 1); };
  vertex += DODO_OUTER_RING;
  angle = static_cast<std::uint16_t>((angle + DODO_SLOT_START_BYTES) & SINE_BYTE_BITS);
  for (std::uint16_t left = DODO_SLOT_VERTICES; left != 0; --left)
  {
    const std::uint16_t x = slotHalf(angle);
    SetVertexWord(vertices, vertex, x);
    SetVertexWord(vertices, vertex + DODO_SLOT_OPPOSITE, Negate(x));
    SetVertexWord(vertices, vertex + VERTEX_Z, DODO_SLOT_Z);
    SetVertexWord(vertices, vertex + DODO_SLOT_OPPOSITE + VERTEX_Z, DODO_SLOT_Z);
    angle = static_cast<std::uint16_t>((angle - QUARTER_TURN_BYTES) & SINE_BYTE_BITS);
    const std::uint16_t y = slotHalf(angle);
    SetVertexWord(vertices, vertex + 2, y);
    SetVertexWord(vertices, vertex + DODO_SLOT_OPPOSITE + 2, Negate(y));
    angle = static_cast<std::uint16_t>((angle + HALF_TURN_BYTES) & SINE_BYTE_BITS);
    vertex += VERTEX_BYTES;
  }
  RotateVertices(_state, 0, DODO_ROTATED_VERTICES, 0, VERTEX_Z, 4);
  RotateVertices(_state, 0, DODO_ROTATED_VERTICES, 2, VERTEX_Z, 5);
  RotateVertices(_state, 0, DODO_ROTATED_VERTICES, 0, VERTEX_Z, 1);
  RotateVertices(_state, 0, DODO_ROTATED_VERTICES, 0, 2, 2);
  RotateVerticesToView(_state, 0, DODO_ROTATED_VERTICES);
  // The first ten reflected through the centre, and the slot's four moved to it.
  vertex = 0;
  std::size_t reflection = DODO_REFLECTED;
  for (std::uint16_t left = DODO_REFLECTED_VERTICES; left != 0; --left)
  {
    ReflectVertexAboutCenter(_state, vertex, reflection);
    vertex += VERTEX_BYTES;
    reflection += VERTEX_BYTES;
  }
  for (std::uint16_t left = DODO_SLOT_CORNERS; left != 0; --left)
  {
    OffsetVertexByCenter(_state, vertex);
    vertex += VERTEX_BYTES;
  }
}

DodoRadii ScaleDodoRadii(std::int8_t _value)
{
  // The byte sign-extended, then inner = 2a + a/4 + a/8 - a/32 and outer = 3a + a/2 + a/4 + a/16 - a/64, each shift arithmetic,
  // in 16 bits.
  auto value = static_cast<std::uint16_t>(static_cast<std::int16_t>(_value));
  auto inner = static_cast<std::uint16_t>(value << 1);
  auto outer = static_cast<std::uint16_t>((value << 1) + value);
  value = Sar(value, 1);
  outer = Offset(outer, value);
  value = Sar(value, 1);
  inner = Offset(inner, value);
  outer = Offset(outer, value);
  value = Sar(value, 1);
  inner = Offset(inner, value);
  value = Sar(value, 1);
  outer = Offset(outer, value);
  value = Sar(value, 1);
  inner = static_cast<std::uint16_t>(inner - value);
  value = Sar(value, 1);
  outer = static_cast<std::uint16_t>(outer - value);
  return DodoRadii{static_cast<std::int16_t>(inner), static_cast<std::int16_t>(outer)};
}

void RunVertexProgram(GameState& _state, std::span<const std::uint8_t> _program, Vector _accumulator)
{
  auto x = static_cast<std::uint16_t>(_accumulator.x);
  auto y = static_cast<std::uint16_t>(_accumulator.y);
  auto z = static_cast<std::uint16_t>(_accumulator.z);
  const std::span<std::uint16_t> vertices = Vertices(_state);
  for (const std::uint8_t op : _program)
  {
    const std::size_t vertex = std::size_t{static_cast<std::uint8_t>(op & VERTEX_INDEX_BITS)} * VERTEX_BYTES;
    const std::uint16_t vertexX = VertexWord(vertices, vertex);
    const std::uint16_t vertexY = VertexWord(vertices, vertex + 2);
    const std::uint16_t vertexZ = VertexWord(vertices, vertex + 4);
    switch (static_cast<std::uint8_t>(op & VERTEX_OP_BITS))
    {
    case VERTEX_OP_LOAD:
      x = vertexX;
      y = vertexY;
      z = vertexZ;
      break;
    case VERTEX_OP_STORE:
      SetVertexWord(vertices, vertex, x);
      SetVertexWord(vertices, vertex + 2, y);
      SetVertexWord(vertices, vertex + 4, z);
      break;
    case VERTEX_OP_AVERAGE:
    {
      const auto average = [](std::uint16_t _sum, std::uint16_t _value)
      { return static_cast<std::uint16_t>(static_cast<std::int16_t>(Offset(_sum, _value)) >> 1); };
      x = average(x, vertexX);
      y = average(y, vertexY);
      z = average(z, vertexZ);
      break;
    }
    default:
      x = Offset(x, vertexX);
      y = Offset(y, vertexY);
      z = Offset(z, vertexZ);
      break;
    }
  }
}

bool TriangleWindingSign(Triangle _triangle)
{
  const WindingProducts products = Winding(_triangle);
  const auto high = static_cast<std::uint16_t>((products.first >> 16) - (products.second >> 16));
  const std::uint16_t sign = high != 0 ? high : static_cast<std::uint16_t>(products.first - products.second);
  return Negative(sign);
}

void DrawVisibleFaces(GameState& _state, const Blueprint& _blueprint, std::uint16_t _faces)
{
  if (_faces == 0)
  {
    return;
  }
  std::uint16_t next = 0;
  // The face count is kept round each face, and its count down to 0 ends the faces.
  std::uint16_t faces = _faces;
  do
  {
    const Triangle facing = LoadTriangle(_state, _blueprint.faceList, next, 0);
    next = Offset(next, TRIANGLE_VERTEX_BYTES);
    if (TriangleWindingSign(facing))
    {
      next = DrawFaceItems(_state, _blueprint, next);
    }
    else
    {
      // SkipHiddenFace: on by the count byte plus one, past the count byte and the items.
      next = Offset(next, Offset(_blueprint.faceList[next], 1));
    }
  } while (Loop(faces));
}

ShipRangeCheck CheckShipInRange(GameState& _state, SpaceObject& _slot)
{
  ShipRangeCheck check{IsObjectNear(_state, _slot), ShipRange{}};
  if (check.near.nearby)
  {
    check.range = ShipWithinRange(_slot);
    if (check.range.within)
    {
      return check;
    }
  }
  (void)EraseScannerBlip(_state, _slot);
  return check;
}

void TransformSunOrPlanet(GameState& _state, SpaceObject& _slot)
{
  // The shift, as the disc's scale (+0Ah) and its depth (+3Dh), and for ScalePositionDown.
  const std::uint8_t shift = GetPositionScaleShift(_slot).shift;
  _slot.pitch = WithLow(_slot.pitch, shift); // the disc's scale shift
  _slot.depth = shift;
  const Vector view = TransformToViewWithBlip(_state, _slot, ScalePositionDown(_slot, shift));
  _slot.viewX = static_cast<std::uint16_t>(view.x);
  _slot.viewY = static_cast<std::uint16_t>(view.y);
  _slot.viewZ = static_cast<std::uint16_t>(view.z);
  _slot.type = static_cast<std::uint8_t>(_slot.type | SLOT_VISIBLE);
}

void ClassifyStationPosition(GameState& _state, SpaceObject& _slot)
{
  ClassifyViewPosition(_state, _slot, CompassPosition(_slot));
}

void TransformShip(GameState& _state, SpaceObject& _slot)
{
  // The position, the slot's words at +4, +6 and +8; the slot is kept round the transform and the scoop.
  const Vector position{static_cast<std::int16_t>(_slot.x), static_cast<std::int16_t>(_slot.y), static_cast<std::int16_t>(_slot.z)};
  const Vector view = TransformToViewWithBlip(_state, _slot, position);
  if (_state.Data().commander.fuelScoopsFitted == 1 && _state.Data().flight.gameOverFrames == 0)
  {
    // The scoop keeps the view position round its work.
    TryScoopObject(_state, _slot, view);
  }
  _slot.depth = 0;
  ClassifyViewPosition(_state, _slot, view);
}

bool InView(const SpaceObject& _slot) noexcept
{
  return (_slot.type & SLOT_DRAWABLE) == SLOT_DRAWABLE;
}

bool Flashes(const SpaceObject& _slot) noexcept
{
  return (_slot.flags & STATE_FLASHING) != 0;
}

SnapshotDrawing DrawingOf(const SpaceObject& _slot) noexcept
{
  if ((_slot.type & SLOT_ACTIVE) == 0 || IsSunOrPlanet(_slot))
  {
    return SnapshotDrawing::None;
  }
  if (IsStation(_slot).station)
  {
    // DrawStationIfDistant: a model at depth 0, or at depth 1 while its compass z is below 1B58h; else the distant disc.
    const bool nearby = _slot.depth == 0 || (_slot.depth == 1 && _slot.compassZ < DISTANT_STATION_Z);
    return nearby ? SnapshotDrawing::Model : SnapshotDrawing::None;
  }
  if ((_slot.type & SLOT_IN_RANGE) == 0)
  {
    return SnapshotDrawing::None;
  }
  // FlashedOff: off while its off frames last; on the last of them it counts down to 0, turns on and is drawn.
  if ((_slot.flags & STATE_FLASHING) != 0 && (_slot.flags & STATE_FLASH_OFF) != 0 && _slot.FlashFrames() != 1)
  {
    return SnapshotDrawing::None;
  }
  return _slot.detail < _slot.size ? SnapshotDrawing::Dot : SnapshotDrawing::Model;
}

void RunBlueprintHandler(GameState& _state, const Blueprint& _blueprint)
{
  // The original saves the slot and the way back to RenderBlueprintBody, then reads the handler's word and the half-width byte,
  // into boxHalfWidth with a high byte of 0, and jumps to the handler with the blueprint read past them. The slot and the way
  // back are the stack the handler returns through, which this unit keeps itself.
  _state.Data().scene.boxHalfWidth = _blueprint.halfWidth;
  if (_blueprint.shape == BlueprintShape::Box)
  {
    BuildBoxCornerVertices(_state, _blueprint.extents);
  }
  else
  {
    BuildDodoVertices(_state);
  }
  // Every blueprint's vertex program begins by loading a vertex into the accumulator, so the accumulator the handler leaves
  // behind is never read.
  RenderBlueprint(_state, _blueprint, Vector{});
}

const Blueprint& BlueprintOf(std::uint8_t _kind)
{
  return BLUEPRINTS.at(_kind);
}

std::uint16_t FaceFillPattern(std::uint8_t _index)
{
  return FACE_FILL_PATTERNS.at(_index);
}

void TransformAndDrawObjects(GameState& _state, Host& _host)
{
  (void)SetSinCos(_state, 0, _state.Data().playerShip.playerPitchAngle);
  (void)SetSinCos(_state, 1, _state.Data().playerShip.playerYawAngle);
  (void)SetSinCos(_state, 2, _state.Data().playerShip.playerRollAngle);
  // shipSlotCount, a byte count kept round each slot: a count of 0 classifies 65,536. The pass steps on from where each slot's
  // classification leaves it.
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint32_t count = LoopCount(_state.Data().world.shipSlotCount); count != 0; --count)
  {
    slot = SlotAfter(ClassifyObject(_state, _state.Slot(slot)));
  }
  while (DrawFarthestObject(_state, _host))
  {
  }
}

Vector TransformToViewWithBlip(GameState& _state, SpaceObject& _slot, Vector _position)
{
  const Vector camera = RotatePitchYawRoll(_state, _position);
  // The camera z's high byte, into +3Ch.
  _slot.cameraZHigh = High(static_cast<std::uint16_t>(camera.z));
  UpdateScannerBlip(_state, _slot, camera);
  return RotateToViewDirection(_state, camera);
}

Vector TransformToView(GameState& _state, Vector _position)
{
  return RotateToViewDirection(_state, RotatePitchYawRoll(_state, _position));
}

void DrawSunOrPlanet(GameState& _state, SpaceObject& _slot)
{
  if (IsPlanet(_slot))
  {
    if (const std::optional<std::uint16_t> radius = SizePlanet(_state, _slot))
    {
      DrawSunOrPlanetDisc(_state, _slot, *radius);
    }
    return;
  }
  // The disc is the slot's that SizeSun leaves: the sun's, or, once a supernova's heat killed and its energy bomb's loop moved the
  // original's slot offset, that slot's, as the original draws it.
  if (const std::optional<SunDisc> disc = SizeSun(_state, _slot))
  {
    DrawSunOrPlanetDisc(_state, _state.Slot(disc->slot), disc->radius);
  }
}

void DrawDistantStation(GameState& _state, SpaceObject& _slot)
{
  const ScreenPoint at = ProjectToScreen(CompassPosition(_slot));
  const auto x = static_cast<std::uint16_t>(at.x);
  const auto row = static_cast<std::uint16_t>(at.y);
  _state.Data().drawing.drawColor = STATION_COLOR;
  const std::uint8_t depth = High(_slot.compassZ); // the station's depth
  if (depth < NEAR_STATION_DEPTH)
  {
    DrawDisc(_state, NEAR_STATION_RADIUS, x, row);
    return;
  }
  // r = (21h - depth) / 2, as bytes: none when 25h - depth, taken as signed, is 4 or less, and none when r is 0. The radius is 2r.
  const std::uint8_t beyond = Negate(static_cast<std::uint8_t>(depth - STATION_DEPTH_BASE));
  if (static_cast<std::int8_t>(beyond) <= static_cast<std::int8_t>(STATION_DEPTH_SMALLEST))
  {
    return;
  }
  const auto rows = static_cast<std::uint8_t>(static_cast<std::uint8_t>(beyond - STATION_DEPTH_SMALLEST) >> 1);
  if (rows == 0)
  {
    return;
  }
  DrawDisc(_state, static_cast<std::uint16_t>(rows << 1), x, row);
}

void LoadPlayerAngles(GameState& _state)
{
  (void)SetSinCos(_state, 0, _state.Data().playerShip.playerPitchAngle);
  (void)SetSinCos(_state, 1, _state.Data().playerShip.playerYawAngle);
  (void)SetSinCos(_state, 2, _state.Data().playerShip.playerRollAngle);
}

ScreenPoint ProjectToScreen(Vector _view)
{
  // The magnitudes, with whether x and y were negative kept to put their signs back.
  const bool negativeX = _view.x < 0;
  const bool negativeY = _view.y < 0;
  const std::uint16_t x = negativeX ? Negate(static_cast<std::uint16_t>(_view.x)) : static_cast<std::uint16_t>(_view.x);
  const std::uint16_t y = negativeY ? Negate(static_cast<std::uint16_t>(_view.y)) : static_cast<std::uint16_t>(_view.y);
  const auto z = static_cast<std::uint16_t>(_view.z);
  // 256|x| over z first, then 256|y|, each dividend's top byte 0.
  const std::uint16_t across = DivideUnsigned(std::uint32_t{x} << 8, z, SCREEN_DIVIDE_TRAP).quotient;
  const std::uint16_t down = DivideUnsigned(std::uint32_t{y} << 8, z, SCREEN_DIVIDE_TRAP).quotient;
  return ScreenPoint{static_cast<std::int16_t>(Offset(negativeX ? Negate(across) : across, SCREEN_CENTER_X)),
                     static_cast<std::int16_t>(Offset(negativeY ? Negate(down) : down, SCREEN_CENTER_Y))};
}

} // namespace Elite
