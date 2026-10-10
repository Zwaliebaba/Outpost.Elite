#include "pch.h"

#include "Scene.h"

#include "Arithmetic.h"
#include "Combat.h"
#include "DataOverlay.h"
#include "Maths.h"

#include <initializer_list>
#include <optional>
#include <utility>

namespace Elite
{

namespace
{

// The routines of other subsystems these call, by entry.
constexpr std::uint16_t DRAW_CLIPPED_LINE = 0x1603;
constexpr std::uint16_t DRAW_DISC = 0x1826;
constexpr std::uint16_t FILL_TRIANGLE = 0x1BFB;
constexpr std::uint16_t DETONATE_ENERGY_BOMB = 0x2ED6;
constexpr std::uint16_t KILL_PLAYER = 0x3115;
constexpr std::uint16_t IS_SUN_OR_PLANET = 0x3F2A;
constexpr std::uint16_t IS_PLANET = 0x3F37;
constexpr std::uint16_t IS_STATION = 0x3F40;
constexpr std::uint16_t UPDATE_SCANNER_BLIP = 0x40EC;
constexpr std::uint16_t UPDATE_COMPASS = 0x418F;
constexpr std::uint16_t TRY_SCOOP_OBJECT = 0x4401;
constexpr std::uint16_t DRAW_DISTANT_STATION = 0x45C6;
constexpr std::uint16_t REMOVE_OBJECT = 0x4F98;

// The instruction after each divide, where DivideOverflowInterrupt looks for its opcode. ProjectVertices'
// divides take a memory operand, so the handler reads their ModRM byte 74h and saturates only AL.
constexpr std::uint16_t VERTEX_X_DIVIDE_RETURN = 0x236C;
constexpr std::uint16_t VERTEX_Y_DIVIDE_RETURN = 0x2395;
constexpr std::uint16_t DISC_Y_DIVIDE_RETURN = 0x4044;
constexpr std::uint16_t DISC_X_DIVIDE_RETURN = 0x4064;
constexpr std::uint16_t SCREEN_X_DIVIDE_RETURN = 0x8D4B;
constexpr std::uint16_t SCREEN_Y_DIVIDE_RETURN = 0x8D56;

// An object slot's fields beyond those Ships.h names (SLOT_BYTES, SLOT_PITCH, SLOT_YAW, SLOT_ROLL, SLOT_VIEW_X, SLOT_VIEW_Y,
// SLOT_VIEW_Z, SLOT_FLAGS, SLOT_DETAIL).
constexpr std::uint16_t SLOT_POSITION_X = 0x04; // the low words of the 24-bit position
constexpr std::uint16_t SLOT_POSITION_Y = 0x06;
constexpr std::uint16_t SLOT_POSITION_Z = 0x08;
constexpr std::uint16_t SLOT_SCALE_SHIFT = 0x0A; // byte; for a ship, the word is its pitch angle
constexpr std::uint16_t SLOT_COLOR = 0x0B;
constexpr std::uint16_t SLOT_COMPASS_X = 0x20;
constexpr std::uint16_t SLOT_COMPASS_Y = 0x22;
constexpr std::uint16_t SLOT_COMPASS_Z = 0x24;
constexpr std::uint16_t SLOT_FRAMES_AWAY = 0x34;
constexpr std::uint16_t SLOT_CAMERA_Z_HIGH = 0x3C;
constexpr std::uint16_t SLOT_DEPTH = 0x3D;
constexpr std::uint16_t SLOT_SIZE = 0x3E;

// Byte 0 of a slot, with Ships.h's SLOT_ACTIVE.
constexpr std::uint8_t SLOT_TYPE_BITS = 0x3E;
constexpr std::uint8_t SLOT_IN_RANGE = 0x40;
constexpr std::uint8_t SLOT_VISIBLE = 0x80;
constexpr std::uint8_t SLOT_KEEP_BITS = 0x3F;
constexpr std::uint8_t SLOT_DRAWABLE = SLOT_VISIBLE | SLOT_ACTIVE;
// Byte +1Eh, SLOT_FLAGS.
constexpr std::uint8_t STATE_DRAWN = 0x80;
constexpr std::uint8_t STATE_FLASH_OFF = 0x40;
constexpr std::uint8_t STATE_FLASHING = 0x20;
constexpr std::uint8_t STATE_BLIP_SHOWN = 0x02;
constexpr std::uint8_t FLASH_OFF_FRAMES = 0x14;
constexpr std::uint8_t FLASH_ON_FRAMES = 0x19;

// DH of a dividend ShiftIntoDividend makes: CWD's sign, or what XOR DH,DH leaves.
constexpr std::uint8_t HIGH_BYTE_ONES = 0xFF;
constexpr std::uint8_t HIGH_BYTE_ZERO = 0x00;

constexpr std::uint16_t SCREEN_CENTER_X = 0x80;
constexpr std::uint16_t SCREEN_CENTER_Y = 0x40;
constexpr std::uint16_t SCREEN_RIGHT = 0xFF;
constexpr std::uint16_t SCREEN_BOTTOM = 0x7F;
constexpr std::uint16_t NEAR_VERTEX_X = 0x8000; // ProjectVertices' mark for a vertex behind nearPlaneZ
constexpr std::uint16_t OFF_SCREEN = 0x7FFF;    // what DrawVisibleFaces skips: the divide trap's saturation
constexpr std::uint16_t POINT_BYTES = 4;
constexpr std::uint16_t VERTEX_BYTES = 6;
constexpr std::uint16_t VERTEX_Z = 4;
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
constexpr std::uint16_t PLANET_SCALE = 0x32;
constexpr std::uint16_t SUN_SCALE = 0x64;
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
constexpr std::uint16_t SLOT_STATION_DEPTH = 0x25;
constexpr std::uint8_t STATION_COLOR = 3;
constexpr std::uint8_t NEAR_STATION_DEPTH = 0x14;
constexpr std::uint16_t NEAR_STATION_RADIUS = 0x0E;
constexpr std::uint8_t STATION_DEPTH_BASE = 0x25;
constexpr std::uint8_t STATION_DEPTH_SMALLEST = 4;

// The blueprint handlers' vertices: the angle tables' words, a 2048-entry turn, and the vertices written.
constexpr std::uint16_t ANGLE_BITS = 0x7FF;
constexpr std::uint16_t SINE_BYTE_BITS = 0xFFF;
constexpr std::uint16_t SINE_HIGH_BYTES = 0x2FA1; // sineTable's high bytes, a signed byte each
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
constexpr std::uint16_t DODO_REFLECTED = 0x7284;   // vertex 14
constexpr std::uint16_t BOX_CORNERS_ROTATED = 3;
constexpr std::uint16_t BOX_FOURTH_CORNER = 0x12;
constexpr std::uint16_t BOX_SECOND_CORNER = 0x7302;  // vertex 35
constexpr std::uint16_t BOX_FIFTH_CORNER = 0x7314;   // vertex 38
constexpr std::uint16_t BOX_SEVENTH_CORNER = 0x7320; // vertex 40
constexpr std::uint16_t BOX_EIGHTH_CORNER = 0x7326;  // vertex 41
constexpr std::uint16_t BOX_REFLECTION_STEP = 0x18;  // four vertices
constexpr std::uint16_t BOX_REFLECTION_SKIP = 0x12;  // three vertices

// The handlers' rotations by the drawn object's angles: -roll into rotation pair 3, yaw into 4, and pitch with the
// player's into 5 (CS:3781, CS:38C0).
void SetDrawAngles(GameState& _state)
{
  (void)SetSinCos(_state, 3, Negate(_state.Get(DS.drawRollAngle)));
  (void)SetSinCos(_state, 4, _state.Get(DS.drawYawAngle));
  (void)SetSinCos(_state, 5, static_cast<std::uint16_t>(_state.Get(DS.drawPitchAngle) + _state.Get(DS.playerPitchAngle)));
}

[[nodiscard]] bool Negative(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0;
}

// and r,r / jns / neg r: 8000h stays 8000h.
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

[[nodiscard]] bool Flag(Guest& _guest, std::uint16_t _flag) noexcept
{
  return (_guest.Regs().flags & _flag) != 0;
}

void OrByte(Guest& _guest, std::uint16_t _offset, std::uint8_t _bits) noexcept
{
  _guest.SetByte(_offset, static_cast<std::uint8_t>(_guest.Byte(_offset) | _bits));
}

// The overflow flag of ADD and SUB on words.
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
// rotated by rotation pair _pair (RotateBySinCosN) and written back, the first, then the second. Nothing reads what the loop
// leaves in the registers: SI past the vertices, CX = 0, AX and BX the last vertex's.
void RotateVertices(GameState& _state, std::uint16_t _first, std::uint16_t _count, std::uint16_t _a, std::uint16_t _b, std::size_t _pair)
{
  std::uint16_t vertex = _first;
  for (std::uint16_t left = _count; left != 0; --left)
  {
    const std::uint16_t first = Offset(vertex, _a);
    const std::uint16_t second = Offset(vertex, _b);
    const Pair rotated = RotateByStoredSinCos(
      _state, _pair, Pair{static_cast<std::int16_t>(_state.Word(first)), static_cast<std::int16_t>(_state.Word(second))});
    _state.SetWord(first, static_cast<std::uint16_t>(rotated.first));
    _state.SetWord(second, static_cast<std::uint16_t>(rotated.second));
    vertex = Offset(vertex, VERTEX_BYTES);
  }
}

// The handlers' last rotation, by the view direction when it is not ahead (CS:383A, CS:39CF): -viewAngle into rotation
// pair 8, and the x and z of _count vertices from _first turned by it. Nothing reads what the original leaves in AX and BX:
// the box's handler loads AX next (CS:3864), and both load them in ReflectVertexAboutCenter (CS:3740) before reading them.
void RotateVerticesToView(GameState& _state, std::uint16_t _first, std::uint16_t _count)
{
  const std::uint16_t angle = _state.Get(DS.viewAngle);
  if (angle == 0)
  {
    return;
  }
  (void)SetSinCos(_state, 8, Negate(angle));
  RotateVertices(_state, _first, _count, 0, VERTEX_Z, 8);
}

// What RenderBlueprintBody does with the blueprint at SI (CS:3CF2-3D20): the vertex program, the projection, the
// edge lists and the faces.
void RenderBlueprint(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  RunVertexProgramEntry(_guest);
  regs.cx = WithLow(regs.cx, _guest.Byte(regs.si));
  _guest.Set(DS.projectedVertexCount, regs.cx);
  regs.si = Offset(regs.si, 1);
  ProjectVertices(_guest);
  // Fixed edges, then face edges: a count byte, then two bytes for each.
  regs.cx = WithLow(regs.cx, _guest.Byte(regs.si));
  _guest.Set(DS.fixedEdgeCount, regs.cx);
  regs.cx = static_cast<std::uint16_t>(regs.cx << 1);
  regs.si = Offset(regs.si, 1);
  _guest.Set(DS.fixedEdgeList, regs.si);
  regs.si = Offset(regs.si, regs.cx);
  regs.cx = WithLow(regs.cx, _guest.Byte(regs.si));
  _guest.Set(DS.faceEdgeCount, regs.cx);
  regs.cx = static_cast<std::uint16_t>(regs.cx << 1);
  regs.si = Offset(regs.si, 1);
  _guest.Set(DS.faceEdgeList, regs.si);
  regs.si = Offset(regs.si, regs.cx);
  regs.cx = WithLow(regs.cx, _guest.Byte(regs.si));
  regs.si = Offset(regs.si, 1);
  DrawVisibleFaces(_guest);
}

// mov dl,ah / mov ah,al / xor al,al, DH being _high: DX:AX = AX * 256, what ProjectVertices,
// DrawSunOrPlanet and ProjectToScreen divide. After CWD, DH is FFh for the one magnitude still negative,
// 8000h.
void ShiftIntoDividend(Machine::Registers& _regs, std::uint8_t _high) noexcept
{
  _regs.dx = WithHigh(High(_regs.ax), _high);
  _regs.ax = static_cast<std::uint16_t>(Low(_regs.ax) << 8);
}

// One coordinate of a vertex: 256|_value| / z, z at [SI+4], plus one when the quotient is below twice
// the remainder, with _value's sign put back. BL holds the sign as the original keeps it.
void ProjectVertexCoordinate(Guest& _guest, std::uint16_t _value, std::uint16_t _returnOffset)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _value;
  regs.bx = WithLow(regs.bx, 0);
  if (Negative(regs.ax))
  {
    regs.bx = WithLow(regs.bx, 1);
    regs.ax = Negate(regs.ax);
  }
  ShiftIntoDividend(regs, Negative(regs.ax) ? HIGH_BYTE_ONES : HIGH_BYTE_ZERO);
  DivideUnsignedOnRegisters(_guest, _guest.Word(Offset(regs.si, VERTEX_Z)), _returnOffset);
  regs.dx = static_cast<std::uint16_t>(regs.dx << 1);
  if (regs.ax < regs.dx)
  {
    regs.ax = Offset(regs.ax, 1);
  }
  regs.bx = WithLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) - 1));
  if (Low(regs.bx) == 0)
  {
    regs.ax = Negate(regs.ax);
  }
}

