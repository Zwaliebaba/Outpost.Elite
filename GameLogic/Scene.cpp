#include "pch.h"

#include "Scene.h"

#include "DataOverlay.h"
#include "Maths.h"

#include <initializer_list>
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
constexpr std::uint16_t IS_OBJECT_NEAR = 0x3B9A;
constexpr std::uint16_t IS_SUN_OR_PLANET = 0x3F2A;
constexpr std::uint16_t IS_PLANET = 0x3F37;
constexpr std::uint16_t IS_STATION = 0x3F40;
constexpr std::uint16_t UPDATE_SCANNER_BLIP = 0x40EC;
constexpr std::uint16_t UPDATE_COMPASS = 0x418F;
constexpr std::uint16_t ERASE_SCANNER_BLIP = 0x42D6;
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

// An object slot: 64 bytes from shipSlots.
constexpr std::uint16_t SLOT_BYTES = 0x40;
constexpr std::uint16_t SLOT_POSITION_X = 0x04; // the low words of the 24-bit position
constexpr std::uint16_t SLOT_POSITION_Y = 0x06;
constexpr std::uint16_t SLOT_POSITION_Z = 0x08;
constexpr std::uint16_t SLOT_SCALE_SHIFT = 0x0A; // byte; for a ship, the word is its pitch angle
constexpr std::uint16_t SLOT_COLOR = 0x0B;
constexpr std::uint16_t SLOT_PITCH = 0x0A;
constexpr std::uint16_t SLOT_YAW = 0x0C;
constexpr std::uint16_t SLOT_ROLL = 0x0E;
constexpr std::uint16_t SLOT_VIEW_X = 0x10;
constexpr std::uint16_t SLOT_VIEW_Y = 0x12;
constexpr std::uint16_t SLOT_VIEW_Z = 0x14;
constexpr std::uint16_t SLOT_VIEW_Z_HIGH = 0x15;
constexpr std::uint16_t SLOT_STATE = 0x1E;
constexpr std::uint16_t SLOT_COMPASS_X = 0x20;
constexpr std::uint16_t SLOT_COMPASS_Y = 0x22;
constexpr std::uint16_t SLOT_COMPASS_Z = 0x24;
constexpr std::uint16_t SLOT_FLASH_FRAMES = 0x2C;
constexpr std::uint16_t SLOT_FRAMES_AWAY = 0x34;
constexpr std::uint16_t SLOT_CAMERA_Z_HIGH = 0x3C;
constexpr std::uint16_t SLOT_DEPTH = 0x3D;
constexpr std::uint16_t SLOT_SIZE = 0x3E;
constexpr std::uint16_t SLOT_DETAIL = 0x3F;

// Byte 0 of a slot.
constexpr std::uint8_t SLOT_ACTIVE = 0x01;
constexpr std::uint8_t SLOT_TYPE_BITS = 0x3E;
constexpr std::uint8_t SLOT_IN_RANGE = 0x40;
constexpr std::uint8_t SLOT_VISIBLE = 0x80;
constexpr std::uint8_t SLOT_KEEP_BITS = 0x3F;
constexpr std::uint8_t SLOT_DRAWABLE = SLOT_VISIBLE | SLOT_ACTIVE;
// Byte +1Eh.
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

[[nodiscard]] std::uint16_t Negate(std::uint16_t _value) noexcept
{
  return static_cast<std::uint16_t>(0u - _value);
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

[[nodiscard]] std::uint8_t Low(std::uint16_t _register) noexcept
{
  return static_cast<std::uint8_t>(_register);
}

[[nodiscard]] std::uint8_t High(std::uint16_t _register) noexcept
{
  return static_cast<std::uint8_t>(_register >> 8);
}

[[nodiscard]] std::uint16_t WithLow(std::uint16_t _register, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_register & 0xFF00) | _low);
}

[[nodiscard]] std::uint16_t WithHigh(std::uint16_t _register, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>((_register & 0x00FF) | (_high << 8));
}

