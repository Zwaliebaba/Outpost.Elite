#include "pch.h"

#include "Docking.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Flight.h"
#include "Maths.h"
#include "Scene.h"
#include "Ships.h"
#include "Sound.h"
#include "Text.h"
#include "Timer.h"
#include "Video.h"

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;

// The station tunnel: ten frames of one to ten of tunnelRectangles' ten rectangles, ten bytes each, and
// ten of ten to one; docked, each frame waits for timer ticks first.
constexpr std::uint16_t TUNNEL_FRAMES = 10;
constexpr std::uint16_t TUNNEL_RECTANGLE_BYTES = 10;
constexpr std::uint8_t TUNNEL_COLOR = 3;
constexpr std::uint16_t DOCKED_FIRST_TICKS = 0x19;
constexpr std::uint16_t TUNNEL_FRAME_TICKS = 0x14;
constexpr std::uint16_t DOCKING_MESSAGE_FRAMES = 0x19;
// Where PlayStationTunnel jumps back (ADR-015): its two frame loops, their rectangles' loops and their waits for the timer.
constexpr std::uint16_t FIRST_TUNNEL_FRAME = 0x2D7A;
constexpr std::uint16_t DOCKED_FIRST_WAIT = 0x2D9A;
constexpr std::uint16_t FIRST_RECTANGLES = 0x2DA1;
constexpr std::uint16_t FIRST_FRAME_WAIT = 0x2DB8;
constexpr std::uint16_t LAST_TUNNEL_FRAME = 0x2DC9;
constexpr std::uint16_t LAST_FRAME_WAIT = 0x2DE7;
constexpr std::uint16_t LAST_RECTANGLES = 0x2DEE;
// The BX PlayStationTunnel calls UpdateStardust with is what TransformAndDrawObjects leaves there, which MoveObjectsByVelocity
// keeps. It reaches nothing but ComputeStardustShift's divide trap, which only a speed above 34h reaches, and no speed the game
// sets is above 30h (TOP_SPEED, MOST_SPEED): 0 stands in for it.
constexpr std::uint16_t STARDUST_BX = 0;

// The station's slot, whose fields SLOT_X, SLOT_Y, SLOT_Z and SLOT_FLAGS name (Ships.h).
constexpr std::uint8_t STATION_SHOT = 0x01; // in the slot's flags: docking is refused

// The docking computer's states (dockingComputerState).
constexpr std::uint8_t SLOW_TO_STOP = 0;
constexpr std::uint8_t AIM_ROLL_AT_APPROACH = 1;
constexpr std::uint8_t ROLL_TO_APPROACH = 2;
constexpr std::uint8_t PITCH_TO_APPROACH = 3;
constexpr std::uint8_t FLY_TO_APPROACH = 4;
constexpr std::uint8_t AIM_ROLL_AT_STATION = 5;
constexpr std::uint8_t ROLL_TO_STATION = 6;
constexpr std::uint8_t PITCH_TO_STATION = 7;
constexpr std::uint8_t CLOSE_IN = 8;
constexpr std::uint8_t MATCH_SPIN = 9;
constexpr std::uint8_t ROLL_WITH_SPIN = 10;
constexpr std::uint8_t ROLL_WITH_SPIN_TURNED = 11;
constexpr std::uint16_t APPROACH_DISTANCE = 0x7D0; // the approach point, in front of the slot along z
constexpr std::uint16_t QUARTER_TURN = 0x200;
constexpr std::uint16_t ANGLE_SIGN = 0x400; // of an 11-bit angle
constexpr std::uint16_t ROLL_TOLERANCE = 0x13;
constexpr std::uint16_t APPROACH_PITCH_TOLERANCE = 0x0F;
constexpr std::uint16_t STATION_PITCH_TOLERANCE = 9;
constexpr std::uint16_t SPIN_TOLERANCE = 0x0B;
constexpr std::uint8_t ROLL_STEP = 0xF7; // -9, and 9 the other way
constexpr std::uint8_t APPROACH_PITCH_STEP = 6;
constexpr std::uint8_t STATION_PITCH_STEP = 3;
constexpr std::uint16_t SLOW_DOWN_DISTANCE = 0x15E;
constexpr std::uint16_t STOP_CLOSING_DISTANCE = 0x28A;
constexpr std::uint16_t CLOSE_SLOWLY_DISTANCE = 0x3E8;
constexpr std::uint16_t SPEED_STEP = 4;
constexpr std::uint16_t LEAST_SPEED = 4;
constexpr std::uint16_t MOST_SPEED = 0x30;

constexpr std::uint16_t TUNNEL_EDGES = 4;
constexpr std::uint8_t VIEW_CENTER_X = 0x80;
constexpr std::uint8_t VIEW_CENTER_ROW = 0x40;
constexpr std::uint16_t VIEW_ROW_BYTES = 0x40;
constexpr std::uint16_t VIEW_LAST_WORD = 0x1FFE; // of the buffer at DS:0000, rows 0-127