// The bytes LoadTriangle takes, one a vertex.
constexpr std::uint16_t TRIANGLE_VERTEX_BYTES = 3;

// The three vertex bytes at _indices, each shifted left by _shift, as a byte, into an offset in vertexBuffer, and the points
// there. The second's x goes through faceTestScratch, written before its y is read, as the original keeps it there.
[[nodiscard]] Triangle LoadTriangle(GameState& _state, std::uint16_t _indices, std::uint8_t _shift)
{
  const auto vertex = [&_state, _indices, _shift](std::uint16_t _which)
  { return Offset(DS.vertexBuffer.offset, static_cast<std::uint8_t>(_state.Byte(Offset(_indices, _which)) << _shift)); };
  const auto coordinate = [&_state](std::uint16_t _vertex, std::uint16_t _bytes)
  { return static_cast<std::int16_t>(_state.Word(Offset(_vertex, _bytes))); };
  const std::uint16_t first = vertex(0);
  const ScreenPoint firstPoint{coordinate(first, 0), coordinate(first, 2)};
  const std::uint16_t second = vertex(1);
  const std::int16_t secondX = coordinate(second, 0);
  _state.Set(DS.faceTestScratch, static_cast<std::uint16_t>(secondX));
  const ScreenPoint secondPoint{secondX, coordinate(second, 2)};
  const std::uint16_t third = vertex(2);
  return Triangle{firstPoint, secondPoint, ScreenPoint{coordinate(third, 0), coordinate(third, 2)}};
}

// A triangle in the registers TriangleWindingSign and FillTriangle take it in: (AX, DX), (BX, BP), (CX, DI).
[[nodiscard]] Triangle TriangleIn(const Machine::Registers& _regs) noexcept
{
  const auto point = [](std::uint16_t _x, std::uint16_t _y)
  { return ScreenPoint{static_cast<std::int16_t>(_x), static_cast<std::int16_t>(_y)}; };
  return Triangle{point(_regs.ax, _regs.dx), point(_regs.bx, _regs.bp), point(_regs.cx, _regs.di)};
}

// The two products TriangleWindingSign compares, each as IMUL leaves it in DX:AX.
struct WindingProducts
{
  std::uint32_t first;  // (x0-x1)(y2-y1)
  std::uint32_t second; // (y0-y1)(x2-x1)
};

// SUB in 16 bits, then IMUL.
[[nodiscard]] WindingProducts Winding(const Triangle& _triangle) noexcept
{
  const auto difference = [](std::int16_t _a, std::int16_t _b)
  { return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint16_t>(_a) - static_cast<std::uint16_t>(_b))); };
  const auto product = [](std::int16_t _a, std::int16_t _b) { return static_cast<std::uint32_t>(std::int32_t{_a} * _b); };
  return WindingProducts{product(difference(_triangle.first.x, _triangle.second.x), difference(_triangle.third.y, _triangle.second.y)),
                         product(difference(_triangle.first.y, _triangle.second.y), difference(_triangle.third.x, _triangle.second.x))};
}

// What LoadTriangle's code leaves: the triangle in those registers, and SI past its three vertex bytes.
void TriangleOut(Machine::Registers& _regs, const Triangle& _triangle) noexcept
{
  _regs.ax = static_cast<std::uint16_t>(_triangle.first.x);
  _regs.dx = static_cast<std::uint16_t>(_triangle.first.y);
  _regs.bx = static_cast<std::uint16_t>(_triangle.second.x);
  _regs.bp = static_cast<std::uint16_t>(_triangle.second.y);
  _regs.cx = static_cast<std::uint16_t>(_triangle.third.x);
  _regs.di = static_cast<std::uint16_t>(_triangle.third.y);
  _regs.si = Offset(_regs.si, TRIANGLE_VERTEX_BYTES);
}

// An edge item, BX = twice the edge's index in faceEdgeList: its colour, then DrawClippedLine unless an
// end is off screen.
void DrawFaceEdge(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = _guest.Get(DS.faceEdgeList);
  regs.dx = _guest.Word(Offset(regs.bx, regs.di));
  regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(High(regs.dx) & EDGE_COLOR_BITS));
  _guest.Set(DS.drawColor, Low(regs.ax));
  regs.dx = static_cast<std::uint16_t>(regs.dx & EDGE_VERTEX_BITS);
  regs.bx = Low(regs.dx);
  regs.di = DS.vertexBuffer.offset;
  regs.cx = _guest.Word(Offset(regs.bx, regs.di));
  regs.ax = _guest.Word(Offset(regs.bx, Offset(regs.di, 2)));
  regs.bx = High(regs.dx);
  regs.dx = _guest.Word(Offset(regs.bx, regs.di));
  regs.bx = _guest.Word(Offset(regs.bx, Offset(regs.di, 2)));
  if (regs.cx == OFF_SCREEN || regs.dx == OFF_SCREEN)
  {
    return;
  }
  const std::uint16_t list = regs.si;
  _guest.Call(DRAW_CLIPPED_LINE);
  regs.si = list;
}

// A triangle item, BX = the offset of its pattern in faceFillPatterns, then three vertex bytes at SI:
// FillTriangle unless a corner is off screen.
void DrawFaceTriangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Word(Offset(DS.faceFillPatterns.offset, regs.bx));
  _guest.Set(DS.triangleFillPattern, regs.ax);
  TriangleOut(regs, LoadTriangle(_guest.State(), regs.si, 1));
  if (regs.ax == OFF_SCREEN || regs.bx == OFF_SCREEN || regs.cx == OFF_SCREEN)
  {
    return;
  }
  const std::uint16_t list = regs.si;
  _guest.Call(FILL_TRIANGLE);
  regs.si = list;
}