[[nodiscard]] std::uint16_t Offset(std::uint16_t _base, std::uint16_t _bytes) noexcept
{
  return static_cast<std::uint16_t>(_base + _bytes);
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
  DivideUnsigned(_guest, _guest.Word(Offset(regs.si, VERTEX_Z)), _returnOffset);
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

// The three vertex bytes at SI, each shifted left by _shift into an offset in vertexBuffer, as
// TriangleWindingSign and FillTriangle take them: (AX, DX), (BX, BP) through faceTestScratch, (CX, DI).
void LoadTriangle(Guest& _guest, std::uint8_t _shift)
{
  Machine::Registers& regs = _guest.Regs();
  const auto nextVertex = [&_guest, &regs, _shift]()
  {
    regs.bx = static_cast<std::uint8_t>(_guest.Byte(regs.si) << _shift);
    regs.si = Offset(regs.si, 1);
    return Offset(regs.bx, regs.di);
  };
  regs.di = DS.vertexBuffer.offset;
  std::uint16_t vertex = nextVertex();
  regs.ax = _guest.Word(vertex);
  regs.dx = _guest.Word(Offset(vertex, 2));
  vertex = nextVertex();
  regs.cx = _guest.Word(vertex);
  _guest.Set(DS.faceTestScratch, regs.cx);
  regs.bp = _guest.Word(Offset(vertex, 2));
  vertex = nextVertex();
  regs.cx = _guest.Word(vertex);
  regs.di = _guest.Word(Offset(vertex, 2));
  regs.bx = _guest.Get(DS.faceTestScratch);
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
  LoadTriangle(_guest, 1);
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

// ClassifyViewPosition (CS:3CA1), the shared tail of TransformShip and ClassifyStationPosition: AX, BX,
// CX are slot DI's view position. Visible, CF clear and byte 0 bit 7 set, when z is at least nearClipZ
// and twice |x| and twice |y| are at most z; the position is stored when z passes.
void ClassifyViewPosition(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (Negative(regs.cx) || regs.cx < _guest.Get(DS.nearClipZ))
  {
    _guest.SetFlag(Machine::FLAG_CARRY, true);
    return;
  }
  _guest.SetWord(Offset(regs.di, SLOT_VIEW_X), regs.ax);
  _guest.Set(DS.drawCenterX, regs.ax);
  _guest.SetWord(Offset(regs.di, SLOT_VIEW_Y), regs.bx);
  _guest.Set(DS.drawCenterY, regs.bx);
  _guest.SetWord(Offset(regs.di, SLOT_VIEW_Z), regs.cx);
  _guest.Set(DS.drawCenterZ, regs.cx);
  regs.ax = static_cast<std::uint16_t>(Magnitude(regs.ax) << 1);
  if (regs.cx < regs.ax)
  {
    _guest.SetFlag(Machine::FLAG_CARRY, true);
    return;
  }
  regs.bx = static_cast<std::uint16_t>(Magnitude(regs.bx) << 1);
  if (regs.cx < regs.bx)
  {
    _guest.SetFlag(Machine::FLAG_CARRY, true);
    return;
  }
  OrByte(_guest, regs.di, SLOT_VISIBLE);
  _guest.SetFlag(Machine::FLAG_CARRY, false);
}

// RotateToViewDirection (CS:3EE6), the shared tail of the two TransformToView entries: rotates (x, z)
// by -viewAngle through rotation slot 8 when the view is not the front one.
void RotateToViewDirection(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t viewAngle = _guest.Get(DS.viewAngle);
  if (viewAngle == 0)
  {
    return;
  }
  const std::uint16_t x = regs.ax;
  const std::uint16_t y = regs.bx;
  const std::uint16_t z = regs.cx;
  regs.ax = Negate(viewAngle);
  SetSinCos(_guest, DS.rotationSinCos.At(8));
  regs.bx = z;
  regs.ax = x;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(8));
  regs.cx = regs.bx;
  regs.bx = y;
}

// CheckShipInRange's test once IsObjectNear has passed: false at the first bound the slot exceeds.
[[nodiscard]] bool ShipWithinRange(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t axisLimit = _guest.Get(DS.maxAxisDistance);
  regs.ax = Magnitude(_guest.Word(Offset(regs.di, SLOT_POSITION_X)));
  if (regs.ax >= axisLimit)
  {
    return false;
  }
  regs.bx = Magnitude(_guest.Word(Offset(regs.di, SLOT_POSITION_Y)));
  if (regs.bx >= axisLimit)
  {
    return false;
  }
  regs.cx = Magnitude(_guest.Word(Offset(regs.di, SLOT_POSITION_Z)));
  if (regs.cx >= _guest.Get(DS.maxAxisDistance))
  {
    return false;
  }
  // mul: the high words of the unsigned squares, summed in BP.
  const auto square = [&regs](std::uint16_t _value)
  {
    const std::uint32_t product = std::uint32_t{_value} * _value;
    regs.ax = static_cast<std::uint16_t>(product);
    regs.dx = static_cast<std::uint16_t>(product >> 16);
  };
  square(regs.ax);
  regs.bp = regs.dx;
  square(regs.bx);
  regs.bp = Offset(regs.bp, regs.dx);
  if (regs.bp >= _guest.Get(DS.maxDistanceSquaredHigh))
  {
    return false;
  }
  square(regs.cx);
  regs.bp = Offset(regs.bp, regs.dx);
  if (regs.bp >= _guest.Get(DS.maxDistanceSquaredHigh))
  {
    return false;
  }
  regs.bp = static_cast<std::uint16_t>(regs.bp >> SQUARED_DISTANCE_SHIFT);
  regs.ax = regs.bp;
  _guest.SetByte(Offset(regs.di, SLOT_SIZE), Low(regs.ax));
  OrByte(_guest, regs.di, SLOT_IN_RANGE);
  return true;
}

// One slot of TransformAndDrawObjects' first pass, DI the slot.
void ClassifyObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t state = Offset(regs.di, SLOT_STATE);
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
    CheckShipInRange(_guest);
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

// Slot DI's flashing, for an object with +1Eh bit 5: true when it is in its off frames and not drawn.
[[nodiscard]] bool FlashedOff(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t state = Offset(regs.di, SLOT_STATE);
  if ((_guest.Byte(state) & STATE_FLASHING) == 0)
  {
    return false;
  }
  const std::uint16_t frames = Offset(regs.di, SLOT_FLASH_FRAMES);
  const bool off = (_guest.Byte(state) & STATE_FLASH_OFF) != 0;
  _guest.SetByte(frames, static_cast<std::uint8_t>(_guest.Byte(frames) - 1));
  if (_guest.Byte(frames) != 0)
  {
    return off;
  }
  _guest.SetByte(state, static_cast<std::uint8_t>(_guest.Byte(state) ^ STATE_FLASH_OFF));
  _guest.SetByte(frames, off ? FLASH_OFF_FRAMES : FLASH_ON_FRAMES);
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
  if (FlashedOff(_guest))
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
    if (Low(regs.ax) == SLOT_DRAWABLE && (_guest.Byte(Offset(regs.di, SLOT_STATE)) & STATE_DRAWN) == 0)
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
  OrByte(_guest, Offset(regs.di, SLOT_STATE), STATE_DRAWN);
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
  DivideUnsigned(_guest, _guest.Word(Offset(regs.di, SLOT_VIEW_Z)), _returnOffset);
  regs.bp = static_cast<std::uint16_t>(regs.bp - 1);
  if (regs.bp == 0)
  {
    regs.ax = Negate(regs.ax);
  }
}

// center + BX must not overflow or be negative, and center - BX must not overflow or pass _limit.
[[nodiscard]] bool DiscWithin(Guest& _guest, std::uint16_t _center, std::uint16_t _limit)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = static_cast<std::uint16_t>(_center + regs.bx);
  if (AddOverflows(_center, regs.bx) || Negative(regs.ax))
  {
    return false;
  }
  const auto once = static_cast<std::uint16_t>(regs.ax - regs.bx);
  regs.ax = static_cast<std::uint16_t>(once - regs.bx);
  return !SubtractOverflows(once, regs.bx) && static_cast<std::int16_t>(regs.ax) <= static_cast<std::int16_t>(_limit);
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
  if (!DiscWithin(_guest, regs.dx, SCREEN_RIGHT) || !DiscWithin(_guest, regs.cx, SCREEN_BOTTOM))
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
    ScaleByInverseDistance(_guest);
  }
  _guest.Set(DS.cabinTemperature, Low(regs.ax));
  if ((_guest.Byte(Offset(regs.di, SLOT_VIEW_Z_HIGH)) & 0x80) != 0)
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

