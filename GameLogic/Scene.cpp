#include "pch.h"

#include "Scene.h"

#include "Arithmetic.h"
#include "Combat.h"
#include "DataOverlay.h"
#include "Equipment.h"
#include "Maths.h"
#include "Video.h"

#include <initializer_list>
#include <optional>

namespace Elite
{

namespace
{

// "Clobbers all": every register but DS, which no routine of the reference's changes for its caller.
constexpr std::uint16_t ALL_BUT_DS = Machine::REGISTER_AX | Machine::REGISTER_BX | Machine::REGISTER_CX | Machine::REGISTER_DX |
                                     Machine::REGISTER_SI | Machine::REGISTER_DI | Machine::REGISTER_BP | Machine::REGISTER_ES;

// DrawSunOrPlanet's and DrawDistantStation's, widened from every register to every one but DS (ADR-012 item 6): their one caller,
// TransformAndDrawObjects' second pass (CS:3DFB, CS:3E85), pops DI and goes back to its search (CS:3D95), which loads CX, DI,
// BP, AH and DX before it reads them, and AL at CS:3DA5; nothing there reads ES.
constexpr Machine::NativeContract DRAWS_SUN_OR_PLANET{ALL_BUT_DS, 0};
constexpr Machine::NativeContract DRAWS_DISTANT_STATION{ALL_BUT_DS, 0};

// TransformAndDrawObjects': every register but DS, as its callers' contracts already give it.
constexpr Machine::NativeContract TRANSFORMS_AND_DRAWS_OBJECTS{ALL_BUT_DS, 0};

// The ship slots DetonateEnergyBomb's LOOP looks at begin at firstShipSlot, after the sun, the planet and the station: its count is
// objectSlotCount less these.
constexpr std::uint8_t BOMB_FIRST_SLOT = 3;

// The blueprint handlers, as a blueprint's first word names them, and the bytes before the rest of a blueprint: that word and the
// half-width byte.
constexpr std::uint16_t BUILD_BOX_CORNER_VERTICES = 0x377A;
constexpr std::uint16_t BUILD_DODO_VERTICES = 0x38BF;
constexpr std::uint16_t BLUEPRINT_HANDLER_BYTES = 3;

// The instruction after each divide, where DivideOverflowInterrupt looks for its opcode. ProjectVertices'
// divides take a memory operand, so the handler reads their ModRM byte 74h and saturates only AL.
constexpr std::uint16_t VERTEX_X_DIVIDE_RETURN = 0x236C;
constexpr std::uint16_t VERTEX_Y_DIVIDE_RETURN = 0x2395;
constexpr std::uint16_t DISC_Y_DIVIDE_RETURN = 0x4044;
constexpr std::uint16_t DISC_X_DIVIDE_RETURN = 0x4064;
constexpr std::uint16_t SCREEN_X_DIVIDE_RETURN = 0x8D4B;
constexpr std::uint16_t SCREEN_Y_DIVIDE_RETURN = 0x8D56;

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

// DH of a dividend ScaledDividend makes: CWD's sign.
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

// RenderBlueprint (CS:3CF2-3D20), what RenderBlueprintBody does with the blueprint body at _body: the vertex program from
// _accumulator, the projection, the edge lists and the faces. Each count is a byte loaded into CL and stored with all of CX,
// whose CH the LOOPs before it leave 0, or the edge count's doubling. Returns the direction flag as DrawVisibleFaces leaves it.
[[nodiscard]] bool RenderBlueprint(GameState& _state, std::uint16_t _body, Vector _accumulator, bool _backward)
{
  const VertexProgramEnd program = RunVertexProgram(_state, _body, _accumulator);
  std::uint16_t at = program.next;
  std::uint16_t count = 0;
  SetLow(count, _state.Byte(at));
  _state.Set(DS.projectedVertexCount, count);
  at = Offset(at, 1);
  // ProjectVertices' divides save BX, the accumulator's y, which RunVertexProgram leaves there.
  ProjectVertices(_state, High(static_cast<std::uint16_t>(program.accumulator.y)));
  count = 0;
  // Fixed edges, then face edges: a count byte, then two bytes for each.
  SetLow(count, _state.Byte(at));
  _state.Set(DS.fixedEdgeCount, count);
  count = static_cast<std::uint16_t>(count << 1);
  at = Offset(at, 1);
  _state.Set(DS.fixedEdgeList, at);
  at = Offset(at, count);
  SetLow(count, _state.Byte(at));
  _state.Set(DS.faceEdgeCount, count);
  count = static_cast<std::uint16_t>(count << 1);
  at = Offset(at, 1);
  _state.Set(DS.faceEdgeList, at);
  at = Offset(at, count);
  SetLow(count, _state.Byte(at));
  at = Offset(at, 1);
  return DrawVisibleFaces(_state, at, count, _backward).backward;
}

// CWD / MOV DL,AH / MOV AH,AL / XOR AL,AL: DX:AX = _magnitude * 256, what ProjectVertices and DrawSunOrPlanetDisc divide. DH is
// FFh for the one magnitude NEG leaves negative, 8000h, which overflows the divide.
[[nodiscard]] std::uint32_t ScaledDividend(std::uint16_t _magnitude) noexcept
{
  return (std::uint32_t{Negative(_magnitude) ? HIGH_BYTE_ONES : HIGH_BYTE_ZERO} << 24) | (std::uint32_t{_magnitude} << 8);
}

// One coordinate of a vertex (CS:2356, CS:237E): 256|_value| over the z at DS:_z, by DIV [SI+4], plus one when the quotient is
// below twice the remainder (SHL DX,1 / CMP AX,DX / ADC AX,0), with _value's sign put back. A divide that overflows saves BX,
// _trapHigh over the sign BL holds, 1 for a negative _value.
[[nodiscard]] std::uint16_t ProjectVertexCoordinate(GameState& _state, std::uint16_t _value, std::uint16_t _z, std::uint16_t _returnOffset,
                                                    std::uint8_t _trapHigh)
{
  const bool negative = Negative(_value);
  const std::uint16_t magnitude = negative ? Negate(_value) : _value;
  const WordQuotient divided = DivideUnsigned(_state, ScaledDividend(magnitude), _state.Word(_z), _returnOffset,
                                              Join(_trapHigh, negative ? std::uint8_t{1} : std::uint8_t{0}));
  std::uint16_t coordinate = divided.quotient;
  if (coordinate < static_cast<std::uint16_t>(divided.remainder << 1))
  {
    coordinate = Offset(coordinate, 1);
  }
  return negative ? Negate(coordinate) : coordinate;
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

// An edge item (CS:3AF3): _item, the edge's offset in faceEdgeList; the edge's colour from its high byte's low bits, then
// DrawClippedLine from its second vertex to its first, unless either's x is 7FFFh, the trap's saturation. PUSH SI / POP SI round
// the line keep the list. Returns what DrawClippedLine returns: whether DrawLine filled, and cleared the direction flag.
[[nodiscard]] bool DrawFaceEdge(GameState& _state, std::uint8_t _item)
{
  const std::uint16_t edge = _state.Word(Offset(_state.Get(DS.faceEdgeList), _item));
  _state.Set(DS.drawColor, static_cast<std::uint8_t>(High(edge) & EDGE_COLOR_BITS));
  const auto vertices = static_cast<std::uint16_t>(edge & EDGE_VERTEX_BITS);
  const std::uint16_t to = Offset(DS.vertexBuffer.offset, Low(vertices));
  const std::uint16_t from = Offset(DS.vertexBuffer.offset, High(vertices));
  const std::uint16_t toX = _state.Word(to);
  const std::uint16_t fromX = _state.Word(from);
  if (toX == OFF_SCREEN || fromX == OFF_SCREEN)
  {
    return false;
  }
  return DrawClippedLine(_state, fromX, _state.Word(Offset(from, 2)), toX, _state.Word(Offset(to, 2)));
}

// A triangle item (CS:3B30): _item, the offset of its pattern in faceFillPatterns, into triangleFillPattern, then the three
// vertex bytes at _vertices: FillTriangle unless a corner's x is 7FFFh. PUSH SI / POP SI round it keep the list. Returns the
// offset past the vertex bytes.
[[nodiscard]] std::uint16_t DrawFaceTriangle(GameState& _state, std::uint8_t _item, std::uint16_t _vertices, bool _backward)
{
  _state.Set(DS.triangleFillPattern, _state.Word(Offset(DS.faceFillPatterns.offset, _item)));
  const Triangle triangle = LoadTriangle(_state, _vertices, 1);
  const auto offScreen = [](ScreenPoint _point) { return static_cast<std::uint16_t>(_point.x) == OFF_SCREEN; };
  if (!offScreen(triangle.first) && !offScreen(triangle.second) && !offScreen(triangle.third))
  {
    (void)FillTriangle(_state, triangle, _backward);
  }
  return Offset(_vertices, TRIANGLE_VERTEX_BYTES);
}

// A facing face's items (CS:3AE8): a count byte into CL, CH 0, then the items, an edge's one byte counted off by LOOP and a
// triangle's four by SUB CX,4 / JE, which ends only on 0. Returns the offset past them, and the direction flag as the edges'
// DrawLine leave it.
[[nodiscard]] FacesEnd DrawFaceItems(GameState& _state, std::uint16_t _items, bool _backward)
{
  FacesEnd end{Offset(_items, 1), _backward};
  std::uint16_t left = _state.Byte(_items);
  for (;;)
  {
    const std::uint8_t item = _state.Byte(end.next);
    end.next = Offset(end.next, 1);
    // SHL BL,1 / JB, then SHR BL,1: bit 7 says a triangle, and the rest is the offset.
    const auto offset = static_cast<std::uint8_t>(item & ~FACE_TRIANGLE);
    if ((item & FACE_TRIANGLE) == 0)
    {
      if (DrawFaceEdge(_state, offset))
      {
        end.backward = false;
      }
      if (!Loop(left))
      {
        return end;
      }
    }
    else
    {
      end.next = DrawFaceTriangle(_state, offset, end.next, end.backward);
      left = static_cast<std::uint16_t>(left - TRIANGLE_ITEM_BYTES);
      if (left == 0)
      {
        return end;
      }
    }
  }
}

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

// A position as the transforms hold it, in AX, BX and CX.
[[nodiscard]] Vector PositionIn(const Machine::Registers& _regs) noexcept
{
  return Vector{static_cast<std::int16_t>(_regs.ax), static_cast<std::int16_t>(_regs.bx), static_cast<std::int16_t>(_regs.cx)};
}

void PositionOut(Machine::Registers& _regs, Vector _position) noexcept
{
  _regs.ax = static_cast<std::uint16_t>(_position.x);
  _regs.bx = static_cast<std::uint16_t>(_position.y);
  _regs.cx = static_cast<std::uint16_t>(_position.z);
}

// What ClassifyViewPosition's code leaves of the view position _view it took in AX, BX and CX: twice |x| in AX once z passes,
// twice |y| in BX once x does, and CF clear only when the object is visible.
void ClassifyViewOut(Guest& _guest, Vector _view, ViewTest _test)
{
  Machine::Registers& regs = _guest.Regs();
  PositionOut(regs, _view);
  if (_test != ViewTest::TooNear)
  {
    regs.ax = DoubledMagnitude(regs.ax);
  }
  if (_test == ViewTest::WideY || _test == ViewTest::Visible)
  {
    regs.bx = DoubledMagnitude(regs.bx);
  }
  _guest.SetFlag(Machine::FLAG_CARRY, _test != ViewTest::Visible);
}

// A station's compass position, as MOV AX,[DI+20h] / MOV BX,[DI+22h] / MOV CX,[DI+24h] load it.
[[nodiscard]] Vector CompassPosition(ObjectSlot _slot) noexcept
{
  return Vector{static_cast<std::int16_t>(_slot.Get(SlotWord::CompassX)), static_cast<std::int16_t>(_slot.Get(SlotWord::CompassY)),
                static_cast<std::int16_t>(_slot.Get(SlotWord::CompassZ))};
}

// An object's view position, as MOV AX,[DI+10h] / MOV BX,[DI+12h] / MOV CX,[DI+14h] load it.
[[nodiscard]] Vector ViewPosition(ObjectSlot _slot) noexcept
{
  return Vector{static_cast<std::int16_t>(_slot.Get(SlotWord::ViewX)), static_cast<std::int16_t>(_slot.Get(SlotWord::ViewY)),
                static_cast<std::int16_t>(_slot.Get(SlotWord::ViewZ))};
}

// RotateToViewDirection (CS:3EE6), the shared tail of the two TransformToView entries: _view with (x, z) rotated by -viewAngle
// through rotation pair 8, which it sets, when the view is not the front one. PUSH BX, AX and CX round SetSinCos8 keep the
// position, which comes back in the registers it went in.
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

// ClassifyObject (CS:3D41), one slot of TransformAndDrawObjects' first pass: _slot's drawn bit (+1Eh bit 7) cleared; an active
// object's visible and in-range bits cleared, and one off the scanner counted a frame further, removed (RemoveObject) on the
// 255th; then the sun or the planet transformed (TransformSunOrPlanet), a ship in range transformed (CheckShipInRange,
// TransformShip), and the station classified at its compass position (ClassifyStationPosition) when UpdateCompass has made it the
// compass's target and its depth is below 2. Returns the slot DI holds after it, from which the pass steps on: UpdateCompass,
// once it runs, leaves DI at stationSlot; every other callee keeps it.
[[nodiscard]] std::uint16_t ClassifyObject(GameState& _state, ObjectSlot _slot)
{
  _slot.Set(SlotByte::Flags, static_cast<std::uint8_t>(_slot.Get(SlotByte::Flags) & ~STATE_DRAWN));
  if ((_slot.Get(SlotByte::Type) & SLOT_ACTIVE) == 0)
  {
    return _slot.Offset();
  }
  _slot.Set(SlotByte::Type, static_cast<std::uint8_t>(_slot.Get(SlotByte::Type) & SLOT_KEEP_BITS));
  if (_slot.Get(SlotByte::FramesAway) != 0 && (_slot.Get(SlotByte::Flags) & STATE_BLIP_SHOWN) == 0)
  {
    // Off the scanner: despawned on the 255th frame.
    _slot.Set(SlotByte::FramesAway, static_cast<std::uint8_t>(_slot.Get(SlotByte::FramesAway) + 1));
    if (_slot.Get(SlotByte::FramesAway) == 0)
    {
      (void)RemoveObject(_state, _slot);
      return _slot.Offset();
    }
  }
  if (IsSunOrPlanet(_slot))
  {
    (void)TransformSunOrPlanet(_state, _slot);
    return _slot.Offset();
  }
  if (!IsStation(_slot).station)
  {
    if (CheckShipInRange(_state, _slot).InRange())
    {
      (void)TransformShip(_state, _slot);
    }
    return _slot.Offset();
  }
  const std::uint16_t station = UpdateCompass(_state) ? DS.stationSlot.offset : _slot.Offset();
  const ObjectSlot target(_state, station);
  if (_state.Get(DS.compassTargetIsStation) != 0 && target.Get(SlotByte::Depth) < DISTANT_DEPTH)
  {
    (void)ClassifyStationPosition(_state, target);
  }
  return station;
}

// The station in _slot (CS:3DEE), when it is far enough away to draw as a disc: true if it is, and DrawDistantStation drew it.
// One at depth 1 nearer than 1B58h has its compass position doubled into its view position instead.
[[nodiscard]] bool DrawStationIfDistant(GameState& _state, ObjectSlot _slot, bool _backward)
{
  const std::uint8_t depth = _slot.Get(SlotByte::Depth);
  if (depth == 0)
  {
    return false;
  }
  if (depth == 1)
  {
    const std::uint16_t z = _slot.Get(SlotWord::CompassZ);
    if (z < DISTANT_STATION_Z)
    {
      _slot.Set(SlotWord::ViewZ, static_cast<std::uint16_t>(z << 1));
      _slot.Set(SlotWord::ViewX, static_cast<std::uint16_t>(_slot.Get(SlotWord::CompassX) << 1));
      _slot.Set(SlotWord::ViewY, static_cast<std::uint16_t>(_slot.Get(SlotWord::CompassY) << 1));
      return false;
    }
  }
  // PUSH DI / POP DI round it keep the slot.
  DrawDistantStation(_state, _slot, _backward);
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

// DrawObjectAsDot (CS:3E8C): _slot's view position projected (ProjectToScreen), as a disc of radius 2 in colour 3. PUSH DI /
// POP DI round it keep the slot.
void DrawObjectAsDot(GameState& _state, ObjectSlot _slot, bool _backward)
{
  const ScreenPoint at = ProjectToScreen(_state, ViewPosition(_slot));
  _state.Set(DS.drawColor, DOT_COLOR);
  DrawDisc(_state, DOT_RADIUS, static_cast<std::uint16_t>(at.x), static_cast<std::uint16_t>(at.y), _backward);
}

// DrawObject (CS:3DE1), the visible object in _slot, not yet drawn: the sun or the planet (DrawSunOrPlanet); a station far enough
// off as a disc (DrawStationIfDistant); nothing in a flashing object's off frames; a dot (DrawObjectAsDot) when its size (+3Eh)
// passes its level of detail (+3Fh); else its blueprint, from blueprintTable by its type (RunBlueprintHandler), at its angles and
// view position. PUSH DI / POP DI round the sun's, the station's and the dot's keep the slot, and the search the draws jump back
// to loads what it reads. Returns the direction flag as the drawing leaves it.
[[nodiscard]] bool DrawObject(GameState& _state, Hardware& _hardware, ObjectSlot _slot, bool _backward)
{
  if (IsSunOrPlanet(_slot))
  {
    DrawSunOrPlanet(_state, _hardware, _slot, _backward);
    return _backward;
  }
  if (IsStation(_slot).station && DrawStationIfDistant(_state, _slot, _backward))
  {
    return _backward;
  }
  if (FlashedOff(_slot))
  {
    return _backward;
  }
  if (_slot.Get(SlotByte::Detail) < _slot.Get(SlotByte::Size))
  {
    DrawObjectAsDot(_state, _slot, _backward);
    return _backward;
  }
  // MOV BL,[DI] / AND BX,3Eh: the type, doubled, indexes blueprintTable.
  const std::uint16_t blueprint =
    _state.Word(Offset(static_cast<std::uint16_t>(_slot.Get(SlotByte::Type) & SLOT_TYPE_BITS), DS.blueprintTable.offset));
  _state.Set(DS.drawPitchAngle, _slot.Get(SlotWord::Pitch));
  _state.Set(DS.drawYawAngle, _slot.Get(SlotWord::Yaw));
  _state.Set(DS.drawRollAngle, _slot.Get(SlotWord::Roll));
  _state.Set(DS.drawCenterX, _slot.Get(SlotWord::ViewX));
  _state.Set(DS.drawCenterY, _slot.Get(SlotWord::ViewY));
  _state.Set(DS.drawCenterZ, _slot.Get(SlotWord::ViewZ));
  return RunBlueprintHandler(_state, blueprint, _backward);
}

// DrawFarthestObject (CS:3D95), TransformAndDrawObjects' second pass, to which every draw jumps back: of shipSlotCount slots
// (MOV CL / XOR CH,CH, then LOOP: 65,536 for 0), the visible object not yet drawn with the largest depth class (+3Dh), then
// the largest view z, marked drawn (+1Eh bit 7) and drawn (DrawObject). Returns the direction flag as the drawing leaves it, or
// none once no object is left.
[[nodiscard]] std::optional<bool> DrawFarthestObject(GameState& _state, Hardware& _hardware, bool _backward)
{
  // DX the slot found, FFFFh none; AH its depth and BP its z, from 0.
  std::optional<std::uint16_t> farthest;
  std::uint8_t farthestDepth = 0;
  std::uint16_t farthestZ = 0;
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint32_t count = LoopCount(_state.Get(DS.shipSlotCount)); count != 0; --count)
  {
    const ObjectSlot object(_state, slot);
    if ((object.Get(SlotByte::Type) & SLOT_DRAWABLE) == SLOT_DRAWABLE && (object.Get(SlotByte::Flags) & STATE_DRAWN) == 0)
    {
      const std::uint8_t depth = object.Get(SlotByte::Depth);
      if (farthestDepth < depth || (farthestDepth == depth && farthestZ < object.Get(SlotWord::ViewZ)))
      {
        farthestDepth = depth;
        farthestZ = object.Get(SlotWord::ViewZ);
        farthest = slot;
      }
    }
    slot = Offset(slot, SLOT_BYTES);
  }
  if (!farthest)
  {
    return std::nullopt;
  }
  ObjectSlot object(_state, *farthest);
  object.Set(SlotByte::Flags, static_cast<std::uint8_t>(object.Get(SlotByte::Flags) | STATE_DRAWN));
  return DrawObject(_state, _hardware, object, _backward);
}

// One coordinate of the sun's or planet's centre (CS:4030, CS:404E): 256|_value| over _slot's view z, by DIV [DI+14h], rounded
// down, with _value's sign put back. A divide that overflows saves BX, _radius.
[[nodiscard]] std::uint16_t ProjectDiscCoordinate(GameState& _state, ObjectSlot _slot, std::uint16_t _value, std::uint16_t _returnOffset,
                                                  std::uint16_t _radius)
{
  const bool negative = Negative(_value);
  const std::uint16_t magnitude = negative ? Negate(_value) : _value;
  const std::uint16_t coordinate =
    DivideUnsigned(_state, ScaledDividend(magnitude), _slot.Get(SlotWord::ViewZ), _returnOffset, _radius).quotient;
  return negative ? Negate(coordinate) : coordinate;
}

// _center + _radius must not overflow or be negative, and _center - _radius must not overflow or pass _limit: ADD, JO, CMP 0, JL,
// then SUB twice, JO, CMP _limit, JG.
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

// DrawSunOrPlanetDisc (CS:402C): a disc of radius _radius at _slot's view position projected, in its colour (+0Bh), drawn only
// when the disc's whole box is on the 256x128 view: DrawDisc with twice the radius.
void DrawSunOrPlanetDisc(GameState& _state, ObjectSlot _slot, std::uint16_t _radius, bool _backward)
{
  const auto row = Offset(ProjectDiscCoordinate(_state, _slot, _slot.Get(SlotWord::ViewY), DISC_Y_DIVIDE_RETURN, _radius), SCREEN_CENTER_Y);
  const auto x = Offset(ProjectDiscCoordinate(_state, _slot, _slot.Get(SlotWord::ViewX), DISC_X_DIVIDE_RETURN, _radius), SCREEN_CENTER_X);
  if (!DiscWithin(x, _radius, SCREEN_RIGHT) || !DiscWithin(row, _radius, SCREEN_BOTTOM))
  {
    return;
  }
  _state.Set(DS.drawColor, _slot.Get(SlotByte::Color));
  DrawDisc(_state, static_cast<std::uint16_t>(_radius << 1), x, row, _backward);
}

// What SupernovaHeat makes of the sun once supernovaFrames has counted down to 1.
struct SupernovaGlow
{
  std::uint16_t heat; // AX, AH 0: the heat, which stands for the sun's radius
  std::uint16_t slot; // DI after it: the sun's, or past the ship slots once an energy bomb went off and its LOOP looked at them
};

// SupernovaHeat (CS:3F57): once supernovaFrames has counted down to 1, where an INC keeps it, the sun's heat is supernovaHeat,
// first the cabin temperature, growing by a quarter of itself (at least 1) a frame; when it overflows a byte, the player is
// killed (KillPlayer, with StartPlayerDeathSound's STI) and an energy bomb goes off (DetonateEnergyBomb, its copies backwards when
// _backward), and the heat is FFh. None while the countdown runs, or when it is 0. _slot is the sun's slot, DI.
[[nodiscard]] std::optional<SupernovaGlow> SupernovaHeat(GameState& _state, Hardware& _hardware, std::uint16_t _slot, bool _backward)
{
  if (_state.Get(DS.supernovaFrames) == 0)
  {
    return std::nullopt;
  }
  _state.Set(DS.supernovaFrames, static_cast<std::uint16_t>(_state.Get(DS.supernovaFrames) - 1));
  if (_state.Get(DS.supernovaFrames) != 0)
  {
    return std::nullopt;
  }
  _state.Set(DS.supernovaFrames, 1);
  if (_state.Get(DS.supernovaHeat) == 0)
  {
    _state.Set(DS.supernovaHeat, _state.Get(DS.cabinTemperature));
  }
  auto step = static_cast<std::uint8_t>(_state.Get(DS.supernovaHeat) >> 2);
  if (step == 0)
  {
    step = 1;
  }
  const unsigned sum = unsigned{step} + _state.Get(DS.supernovaHeat);
  SupernovaGlow glow{static_cast<std::uint8_t>(sum), _slot};
  if (sum > 0xFF)
  {
    if (KillPlayer(_state))
    {
      _hardware.EnableInterrupts();
    }
    // DetonateEnergyBomb's LOOP leaves DI past the ship slots, by the count it loads: objectSlotCount less 3, a byte, 0 for
    // 65,536.
    const auto ships = static_cast<std::uint8_t>(_state.Get(DS.objectSlotCount) - BOMB_FIRST_SLOT);
    (void)DetonateEnergyBomb(_state, _hardware, _backward);
    glow.slot = static_cast<std::uint16_t>(DS.firstShipSlot.offset + LoopCount(ships) * ObjectSlot::BYTES);
    glow.heat = 0xFF;
  }
  _state.Set(DS.supernovaHeat, Low(glow.heat));
  return glow;
}

// The sun's disc, as SizeSun leaves it for DrawSunOrPlanetDisc.
struct SunDisc
{
  std::uint16_t radius; // AX
  std::uint16_t slot;   // DI: the sun's, or where a supernova's energy bomb left it
};

// SizeSun (CS:3F57-3FF4): the sun's radius, SupernovaHeat's heat or else 100 scaled to _slot's distance (ScaleByInverseDistance),
// into the cabin temperature. Behind the view, by bit 7 of the view z's high byte in the slot DI then holds, nothing more; in
// front, sunFringeMask 1, 3 or 7 below radii 28h, B4h and C3h; from C3h, with fuel scoops fitted, 6 more fuel, written, then FFh
// over it on a carry, with FUEL SCOOPS ACTIVE for 5 frames; from FDh, KillPlayer, whose StartPlayerDeathSound leaves its step length
// in AL over the radius's low byte, as there is no PUSH AX round it here, and a radius past a byte held to FFh. Returns the disc
// in front of the view.
[[nodiscard]] std::optional<SunDisc> SizeSun(GameState& _state, Hardware& _hardware, ObjectSlot _slot, bool _backward)
{
  SunDisc disc{0, _slot.Offset()};
  if (const std::optional<SupernovaGlow> glow = SupernovaHeat(_state, _hardware, _slot.Offset(), _backward))
  {
    disc = SunDisc{glow->heat, glow->slot};
  }
  else
  {
    // MOV DX,64h / XOR AX,AX: DX:AX = 100 * 65536.
    disc.radius = ScaleByInverseDistance(_state, _slot, std::uint32_t{SUN_SCALE} << 16).scaled;
  }
  _state.Set(DS.cabinTemperature, Low(disc.radius));
  if ((ObjectSlot(_state, disc.slot).Get(SlotByte::ViewZHigh) & 0x80) != 0)
  {
    return std::nullopt;
  }
  _state.Set(DS.sunFringeMask, FRINGE_SMALL);
  if (disc.radius < FRINGE_RADIUS_SMALL)
  {
    return disc;
  }
  _state.Set(DS.sunFringeMask, FRINGE_MEDIUM);
  if (disc.radius < FRINGE_RADIUS_LARGE)
  {
    return disc;
  }
  _state.Set(DS.sunFringeMask, FRINGE_LARGE);
  if (disc.radius < SCOOP_RADIUS)
  {
    return disc;
  }
  if (_state.Get(DS.fuelScoopsFitted) == 1)
  {
    const unsigned fuel = unsigned{_state.Get(DS.fuel)} + SCOOP_FUEL;
    _state.Set(DS.fuel, static_cast<std::uint8_t>(fuel));
    if (fuel > 0xFF)
    {
      _state.Set(DS.fuel, 0xFF);
      _state.Set(DS.messagePointer, DS.fuelScoopsActiveText.offset);
      _state.Set(DS.messageFrames, SCOOP_MESSAGE_FRAMES);
    }
  }
  if (disc.radius >= FATAL_RADIUS)
  {
    if (const std::optional<std::uint8_t> stepLength = KillPlayer(_state))
    {
      SetLow(disc.radius, *stepLength);
      _hardware.EnableInterrupts();
    }
    if (High(disc.radius) != 0)
    {
      disc.radius = RADIUS_LIMIT;
    }
  }
  return disc;
}

// SizePlanet (CS:3FF6): sunFringeMask cleared, the radius ScaleByInverseDistance makes of 50 at _slot's distance, and the
// altitude, twice 255 less that radius held to 127. In front of the view, a radius of FDh or more kills the player, with
// StartPlayerDeathSound's STI, and one past a byte draws as FFh. Returns the disc's radius in front of the view.
std::optional<std::uint16_t> SizePlanet(GameState& _state, Hardware& _hardware, ObjectSlot _slot)
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
  if ((_slot.Get(SlotByte::ViewZHigh) & 0x80) != 0)
  {
    return std::nullopt;
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
  return radius;
}

} // namespace

void ProjectVertices(GameState& _state, std::uint8_t _trapHigh)
{
  // MOV CX,[projectedVertexCount] / AND CX,CX / JE, then LOOP: a count of the whole word, 0 none. SI walks the vertices and DI the
  // points they become, in place; PUSH SI / POP SI keep the caller's.
  const std::uint16_t count = _state.Get(DS.projectedVertexCount);
  std::uint16_t vertex = DS.vertexBuffer.offset;
  std::uint16_t point = vertex;
  for (std::uint16_t left = count; left != 0; --left)
  {
    const auto z = Offset(vertex, VERTEX_Z);
    if (static_cast<std::int16_t>(_state.Word(z)) < static_cast<std::int16_t>(_state.Get(DS.nearPlaneZ)))
    {
      _state.SetWord(point, NEAR_VERTEX_X);
    }
    else
    {
      const std::uint16_t x = ProjectVertexCoordinate(_state, _state.Word(vertex), z, VERTEX_X_DIVIDE_RETURN, _trapHigh);
      _state.SetWord(point, Offset(x, SCREEN_CENTER_X));
      const std::uint16_t y = ProjectVertexCoordinate(_state, _state.Word(Offset(vertex, 2)), z, VERTEX_Y_DIVIDE_RETURN, _trapHigh);
      _state.SetWord(Offset(point, 2), Offset(y, SCREEN_CENTER_Y));
    }
    point = Offset(point, POINT_BYTES);
    vertex = Offset(vertex, VERTEX_BYTES);
  }
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

FacesEnd DrawVisibleFaces(GameState& _state, std::uint16_t _list, std::uint16_t _faces, bool _backward)
{
  FacesEnd end{_list, _backward};
  if (_faces == 0)
  {
    return end;
  }
  // PUSH CX / POP CX round each face keep the count, and DEC CX / JE ends the faces.
  std::uint16_t faces = _faces;
  do
  {
    const Triangle facing = LoadTriangle(_state, end.next, 0);
    end.next = Offset(end.next, TRIANGLE_VERTEX_BYTES);
    if (TriangleWindingSign(facing))
    {
      end = DrawFaceItems(_state, end.next, end.backward);
    }
    else
    {
      // SkipHiddenFace (CS:3B8D): MOV CL,[SI] / STC / ADC SI,CX, CH 0: past the count byte and the items.
      end.next = Offset(end.next, Offset(_state.Byte(end.next), 1));
    }
  } while (Loop(faces));
  return end;
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

ViewWithBlip TransformSunOrPlanet(GameState& _state, ObjectSlot _slot)
{
  // MOV [DI+0Ah],CL / MOV [DI+3Dh],CL / MOV DH,CL: the shift, as the disc's scale and its depth, and for ScalePositionDown.
  const std::uint8_t shift = GetPositionScaleShift(_slot).shift;
  _slot.Set(SlotByte::DiscScale, shift);
  _slot.Set(SlotByte::Depth, shift);
  // PUSH DI / POP DI round TransformToViewWithBlip keep the slot.
  const ViewWithBlip transformed = TransformToViewWithBlip(_state, _slot, ScalePositionDown(_slot, shift));
  _slot.Set(SlotWord::ViewX, static_cast<std::uint16_t>(transformed.view.x));
  _slot.Set(SlotWord::ViewY, static_cast<std::uint16_t>(transformed.view.y));
  _slot.Set(SlotWord::ViewZ, static_cast<std::uint16_t>(transformed.view.z));
  _slot.Set(SlotByte::Type, static_cast<std::uint8_t>(_slot.Get(SlotByte::Type) | SLOT_VISIBLE));
  return transformed;
}

ViewTest ClassifyStationPosition(GameState& _state, ObjectSlot _slot)
{
  return ClassifyViewPosition(_state, _slot, CompassPosition(_slot));
}

TransformedShip TransformShip(GameState& _state, ObjectSlot _slot)
{
  // MOV AX,[DI+4] / MOV BX,[DI+6] / MOV CX,[DI+8], then PUSH DI / POP DI round the transform and the scoop, which keep the slot.
  const Vector position{static_cast<std::int16_t>(_slot.Get(SlotWord::X)), static_cast<std::int16_t>(_slot.Get(SlotWord::Y)),
                        static_cast<std::int16_t>(_slot.Get(SlotWord::Z))};
  const ViewWithBlip transformed = TransformToViewWithBlip(_state, _slot, position);
  bool blipTouched = transformed.blip.has_value();
  if (_state.Get(DS.fuelScoopsFitted) == 1 && _state.Get(DS.gameOverFrames) == 0)
  {
    // The scoop keeps AX, BX and CX, the view position, round its work.
    if (TryScoopObject(_state, _slot, transformed.view).removedBlip)
    {
      blipTouched = true;
    }
  }
  _slot.Set(SlotByte::Depth, 0);
  return TransformedShip{transformed.view, ClassifyViewPosition(_state, _slot, transformed.view), blipTouched};
}

bool RunBlueprintHandler(GameState& _state, std::uint16_t _blueprint, bool _backward)
{
  // PUSH DI and the way back to RenderBlueprintBody (CS:3CF2), then the handler's word, BL into boxHalfWidth with BH 0, and JMP AX
  // to the handler with SI past them. The slot and the way back are the stack the handler returns through, which this unit
  // keeps itself.
  const std::uint16_t handler = _state.Word(_blueprint);
  _state.Set(DS.boxHalfWidth, _state.Byte(Offset(_blueprint, 2)));
  std::uint16_t body = Offset(_blueprint, BLUEPRINT_HANDLER_BYTES);
  // Every blueprint's handler is one of these two; the blueprints are never written.
  if (handler == BUILD_BOX_CORNER_VERTICES)
  {
    body = BuildBoxCornerVertices(_state, body);
  }
  else if (handler == BUILD_DODO_VERTICES)
  {
    (void)BuildDodoVertices(_state);
  }
  // Every blueprint's vertex program begins by loading a vertex into the accumulator, so what the handler leaves in BP, BX and
  // DX is never read.
  return RenderBlueprint(_state, body, Vector{}, _backward);
}

bool TransformAndDrawObjects(GameState& _state, Hardware& _hardware, bool _backward)
{
  (void)SetSinCos(_state, 0, _state.Get(DS.playerPitchAngle));
  (void)SetSinCos(_state, 1, _state.Get(DS.playerYawAngle));
  (void)SetSinCos(_state, 2, _state.Get(DS.playerRollAngle));
  // MOV CL,shipSlotCount / XOR CH,CH, then LOOP with PUSH CX / POP CX round each slot: a count of 0 classifies 65,536. DI steps on
  // from where each slot's classification leaves it.
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint32_t count = LoopCount(_state.Get(DS.shipSlotCount)); count != 0; --count)
  {
    slot = Offset(ClassifyObject(_state, ObjectSlot(_state, slot)), SLOT_BYTES);
  }
  bool backward = _backward;
  while (const std::optional<bool> drawn = DrawFarthestObject(_state, _hardware, backward))
  {
    backward = *drawn;
  }
  return backward;
}

ViewWithBlip TransformToViewWithBlip(GameState& _state, ObjectSlot _slot, Vector _position)
{
  const Vector camera = RotatePitchYawRoll(_state, _position);
  // MOV [DI+3Ch],CH.
  _slot.Set(SlotByte::CameraZHigh, High(static_cast<std::uint16_t>(camera.z)));
  const std::optional<DashboardPixel> blip = UpdateScannerBlip(_state, _slot, camera);
  return ViewWithBlip{RotateToViewDirection(_state, camera), blip};
}

Vector TransformToView(GameState& _state, Vector _position)
{
  return RotateToViewDirection(_state, RotatePitchYawRoll(_state, _position));
}

void DrawSunOrPlanet(GameState& _state, Hardware& _hardware, ObjectSlot _slot, bool _backward)
{
  if (IsPlanet(_slot))
  {
    if (const std::optional<std::uint16_t> radius = SizePlanet(_state, _hardware, _slot))
    {
      DrawSunOrPlanetDisc(_state, _slot, *radius, _backward);
    }
    return;
  }
  // The disc is the slot's that SizeSun leaves DI on: the sun's, or, once a supernova's heat killed and its energy bomb's LOOP moved
  // DI, that slot's, as the original draws it.
  if (const std::optional<SunDisc> disc = SizeSun(_state, _hardware, _slot, _backward))
  {
    DrawSunOrPlanetDisc(_state, ObjectSlot(_state, disc->slot), disc->radius, _backward);
  }
}

void DrawDistantStation(GameState& _state, ObjectSlot _slot, bool _backward)
{
  const ScreenPoint at = ProjectToScreen(_state, CompassPosition(_slot));
  const auto x = static_cast<std::uint16_t>(at.x);
  const auto row = static_cast<std::uint16_t>(at.y);
  _state.Set(DS.drawColor, STATION_COLOR);
  const std::uint8_t depth = _slot.Get(SlotByte::StationDepth);
  if (depth < NEAR_STATION_DEPTH)
  {
    DrawDisc(_state, NEAR_STATION_RADIUS, x, row, _backward);
    return;
  }
  // r = (21h - depth) / 2, as bytes: SUB AL,25h / NEG AL / SUB AL,4 / JLE, none when 25h - depth, taken as signed, is 4 or less;
  // SHR AL,1 / JE, none when r is 0. The radius is 2r.
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
  DrawDisc(_state, static_cast<std::uint16_t>(rows << 1), x, row, _backward);
}

SinCos LoadPlayerAngles(GameState& _state)
{
  (void)SetSinCos(_state, 0, _state.Get(DS.playerPitchAngle));
  (void)SetSinCos(_state, 1, _state.Get(DS.playerYawAngle));
  return SetSinCos(_state, 2, _state.Get(DS.playerRollAngle));
}

ScreenPoint ProjectToScreen(GameState& _state, Vector _view)
{
  // AND / JNS / NEG: the magnitudes, BP bit 1 for a negative x and bit 0 for a negative y.
  const bool negativeX = _view.x < 0;
  const bool negativeY = _view.y < 0;
  const std::uint16_t x = negativeX ? Negate(static_cast<std::uint16_t>(_view.x)) : static_cast<std::uint16_t>(_view.x);
  const std::uint16_t y = negativeY ? Negate(static_cast<std::uint16_t>(_view.y)) : static_cast<std::uint16_t>(_view.y);
  const auto z = static_cast<std::uint16_t>(_view.z);
  // XOR DH,DH / MOV DL,AH / MOV AH,AL / XOR AL,AL, then DIV CX: x first, with BX = |y| for the trap; XCHG BX,AX, then y, with BX
  // the first quotient.
  const std::uint16_t across = DivideUnsigned(_state, std::uint32_t{x} << 8, z, SCREEN_X_DIVIDE_RETURN, y).quotient;
  const std::uint16_t down = DivideUnsigned(_state, std::uint32_t{y} << 8, z, SCREEN_Y_DIVIDE_RETURN, across).quotient;
  return ScreenPoint{static_cast<std::int16_t>(Offset(negativeX ? Negate(across) : across, SCREEN_CENTER_X)),
                     static_cast<std::int16_t>(Offset(negativeY ? Negate(down) : down, SCREEN_CENTER_Y))};
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

constexpr Machine::NativeContract RETURNS_CARRY{0, FLAG_CARRY};
// TransformShip's: DX, TransformToViewWithBlip's leftover, is not compared. Its one caller, ClassifyObject in
// TransformAndDrawObjects (CS:3D87), goes back to the first pass, which reads DX nowhere before MOV DX,0FFFFh (CS:3DA2).
constexpr Machine::NativeContract TRANSFORMS_SHIP{REGISTER_DX, FLAG_CARRY};
// TransformSunOrPlanet's: DX, TransformToViewWithBlip's leftover, and BP, ScalePositionDown's, are not compared. Its one caller,
// ClassifyObject at CS:3D8C, goes back to the first pass, which reads neither before XOR BP,BP and MOV DX,0FFFFh (CS:3D9E).
constexpr Machine::NativeContract TRANSFORMS_SUN_OR_PLANET{REGISTER_DX | REGISTER_BP, 0};

// The rotations' leftover in DX, which no caller reads (ADR-012).
constexpr Machine::NativeContract CLOBBERS_DX{REGISTER_DX, 0};

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract BUILDS_BOX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_DI{REGISTER_AX | REGISTER_DI, 0};
// TriangleWindingSign changes only AX, BX, CX, DX and DI. DrawVisibleFaces reads SI after it, and RenderBlueprintBody's contract
// compared the AX, BX, DX, BP and ES it leaves after a hidden last face (ADR-012) until both were de-assembled at level 5; the
// hook now has no caller in the game, and the entry still leaves them as the original does.
constexpr Machine::NativeContract WINDING_SIGN{REGISTER_CX | REGISTER_DI, FLAG_SIGN};
constexpr Machine::NativeContract PROJECTS_VERTICES{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI, 0};
constexpr Machine::NativeContract DRAWS_VISIBLE_FACES{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0};
// RunBlueprintHandler's, and RenderBlueprintBody's, widened to it from every register (ADR-012 item 6): RenderBlueprintBody's RET
// goes back to RunBlueprintHandler's one caller, TransformAndDrawObjects' second pass (CS:3E7E), which jumps to its search (CS:3D95)
// and loads CX, DI, BP, AH and DX before it reads them, and AL at CS:3DA5.
constexpr Machine::NativeContract RENDERS_BLUEPRINT{ALL_BUT_DS & ~REGISTER_DI, 0};
// ProjectToScreen's: the second divide's remainder in DX, and BP = 0 after its sign bits are shifted out.
constexpr Machine::NativeContract PROJECTS_TO_SCREEN{REGISTER_DX | REGISTER_BP, 0};

} // namespace

// ── The entries of the routines de-assembled (ADR-012) ──

void ProjectVerticesEntry(Guest& _guest)
{
  ProjectVertices(_guest.State(), High(_guest.Regs().bx));
  _guest.Clobber(PROJECTS_VERTICES);
}

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

void ClassifyStationPositionEntry(Guest& _guest)
{
  const ObjectSlot slot(_guest.State(), _guest.Regs().di);
  const ViewTest test = ClassifyStationPosition(_guest.State(), slot);
  // The contract compares AX, BX and CX: the compass position it loaded, as ClassifyViewPosition's code leaves it.
  ClassifyViewOut(_guest, CompassPosition(slot), test);
  _guest.Clobber(RETURNS_CARRY);
}

void TransformSunOrPlanetEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ViewWithBlip transformed = TransformSunOrPlanet(_guest.State(), ObjectSlot(_guest.State(), regs.di));
  // The view position stays in AX, BX and CX after it is stored, and ES is the video segment once UpdateScannerBlip draws a blip.
  PositionOut(regs, transformed.view);
  if (transformed.blip)
  {
    regs.es = GameState::VIDEO_SEGMENT;
  }
  _guest.Clobber(TRANSFORMS_SUN_OR_PLANET);
}

void TransformShipEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const TransformedShip ship = TransformShip(_guest.State(), ObjectSlot(_guest.State(), regs.di));
  // XorScannerBlip, drawing the blip or erasing it in the scoop, leaves ES on the video segment, and the contract compares ES. DI
  // is popped back to the slot, and AX, BX and CX, CF with them, are as ClassifyViewPosition's code leaves the view position.
  if (ship.blipTouched)
  {
    regs.es = GameState::VIDEO_SEGMENT;
  }
  ClassifyViewOut(_guest, ship.view, ship.test);
  _guest.Clobber(TRANSFORMS_SHIP);
}

void TransformToViewWithBlipEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ViewWithBlip transformed = TransformToViewWithBlip(_guest.State(), ObjectSlot(_guest.State(), regs.di), PositionIn(regs));
  PositionOut(regs, transformed.view);
  // UpdateScannerBlip leaves ES the video segment once it draws a blip, and the contract compares ES.
  if (transformed.blip)
  {
    regs.es = GameState::VIDEO_SEGMENT;
  }
  _guest.Clobber(CLOBBERS_DX);
}

void TransformToViewEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PositionOut(regs, TransformToView(_guest.State(), PositionIn(regs)));
  _guest.Clobber(CLOBBERS_DX);
}

void DrawVisibleFacesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const FacesEnd end = DrawVisibleFaces(_guest.State(), regs.si, regs.cx, _guest.Flag(Machine::FLAG_DIRECTION));
  regs.si = end.next;
  // DrawLine's CLD before a horizontal line's REP STOSB, which the string instructions after it go on with.
  _guest.SetFlag(Machine::FLAG_DIRECTION, end.backward);
  _guest.Clobber(DRAWS_VISIBLE_FACES);
}

void RunBlueprintHandlerEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // PUSH DI, and POP DI at RenderBlueprintBody's end: the slot comes back.
  _guest.SetFlag(Machine::FLAG_DIRECTION, RunBlueprintHandler(_guest.State(), regs.si, _guest.Flag(Machine::FLAG_DIRECTION)));
  _guest.Clobber(RENDERS_BLUEPRINT);
}

void RenderBlueprintBodyEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Vector accumulator{static_cast<std::int16_t>(regs.bp), static_cast<std::int16_t>(regs.bx), static_cast<std::int16_t>(regs.dx)};
  _guest.SetFlag(Machine::FLAG_DIRECTION, RenderBlueprint(_guest.State(), regs.si, accumulator, _guest.Flag(Machine::FLAG_DIRECTION)));
  // POP DI: the slot RunBlueprintHandler pushed under the way back here.
  regs.di = _guest.Pop();
  _guest.Clobber(RENDERS_BLUEPRINT);
}