// A facing face's item count and items at SI.
void DrawFaceItems(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = WithLow(regs.cx, _guest.Byte(regs.si));
  regs.si = Offset(regs.si, 1);
  for (;;)
  {
    const std::uint16_t items = regs.cx;
    const std::uint8_t item = _guest.Byte(regs.si);
    regs.si = Offset(regs.si, 1);
    regs.bx = static_cast<std::uint8_t>(item & ~FACE_TRIANGLE);
    if ((item & FACE_TRIANGLE) == 0)
    {
      DrawFaceEdge(_guest);
      regs.cx = items;
      if (!Loop(regs.cx))
      {
        return;
      }
    }
    else
    {
      DrawFaceTriangle(_guest);
      regs.cx = static_cast<std::uint16_t>(items - TRIANGLE_ITEM_BYTES);
      if (regs.cx == 0)
      {
        return;
      }
    }
  }
}

// How far ClassifyViewPosition got with a view position.
enum class ViewTest : std::uint8_t
{
  TooNear, // z negative or below nearClipZ: nothing stored
  WideX,   // stored, but twice |x| is beyond z
  WideY,   // twice |y| is beyond z
  Visible, // and byte 0 bit 7 set
};

// shl of the magnitude: what ClassifyViewPosition compares with z.
[[nodiscard]] std::uint16_t DoubledMagnitude(std::uint16_t _value) noexcept
{
  return static_cast<std::uint16_t>(Magnitude(_value) << 1);
}

// ClassifyViewPosition (CS:3CA1), the shared tail of TransformShip and ClassifyStationPosition: _view is _slot's view
// position. Visible, with byte 0 bit 7 set, when z is at least nearClipZ and twice |x| and twice |y| are at most z; the
// position is stored, in the slot and as drawCenter, when z passes.
[[nodiscard]] ViewTest ClassifyViewPosition(GameState& _state, ObjectSlot _slot, Vector _view)
{
  const auto x = static_cast<std::uint16_t>(_view.x);
  const auto y = static_cast<std::uint16_t>(_view.y);
  const auto z = static_cast<std::uint16_t>(_view.z);
  if (Negative(z) || z < _state.Get(DS.nearClipZ))
  {
    return ViewTest::TooNear;
  }
  _slot.Set(SlotWord::ViewX, x);
  _state.Set(DS.drawCenterX, x);
  _slot.Set(SlotWord::ViewY, y);
  _state.Set(DS.drawCenterY, y);
  _slot.Set(SlotWord::ViewZ, z);
  _state.Set(DS.drawCenterZ, z);
  if (z < DoubledMagnitude(x))
  {
    return ViewTest::WideX;
  }
  if (z < DoubledMagnitude(y))
  {
    return ViewTest::WideY;
  }
  _slot.Set(SlotByte::Type, static_cast<std::uint8_t>(_slot.Get(SlotByte::Type) | SLOT_VISIBLE));
  return ViewTest::Visible;
}

// ClassifyViewPosition on the view position in AX, BX and CX of the slot at DI, with the registers its code leaves: twice |x|
// in AX once z passes, twice |y| in BX once x does, and CF clear only when the object is visible.
void ClassifyViewPositionOnRegisters(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ViewTest test = ClassifyViewPosition(
    _guest.State(), ObjectSlot(_guest.State(), regs.di),
    Vector{static_cast<std::int16_t>(regs.ax), static_cast<std::int16_t>(regs.bx), static_cast<std::int16_t>(regs.cx)});
  if (test != ViewTest::TooNear)
  {
    regs.ax = DoubledMagnitude(regs.ax);
  }
  if (test == ViewTest::WideY || test == ViewTest::Visible)
  {
    regs.bx = DoubledMagnitude(regs.bx);
  }
  _guest.SetFlag(Machine::FLAG_CARRY, test != ViewTest::Visible);
}

// RotateToViewDirection (CS:3EE6), the shared tail of the two TransformToView entries: _view with (x, z) rotated by -viewAngle
// through rotation pair 8, which it sets, when the view is not the front one.
[[nodiscard]] Vector RotateToViewDirection(GameState& _state, Vector _view)
{
  const std::uint16_t viewAngle = _state.Get(DS.viewAngle);
  if (viewAngle == 0)
  {
    return _view;
  }
  (void)SetSinCos(_state, 8, Negate(viewAngle));
  const Pair xz = RotateByStoredSinCos(_state, 8, Pair{_view.x, _view.z});
  return Vector{xz.first, _view.y, xz.second};
}

// RotateToViewDirection on the view position in AX, BX and CX, where the TransformToView entries hold it, and back.
void RotateToViewDirectionOnRegisters(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Vector view = RotateToViewDirection(
    _guest.State(), Vector{static_cast<std::int16_t>(regs.ax), static_cast<std::int16_t>(regs.bx), static_cast<std::int16_t>(regs.cx)});
  regs.ax = static_cast<std::uint16_t>(view.x);
  regs.bx = static_cast<std::uint16_t>(view.y);
  regs.cx = static_cast<std::uint16_t>(view.z);
}

// mul of a word by itself: DX:AX.
[[nodiscard]] std::uint32_t UnsignedSquare(std::uint16_t _value) noexcept
{
  return std::uint32_t{_value} * _value;
}

// CheckShipInRange's test once IsObjectNear has passed (CS:3BEF): |x|, |y| and |z| below maxAxisDistance, then the high words
// of their unsigned squares, summed, below maxDistanceSquaredHigh after the second and the third. Within, the sum shifted
// right 6 is the ship's size, and byte 0 bit 6 is set.
[[nodiscard]] ShipRange ShipWithinRange(GameState& _state, ObjectSlot _slot)
{
  ShipRange range{};
  constexpr std::array<SlotWord, 3> POSITION = {SlotWord::X, SlotWord::Y, SlotWord::Z};
  for (std::size_t axis = 0; axis < POSITION.size(); ++axis)
  {
    range.magnitudes[axis] = Magnitude(_slot.Get(POSITION[axis]));
    range.axes = static_cast<std::uint8_t>(axis + 1);
    if (range.magnitudes[axis] >= _state.Get(DS.maxAxisDistance))
    {
      return range;
    }
  }
  for (std::size_t axis = 0; axis < POSITION.size(); ++axis)
  {
    range.squaredHigh = Offset(range.squaredHigh, static_cast<std::uint16_t>(UnsignedSquare(range.magnitudes[axis]) >> 16));
    range.squares = static_cast<std::uint8_t>(axis + 1);
    if (axis != 0 && range.squaredHigh >= _state.Get(DS.maxDistanceSquaredHigh))
    {
      return range;
    }
  }
  range.within = true;
  _slot.Set(SlotByte::Size, Low(static_cast<std::uint16_t>(range.squaredHigh >> SQUARED_DISTANCE_SHIFT)));
  _slot.Set(SlotByte::Type, static_cast<std::uint8_t>(_slot.Get(SlotByte::Type) | SLOT_IN_RANGE));
  return range;
}

// What ShipWithinRange's code leaves in the registers, which CheckShipInRange's contract compares: the magnitudes it took in
// AX, BX and CX, then the last square in DX:AX and the high words' sum in BP, and within, that sum shifted right 6 in BP and AX.
void ShipRangeOut(Machine::Registers& _regs, const ShipRange& _range) noexcept
{
  _regs.ax = _range.magnitudes[0];
  if (_range.axes > 1)
  {
    _regs.bx = _range.magnitudes[1];
  }
  if (_range.axes > 2)
  {
    _regs.cx = _range.magnitudes[2];
  }
  if (_range.squares == 0)
  {
    return;
  }
  const std::uint32_t lastSquare = UnsignedSquare(_range.magnitudes[_range.squares - 1u]);
  _regs.ax = static_cast<std::uint16_t>(lastSquare);
  _regs.dx = static_cast<std::uint16_t>(lastSquare >> 16);
  _regs.bp = _range.squaredHigh;
  if (_range.within)
  {
    _regs.bp = static_cast<std::uint16_t>(_regs.bp >> SQUARED_DISTANCE_SHIFT);
    _regs.ax = _regs.bp;
  }
}

// One slot of TransformAndDrawObjects' first pass, DI the slot.
void ClassifyObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t state = Offset(regs.di, SLOT_FLAGS);
  _guest.SetByte(state, static_cast<std::uint8_t>(_guest.Byte(state) & ~STATE_DRAWN));
  if ((_guest.Byte(regs.di) & SLOT_ACTIVE) == 0)
  {
    return;
  }
  _guest.SetByte(regs.di, static_cast<std::uint8_t>(_guest.Byte(regs.di) & SLOT_KEEP_BITS));
  const std::uint16_t away = Offset(regs.di, SLOT_FRAMES_AWAY);
  if (_guest.Byte(away) != 0 && (_guest.Byte(state) & STATE_BLIP_SHOWN) == 0)
  {
    // Off the scanner: despawned on the 255th frame.
    _guest.SetByte(away, static_cast<std::uint8_t>(_guest.Byte(away) + 1));
    if (_guest.Byte(away) == 0)
    {
      _guest.Call(REMOVE_OBJECT);
      return;
    }
  }
  _guest.Call(IS_SUN_OR_PLANET);
  if (Flag(_guest, Machine::FLAG_ZERO))
  {
    TransformSunOrPlanet(_guest);
    return;
  }
  _guest.Call(IS_STATION);
  if (!Flag(_guest, Machine::FLAG_ZERO))
  {
    CheckShipInRangeEntry(_guest);
    if (!Flag(_guest, Machine::FLAG_CARRY))
    {
      TransformShip(_guest);
    }
    return;
  }
  // UpdateCompass leaves DI at the station's slot.
  _guest.Call(UPDATE_COMPASS);
  if (_guest.Get(DS.compassTargetIsStation) == 0 || _guest.Byte(Offset(regs.di, SLOT_DEPTH)) >= DISTANT_DEPTH)
  {
    return;
  }
  ClassifyStationPosition(_guest);
}