// The planet's size and the altitude it gives, and death by collision: false when it is behind the view.
[[nodiscard]] bool SizePlanet(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.sunFringeMask, FRINGE_NONE);
  regs.dx = PLANET_SCALE;
  regs.ax = 0;
  ScaleByInverseDistance(_guest);
  auto altitude = static_cast<std::uint8_t>(~Low(regs.ax));
  if (altitude > ALTITUDE_LIMIT)
  {
    altitude = ALTITUDE_LIMIT;
  }
  altitude = static_cast<std::uint8_t>(altitude << 1);
  regs.bx = WithLow(regs.bx, altitude);
  _guest.Set(DS.altitude, altitude);
  if ((_guest.Byte(Offset(regs.di, SLOT_VIEW_Z_HIGH)) & 0x80) != 0)
  {
    return false;
  }
  if (regs.ax >= FATAL_RADIUS)
  {
    const std::uint16_t radius = regs.ax;
    _guest.Call(KILL_PLAYER);
    regs.ax = radius;
    if (High(regs.ax) != 0)
    {
      regs.ax = RADIUS_LIMIT;
    }
  }
  return true;
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

void ReflectVertexAboutCenter(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint16_t bytes = 0;
  for (const DataField<std::uint16_t> center : {DS.drawCenterX, DS.drawCenterY, DS.drawCenterZ})
  {
    regs.ax = _guest.Get(center);
    regs.bx = regs.ax;
    regs.ax = static_cast<std::uint16_t>(regs.ax - _guest.Word(Offset(regs.si, bytes)));
    _guest.SetWord(Offset(regs.di, bytes), regs.ax);
    _guest.SetWord(Offset(regs.si, bytes), Offset(_guest.Word(Offset(regs.si, bytes)), regs.bx));
    bytes = Offset(bytes, 2);
  }
}