constexpr std::uint16_t HALF_TURN = 0x400; // in 2048ths
constexpr std::uint16_t ANGLE_MASK = 0x7FF;
constexpr std::uint8_t SHIP_TYPE_MASK = 0x1F; // of the slot's first byte, shifted right once

// The byte of a stardust particle that holds its lifetime, the last that ResetStardust stores.
constexpr std::uint16_t PARTICLE_LIFETIME = 4;

// A point about the view's centre, as DrawLine takes it: x and row from the top-left.
[[nodiscard]] std::uint16_t FromCenter(std::uint16_t _point) noexcept
{
  const auto x = static_cast<std::uint8_t>(Low(_point) + VIEW_CENTER_X);
  const auto row = static_cast<std::uint8_t>(High(_point) + VIEW_CENTER_ROW);
  return static_cast<std::uint16_t>((row << 8) | x);
}

// REP STOSB or REP STOSW: _value, or its low byte, _count times from _segment:_offset, a byte or a word at a time
// (_bytes), backwards when _backward. Returns the offset after the last, which the original leaves in DI.
std::uint16_t Store(GameState& _state, std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _count, std::uint16_t _value,
                    std::uint16_t _bytes, bool _backward)
{
  const auto step = static_cast<std::uint16_t>(_backward ? 0u - _bytes : _bytes);
  std::uint16_t at = _offset;
  for (std::uint16_t left = _count; left != 0; --left)
  {
    if (_bytes == 2)
    {
      _state.SetFarWord(_segment, at, _value);
    }
    else
    {
      _state.SetFarByte(_segment, at, Low(_value));
    }
    at = Offset(at, step);
  }
  return at;
}

// Where DrawTunnelRectangles stopped.
struct TunnelDrawn
{
  std::uint16_t next; // past the last rectangle drawn, where the original leaves SI
  bool filled;        // a DrawLine filled bytes with REP STOSB (DrawLineOut)
};

// _count of tunnelRectangles from _first, each by DrawTunnelRectangle in the tunnel's colour, and the LOOP back to CS:_loop
// (2DA1, 2DEE), which carries the count and the next rectangle (ADR-015).
TunnelDrawn DrawTunnelRectangles(GameState& _state, Hardware& _hardware, std::uint16_t _first, std::uint16_t _count, std::uint16_t _loop)
{
  TunnelDrawn drawn{_first, false};
  std::uint16_t count = _count;
  for (;;)
  {
    _state.Set(DS.drawColor, TUNNEL_COLOR);
    drawn.filled = DrawTunnelRectangle(_state, drawn.next) || drawn.filled;
    drawn.next = Offset(drawn.next, TUNNEL_RECTANGLE_BYTES);
    if (--count == 0)
    {
      return drawn;
    }
    _hardware.LoopTurn(_loop, {count, drawn.next});
  }
}

// MOV CX,_ticks, then WaitForTimerTick at CS:_loop and the LOOP back there, which carries the count (ADR-015): about _ticks
// milliseconds. The original leaves CX = 0.
void WaitTimerTicks(GameState& _state, Hardware& _hardware, std::uint16_t _ticks, std::uint16_t _loop)
{
  std::uint16_t count = _ticks;
  for (;;)
  {
    WaitForTimerTick(_state, _hardware);
    if (--count == 0)
    {
      return;
    }
    _hardware.LoopTurn(_loop, {count});
  }
}

// _text posted for 25 frames. Returns its offset, which the original leaves in AX (mov ax, _text).
[[nodiscard]] std::uint16_t PostDockingMessage(GameState& _state, DataAt _text)
{
  _state.Set(DS.messagePointer, _text.offset);
  _state.Set(DS.messageFrames, DOCKING_MESSAGE_FRAMES);
  return _text.offset;
}

// The station's position, z moved out to the approach point when _approach: what the original loads into AX, BX
// and CX, with DI the station's slot.
[[nodiscard]] Vector LoadStationPosition(GameState& _state, bool _approach)
{
  const ObjectSlot station(_state, DS.stationSlot.offset);
  const std::uint16_t z = station.Get(SlotWord::Z);
  return Vector{static_cast<std::int16_t>(station.Get(SlotWord::X)), static_cast<std::int16_t>(station.Get(SlotWord::Y)),
                static_cast<std::int16_t>(_approach ? Offset(z, APPROACH_DISTANCE) : z)};
}

// dockingComputerSteering and rollRate both _steering.
void Steer(GameState& _state, std::uint16_t _steering)
{
  _state.Set(DS.dockingComputerSteering, _steering);
  _state.Set(DS.rollRate, _steering);
}

// playerSpeed up by 4, and then held at 48: ADD and a MOV over it.
void SpeedUp(GameState& _state)
{
  const auto speed = static_cast<std::uint16_t>(_state.Get(DS.playerSpeed) + SPEED_STEP);
  _state.Set(DS.playerSpeed, speed);
  if (speed > MOST_SPEED)
  {
    _state.Set(DS.playerSpeed, MOST_SPEED);
  }
}