// The station in slot DI, when it is far enough away to draw as a disc: true if drawn so. One at depth 1
// nearer than 1B58h has its compass position doubled into its view position instead.
[[nodiscard]] bool DrawStationIfDistant(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t depth = _guest.Byte(Offset(regs.di, SLOT_DEPTH));
  if (depth == 0)
  {
    return false;
  }
  if (depth == 1)
  {
    regs.ax = _guest.Word(Offset(regs.di, SLOT_COMPASS_Z));
    if (regs.ax < DISTANT_STATION_Z)
    {
      regs.ax = static_cast<std::uint16_t>(regs.ax << 1);
      _guest.SetWord(Offset(regs.di, SLOT_VIEW_Z), regs.ax);
      regs.ax = static_cast<std::uint16_t>(_guest.Word(Offset(regs.di, SLOT_COMPASS_X)) << 1);
      _guest.SetWord(Offset(regs.di, SLOT_VIEW_X), regs.ax);
      regs.ax = static_cast<std::uint16_t>(_guest.Word(Offset(regs.di, SLOT_COMPASS_Y)) << 1);
      _guest.SetWord(Offset(regs.di, SLOT_VIEW_Y), regs.ax);
      return false;
    }
  }
  const std::uint16_t slot = regs.di;
  _guest.Call(DRAW_DISTANT_STATION);
  regs.di = slot;
  return true;
}

// _slot's flashing, for an object with +1Eh bit 5, the one that carries the device: true when it is in its off frames and not
// drawn.
[[nodiscard]] bool FlashedOff(ObjectSlot _slot)
{
  if ((_slot.Get(SlotByte::Flags) & STATE_FLASHING) == 0)
  {
    return false;
  }
  const bool off = (_slot.Get(SlotByte::Flags) & STATE_FLASH_OFF) != 0;
  _slot.Set(SlotByte::FlashFrames, static_cast<std::uint8_t>(_slot.Get(SlotByte::FlashFrames) - 1));
  if (_slot.Get(SlotByte::FlashFrames) != 0)
  {
    return off;
  }
  _slot.Set(SlotByte::Flags, static_cast<std::uint8_t>(_slot.Get(SlotByte::Flags) ^ STATE_FLASH_OFF));
  _slot.Set(SlotByte::FlashFrames, off ? FLASH_OFF_FRAMES : FLASH_ON_FRAMES);
  return false;
}

// DrawObjectAsDot (CS:3E8C): slot DI's view position as a disc of radius 2 in colour 3.
void DrawObjectAsDot(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  regs.ax = _guest.Word(Offset(regs.di, SLOT_VIEW_X));
  regs.bx = _guest.Word(Offset(regs.di, SLOT_VIEW_Y));
  regs.cx = _guest.Word(Offset(regs.di, SLOT_VIEW_Z));
  ProjectToScreen(_guest);
  regs.cx = regs.bx;
  regs.dx = regs.ax;
  _guest.Set(DS.drawColor, DOT_COLOR);
  regs.bx = DOT_RADIUS;
  _guest.Call(DRAW_DISC);
  regs.di = slot;
}

// The not yet drawn visible object in slot DI: by its kind, its distance and its level of detail.
void DrawObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(IS_SUN_OR_PLANET);
  if (Flag(_guest, Machine::FLAG_ZERO))
  {
    const std::uint16_t slot = regs.di;
    DrawSunOrPlanet(_guest);
    regs.di = slot;
    return;
  }
  _guest.Call(IS_STATION);
  if (Flag(_guest, Machine::FLAG_ZERO) && DrawStationIfDistant(_guest))
  {
    return;
  }
  if (FlashedOff(ObjectSlot(_guest.State(), regs.di)))
  {
    return;
  }
  regs.ax = WithLow(regs.ax, _guest.Byte(Offset(regs.di, SLOT_DETAIL)));
  if (Low(regs.ax) < _guest.Byte(Offset(regs.di, SLOT_SIZE)))
  {
    DrawObjectAsDot(_guest);
    return;
  }
  regs.bx = Offset(static_cast<std::uint16_t>(_guest.Byte(regs.di) & SLOT_TYPE_BITS), DS.blueprintTable.offset);
  regs.si = _guest.Word(regs.bx);
  regs.ax = _guest.Word(Offset(regs.di, SLOT_PITCH));
  _guest.Set(DS.drawPitchAngle, regs.ax);
  regs.ax = _guest.Word(Offset(regs.di, SLOT_YAW));
  _guest.Set(DS.drawYawAngle, regs.ax);
  regs.ax = _guest.Word(Offset(regs.di, SLOT_ROLL));
  _guest.Set(DS.drawRollAngle, regs.ax);
  regs.ax = _guest.Word(Offset(regs.di, SLOT_VIEW_X));
  _guest.Set(DS.drawCenterX, regs.ax);
  regs.ax = _guest.Word(Offset(regs.di, SLOT_VIEW_Y));
  _guest.Set(DS.drawCenterY, regs.ax);
  regs.ax = _guest.Word(Offset(regs.di, SLOT_VIEW_Z));
  _guest.Set(DS.drawCenterZ, regs.ax);
  RunBlueprintHandler(_guest);
}

// DrawFarthestObject (CS:3D95): marks and draws the visible object not yet drawn with the largest depth
// class, then the largest z. False when none is left.
[[nodiscard]] bool DrawFarthestObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = _guest.Get(DS.shipSlotCount);
  regs.di = DS.shipSlots.offset;
  regs.bp = 0;
  regs.ax = WithHigh(regs.ax, 0);
  regs.dx = 0xFFFF;
  do
  {
    regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(_guest.Byte(regs.di) & SLOT_DRAWABLE));
    if (Low(regs.ax) == SLOT_DRAWABLE && (_guest.Byte(Offset(regs.di, SLOT_FLAGS)) & STATE_DRAWN) == 0)
    {
      const std::uint8_t depth = _guest.Byte(Offset(regs.di, SLOT_DEPTH));
      if (High(regs.ax) < depth || (High(regs.ax) == depth && regs.bp < _guest.Word(Offset(regs.di, SLOT_VIEW_Z))))
      {
        regs.ax = WithHigh(regs.ax, depth);
        regs.bp = _guest.Word(Offset(regs.di, SLOT_VIEW_Z));
        regs.dx = regs.di;
      }
    }
    regs.di = Offset(regs.di, SLOT_BYTES);
  } while (Loop(regs.cx));
  if (regs.dx == 0xFFFF)
  {
    regs.dx = 0;
    return false;
  }
  regs.di = regs.dx;
  OrByte(_guest, Offset(regs.di, SLOT_FLAGS), STATE_DRAWN);
  DrawObject(_guest);
  return true;
}

// One coordinate of the sun's or planet's center: 256|_value| / z, z at slot DI's +14h, rounded down,
// with _value's sign put back. BP holds the sign as the original keeps it.
void ProjectDiscCoordinate(Guest& _guest, std::uint16_t _value, std::uint16_t _returnOffset)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bp = 0;
  regs.ax = _value;
  if (Negative(regs.ax))
  {
    regs.bp = 1;
    regs.ax = Negate(regs.ax);
  }
  ShiftIntoDividend(regs, Negative(regs.ax) ? HIGH_BYTE_ONES : HIGH_BYTE_ZERO);
  DivideUnsignedOnRegisters(_guest, _guest.Word(Offset(regs.di, SLOT_VIEW_Z)), _returnOffset);
  regs.bp = static_cast<std::uint16_t>(regs.bp - 1);
  if (regs.bp == 0)
  {
    regs.ax = Negate(regs.ax);
  }
}

// What DiscWithin finds, and the last edge it computed, center + radius or center - radius, where the original leaves it in AX.
struct DiscFit
{
  bool within;
  std::uint16_t edge;
};

// _center + _radius must not overflow or be negative, and _center - _radius must not overflow or pass _limit.
[[nodiscard]] DiscFit DiscWithin(std::uint16_t _center, std::uint16_t _radius, std::uint16_t _limit) noexcept
{
  const auto upperEdge = static_cast<std::uint16_t>(_center + _radius);
  if (AddOverflows(_center, _radius) || Negative(upperEdge))
  {
    return DiscFit{false, upperEdge};
  }
  const auto once = static_cast<std::uint16_t>(upperEdge - _radius);
  const auto lowerEdge = static_cast<std::uint16_t>(once - _radius);
  return DiscFit{!SubtractOverflows(once, _radius) && static_cast<std::int16_t>(lowerEdge) <= static_cast<std::int16_t>(_limit), lowerEdge};
}