void RunVertexProgram(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = _guest.Byte(regs.si);
  regs.si = Offset(regs.si, 1);
  if (regs.cx == 0)
  {
    return;
  }
  do
  {
    const std::uint8_t op = _guest.Byte(regs.si);
    regs.si = Offset(regs.si, 1);
    regs.di = DS.vertexBuffer.At(op & VERTEX_INDEX_BITS);
    regs.ax = static_cast<std::uint8_t>(op & VERTEX_OP_BITS);
    const std::uint16_t x = _guest.Word(regs.di);
    const std::uint16_t y = _guest.Word(Offset(regs.di, 2));
    const std::uint16_t z = _guest.Word(Offset(regs.di, 4));
    switch (regs.ax)
    {
    case VERTEX_OP_LOAD:
      regs.bp = x;
      regs.bx = y;
      regs.dx = z;
      break;
    case VERTEX_OP_STORE:
      _guest.SetWord(regs.di, regs.bp);
      _guest.SetWord(Offset(regs.di, 2), regs.bx);
      _guest.SetWord(Offset(regs.di, 4), regs.dx);
      break;
    case VERTEX_OP_AVERAGE:
    {
      const auto average = [](std::uint16_t _sum, std::uint16_t _value)
      { return static_cast<std::uint16_t>(static_cast<std::int16_t>(Offset(_sum, _value)) >> 1); };
      regs.bp = average(regs.bp, x);
      regs.bx = average(regs.bx, y);
      regs.dx = average(regs.dx, z);
      break;
    }
    default:
      regs.bp = Offset(regs.bp, x);
      regs.bx = Offset(regs.bx, y);
      regs.dx = Offset(regs.dx, z);
      break;
    }
  } while (Loop(regs.cx));
}

void TriangleWindingSign(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto product = [](std::uint16_t _a, std::uint16_t _b)
  { return static_cast<std::uint32_t>(std::int32_t{static_cast<std::int16_t>(_a)} * static_cast<std::int16_t>(_b)); };
  regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bx);
  regs.cx = static_cast<std::uint16_t>(regs.cx - regs.bx);
  regs.dx = static_cast<std::uint16_t>(regs.dx - regs.bp);
  regs.di = static_cast<std::uint16_t>(regs.di - regs.bp);
  const std::uint32_t first = product(regs.ax, regs.di);
  const std::uint32_t second = product(regs.dx, regs.cx);
  regs.ax = static_cast<std::uint16_t>(second);
  regs.dx = static_cast<std::uint16_t>(second >> 16);
  regs.bx = static_cast<std::uint16_t>(first);
  regs.cx = static_cast<std::uint16_t>((first >> 16) - regs.dx);
  std::uint16_t sign = regs.cx;
  if (regs.cx == 0)
  {
    regs.bx = static_cast<std::uint16_t>(regs.bx - regs.ax);
    sign = regs.bx;
  }
  _guest.SetFlag(Machine::FLAG_SIGN, Negative(sign));
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
    LoadTriangle(_guest, 0);
    TriangleWindingSign(_guest);
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

void CheckShipInRange(Guest& _guest)
{
  _guest.Call(IS_OBJECT_NEAR);
  if (Flag(_guest, Machine::FLAG_CARRY) && ShipWithinRange(_guest))
  {
    _guest.SetFlag(Machine::FLAG_CARRY, false);
    return;
  }
  _guest.Call(ERASE_SCANNER_BLIP);
  _guest.SetFlag(Machine::FLAG_CARRY, true);
}