// playerSpeed down by 4, and 4 again where that reaches 0: SUB and a MOV over it.
void SlowDown(GameState& _state)
{
  const auto speed = static_cast<std::uint16_t>(_state.Get(DS.playerSpeed) - SPEED_STEP);
  _state.Set(DS.playerSpeed, speed);
  if (speed == 0)
  {
    _state.Set(DS.playerSpeed, LEAST_SPEED);
  }
}

// SAR r,1 twice: a rotated coordinate quartered for ArcTangent2.
[[nodiscard]] std::int16_t Quarter(std::int16_t _value) noexcept
{
  return static_cast<std::int16_t>(Sar(static_cast<std::uint16_t>(_value), 2));
}

// States 1 and 5 (CS:8652, CS:8803): dockingTargetAngle, the roll that brings the target (the approach point, or the
// station) into the pitch plane, whichever of up or down is nearer; then on to state _next. Returns that angle.
std::uint16_t AimRoll(GameState& _state, bool _approach, std::uint8_t _next)
{
  (void)LoadPlayerAngles(_state);
  const Vector target = RotatePitchYawRoll(_state, LoadStationPosition(_state, _approach));
  const std::uint16_t angle = ArcTangent2(_state, Quarter(target.x), Quarter(target.y));
  std::uint16_t magnitude = angle;
  if ((magnitude & ANGLE_SIGN) != 0)
  {
    magnitude = Negate(static_cast<std::uint16_t>(magnitude | ~ANGLE_MASK));
  }
  std::uint16_t roll = angle;
  if (magnitude >= QUARTER_TURN)
  {
    roll = Offset(roll, HALF_TURN);
  }
  roll = static_cast<std::uint16_t>(Offset(roll, _state.Get(DS.playerRollAngle)) & ANGLE_MASK);
  _state.Set(DS.dockingTargetAngle, roll);
  _state.Set(DS.dockingComputerState, _next);
  return roll;
}

// An 11-bit angle sign-extended to 16 bits, as AngleWithinTolerance leaves its second.
[[nodiscard]] std::uint16_t SignExtendAngle(std::uint16_t _angle) noexcept
{
  const auto angle = static_cast<std::uint16_t>(_angle & ANGLE_MASK);
  return (angle & ANGLE_SIGN) != 0 ? static_cast<std::uint16_t>(angle | ~ANGLE_MASK) : angle;
}

// States 2 and 6: roll at 9 a frame towards dockingTargetAngle, and onto it, sign-extended, once within 19; then on to
// state _next. Returns what AngleWithinTolerance found.
AngleTolerance RollToTarget(GameState& _state, std::uint8_t _next)
{
  const std::uint16_t target = _state.Get(DS.dockingTargetAngle);
  const AngleTolerance found = AngleWithinTolerance(_state.Get(DS.playerRollAngle), target, ROLL_TOLERANCE);
  if (found.within)
  {
    _state.Set(DS.playerRollAngle, SignExtendAngle(target));
    _state.Set(DS.dockingComputerState, _next);
    _state.Set(DS.dockingComputerSteering, 0);
    _state.Set(DS.rollRate, 0);
    return found;
  }
  const auto turn = static_cast<std::uint16_t>(_state.Get(DS.dockingTargetAngle) - _state.Get(DS.playerRollAngle));
  Steer(_state, Join(0, (turn & ANGLE_SIGN) != 0 ? Negate(ROLL_STEP) : ROLL_STEP));
  return found;
}

// States 3 and 7 (CS:86E3, CS:8890): pitch towards the target by _step a frame, or by half the angle once within
// _tolerance; the second time that happens, on to state _next at speed 4, else back to state _again. Returns the
// steering it sets.
std::uint16_t PitchToTarget(GameState& _state, bool _approach, std::uint16_t _tolerance, std::uint8_t _step, std::uint8_t _again,
                            std::uint8_t _next)
{
  if (!_approach)
  {
    (void)LoadPlayerAngles(_state);
  }
  const Vector target = RotatePitchYawRoll(_state, LoadStationPosition(_state, _approach));
  const std::uint16_t angle = ArcTangent2(_state, Quarter(target.y), Quarter(target.z));
  if (!AngleWithinTolerance(angle, 0, _tolerance).within)
  {
    // MOV AL,step / TEST AX,400h / NEG AL / MOV AH,AL / XOR AL,AL: the step, against the angle's sign, in the high byte.
    const std::uint16_t steering = Join((angle & ANGLE_SIGN) != 0 ? Negate(_step) : _step, 0);
    Steer(_state, steering);
    return steering;
  }
  // SAR AX,1 / MOV AH,AL / XOR AL,AL: half the angle, its low byte in the high byte.
  const std::uint16_t steering = Join(Low(Sar(angle, 1)), 0);
  Steer(_state, steering);
  if (_state.Get(DS.dockingAlignPasses) != 1)
  {
    _state.Set(DS.dockingAlignPasses, static_cast<std::uint8_t>(_state.Get(DS.dockingAlignPasses) + 1));
    _state.Set(DS.dockingComputerState, _again);
    return steering;
  }
  _state.Set(DS.dockingComputerState, _next);
  _state.Set(DS.playerSpeed, LEAST_SPEED);
  _state.Set(DS.velocityDirty, 1);
  return steering;
}