// DrawSunOrPlanetDisc (CS:402C): radius AX at slot DI's projected center, in its colour (+0Bh), drawn only
// when the disc's whole box is on the 256x128 view.
void DrawSunOrPlanetDisc(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = regs.ax;
  ProjectDiscCoordinate(_guest, _guest.Word(Offset(regs.di, SLOT_VIEW_Y)), DISC_Y_DIVIDE_RETURN);
  regs.ax = Offset(regs.ax, SCREEN_CENTER_Y);
  regs.cx = regs.ax;
  ProjectDiscCoordinate(_guest, _guest.Word(Offset(regs.di, SLOT_VIEW_X)), DISC_X_DIVIDE_RETURN);
  regs.ax = Offset(regs.ax, SCREEN_CENTER_X);
  regs.dx = regs.ax;
  const DiscFit across = DiscWithin(regs.dx, regs.bx, SCREEN_RIGHT);
  regs.ax = across.edge;
  if (!across.within)
  {
    return;
  }
  const DiscFit down = DiscWithin(regs.cx, regs.bx, SCREEN_BOTTOM);
  regs.ax = down.edge;
  if (!down.within)
  {
    return;
  }
  regs.bx = static_cast<std::uint16_t>(regs.bx << 1);
  regs.ax = WithLow(regs.ax, _guest.Byte(Offset(regs.di, SLOT_COLOR)));
  _guest.Set(DS.drawColor, Low(regs.ax));
  _guest.Call(DRAW_DISC);
}

// Once supernovaFrames has counted down to 1, the sun's heat is supernovaHeat, growing by a quarter (at
// least 1) a frame and killing the player when it overflows. True when it stands for the sun's size, in AX.
[[nodiscard]] bool SupernovaHeat(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.supernovaFrames) == 0)
  {
    return false;
  }
  _guest.Set(DS.supernovaFrames, static_cast<std::uint16_t>(_guest.Get(DS.supernovaFrames) - 1));
  if (_guest.Get(DS.supernovaFrames) != 0)
  {
    return false;
  }
  _guest.Set(DS.supernovaFrames, 1);
  if (_guest.Get(DS.supernovaHeat) == 0)
  {
    regs.ax = WithLow(regs.ax, _guest.Get(DS.cabinTemperature));
    _guest.Set(DS.supernovaHeat, Low(regs.ax));
  }
  auto step = static_cast<std::uint8_t>(_guest.Get(DS.supernovaHeat) >> 2);
  if (step == 0)
  {
    step = 1;
  }
  const unsigned heat = unsigned{step} + _guest.Get(DS.supernovaHeat);
  regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(heat));
  if (heat > 0xFF)
  {
    _guest.Call(KILL_PLAYER);
    _guest.Call(DETONATE_ENERGY_BOMB);
    regs.ax = WithLow(regs.ax, 0xFF);
  }
  _guest.Set(DS.supernovaHeat, Low(regs.ax));
  regs.ax = WithHigh(regs.ax, 0);
  return true;
}

// The sun's size, cabin temperature, fringe, fuel scooping and heat death: false when it is behind the
// view and not drawn.
[[nodiscard]] bool SizeSun(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (!SupernovaHeat(_guest))
  {
    regs.dx = SUN_SCALE;
    regs.ax = 0;
    ScaleByInverseDistanceEntry(_guest);
  }
  _guest.Set(DS.cabinTemperature, Low(regs.ax));
  if ((ObjectSlot(_guest.State(), regs.di).Get(SlotByte::ViewZHigh) & 0x80) != 0)
  {
    return false;
  }
  _guest.Set(DS.sunFringeMask, FRINGE_SMALL);
  if (regs.ax < FRINGE_RADIUS_SMALL)
  {
    return true;
  }
  _guest.Set(DS.sunFringeMask, FRINGE_MEDIUM);
  if (regs.ax < FRINGE_RADIUS_LARGE)
  {
    return true;
  }
  _guest.Set(DS.sunFringeMask, FRINGE_LARGE);
  if (regs.ax < SCOOP_RADIUS)
  {
    return true;
  }
  if (_guest.Get(DS.fuelScoopsFitted) == 1)
  {
    const unsigned fuel = unsigned{_guest.Get(DS.fuel)} + SCOOP_FUEL;
    _guest.Set(DS.fuel, static_cast<std::uint8_t>(fuel));
    if (fuel > 0xFF)
    {
      _guest.Set(DS.fuel, 0xFF);
      regs.bx = DS.fuelScoopsActiveText.offset;
      _guest.Set(DS.messagePointer, regs.bx);
      _guest.Set(DS.messageFrames, SCOOP_MESSAGE_FRAMES);
    }
  }
  if (regs.ax >= FATAL_RADIUS)
  {
    // KillPlayer may change AL, and the radius with it.
    _guest.Call(KILL_PLAYER);
    if (High(regs.ax) != 0)
    {
      regs.ax = RADIUS_LIMIT;
    }
  }
  return true;
}

// What SizePlanet finds.
struct PlanetSize
{
  InverseDistanceScale scale;          // ScaleByInverseDistance's result, which the original leaves in AX and BX
  std::uint8_t altitude;               // over BL
  std::optional<std::uint16_t> radius; // in front of the view, the disc's radius
};

// SizePlanet (CS:3FF6): sunFringeMask cleared, the radius ScaleByInverseDistance makes of 50 at _slot's distance, and the
// altitude, twice 255 less that radius held to 127. In front of the view, a radius of FDh or more kills the player, with
// StartPlayerDeathSound's STI, and one past a byte draws as FFh.
PlanetSize SizePlanet(GameState& _state, Hardware& _hardware, ObjectSlot _slot)
{
  _state.Set(DS.sunFringeMask, FRINGE_NONE);
  // MOV DX,32h / XOR AX,AX: DX:AX = 50 * 65536.
  const InverseDistanceScale scale = ScaleByInverseDistance(_state, _slot, std::uint32_t{PLANET_SCALE} << 16);
  auto altitude = static_cast<std::uint8_t>(~Low(scale.scaled));
  if (altitude > ALTITUDE_LIMIT)
  {
    altitude = ALTITUDE_LIMIT;
  }
  altitude = static_cast<std::uint8_t>(altitude << 1);
  _state.Set(DS.altitude, altitude);
  PlanetSize size{scale, altitude, std::nullopt};
  if ((_slot.Get(SlotByte::ViewZHigh) & 0x80) != 0)
  {
    return size;
  }
  std::uint16_t radius = scale.scaled;
  if (radius >= FATAL_RADIUS)
  {
    // PUSH AX / CALL KillPlayer / POP AX.
    if (KillPlayer(_state))
    {
      _hardware.EnableInterrupts();
    }
    if (High(radius) != 0)
    {
      radius = RADIUS_LIMIT;
    }
  }
  size.radius = radius;
  return size;
}

// SizePlanet for slot DI, and what it leaves for DrawSunOrPlanetDisc and DrawSunOrPlanet's callers: AX the radius, or
// ScaleByInverseDistance's result behind the view, and BX the divisor's high byte over the altitude. False when the planet is
// behind the view and not drawn.
[[nodiscard]] bool SizePlanetOnRegisters(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const PlanetSize size = SizePlanet(_guest.State(), _guest.Devices(), ObjectSlot(_guest.State(), regs.di));
  regs.ax = size.radius.value_or(size.scale.scaled);
  regs.bx = Join(High(size.scale.divisor), size.altitude);
  return size.radius.has_value();
}

} // namespace

void ProjectVertices(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = _guest.Get(DS.projectedVertexCount);
  if (regs.cx == 0)
  {
    return;
  }
  const std::uint16_t caller = regs.si;
  regs.si = DS.vertexBuffer.offset;
  regs.di = regs.si;
  do
  {
    regs.ax = _guest.Get(DS.nearPlaneZ);
    if (static_cast<std::int16_t>(_guest.Word(Offset(regs.si, VERTEX_Z))) < static_cast<std::int16_t>(regs.ax))
    {
      _guest.SetWord(regs.di, NEAR_VERTEX_X);
    }
    else
    {
      ProjectVertexCoordinate(_guest, _guest.Word(regs.si), VERTEX_X_DIVIDE_RETURN);
      regs.ax = Offset(regs.ax, SCREEN_CENTER_X);
      _guest.SetWord(regs.di, regs.ax);
      ProjectVertexCoordinate(_guest, _guest.Word(Offset(regs.si, 2)), VERTEX_Y_DIVIDE_RETURN);
      regs.ax = Offset(regs.ax, SCREEN_CENTER_Y);
      _guest.SetWord(Offset(regs.di, 2), regs.ax);
    }
    regs.di = Offset(regs.di, POINT_BYTES);
    regs.si = Offset(regs.si, VERTEX_BYTES);
  } while (Loop(regs.cx));
  regs.si = caller;
}

Vector ReflectVertexAboutCenter(GameState& _state, std::uint16_t _vertex, std::uint16_t _reflection)
{
  std::array<std::uint16_t, 3> center{};
  constexpr std::array<DataField<std::uint16_t>, 3> CENTER = {DS.drawCenterX, DS.drawCenterY, DS.drawCenterZ};
  for (std::size_t axis = 0; axis < CENTER.size(); ++axis)
  {
    const auto bytes = static_cast<std::uint16_t>(2 * axis);
    center[axis] = _state.Get(CENTER[axis]);
    _state.SetWord(Offset(_reflection, bytes), static_cast<std::uint16_t>(center[axis] - _state.Word(Offset(_vertex, bytes))));
    _state.SetWord(Offset(_vertex, bytes), Offset(_state.Word(Offset(_vertex, bytes)), center[axis]));
  }
  return Vector{static_cast<std::int16_t>(center[0]), static_cast<std::int16_t>(center[1]), static_cast<std::int16_t>(center[2])};
}