void TransformSunOrPlanet(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  GetPositionScaleShift(_guest);
  _guest.SetByte(Offset(regs.di, SLOT_SCALE_SHIFT), Low(regs.cx));
  _guest.SetByte(Offset(regs.di, SLOT_DEPTH), Low(regs.cx));
  regs.dx = WithHigh(regs.dx, Low(regs.cx));
  ScalePositionDown(_guest);
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
  ClassifyViewPosition(_guest);
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
  ClassifyViewPosition(_guest);
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
  RunVertexProgram(_guest);
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
  regs.di = slot;
}

void TransformAndDrawObjects(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Get(DS.playerPitchAngle);
  SetSinCos(_guest, DS.rotationSinCos.At(0));
  regs.ax = _guest.Get(DS.playerYawAngle);
  SetSinCos(_guest, DS.rotationSinCos.At(1));
  regs.ax = _guest.Get(DS.playerRollAngle);
  SetSinCos(_guest, DS.rotationSinCos.At(2));
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
  RotatePitchYawRoll(_guest);
  _guest.SetByte(Offset(regs.di, SLOT_CAMERA_Z_HIGH), High(regs.cx));
  _guest.Call(UPDATE_SCANNER_BLIP);
  RotateToViewDirection(_guest);
}

void TransformToView(Guest& _guest)
{
  RotatePitchYawRoll(_guest);
  RotateToViewDirection(_guest);
}

void DrawSunOrPlanet(Guest& _guest)
{
  _guest.Call(IS_PLANET);
  const bool drawn = Flag(_guest, Machine::FLAG_ZERO) ? SizePlanet(_guest) : SizeSun(_guest);
  if (drawn)
  {
    DrawSunOrPlanetDisc(_guest);
  }
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
  DivideUnsigned(_guest, regs.cx, SCREEN_X_DIVIDE_RETURN);
  std::swap(regs.ax, regs.bx);
  ShiftIntoDividend(regs, HIGH_BYTE_ZERO);
  DivideUnsigned(_guest, regs.cx, SCREEN_Y_DIVIDE_RETURN);
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

constexpr std::array ENTRIES = {
  NativeEntry{0x2340, "ProjectVertices", &ProjectVertices,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI, 0}},
  NativeEntry{0x3740, "ReflectVertexAboutCenter", &ReflectVertexAboutCenter, Machine::NativeContract{REGISTER_AX | REGISTER_BX, 0}},
  NativeEntry{0x3A40, "RunVertexProgram", &RunVertexProgram, Machine::NativeContract{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0}},
  NativeEntry{0x3A9B, "TriangleWindingSign", &TriangleWindingSign, Machine::NativeContract{ALL_BUT_DS, FLAG_SIGN}},
  NativeEntry{0x3AB3, "DrawVisibleFaces", &DrawVisibleFaces,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0}},
  NativeEntry{0x3BEA, "CheckShipInRange", &CheckShipInRange, RETURNS_CARRY},
  NativeEntry{0x3C52, "TransformSunOrPlanet", &TransformSunOrPlanet, PRESERVES_ALL},
  NativeEntry{0x3C72, "ClassifyStationPosition", &ClassifyStationPosition, RETURNS_CARRY},
  NativeEntry{0x3C7E, "TransformShip", &TransformShip, RETURNS_CARRY},
  NativeEntry{0x3CDD, "RunBlueprintHandler", &RunBlueprintHandler, Machine::NativeContract{ALL_BUT_DS & ~REGISTER_DI, 0}},
  NativeEntry{0x3D25, "TransformAndDrawObjects", &TransformAndDrawObjects, Machine::NativeContract{ALL_BUT_DS, 0}},
  NativeEntry{0x3ED7, "TransformToViewWithBlip", &TransformToViewWithBlip, PRESERVES_ALL},
  NativeEntry{0x3EE3, "TransformToView", &TransformToView, PRESERVES_ALL},
  NativeEntry{0x3F4F, "DrawSunOrPlanet", &DrawSunOrPlanet, PRESERVES_ALL},
  NativeEntry{0x8D2E, "ProjectToScreen", &ProjectToScreen, Machine::NativeContract{REGISTER_DX | REGISTER_BP, 0}},
};

} // namespace

std::span<const NativeEntry> SceneEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