// CWD / IDIV BX: _coordinate over _frames, the frames left; with no frame left it divides by 0 into the game's trap, which saves
// BX, _frames.
[[nodiscard]] std::uint16_t VelocityOver(GameState& _state, std::uint16_t _coordinate, std::uint16_t _frames)
{
  const auto dividend = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(_coordinate)));
  return DivideSignedWord(_state, dividend, _frames, _frames).quotient;
}

// What VectorLength leaves in BX: the last odd number its root subtracted, twice the root of the top 16 bits of _vector's squared
// length, plus 1.
[[nodiscard]] std::uint16_t VectorLengthOddLeftover(Vector _vector, std::uint16_t _length)
{
  const auto square = [](std::int16_t _value) { return static_cast<std::uint32_t>(std::int32_t{_value} * _value); };
  const std::uint32_t sum = square(_vector.x) + square(_vector.y) + square(_vector.z);
  unsigned shift = 0;
  if ((sum >> 24) != 0)
  {
    shift = 8;
  }
  else if ((sum >> 16) != 0)
  {
    shift = 4;
  }
  return static_cast<std::uint16_t>(((_length >> shift) << 1) + 1);
}

// State 4 (CS:8762): fly straight to the approach point, faster while 350 or more away (SpeedUp, SlowDown), at the velocity
// that gets there in the frames left, the distance over the speed: XOR DX,DX / DIV playerSpeed, a speed of 0 dividing into the
// game's trap, which saves the BX VectorLength left. On the last frame there, the ship stops on it and goes on to state 5.
// MoveObjectsByVelocity moves the world either way.
void FlyToApproachPoint(GameState& _state)
{
  Steer(_state, 0);
  const Vector approach = LoadStationPosition(_state, true);
  const std::uint16_t distance = VectorLength(approach);
  if (distance >= SLOW_DOWN_DISTANCE)
  {
    SpeedUp(_state);
  }
  else
  {
    SlowDown(_state);
  }
  const std::uint16_t frames =
    DivideWord(_state, distance, _state.Get(DS.playerSpeed), VectorLengthOddLeftover(approach, distance)).quotient;
  if (frames == 1)
  {
    _state.Set(DS.playerSpeed, 0);
    _state.Set(DS.velocityDirty, 1);
    _state.Set(DS.playerVelocityX, static_cast<std::uint16_t>(approach.x));
    _state.Set(DS.playerVelocityY, static_cast<std::uint16_t>(approach.y));
    _state.Set(DS.playerVelocityZ, static_cast<std::uint16_t>(approach.z));
    MoveObjectsByVelocity(_state);
    _state.Set(DS.dockingComputerState, AIM_ROLL_AT_STATION);
    _state.Set(DS.dockingAlignPasses, 0);
    return;
  }
  // MOV AX,[DI+axis] (and ADD AX,7D0h for z), then the divide, for x, y and z.
  _state.Set(DS.playerVelocityX, VelocityOver(_state, static_cast<std::uint16_t>(approach.x), frames));
  _state.Set(DS.playerVelocityY, VelocityOver(_state, static_cast<std::uint16_t>(approach.y), frames));
  _state.Set(DS.playerVelocityZ, VelocityOver(_state, static_cast<std::uint16_t>(approach.z), frames));
  MoveObjectsByVelocity(_state);
}

// What CloseIn did.
struct ClosingIn
{
  bool stopped;             // within 650: the spin is matched next
  std::uint16_t lastRandom; // once it stopped, the last random number ResetStardust drew
};

// State 8 (CS:8904): straight in along z, slowing within 1000, until within 650: then the front view, locked.
ClosingIn CloseIn(GameState& _state)
{
  const std::uint16_t distance = Negate(ObjectSlot(_state, DS.stationSlot.offset).Get(SlotWord::Z));
  if (distance < STOP_CLOSING_DISTANCE)
  {
    _state.Set(DS.dockingComputerState, MATCH_SPIN);
    if (_state.Get(DS.viewAngle) != 0)
    {
      _state.Set(DS.viewAngle, 0);
    }
    const std::uint16_t lastRandom = ResetStardust(_state);
    _state.Set(DS.viewLocked, 1);
    return ClosingIn{true, lastRandom};
  }
  if (distance < CLOSE_SLOWLY_DISTANCE)
  {
    SlowDown(_state);
  }
  else
  {
    SpeedUp(_state);
  }
  _state.Set(DS.playerVelocityX, 0);
  _state.Set(DS.playerVelocityY, 0);
  _state.Set(DS.playerVelocityZ, Negate(_state.Get(DS.playerSpeed)));
  MoveObjectsByVelocity(_state);
  _state.Set(DS.velocityDirty, 1);
  return ClosingIn{false, 0};
}

// The station's spin angle plus _turn, negated for a Dodo (IsStation's CF) (CS:8980, CS:89B9, CS:89EA).
[[nodiscard]] std::uint16_t LoadStationSpin(GameState& _state, std::uint16_t _turn)
{
  const ObjectSlot station(_state, DS.stationSlot.offset);
  const auto spin = Offset(station.Get(SlotWord::Roll), _turn);
  return IsStation(station).dodo ? Negate(spin) : spin;
}