Vector OffsetVertexByCenter(GameState& _state, std::uint16_t _vertex)
{
  std::array<std::uint16_t, 3> center{};
  constexpr std::array<DataField<std::uint16_t>, 3> CENTER = {DS.drawCenterX, DS.drawCenterY, DS.drawCenterZ};
  for (std::size_t axis = 0; axis < CENTER.size(); ++axis)
  {
    const auto bytes = static_cast<std::uint16_t>(2 * axis);
    center[axis] = _state.Get(CENTER[axis]);
    _state.SetWord(Offset(_vertex, bytes), Offset(_state.Word(Offset(_vertex, bytes)), center[axis]));
  }
  return Vector{static_cast<std::int16_t>(center[0]), static_cast<std::int16_t>(center[1]), static_cast<std::int16_t>(center[2])};
}

std::uint16_t BuildBoxCornerVertices(GameState& _state, std::uint16_t _extents)
{
  const std::uint16_t extents = _state.Word(_extents);
  SetDrawAngles(_state);
  // q, the half-height, from the low byte; h, the half-length, from the high, negated; p is boxHalfWidth.
  const std::uint16_t height = Low(extents);
  const std::uint16_t length = Negate(std::uint16_t{High(extents)});
  const std::uint16_t corners = DS.boxCornerVertices.offset;
  _state.SetWord(Offset(corners, VERTEX_Z), length);
  _state.SetWord(Offset(corners, VERTEX_BYTES + VERTEX_Z), length);
  _state.SetWord(Offset(corners, 2 * VERTEX_BYTES + VERTEX_Z), length);
  // (p, q) by -roll, and its opposite, (-p, -q); then (p, -q).
  const Pair upper =
    RotateByStoredSinCos(_state, 3, Pair{static_cast<std::int16_t>(_state.Get(DS.boxHalfWidth)), static_cast<std::int16_t>(height)});
  _state.SetWord(corners, static_cast<std::uint16_t>(upper.first));
  _state.SetWord(Offset(corners, 2), static_cast<std::uint16_t>(upper.second));
  _state.SetWord(Offset(corners, 2 * VERTEX_BYTES), Negate(static_cast<std::uint16_t>(upper.first)));
  _state.SetWord(Offset(corners, 2 * VERTEX_BYTES + 2), Negate(static_cast<std::uint16_t>(upper.second)));
  const Pair lower = RotateByStoredSinCos(
    _state, 3, Pair{static_cast<std::int16_t>(_state.Get(DS.boxHalfWidth)), static_cast<std::int16_t>(Negate(height))});
  _state.SetWord(Offset(corners, VERTEX_BYTES), static_cast<std::uint16_t>(lower.first));
  _state.SetWord(Offset(corners, VERTEX_BYTES + 2), static_cast<std::uint16_t>(lower.second));
  RotateVertices(_state, corners, BOX_CORNERS_ROTATED, 0, VERTEX_Z, 4);
  RotateVertices(_state, corners, BOX_CORNERS_ROTATED, 2, VERTEX_Z, 5);
  RotateVertices(_state, corners, BOX_CORNERS_ROTATED, 0, VERTEX_Z, 1);
  RotateVertices(_state, corners, BOX_CORNERS_ROTATED, 0, 2, 2);
  RotateVerticesToView(_state, corners, BOX_CORNERS_ROTATED);

  // The fourth corner completes the parallelogram, v2 - v1 + v0; the fifth and eighth are the second and third
  // again, and four reflections through the centre make the far face.
  for (std::uint16_t word = corners; word != Offset(corners, 2 * BOX_CORNERS_ROTATED); word = Offset(word, 2))
  {
    _state.SetWord(Offset(word, BOX_FOURTH_CORNER),
                   static_cast<std::uint16_t>(_state.Word(Offset(word, 2 * VERTEX_BYTES)) - _state.Word(Offset(word, VERTEX_BYTES)) +
                                              _state.Word(word)));
  }
  std::uint16_t source = BOX_SECOND_CORNER;
  for (const std::uint16_t copy : {BOX_FIFTH_CORNER, BOX_EIGHTH_CORNER})
  {
    for (std::uint16_t word = 0; word != 2 * BOX_CORNERS_ROTATED; word = Offset(word, 2))
    {
      _state.SetWord(Offset(copy, word), _state.Word(source));
      source = Offset(source, 2);
    }
  }
  std::uint16_t vertex = corners;
  std::uint16_t reflection = BOX_SEVENTH_CORNER;
  (void)ReflectVertexAboutCenter(_state, vertex, reflection);
  vertex = Offset(vertex, BOX_REFLECTION_STEP);
  reflection = static_cast<std::uint16_t>(reflection - BOX_REFLECTION_STEP);
  (void)ReflectVertexAboutCenter(_state, vertex, reflection);
  vertex = Offset(vertex, BOX_REFLECTION_SKIP);
  reflection = static_cast<std::uint16_t>(reflection - VERTEX_BYTES);
  (void)ReflectVertexAboutCenter(_state, vertex, reflection);
  vertex = static_cast<std::uint16_t>(vertex - BOX_REFLECTION_STEP);
  reflection = Offset(reflection, BOX_REFLECTION_STEP);
  (void)ReflectVertexAboutCenter(_state, vertex, reflection);
  return Offset(_extents, 2);
}

Vector BuildDodoVertices(GameState& _state)
{
  SetDrawAngles(_state);
  // From the roll angle, in 72-degree steps: the sine and cosine high bytes of each, scaled to the two rings.
  const auto ringRadii = [&_state](std::uint16_t _angle)
  { return ScaleDodoRadii(static_cast<std::int8_t>(_state.Byte(Offset(_angle, SINE_HIGH_BYTES)))); };
  auto angle = static_cast<std::uint16_t>((_state.Get(DS.drawRollAngle) & ANGLE_BITS) << 1);
  std::uint16_t vertex = DS.vertexBuffer.offset;
  for (std::uint16_t left = DODO_RING_VERTICES; left != 0; --left)
  {
    const DodoRadii sine = ringRadii(angle);
    _state.SetWord(vertex, static_cast<std::uint16_t>(sine.inner));
    _state.SetWord(Offset(vertex, DODO_OUTER_RING), static_cast<std::uint16_t>(sine.outer));
    _state.SetWord(Offset(vertex, VERTEX_Z), DODO_NEAR_RING_Z);
    _state.SetWord(Offset(vertex, DODO_OUTER_RING + VERTEX_Z), DODO_FAR_RING_Z);
    angle = static_cast<std::uint16_t>((angle - QUARTER_TURN_BYTES) & SINE_BYTE_BITS);
    const DodoRadii cosine = ringRadii(angle);
    _state.SetWord(Offset(vertex, 2), static_cast<std::uint16_t>(cosine.inner));
    _state.SetWord(Offset(vertex, DODO_OUTER_RING + 2), static_cast<std::uint16_t>(cosine.outer));
    angle = static_cast<std::uint16_t>((angle + DODO_RING_STEP_BYTES) & SINE_BYTE_BITS);
    vertex = Offset(vertex, VERTEX_BYTES);
  }
  // The slot: two corners at half the high bytes, and their opposites.
  const auto slotHalf = [&_state](std::uint16_t _angle) { return Sar(SignExtend(_state.Byte(Offset(_angle, SINE_HIGH_BYTES))), 1); };
  vertex = Offset(vertex, DODO_OUTER_RING);
  angle = static_cast<std::uint16_t>((angle + DODO_SLOT_START_BYTES) & SINE_BYTE_BITS);
  for (std::uint16_t left = DODO_SLOT_VERTICES; left != 0; --left)
  {
    const std::uint16_t x = slotHalf(angle);
    _state.SetWord(vertex, x);
    _state.SetWord(Offset(vertex, DODO_SLOT_OPPOSITE), Negate(x));
    _state.SetWord(Offset(vertex, VERTEX_Z), DODO_SLOT_Z);
    _state.SetWord(Offset(vertex, DODO_SLOT_OPPOSITE + VERTEX_Z), DODO_SLOT_Z);
    angle = static_cast<std::uint16_t>((angle - QUARTER_TURN_BYTES) & SINE_BYTE_BITS);
    const std::uint16_t y = slotHalf(angle);
    _state.SetWord(Offset(vertex, 2), y);
    _state.SetWord(Offset(vertex, DODO_SLOT_OPPOSITE + 2), Negate(y));
    angle = static_cast<std::uint16_t>((angle + HALF_TURN_BYTES) & SINE_BYTE_BITS);
    vertex = Offset(vertex, VERTEX_BYTES);
  }
  RotateVertices(_state, DS.vertexBuffer.offset, DODO_ROTATED_VERTICES, 0, VERTEX_Z, 4);
  RotateVertices(_state, DS.vertexBuffer.offset, DODO_ROTATED_VERTICES, 2, VERTEX_Z, 5);
  RotateVertices(_state, DS.vertexBuffer.offset, DODO_ROTATED_VERTICES, 0, VERTEX_Z, 1);
  RotateVertices(_state, DS.vertexBuffer.offset, DODO_ROTATED_VERTICES, 0, 2, 2);
  RotateVerticesToView(_state, DS.vertexBuffer.offset, DODO_ROTATED_VERTICES);
  // The first ten reflected through the centre, and the slot's four moved to it.
  vertex = DS.vertexBuffer.offset;
  std::uint16_t reflection = DODO_REFLECTED;
  for (std::uint16_t left = DODO_REFLECTED_VERTICES; left != 0; --left)
  {
    (void)ReflectVertexAboutCenter(_state, vertex, reflection);
    vertex = Offset(vertex, VERTEX_BYTES);
    reflection = Offset(reflection, VERTEX_BYTES);
  }
  Vector center{};
  for (std::uint16_t left = DODO_SLOT_CORNERS; left != 0; --left)
  {
    center = OffsetVertexByCenter(_state, vertex);
    vertex = Offset(vertex, VERTEX_BYTES);
  }
  return center;
}