void TransformAndDrawObjectsEntry(Guest& _guest)
{
  // The direction flag as the drawing leaves it: a face edge's DrawLine clears it, and the string instructions after go by it.
  _guest.SetFlag(Machine::FLAG_DIRECTION, TransformAndDrawObjects(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION)));
  _guest.Clobber(TRANSFORMS_AND_DRAWS_OBJECTS);
}

void DrawSunOrPlanetEntry(Guest& _guest)
{
  DrawSunOrPlanet(_guest.State(), _guest.Devices(), ObjectSlot(_guest.State(), _guest.Regs().di), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(DRAWS_SUN_OR_PLANET);
}

void DrawDistantStationEntry(Guest& _guest)
{
  DrawDistantStation(_guest.State(), ObjectSlot(_guest.State(), _guest.Regs().di), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(DRAWS_DISTANT_STATION);
}

void ProjectToScreenEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ScreenPoint at = ProjectToScreen(_guest.State(), PositionIn(regs));
  regs.ax = static_cast<std::uint16_t>(at.x);
  regs.bx = static_cast<std::uint16_t>(at.y);
  _guest.Clobber(PROJECTS_TO_SCREEN);
}

void TriangleWindingSignEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Triangle triangle = TriangleIn(regs);
  _guest.SetFlag(Machine::FLAG_SIGN, TriangleWindingSign(triangle));
  // The original leaves the second product in DX:AX, and in BX the first's low word, less the second's when the high words
  // agree. After a hidden last face DrawVisibleFaces returned them, and RenderBlueprintBody's contract compared them.
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
  NativeEntry{0x2340, "ProjectVertices", &ProjectVerticesEntry, PROJECTS_VERTICES},
  NativeEntry{0x3740, "ReflectVertexAboutCenter", &ReflectVertexAboutCenterEntry, CLOBBERS_AX},
  NativeEntry{0x3768, "OffsetVertexByCenter", &OffsetVertexByCenterEntry, PRESERVES_ALL},
  NativeEntry{0x377A, "BuildBoxCornerVertices", &BuildBoxCornerVerticesEntry, BUILDS_BOX},
  NativeEntry{0x38BF, "BuildDodoVertices", &BuildDodoVerticesEntry, CLOBBERS_DX},
  NativeEntry{0x3A13, "ScaleDodoRadii", &ScaleDodoRadiiEntry, CLOBBERS_AX},
  NativeEntry{0x3A40, "RunVertexProgram", &RunVertexProgramEntry, CLOBBERS_AX_DI},
  NativeEntry{0x3A9B, "TriangleWindingSign", &TriangleWindingSignEntry, WINDING_SIGN},
  NativeEntry{0x3AB3, "DrawVisibleFaces", &DrawVisibleFacesEntry, DRAWS_VISIBLE_FACES},
  NativeEntry{0x3BEA, "CheckShipInRange", &CheckShipInRangeEntry, RETURNS_CARRY},
  NativeEntry{0x3C52, "TransformSunOrPlanet", &TransformSunOrPlanetEntry, TRANSFORMS_SUN_OR_PLANET},
  NativeEntry{0x3C72, "ClassifyStationPosition", &ClassifyStationPositionEntry, RETURNS_CARRY},
  NativeEntry{0x3C7E, "TransformShip", &TransformShipEntry, TRANSFORMS_SHIP},
  NativeEntry{0x3CDD, "RunBlueprintHandler", &RunBlueprintHandlerEntry, RENDERS_BLUEPRINT},
  NativeEntry{0x3CF2, "RenderBlueprintBody", &RenderBlueprintBodyEntry, RENDERS_BLUEPRINT},
  NativeEntry{0x3D25, "TransformAndDrawObjects", &TransformAndDrawObjectsEntry, TRANSFORMS_AND_DRAWS_OBJECTS},
  NativeEntry{0x3ED7, "TransformToViewWithBlip", &TransformToViewWithBlipEntry, CLOBBERS_DX},
  NativeEntry{0x3EE3, "TransformToView", &TransformToViewEntry, CLOBBERS_DX},
  NativeEntry{0x3F4F, "DrawSunOrPlanet", &DrawSunOrPlanetEntry, DRAWS_SUN_OR_PLANET},
  NativeEntry{0x45C6, "DrawDistantStation", &DrawDistantStationEntry, DRAWS_DISTANT_STATION},
  NativeEntry{0x8A16, "LoadPlayerAngles", &LoadPlayerAnglesEntry, PRESERVES_ALL},
  NativeEntry{0x8D2E, "ProjectToScreen", &ProjectToScreenEntry, PROJECTS_TO_SCREEN},
};

} // namespace

std::span<const NativeEntry> SceneEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