// State 9 (CS:8976): inside, matching the station's spin: MoveObjectsByVelocity, then to state 10 once the roll is within 11
// of the spin (AngleWithinTolerance), or to 11 once it is within 11 of it turned half round. Returns the spin, which the
// original leaves in AX.
std::uint16_t MatchSpin(GameState& _state)
{
  MoveObjectsByVelocity(_state);
  const std::uint16_t spin = LoadStationSpin(_state, 0);
  const std::uint16_t roll = _state.Get(DS.playerRollAngle);
  if (AngleWithinTolerance(spin, roll, SPIN_TOLERANCE).within)
  {
    _state.Set(DS.dockingComputerState, ROLL_WITH_SPIN);
  }
  else if (AngleWithinTolerance(spin, Offset(roll, HALF_TURN), SPIN_TOLERANCE).within)
  {
    // ADD CX,400h on the roll the first test left sign-extended in CX, which the second masks off again.
    _state.Set(DS.dockingComputerState, ROLL_WITH_SPIN_TURNED);
  }
  return spin;
}

// States 10 and 11 (CS:89AF, CS:89E0): MoveObjectsByVelocity, then the roll steered by half its difference from the station's
// spin plus _turn, negated as a byte: NEG AL / XOR AH,AH. Returns the steering, which the original leaves in AX.
std::uint16_t RollWithSpin(GameState& _state, std::uint16_t _turn)
{
  MoveObjectsByVelocity(_state);
  const auto spin = static_cast<std::uint16_t>(LoadStationSpin(_state, _turn) & ANGLE_MASK);
  const auto roll = static_cast<std::uint16_t>(_state.Get(DS.playerRollAngle) & ANGLE_MASK);
  const std::uint16_t steering = Negate(Low(Sar(static_cast<std::uint16_t>(spin - roll), 1)));
  Steer(_state, steering);
  return steering;
}

} // namespace

bool DrawTunnelRectangle(GameState& _state, std::uint16_t _points)
{
  bool filled = false;
  std::uint16_t point = _points;
  for (std::uint16_t edge = 0; edge < TUNNEL_EDGES; ++edge)
  {
    const std::uint16_t from = FromCenter(_state.Word(point));
    const std::uint16_t to = FromCenter(_state.Word(Offset(point, 2)));
    filled = DrawLine(_state, Low(from), High(from), Low(to), High(to)) || filled;
    point = Offset(point, 2);
  }
  return filled;
}

bool CheckDockingAlignment(const GameState& _state, const ObjectSlot& _station, std::uint16_t _tolerance)
{
  // Pitch near 0 wants yaw near a half turn, and pitch near a half turn wants yaw near 0.
  const std::uint16_t pitch = _state.Get(DS.playerPitchAngle);
  std::uint16_t yaw = HALF_TURN;
  if (!AngleWithinTolerance(pitch, 0, _tolerance).within)
  {
    if (!AngleWithinTolerance(pitch, HALF_TURN, _tolerance).within)
    {
      return false;
    }
    yaw = 0;
  }
  if (!AngleWithinTolerance(_state.Get(DS.playerYawAngle), yaw, _tolerance).within)
  {
    return false;
  }

  // Roll near the station's spin, negated for ship type 0, or half a turn from it.
  const std::uint16_t roll = _state.Get(DS.playerRollAngle);
  std::uint16_t spin = _station.Get(SlotWord::Roll);
  if (((_station.Get(SlotByte::Type) >> 1) & SHIP_TYPE_MASK) == 0)
  {
    spin = Negate(spin);
  }
  spin &= ANGLE_MASK;
  if (AngleWithinTolerance(roll, spin, _tolerance).within)
  {
    return true;
  }
  return AngleWithinTolerance(roll, static_cast<std::uint16_t>((spin + HALF_TURN) & ANGLE_MASK), _tolerance).within;
}

void MaskOutsideTunnel(GameState& _state, std::uint16_t _rectangle, bool _backwards)
{
  // The margins: x/4 bytes either side, and the rows above and below the rectangle, in the space-view buffer at DS:0000,
  // through ES = DS.
  const std::uint16_t segment = _state.DataSegment();
  const auto sideBytes = static_cast<std::uint16_t>(static_cast<std::uint8_t>(_state.Byte(_rectangle) + VIEW_CENTER_X) >> 2);
  const std::uint8_t top = _state.Byte(Offset(_rectangle, 1));
  const auto rows = static_cast<std::uint16_t>(static_cast<std::uint8_t>(Negate(top) << 1));
  const auto marginWords = static_cast<std::uint16_t>(static_cast<std::uint8_t>(top + VIEW_CENTER_ROW) << 5);
  // The rows above, then the sides of each row of the rectangle, forwards or, as the direction flag says, down; DEC BX
  // and JNE count the rows, 65,536 for 0.
  std::uint16_t row = Store(_state, segment, 0, marginWords, 0, 2, _backwards);
  std::uint16_t left = rows;
  do
  {
    Store(_state, segment, row, sideBytes, 0, 1, _backwards);
    row = Offset(row, VIEW_ROW_BYTES);
  } while (--left != 0);

  // The same from the end of the buffer, backwards (STD).
  row = Offset(Store(_state, segment, VIEW_LAST_WORD, marginWords, 0, 2, true), 1);
  left = rows;
  do
  {
    Store(_state, segment, row, sideBytes, 0, 1, true);
    row = static_cast<std::uint16_t>(row - VIEW_ROW_BYTES);
  } while (--left != 0);
}