DodoRadii ScaleDodoRadii(std::int8_t _value)
{
  // CBW, then CX = 2a + a/4 + a/8 - a/32 and DX = 3a + a/2 + a/4 + a/16 - a/64, each shift arithmetic, in 16 bits.
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

VertexProgramEnd RunVertexProgram(GameState& _state, std::uint16_t _program, Vector _accumulator)
{
  auto x = static_cast<std::uint16_t>(_accumulator.x);
  auto y = static_cast<std::uint16_t>(_accumulator.y);
  auto z = static_cast<std::uint16_t>(_accumulator.z);
  const std::uint8_t ops = _state.Byte(_program);
  std::uint16_t next = Offset(_program, 1);
  for (std::uint16_t left = ops; left != 0; --left)
  {
    const std::uint8_t op = _state.Byte(next);
    next = Offset(next, 1);
    const std::uint16_t vertex = DS.vertexBuffer.At(op & VERTEX_INDEX_BITS);
    const std::uint16_t vertexX = _state.Word(vertex);
    const std::uint16_t vertexY = _state.Word(Offset(vertex, 2));
    const std::uint16_t vertexZ = _state.Word(Offset(vertex, 4));
    switch (static_cast<std::uint8_t>(op & VERTEX_OP_BITS))
    {
    case VERTEX_OP_LOAD:
      x = vertexX;
      y = vertexY;
      z = vertexZ;
      break;
    case VERTEX_OP_STORE:
      _state.SetWord(vertex, x);
      _state.SetWord(Offset(vertex, 2), y);
      _state.SetWord(Offset(vertex, 4), z);
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
  return VertexProgramEnd{next, Vector{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(z)}};
}

bool TriangleWindingSign(Triangle _triangle)
{
  const WindingProducts products = Winding(_triangle);
  const auto high = static_cast<std::uint16_t>((products.first >> 16) - (products.second >> 16));
  const std::uint16_t sign = high != 0 ? high : static_cast<std::uint16_t>(products.first - products.second);
  return Negative(sign);
}

void DrawVisibleFaces(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (regs.cx == 0)
  {
    return;
  }
  do
  {
    const std::uint16_t faces = regs.cx;
    TriangleOut(regs, LoadTriangle(_guest.State(), regs.si, 0));
    TriangleWindingSignEntry(_guest);
    regs.cx = WithHigh(regs.cx, 0);
    if (Flag(_guest, Machine::FLAG_SIGN))
    {
      DrawFaceItems(_guest);
    }
    else
    {
      // SkipHiddenFace: stc / adc si, cx over the count byte and the items.
      regs.cx = WithLow(regs.cx, _guest.Byte(regs.si));
      regs.si = Offset(regs.si, Offset(regs.cx, 1));
    }
    regs.cx = faces;
  } while (Loop(regs.cx));
}

ShipRangeCheck CheckShipInRange(GameState& _state, ObjectSlot _slot)
{
  ShipRangeCheck check{IsObjectNear(_state, _slot), ShipRange{}, std::nullopt};
  if (check.near.nearby)
  {
    check.range = ShipWithinRange(_state, _slot);
    if (check.range.within)
    {
      return check;
    }
  }
  check.erasedBlip = EraseScannerBlip(_state, _slot);
  return check;
}

void TransformSunOrPlanet(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  GetPositionScaleShiftEntry(_guest);
  _guest.SetByte(Offset(regs.di, SLOT_SCALE_SHIFT), Low(regs.cx));
  _guest.SetByte(Offset(regs.di, SLOT_DEPTH), Low(regs.cx));
  regs.dx = WithHigh(regs.dx, Low(regs.cx));
  ScalePositionDownEntry(_guest);
  const std::uint16_t slot = regs.di;
  TransformToViewWithBlip(_guest);
  regs.di = slot;
  _guest.SetWord(Offset(regs.di, SLOT_VIEW_X), regs.ax);
  _guest.SetWord(Offset(regs.di, SLOT_VIEW_Y), regs.bx);
  _guest.SetWord(Offset(regs.di, SLOT_VIEW_Z), regs.cx);
  OrByte(_guest, regs.di, SLOT_VISIBLE);
}

void ClassifyStationPosition(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Word(Offset(regs.di, SLOT_COMPASS_X));
  regs.bx = _guest.Word(Offset(regs.di, SLOT_COMPASS_Y));
  regs.cx = _guest.Word(Offset(regs.di, SLOT_COMPASS_Z));
  ClassifyViewPositionOnRegisters(_guest);
}

void TransformShip(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Word(Offset(regs.di, SLOT_POSITION_X));
  regs.bx = _guest.Word(Offset(regs.di, SLOT_POSITION_Y));
  regs.cx = _guest.Word(Offset(regs.di, SLOT_POSITION_Z));
  const std::uint16_t slot = regs.di;
  TransformToViewWithBlip(_guest);
  if (_guest.Get(DS.fuelScoopsFitted) == 1 && _guest.Get(DS.gameOverFrames) == 0)
  {
    _guest.Call(TRY_SCOOP_OBJECT);
  }
  regs.di = slot;
  _guest.SetByte(Offset(regs.di, SLOT_DEPTH), 0);
  ClassifyViewPositionOnRegisters(_guest);
}

void RunBlueprintHandler(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  regs.ax = _guest.Word(regs.si);
  regs.si = Offset(regs.si, 2);
  regs.bx = _guest.Byte(regs.si);
  _guest.Set(DS.boxHalfWidth, regs.bx);
  regs.si = Offset(regs.si, 1);
  // The handler returns to RenderBlueprintBody (CS:3CF2), which the original pushed as its return address.
  _guest.Call(regs.ax);
  RenderBlueprint(_guest);
  regs.di = slot;
}

void RenderBlueprintBody(Guest& _guest)
{
  RenderBlueprint(_guest);
  _guest.Regs().di = _guest.Pop();
}

void TransformAndDrawObjects(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Get(DS.playerPitchAngle);
  SetSinCosEntry(_guest, DS.rotationSinCos.At(0));
  regs.ax = _guest.Get(DS.playerYawAngle);
  SetSinCosEntry(_guest, DS.rotationSinCos.At(1));
  regs.ax = _guest.Get(DS.playerRollAngle);
  SetSinCosEntry(_guest, DS.rotationSinCos.At(2));
  regs.di = DS.shipSlots.offset;
  regs.cx = _guest.Get(DS.shipSlotCount);
  do
  {
    const std::uint16_t slots = regs.cx;
    ClassifyObject(_guest);
    regs.di = Offset(regs.di, SLOT_BYTES);
    regs.cx = slots;
  } while (Loop(regs.cx));
  while (DrawFarthestObject(_guest))
  {
  }
}

void TransformToViewWithBlip(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  RotatePitchYawRollEntry(_guest);
  _guest.SetByte(Offset(regs.di, SLOT_CAMERA_Z_HIGH), High(regs.cx));
  _guest.Call(UPDATE_SCANNER_BLIP);
  RotateToViewDirectionOnRegisters(_guest);
}

void TransformToView(Guest& _guest)
{
  RotatePitchYawRollEntry(_guest);
  RotateToViewDirectionOnRegisters(_guest);
}

void DrawSunOrPlanet(Guest& _guest)
{
  _guest.Call(IS_PLANET);
  const bool drawn = Flag(_guest, Machine::FLAG_ZERO) ? SizePlanetOnRegisters(_guest) : SizeSun(_guest);
  if (drawn)
  {
    DrawSunOrPlanetDisc(_guest);
  }
}

void DrawDistantStation(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Word(Offset(regs.di, SLOT_COMPASS_X));
  regs.bx = _guest.Word(Offset(regs.di, SLOT_COMPASS_Y));
  regs.cx = _guest.Word(Offset(regs.di, SLOT_COMPASS_Z));
  ProjectToScreen(_guest);
  regs.dx = regs.ax;
  regs.cx = regs.bx;
  _guest.Set(DS.drawColor, STATION_COLOR);
  SetLow(regs.ax, _guest.Byte(Offset(regs.di, SLOT_STATION_DEPTH)));
  if (Low(regs.ax) < NEAR_STATION_DEPTH)
  {
    regs.bx = NEAR_STATION_RADIUS;
    _guest.Call(DRAW_DISC);
    return;
  }
  // r = (21h - depth) / 2, as bytes: none when 25h - depth, taken as signed, is 4 or less, or r is 0.
  SetLow(regs.ax, Negate(static_cast<std::uint8_t>(Low(regs.ax) - STATION_DEPTH_BASE)));
  const auto beyond = static_cast<std::int8_t>(Low(regs.ax));
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) - STATION_DEPTH_SMALLEST));
  if (beyond <= static_cast<std::int8_t>(STATION_DEPTH_SMALLEST))
  {
    return;
  }
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) >> 1));
  if (Low(regs.ax) == 0)
  {
    return;
  }
  regs.bx = static_cast<std::uint16_t>(Low(regs.ax) << 1);
  _guest.Call(DRAW_DISC);
}

SinCos LoadPlayerAngles(GameState& _state)
{
  (void)SetSinCos(_state, 0, _state.Get(DS.playerPitchAngle));
  (void)SetSinCos(_state, 1, _state.Get(DS.playerYawAngle));
  return SetSinCos(_state, 2, _state.Get(DS.playerRollAngle));
}

void ProjectToScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bp = 0;
  if (Negative(regs.ax))
  {
    regs.ax = Negate(regs.ax);
    regs.bp = 2;
  }
  if (Negative(regs.bx))
  {
    regs.bx = Negate(regs.bx);
    regs.bp = Offset(regs.bp, 1);
  }
  ShiftIntoDividend(regs, HIGH_BYTE_ZERO);
  DivideUnsignedOnRegisters(_guest, regs.cx, SCREEN_X_DIVIDE_RETURN);
  std::swap(regs.ax, regs.bx);
  ShiftIntoDividend(regs, HIGH_BYTE_ZERO);
  DivideUnsignedOnRegisters(_guest, regs.cx, SCREEN_Y_DIVIDE_RETURN);
  if ((regs.bp & 1) != 0)
  {
    regs.ax = Negate(regs.ax);
  }
  if ((regs.bp & 2) != 0)
  {
    regs.bx = Negate(regs.bx);
  }
  regs.bp = 0;
  std::swap(regs.ax, regs.bx);
  regs.ax = Offset(regs.ax, SCREEN_CENTER_X);
  regs.bx = Offset(regs.bx, SCREEN_CENTER_Y);
}

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_SIGN;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

// "Clobbers all": every register but DS, which no routine of the reference's changes for its caller.
constexpr std::uint16_t ALL_BUT_DS =
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP | REGISTER_ES;

constexpr Machine::NativeContract RETURNS_CARRY{0, FLAG_CARRY};

// The rotations' leftover in DX, which no caller reads (ADR-012).
constexpr Machine::NativeContract CLOBBERS_DX{REGISTER_DX, 0};

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract BUILDS_BOX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_DI{REGISTER_AX | REGISTER_DI, 0};
// TriangleWindingSign changes only AX, BX, CX, DX and DI. DrawVisibleFaces reads SI after it, and RenderBlueprintBody's contract
// compares the AX, BX, DX, BP and ES it leaves after a hidden last face (ADR-012).
constexpr Machine::NativeContract WINDING_SIGN{REGISTER_CX | REGISTER_DI, FLAG_SIGN};

} // namespace

// ── The entries of the routines de-assembled (ADR-012) ──

void ReflectVertexAboutCenterEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Vector center = ReflectVertexAboutCenter(_guest.State(), regs.si, regs.di);
  // The original leaves drawCenterZ in BX, and BuildDodoVertices, whose contract compares BX, ends with it there.
  regs.bx = static_cast<std::uint16_t>(center.z);
  _guest.Clobber(CLOBBERS_AX);
}

void OffsetVertexByCenterEntry(Guest& _guest)
{
  const Vector center = OffsetVertexByCenter(_guest.State(), _guest.Regs().si);
  // The original leaves drawCenterZ in AX, and BuildDodoVertices, whose contract compares AX, ends with it there.
  _guest.Regs().ax = static_cast<std::uint16_t>(center.z);
  _guest.Clobber(PRESERVES_ALL);
}

void BuildBoxCornerVerticesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = BuildBoxCornerVertices(_guest.State(), regs.si);
  _guest.Clobber(BUILDS_BOX);
}

void BuildDodoVerticesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Vector center = BuildDodoVertices(_guest.State());
  // The contract compares what the original's loops leave: drawCenterZ in AX from the last OffsetVertexByCenter and in BX from the
  // last ReflectVertexAboutCenter, CX = 0, and DI past the reflections. PUSH SI / POP SI keep SI.
  regs.ax = static_cast<std::uint16_t>(center.z);
  regs.bx = regs.ax;
  regs.cx = 0;
  regs.di = static_cast<std::uint16_t>(DODO_REFLECTED + DODO_REFLECTED_VERTICES * VERTEX_BYTES);
  _guest.Clobber(CLOBBERS_DX);
}

void LoadPlayerAnglesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const SinCos roll = LoadPlayerAngles(_guest.State());
  regs.ax = static_cast<std::uint16_t>(roll.sine);
  regs.bx = static_cast<std::uint16_t>(roll.cosine);
  _guest.Clobber(PRESERVES_ALL);
}

void ScaleDodoRadiiEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const DodoRadii radii = ScaleDodoRadii(static_cast<std::int8_t>(Low(regs.ax)));
  regs.cx = static_cast<std::uint16_t>(radii.inner);
  regs.dx = static_cast<std::uint16_t>(radii.outer);
  _guest.Clobber(CLOBBERS_AX);
}

void RunVertexProgramEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const VertexProgramEnd end =
    RunVertexProgram(_guest.State(), regs.si,
                     Vector{static_cast<std::int16_t>(regs.bp), static_cast<std::int16_t>(regs.bx), static_cast<std::int16_t>(regs.dx)});
  regs.si = end.next;
  regs.bp = static_cast<std::uint16_t>(end.accumulator.x);
  regs.bx = static_cast<std::uint16_t>(end.accumulator.y);
  regs.dx = static_cast<std::uint16_t>(end.accumulator.z);
  // The original's LOOP leaves CX = 0, and RenderBlueprintBody (CS:3CF5) loads only CL before storing CX as a count.
  regs.cx = 0;
  _guest.Clobber(CLOBBERS_AX_DI);
}

void CheckShipInRangeEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  const ShipRangeCheck check = CheckShipInRange(_guest.State(), slot);
  // Every register as the original leaves it, which the contract compares: IsObjectNear's AL, and EraseScannerBlip's registers
  // once it erased a blip; near, the magnitudes and squares the test took (ShipRangeOut); and out of range, the registers of
  // the EraseScannerBlip after it.
  SetLow(regs.ax, check.near.lastHigh);
  EraseScannerBlipOut(_guest, slot, check.near.erasedBlip);
  if (check.near.nearby)
  {
    ShipRangeOut(regs, check.range);
  }
  EraseScannerBlipOut(_guest, slot, check.erasedBlip);
  _guest.SetFlag(Machine::FLAG_CARRY, !check.InRange());
  _guest.Clobber(RETURNS_CARRY);
}

void TriangleWindingSignEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Triangle triangle = TriangleIn(regs);
  _guest.SetFlag(Machine::FLAG_SIGN, TriangleWindingSign(triangle));
  // The original leaves the second product in DX:AX, and in BX the first's low word, less the second's when the high words
  // agree. After a hidden last face DrawVisibleFaces returns them, and RenderBlueprintBody's contract compares them.
  const WindingProducts products = Winding(triangle);
  regs.ax = static_cast<std::uint16_t>(products.second);
  regs.dx = static_cast<std::uint16_t>(products.second >> 16);
  regs.bx = static_cast<std::uint16_t>(products.first);
  if ((products.first >> 16) == (products.second >> 16))
  {
    regs.bx = static_cast<std::uint16_t>(regs.bx - regs.ax);
  }
  _guest.Clobber(WINDING_SIGN);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x2340, "ProjectVertices", &ProjectVertices,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI, 0}},
  NativeEntry{0x3740, "ReflectVertexAboutCenter", &ReflectVertexAboutCenterEntry, CLOBBERS_AX},
  NativeEntry{0x3768, "OffsetVertexByCenter", &OffsetVertexByCenterEntry, PRESERVES_ALL},
  NativeEntry{0x377A, "BuildBoxCornerVertices", &BuildBoxCornerVerticesEntry, BUILDS_BOX},
  NativeEntry{0x38BF, "BuildDodoVertices", &BuildDodoVerticesEntry, CLOBBERS_DX},
  NativeEntry{0x3A13, "ScaleDodoRadii", &ScaleDodoRadiiEntry, CLOBBERS_AX},
  NativeEntry{0x3A40, "RunVertexProgram", &RunVertexProgramEntry, CLOBBERS_AX_DI},
  NativeEntry{0x3A9B, "TriangleWindingSign", &TriangleWindingSignEntry, WINDING_SIGN},
  NativeEntry{0x3AB3, "DrawVisibleFaces", &DrawVisibleFaces,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0}},
  NativeEntry{0x3BEA, "CheckShipInRange", &CheckShipInRangeEntry, RETURNS_CARRY},
  NativeEntry{0x3C52, "TransformSunOrPlanet", &TransformSunOrPlanet, PRESERVES_ALL},
  NativeEntry{0x3C72, "ClassifyStationPosition", &ClassifyStationPosition, RETURNS_CARRY},
  NativeEntry{0x3C7E, "TransformShip", &TransformShip, RETURNS_CARRY},
  NativeEntry{0x3CDD, "RunBlueprintHandler", &RunBlueprintHandler, Machine::NativeContract{ALL_BUT_DS & ~REGISTER_DI, 0}},
  NativeEntry{0x3CF2, "RenderBlueprintBody", &RenderBlueprintBody, PRESERVES_ALL},
  NativeEntry{0x3D25, "TransformAndDrawObjects", &TransformAndDrawObjects, Machine::NativeContract{ALL_BUT_DS, 0}},
  NativeEntry{0x3ED7, "TransformToViewWithBlip", &TransformToViewWithBlip, CLOBBERS_DX},
  NativeEntry{0x3EE3, "TransformToView", &TransformToView, CLOBBERS_DX},
  NativeEntry{0x3F4F, "DrawSunOrPlanet", &DrawSunOrPlanet, PRESERVES_ALL},
  NativeEntry{0x45C6, "DrawDistantStation", &DrawDistantStation, PRESERVES_ALL},
  NativeEntry{0x8A16, "LoadPlayerAngles", &LoadPlayerAnglesEntry, PRESERVES_ALL},
  NativeEntry{0x8D2E, "ProjectToScreen", &ProjectToScreen, Machine::NativeContract{REGISTER_DX | REGISTER_BP, 0}},
};

} // namespace

std::span<const NativeEntry> SceneEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