void PlayStationTunnel(GameState& _state, Hardware& _hardware, bool _backward)
{
  // MOV AX,leavingStationMessage, or autoDockMessage when docked, for one frame.
  const std::uint16_t message = _state.Get(DS.playerDocked) != 0 ? DS.autoDockMessage.offset : DS.leavingStationMessage.offset;
  _state.Set(DS.messagePointer, message);
  _state.Set(DS.messageFrames, 1);
  (void)UpdateMessageLine(_state, _backward);
  UpdateDashboard(_state);

  // Frame n of the first ten draws the first n rectangles, n in AX, pushed round the frame and popped for its INC; the turns carry
  // it. Leaving, the world moves on behind the tunnel, which masks the view outside the first rectangle; docking, each frame waits
  // for the timer first. The direction flag goes as the original's does: a DrawLine that fills clears it, and so do
  // MaskOutsideTunnel's and FinishSpaceViewFrame's CLD.
  bool backward = _backward;
  for (std::uint16_t frame = 1;;)
  {
    if (_state.Get(DS.playerDocked) == 0)
    {
      backward = TransformAndDrawObjects(_state, _hardware, backward);
      MoveObjectsByVelocity(_state);
      if (UpdateStardust(_state, STARDUST_BX))
      {
        backward = false;
      }
      MaskOutsideTunnel(_state, DS.tunnelRectangles.offset, backward);
      backward = false;
    }
    else
    {
      WaitTimerTicks(_state, _hardware, DOCKED_FIRST_TICKS, DOCKED_FIRST_WAIT);
    }
    if (DrawTunnelRectangles(_state, _hardware, DS.tunnelRectangles.offset, frame, FIRST_RECTANGLES).filled)
    {
      backward = false;
    }
    FinishSpaceViewFrame(_state, _hardware);
    backward = false;
    WaitTimerTicks(_state, _hardware, TUNNEL_FRAME_TICKS, FIRST_FRAME_WAIT);
    // INC AX / CMP AL,0Bh.
    frame = Offset(frame, 1);
    if (Low(frame) == TUNNEL_FRAMES + 1)
    {
      break;
    }
    _hardware.LoopTurn(FIRST_TUNNEL_FRAME, {frame});
  }

  // The last ten draw ten rectangles less one each frame, from one further along each time: CX and SI, pushed twice round the
  // frame, which the turns carry.
  std::uint16_t first = DS.tunnelRectangles.offset;
  for (std::uint16_t frames = TUNNEL_FRAMES;;)
  {
    if (_state.Get(DS.playerDocked) == 0)
    {
      backward = TransformAndDrawObjects(_state, _hardware, backward);
      if (UpdateStardust(_state, STARDUST_BX))
      {
        backward = false;
      }
      MoveObjectsByVelocity(_state);
      MaskOutsideTunnel(_state, first, backward);
      backward = false;
    }
    else
    {
      WaitTimerTicks(_state, _hardware, TUNNEL_FRAME_TICKS, LAST_FRAME_WAIT);
    }
    if (DrawTunnelRectangles(_state, _hardware, first, frames, LAST_RECTANGLES).filled)
    {
      backward = false;
    }
    FinishSpaceViewFrame(_state, _hardware);
    backward = false;
    first = Offset(first, TUNNEL_RECTANGLE_BYTES);
    if (--frames == 0)
    {
      return;
    }
    _hardware.LoopTurn(LAST_TUNNEL_FRAME, {frames, first});
  }
}

std::uint16_t ToggleDockingComputer(GameState& _state, Hardware& _hardware)
{
  _state.Set(DS.dockingKeyReleased, 0);
  if (_state.Get(DS.dockingComputerOn) == 1)
  {
    StopAllSound(_state, _hardware);
    _state.Set(DS.dockingComputerOn, 0);
    _state.Set(DS.viewLocked, 0);
    if (_state.Get(DS.playerSpeed) == 0)
    {
      _state.Set(DS.playerSpeed, LEAST_SPEED);
      _state.Set(DS.velocityDirty, 1);
    }
    StartLowBeep(_state);
    return PostDockingMessage(_state, DS.dockingComputerOffMessage);
  }
  if (!InSafeZone(_state).inside)
  {
    _state.Set(DS.dataA137, 0);
    StartLowBeep(_state);
    return PostDockingMessage(_state, DS.stationOutOfRangeMessage);
  }
  if ((ObjectSlot(_state, DS.stationSlot.offset).Get(SlotByte::Flags) & STATION_SHOT) != 0)
  {
    _state.Set(DS.dataA137, 0);
    StartLowBeep(_state);
    return PostDockingMessage(_state, DS.dockingDeniedMessage);
  }
  _state.Set(DS.dockingComputerSteering, 0);
  _state.Set(DS.dockingComputerState, SLOW_TO_STOP);
  _state.Set(DS.dockingComputerOn, 1);
  StartMusic(_state, _hardware);
  StartBeep(_state);
  return PostDockingMessage(_state, DS.dockingComputerOnMessage);
}

DockingStep RunDockingComputer(GameState& _state)
{
  Steer(_state, 0);
  const std::uint8_t state = _state.Get(DS.dockingComputerState);
  DockingStep step{state, 0, false};
  switch (state)
  {
  case SLOW_TO_STOP:
    // SUB WORD playerSpeed,4 while it is not 0, then on to state 1.
    if (_state.Get(DS.playerSpeed) != 0)
    {
      _state.Set(DS.playerSpeed, static_cast<std::uint16_t>(_state.Get(DS.playerSpeed) - SPEED_STEP));
      _state.Set(DS.velocityDirty, 1);
      return step;
    }
    _state.Set(DS.dockingComputerState, AIM_ROLL_AT_APPROACH);
    _state.Set(DS.dockingAlignPasses, 0);
    return step;
  case AIM_ROLL_AT_APPROACH:
    step.result = AimRoll(_state, true, ROLL_TO_APPROACH);
    return step;
  case ROLL_TO_APPROACH:
  case ROLL_TO_STATION:
  {
    // The roll angle AngleWithinTolerance compared, or the steering it sets.
    const std::uint16_t roll = _state.Get(DS.playerRollAngle);
    const bool within = RollToTarget(_state, state == ROLL_TO_APPROACH ? PITCH_TO_APPROACH : PITCH_TO_STATION).within;
    step.result = within ? roll : _state.Get(DS.dockingComputerSteering);
    return step;
  }
  case PITCH_TO_APPROACH:
    step.result = PitchToTarget(_state, true, APPROACH_PITCH_TOLERANCE, APPROACH_PITCH_STEP, AIM_ROLL_AT_APPROACH, FLY_TO_APPROACH);
    return step;
  case FLY_TO_APPROACH:
    FlyToApproachPoint(_state);
    step.result = _state.Get(DS.playerVelocityZ);
    return step;
  case AIM_ROLL_AT_STATION:
    step.result = AimRoll(_state, false, ROLL_TO_STATION);
    return step;
  case PITCH_TO_STATION:
    step.result = PitchToTarget(_state, false, STATION_PITCH_TOLERANCE, STATION_PITCH_STEP, AIM_ROLL_AT_STATION, CLOSE_IN);
    return step;
  case CLOSE_IN:
  {
    // Once it stops, the last random number ResetStardust drew with the last lifetime it stored over its low byte; otherwise
    // playerVelocityZ, as MoveObjectsByVelocity leaves it.
    const ClosingIn closing = CloseIn(_state);
    step.stopped = closing.stopped;
    step.result = closing.stopped
                    ? WithLow(closing.lastRandom, _state.Byte(Offset(DS.stardust.At(DS.stardust.ENTRY_COUNT - 1), PARTICLE_LIFETIME)))
                    : _state.Get(DS.playerVelocityZ);
    return step;
  }
  case MATCH_SPIN:
    step.result = MatchSpin(_state);
    return step;
  case ROLL_WITH_SPIN:
    step.result = RollWithSpin(_state, 0);
    return step;
  case ROLL_WITH_SPIN_TURNED:
    step.result = RollWithSpin(_state, HALF_TURN);
    return step;
  default:
    return step;
  }
}

void CancelDockingComputer(GameState& _state, Hardware& _hardware)
{
  if (_state.Get(DS.dockingComputerOn) != 1)
  {
    return;
  }
  _state.Set(DS.dockingComputerOn, 0);
  _state.Set(DS.dataA137, 0);
  _state.Set(DS.viewLocked, 0);
  // PUSH AX / POP AX round it: StopAllSound's AX does not come out.
  StopAllSound(_state, _hardware);
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

constexpr Machine::NativeContract CLOBBERS_ALL{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0};
// MaskOutsideTunnel's: the original reads the rectangle through SI and leaves it there, and PlayStationTunnel draws the
// rectangles from it.
constexpr Machine::NativeContract CLOBBERS_ALL_BUT_SI_ES{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI | REGISTER_BP,
                                                         0};
constexpr Machine::NativeContract ALIGNMENT{REGISTER_AX | REGISTER_CX | REGISTER_DX, FLAG_CARRY};
// ArcTangent2's leftovers, which no caller reads (ADR-012).
constexpr Machine::NativeContract CLOBBERS_BX_CX_DX{REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
// PlayStationTunnel's: every register but DS and BP, which its last FinishSpaceViewFrame leaves (PlayStationTunnelEntry).
constexpr Machine::NativeContract PLAYS_STATION_TUNNEL{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_ES, 0};

} // namespace

void DrawTunnelRectangleEntry(Guest& _guest)
{
  DrawLineOut(_guest, DrawTunnelRectangle(_guest.State(), _guest.Regs().si));
  _guest.Clobber(CLOBBERS_ALL);
}

void PlayStationTunnelEntry(Guest& _guest)
{
  PlayStationTunnel(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  // The last FinishSpaceViewFrame's CLD, which the first frame of flight draws by, and its PresentSpaceView's MOV BP,20h, which
  // nothing after it changes: after docking, the status screen RunTitleAndDocked shows next hands it to SelectSystemAtCursor as the
  // count when no system is on the chart, as after the credits (ShowCreditsEntry).
  _guest.SetFlag(Machine::FLAG_DIRECTION, false);
  _guest.Regs().bp = PRESENT_SPACE_VIEW_BP;
  _guest.Clobber(PLAYS_STATION_TUNNEL);
}

void CheckDockingAlignmentEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetFlag(FLAG_CARRY, CheckDockingAlignment(_guest.State(), ObjectSlot(_guest.State(), regs.di), regs.bx));
  _guest.Clobber(ALIGNMENT);
}

void ToggleDockingComputerEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The original leaves the message in AX and, once inside the safe zone with the computer off, the station's slot in BX, which
  // the contract compares.
  const bool looksAtStation = _guest.Get(DS.dockingComputerOn) != 1 && InSafeZone(_guest.State()).inside;
  regs.ax = ToggleDockingComputer(_guest.State(), _guest.Devices());
  if (looksAtStation)
  {
    regs.bx = DS.stationSlot.offset;
  }
  _guest.Clobber(PRESERVES_ALL);
}

void RunDockingComputerEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const DockingStep step = RunDockingComputer(_guest.State());
  // What each state leaves in AX, SI and DI, which the contract compares: the state's result in AX; DI the station's slot from
  // LoadStationPosition and the docking states that read it; SI past the slots as MoveObjectsByVelocity leaves it, once it ran;
  // and, once state 8 stopped, DI past the particles as ResetStardust leaves it. States 0 and 12 and up leave them alone.
  const auto pastShipSlots = [&_guest]
  { return static_cast<std::uint16_t>(DS.shipSlots.offset + LoopCount(_guest.Get(DS.shipSlotCount)) * ObjectSlot::BYTES); };
  switch (step.state)
  {
  case AIM_ROLL_AT_APPROACH:
  case PITCH_TO_APPROACH:
  case AIM_ROLL_AT_STATION:
  case PITCH_TO_STATION:
    regs.ax = step.result;
    regs.di = DS.stationSlot.offset;
    break;
  case ROLL_TO_APPROACH:
  case ROLL_TO_STATION:
    regs.ax = step.result;
    break;
  case CLOSE_IN:
    regs.ax = step.result;
    if (step.stopped)
    {
      regs.di = DS.stardust.At(DS.stardust.ENTRY_COUNT);
      break;
    }
    regs.si = pastShipSlots();
    regs.di = DS.stationSlot.offset;
    break;
  case FLY_TO_APPROACH:
  case MATCH_SPIN:
  case ROLL_WITH_SPIN:
  case ROLL_WITH_SPIN_TURNED:
    regs.ax = step.result;
    regs.si = pastShipSlots();
    regs.di = DS.stationSlot.offset;
    break;
  default:
    break;
  }
  _guest.Clobber(CLOBBERS_BX_CX_DX);
}

void CancelDockingComputerEntry(Guest& _guest)
{
  CancelDockingComputer(_guest.State(), _guest.Devices());
  _guest.Clobber(PRESERVES_ALL);
}

void MaskOutsideTunnelEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  MaskOutsideTunnel(_guest.State(), regs.si, _guest.Flag(Machine::FLAG_DIRECTION));
  // CLD, and ES back on the CGA's memory through AX.
  _guest.SetFlag(Machine::FLAG_DIRECTION, false);
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = Guest::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_ALL_BUT_SI_ES);
}

namespace
{

// PlayStationTunnel waits as a rule: for frames, and docking for the timer too.
constexpr std::array ENTRIES = {
  NativeEntry{0x1AA0, "DrawTunnelRectangle", &DrawTunnelRectangleEntry, CLOBBERS_ALL},
  NativeEntry{0x2D0F, "CheckDockingAlignment", &CheckDockingAlignmentEntry, ALIGNMENT},
  NativeEntry{0x2D5B, "PlayStationTunnel", &PlayStationTunnelEntry, PLAYS_STATION_TUNNEL, Machine::NativeReturn::Near, 0,
              Machine::NativeWait::Always},
  NativeEntry{0x2E0A, "MaskOutsideTunnel", &MaskOutsideTunnelEntry, CLOBBERS_ALL_BUT_SI_ES},
  NativeEntry{0x83B2, "ToggleDockingComputer", &ToggleDockingComputerEntry, PRESERVES_ALL},
  NativeEntry{0x8622, "RunDockingComputer", &RunDockingComputerEntry, CLOBBERS_BX_CX_DX},
  NativeEntry{0x8BAA, "CancelDockingComputer", &CancelDockingComputerEntry, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> DockingEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
