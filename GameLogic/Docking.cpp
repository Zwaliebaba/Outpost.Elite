#include "pch.h"

#include "Docking.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Maths.h"

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::Registers;

// The routines these call through their entries: the original's, or a native routine hooked there.
constexpr std::uint16_t FINISH_SPACE_VIEW_FRAME = 0x0570;
constexpr std::uint16_t UPDATE_STARDUST = 0x068F;
constexpr std::uint16_t RESET_STARDUST = 0x0A43;
constexpr std::uint16_t DRAW_LINE = 0x16D1;
constexpr std::uint16_t DRAW_TUNNEL_RECTANGLE = 0x1AA0;
constexpr std::uint16_t UPDATE_DASHBOARD = 0x254F;
constexpr std::uint16_t MASK_OUTSIDE_TUNNEL = 0x2E0A;
constexpr std::uint16_t IN_SAFE_ZONE = 0x2E63;
constexpr std::uint16_t UPDATE_MESSAGE_LINE = 0x35A3;
constexpr std::uint16_t TRANSFORM_AND_DRAW_OBJECTS = 0x3D25;
constexpr std::uint16_t IS_STATION = 0x3F40;
constexpr std::uint16_t START_MUSIC = 0x7401;
constexpr std::uint16_t STOP_ALL_SOUND = 0x7423;
constexpr std::uint16_t WAIT_FOR_TIMER_TICK = 0x7772;
constexpr std::uint16_t START_BEEP = 0x7A57;
constexpr std::uint16_t START_LOW_BEEP = 0x7A5D;
constexpr std::uint16_t MOVE_OBJECTS_BY_VELOCITY = 0x85EC;
constexpr std::uint16_t LOAD_PLAYER_ANGLES = 0x8A16;

// The station tunnel: ten frames of one to ten of tunnelRectangles' ten rectangles, ten bytes each, and
// ten of ten to one; docked, each frame waits for timer ticks first.
constexpr std::uint16_t TUNNEL_FRAMES = 10;
constexpr std::uint16_t TUNNEL_RECTANGLE_BYTES = 10;
constexpr std::uint8_t TUNNEL_COLOR = 3;
constexpr std::uint16_t DOCKED_FIRST_TICKS = 0x19;
constexpr std::uint16_t TUNNEL_FRAME_TICKS = 0x14;
constexpr std::uint16_t DOCKING_MESSAGE_FRAMES = 0x19;

// The station's slot.
constexpr std::uint16_t SLOT_X = 4;
constexpr std::uint16_t SLOT_Y = 6;
constexpr std::uint16_t SLOT_Z = 8;
constexpr std::uint16_t SLOT_FLAGS = 0x1E;
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
constexpr std::uint16_t STATION_SPIN_ANGLE = 0x0E; // in the station's slot
constexpr std::uint8_t SHIP_TYPE_MASK = 0x1F;      // of the slot's first byte, shifted right once

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

// CX of tunnelRectangles from SI, each by DrawTunnelRectangle in the tunnel's colour, the LOOP back to _loop. Out:
// SI past them.
void DrawTunnelRectangles(Guest& _guest, std::uint16_t _loop)
{
  Registers& regs = _guest.Regs();
  for (;;)
  {
    const std::uint16_t count = regs.cx;
    const std::uint16_t rectangle = regs.si;
    _guest.Set(DS.drawColor, TUNNEL_COLOR);
    _guest.Call(DRAW_TUNNEL_RECTANGLE);
    regs.si = Offset(rectangle, TUNNEL_RECTANGLE_BYTES);
    regs.cx = count;
    if (--regs.cx == 0)
    {
      return;
    }
    _guest.JumpBack(_loop);
  }
}

// mov cx, _ticks, then WaitForTimerTick at _loop and LOOP: about _ticks milliseconds.
void WaitTimerTicks(Guest& _guest, std::uint16_t _ticks, std::uint16_t _loop)
{
  Registers& regs = _guest.Regs();
  regs.cx = _ticks;
  for (;;)
  {
    _guest.Call(WAIT_FOR_TIMER_TICK);
    if (--regs.cx == 0)
    {
      return;
    }
    _guest.JumpBack(_loop);
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

// DI the station's slot, and AX, BX and CX its position, as LoadStationPosition gives it.
void LoadStationPositionRegisters(Registers& _regs, Vector _position) noexcept
{
  _regs.di = DS.stationSlot.offset;
  _regs.ax = static_cast<std::uint16_t>(_position.x);
  _regs.bx = static_cast<std::uint16_t>(_position.y);
  _regs.cx = static_cast<std::uint16_t>(_position.z);
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

// States 1 and 5: dockingTargetAngle, the roll that brings the target (the approach point, or the
// station) into the pitch plane, whichever of up or down is nearer.
void AimRoll(Guest& _guest, bool _approach, std::uint8_t _next)
{
  Registers& regs = _guest.Regs();
  _guest.Call(LOAD_PLAYER_ANGLES);
  LoadStationPositionRegisters(regs, LoadStationPosition(_guest.State(), _approach));
  RotatePitchYawRollEntry(_guest);
  regs.ax = Sar(regs.ax, 2);
  regs.bx = Sar(regs.bx, 2);
  ArcTangent2Entry(_guest);
  std::uint16_t magnitude = regs.ax;
  if ((magnitude & ANGLE_SIGN) != 0)
  {
    magnitude = Negate(static_cast<std::uint16_t>(magnitude | ~ANGLE_MASK));
  }
  if (magnitude >= QUARTER_TURN)
  {
    regs.ax = Offset(regs.ax, HALF_TURN);
  }
  regs.ax = static_cast<std::uint16_t>(Offset(regs.ax, _guest.Get(DS.playerRollAngle)) & ANGLE_MASK);
  _guest.Set(DS.dockingTargetAngle, regs.ax);
  _guest.Set(DS.dockingComputerState, _next);
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

// States 3 and 7: pitch towards the target by _step a frame, or by half the angle once within
// _tolerance; the second time that happens, on to state _next at speed 4, else back to state _again.
void PitchToTarget(Guest& _guest, bool _approach, std::uint16_t _tolerance, std::uint8_t _step, std::uint8_t _again, std::uint8_t _next)
{
  Registers& regs = _guest.Regs();
  if (!_approach)
  {
    _guest.Call(LOAD_PLAYER_ANGLES);
  }
  LoadStationPositionRegisters(regs, LoadStationPosition(_guest.State(), _approach));
  RotatePitchYawRollEntry(_guest);
  regs.ax = Sar(regs.bx, 2);
  regs.bx = Sar(regs.cx, 2);
  ArcTangent2Entry(_guest);
  regs.bx = _tolerance;
  regs.cx = 0;
  AngleWithinToleranceEntry(_guest);
  if (!_guest.Flag(FLAG_CARRY))
  {
    SetLow(regs.ax, (regs.ax & ANGLE_SIGN) != 0 ? Negate(_step) : _step);
    regs.ax = Join(Low(regs.ax), 0);
    Steer(_guest.State(), regs.ax);
    return;
  }
  regs.ax = Join(Low(Sar(regs.ax, 1)), 0);
  Steer(_guest.State(), regs.ax);
  if (_guest.Get(DS.dockingAlignPasses) != 1)
  {
    _guest.Set(DS.dockingAlignPasses, static_cast<std::uint8_t>(_guest.Get(DS.dockingAlignPasses) + 1));
    _guest.Set(DS.dockingComputerState, _again);
    return;
  }
  _guest.Set(DS.dockingComputerState, _next);
  _guest.Set(DS.playerSpeed, LEAST_SPEED);
  _guest.Set(DS.velocityDirty, 1);
}

// cwd; idiv bx: a coordinate over the frames left.
[[nodiscard]] std::uint16_t VelocityOver(Guest& _guest, std::uint16_t _coordinate)
{
  Registers& regs = _guest.Regs();
  regs.ax = _coordinate;
  regs.dx = SignWord(regs.ax);
  DivideSignedWord(_guest, regs.bx);
  return regs.ax;
}

// State 4: fly straight to the approach point, faster while 350 or more away, at the velocity that gets
// there in the frames left; on the last frame there, on to state 5.
void FlyToApproachPoint(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.dockingComputerSteering, 0);
  _guest.Set(DS.rollRate, 0);
  LoadStationPositionRegisters(regs, LoadStationPosition(_guest.State(), true));
  VectorLengthEntry(_guest);
  if (regs.ax >= SLOW_DOWN_DISTANCE)
  {
    SpeedUp(_guest.State());
  }
  else
  {
    SlowDown(_guest.State());
  }
  regs.dx = 0;
  DivideWord(_guest, _guest.Get(DS.playerSpeed));
  if (regs.ax == 1)
  {
    _guest.Set(DS.playerSpeed, 0);
    _guest.Set(DS.velocityDirty, 1);
    regs.ax = _guest.Word(Offset(regs.di, SLOT_X));
    _guest.Set(DS.playerVelocityX, regs.ax);
    regs.ax = _guest.Word(Offset(regs.di, SLOT_Y));
    _guest.Set(DS.playerVelocityY, regs.ax);
    regs.ax = Offset(_guest.Word(Offset(regs.di, SLOT_Z)), APPROACH_DISTANCE);
    _guest.Set(DS.playerVelocityZ, regs.ax);
    _guest.Call(MOVE_OBJECTS_BY_VELOCITY);
    _guest.Set(DS.dockingComputerState, AIM_ROLL_AT_STATION);
    _guest.Set(DS.dockingAlignPasses, 0);
    return;
  }
  regs.bx = regs.ax;
  _guest.Set(DS.playerVelocityX, VelocityOver(_guest, _guest.Word(Offset(regs.di, SLOT_X))));
  _guest.Set(DS.playerVelocityY, VelocityOver(_guest, _guest.Word(Offset(regs.di, SLOT_Y))));
  _guest.Set(DS.playerVelocityZ, VelocityOver(_guest, Offset(_guest.Word(Offset(regs.di, SLOT_Z)), APPROACH_DISTANCE)));
  _guest.Call(MOVE_OBJECTS_BY_VELOCITY);
}

// State 8: straight in along z, slowing within 1000, until within 650: then the front view, locked.
void CloseIn(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.di = DS.stationSlot.offset;
  regs.ax = Negate(_guest.Word(Offset(regs.di, SLOT_Z)));
  if (regs.ax < STOP_CLOSING_DISTANCE)
  {
    _guest.Set(DS.dockingComputerState, MATCH_SPIN);
    if (_guest.Get(DS.viewAngle) != 0)
    {
      _guest.Set(DS.viewAngle, 0);
    }
    _guest.Call(RESET_STARDUST);
    _guest.Set(DS.viewLocked, 1);
    return;
  }
  if (regs.ax < CLOSE_SLOWLY_DISTANCE)
  {
    SlowDown(_guest.State());
  }
  else
  {
    SpeedUp(_guest.State());
  }
  _guest.Set(DS.playerVelocityX, 0);
  _guest.Set(DS.playerVelocityY, 0);
  regs.ax = Negate(_guest.Get(DS.playerSpeed));
  _guest.Set(DS.playerVelocityZ, regs.ax);
  _guest.Call(MOVE_OBJECTS_BY_VELOCITY);
  _guest.Set(DS.velocityDirty, 1);
}

// The station's spin angle plus _turn, negated for a Dodo (IsStation's CF).
void LoadStationSpin(Guest& _guest, std::uint16_t _turn)
{
  Registers& regs = _guest.Regs();
  regs.di = DS.stationSlot.offset;
  const auto spin = Offset(_guest.Word(Offset(regs.di, STATION_SPIN_ANGLE)), _turn);
  _guest.Call(IS_STATION);
  regs.ax = _guest.Flag(FLAG_CARRY) ? Negate(spin) : spin;
}

// States 9-11: inside, matching the station's spin: to state 10 or 11 once the roll is within 11 of it
// or of it turned half round, rolling by half the difference in those states.
void MatchSpin(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(MOVE_OBJECTS_BY_VELOCITY);
  LoadStationSpin(_guest, 0);
  regs.bx = SPIN_TOLERANCE;
  regs.cx = _guest.Get(DS.playerRollAngle);
  AngleWithinToleranceEntry(_guest);
  if (_guest.Flag(FLAG_CARRY))
  {
    _guest.Set(DS.dockingComputerState, ROLL_WITH_SPIN);
    return;
  }
  regs.cx = Offset(regs.cx, HALF_TURN);
  AngleWithinToleranceEntry(_guest);
  if (_guest.Flag(FLAG_CARRY))
  {
    _guest.Set(DS.dockingComputerState, ROLL_WITH_SPIN_TURNED);
  }
}

void RollWithSpin(Guest& _guest, std::uint16_t _turn)
{
  Registers& regs = _guest.Regs();
  _guest.Call(MOVE_OBJECTS_BY_VELOCITY);
  LoadStationSpin(_guest, _turn);
  regs.ax &= ANGLE_MASK;
  regs.bx = static_cast<std::uint16_t>(_guest.Get(DS.playerRollAngle) & ANGLE_MASK);
  regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bx);
  regs.ax = Negate(Low(Sar(regs.ax, 1)));
  Steer(_guest.State(), regs.ax);
}

} // namespace

void DrawTunnelRectangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = TUNNEL_EDGES;
  do
  {
    const std::uint16_t edgesLeft = regs.cx;
    const std::uint16_t point = regs.si;
    regs.dx = FromCenter(_guest.Word(point));
    regs.cx = FromCenter(_guest.Word(static_cast<std::uint16_t>(point + 2)));
    _guest.Call(DRAW_LINE);
    regs.si = static_cast<std::uint16_t>(point + 2);
    regs.cx = static_cast<std::uint16_t>(edgesLeft - 1);
  } while (regs.cx != 0);
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

void PlayStationTunnel(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = _guest.Get(DS.playerDocked) != 0 ? DS.autoDockMessage.offset : DS.leavingStationMessage.offset;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, 1);
  _guest.Call(UPDATE_MESSAGE_LINE);
  _guest.Call(UPDATE_DASHBOARD);

  // Frame AX of the first ten draws the first AX rectangles. Leaving, the world moves on behind the
  // tunnel; docking, each frame waits for the timer.
  regs.ax = 1;
  for (;;)
  {
    const std::uint16_t frame = regs.ax;
    regs.si = DS.tunnelRectangles.offset;
    if (_guest.Get(DS.playerDocked) == 0)
    {
      _guest.Call(TRANSFORM_AND_DRAW_OBJECTS);
      _guest.Call(MOVE_OBJECTS_BY_VELOCITY);
      _guest.Call(UPDATE_STARDUST);
      regs.si = DS.tunnelRectangles.offset;
      _guest.Call(MASK_OUTSIDE_TUNNEL);
      regs.cx = frame;
    }
    else
    {
      WaitTimerTicks(_guest, DOCKED_FIRST_TICKS, 0x2D9A);
      regs.cx = regs.ax;
    }
    DrawTunnelRectangles(_guest, 0x2DA1);
    _guest.Call(FINISH_SPACE_VIEW_FRAME);
    WaitTimerTicks(_guest, TUNNEL_FRAME_TICKS, 0x2DB8);
    regs.ax = Offset(frame, 1);
    if (Low(regs.ax) == TUNNEL_FRAMES + 1)
    {
      break;
    }
    _guest.JumpBack(0x2D7A);
  }

  // The last ten draw ten rectangles less one each frame, from one further along each time.
  regs.si = DS.tunnelRectangles.offset;
  regs.cx = TUNNEL_FRAMES;
  for (;;)
  {
    const std::uint16_t frames = regs.cx;
    const std::uint16_t first = regs.si;
    if (_guest.Get(DS.playerDocked) == 0)
    {
      _guest.Call(TRANSFORM_AND_DRAW_OBJECTS);
      _guest.Call(UPDATE_STARDUST);
      _guest.Call(MOVE_OBJECTS_BY_VELOCITY);
      regs.si = first;
      _guest.Call(MASK_OUTSIDE_TUNNEL);
      regs.cx = frames;
    }
    else
    {
      WaitTimerTicks(_guest, TUNNEL_FRAME_TICKS, 0x2DE7);
      regs.si = first;
      regs.cx = frames;
    }
    DrawTunnelRectangles(_guest, 0x2DEE);
    _guest.Call(FINISH_SPACE_VIEW_FRAME);
    regs.si = Offset(first, TUNNEL_RECTANGLE_BYTES);
    regs.cx = frames;
    if (--regs.cx == 0)
    {
      return;
    }
    _guest.JumpBack(0x2DC9);
  }
}

void ToggleDockingComputer(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.dockingKeyReleased, 0);
  if (_guest.Get(DS.dockingComputerOn) == 1)
  {
    _guest.Call(STOP_ALL_SOUND);
    _guest.Set(DS.dockingComputerOn, 0);
    _guest.Set(DS.viewLocked, 0);
    if (_guest.Get(DS.playerSpeed) == 0)
    {
      _guest.Set(DS.playerSpeed, LEAST_SPEED);
      _guest.Set(DS.velocityDirty, 1);
    }
    _guest.Call(START_LOW_BEEP);
    regs.ax = PostDockingMessage(_guest.State(), DS.dockingComputerOffMessage);
    return;
  }
  _guest.Call(IN_SAFE_ZONE);
  if (!_guest.Flag(FLAG_CARRY))
  {
    _guest.Set(DS.dataA137, 0);
    _guest.Call(START_LOW_BEEP);
    regs.ax = PostDockingMessage(_guest.State(), DS.stationOutOfRangeMessage);
    return;
  }
  regs.bx = DS.stationSlot.offset;
  if ((_guest.Byte(Offset(regs.bx, SLOT_FLAGS)) & STATION_SHOT) != 0)
  {
    _guest.Set(DS.dataA137, 0);
    _guest.Call(START_LOW_BEEP);
    regs.ax = PostDockingMessage(_guest.State(), DS.dockingDeniedMessage);
    return;
  }
  _guest.Set(DS.dockingComputerSteering, 0);
  _guest.Set(DS.dockingComputerState, SLOW_TO_STOP);
  _guest.Set(DS.dockingComputerOn, 1);
  _guest.Call(START_MUSIC);
  _guest.Call(START_BEEP);
  regs.ax = PostDockingMessage(_guest.State(), DS.dockingComputerOnMessage);
}

void RunDockingComputer(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  // States 2 and 6, with what the original leaves in the registers: AX the roll angle it compared or the step it
  // steers by, BX the tolerance, CX the target as AngleWithinTolerance sign-extends it, and DX the excess.
  const auto rollToTarget = [&](std::uint8_t _next)
  {
    const std::uint16_t roll = _guest.Get(DS.playerRollAngle);
    const std::uint16_t target = _guest.Get(DS.dockingTargetAngle);
    const AngleTolerance found = RollToTarget(_guest.State(), _next);
    regs.ax = found.within ? roll : _guest.Get(DS.dockingComputerSteering);
    regs.bx = ROLL_TOLERANCE;
    regs.cx = SignExtendAngle(target);
    regs.dx = found.excess;
  };
  _guest.Set(DS.dockingComputerSteering, 0);
  _guest.Set(DS.rollRate, 0);
  switch (_guest.Get(DS.dockingComputerState))
  {
  case SLOW_TO_STOP:
    if (_guest.Get(DS.playerSpeed) != 0)
    {
      _guest.Set(DS.playerSpeed, static_cast<std::uint16_t>(_guest.Get(DS.playerSpeed) - SPEED_STEP));
      _guest.Set(DS.velocityDirty, 1);
      return;
    }
    _guest.Set(DS.dockingComputerState, AIM_ROLL_AT_APPROACH);
    _guest.Set(DS.dockingAlignPasses, 0);
    return;
  case AIM_ROLL_AT_APPROACH:
    AimRoll(_guest, true, ROLL_TO_APPROACH);
    return;
  case ROLL_TO_APPROACH:
    rollToTarget(PITCH_TO_APPROACH);
    return;
  case PITCH_TO_APPROACH:
    PitchToTarget(_guest, true, APPROACH_PITCH_TOLERANCE, APPROACH_PITCH_STEP, AIM_ROLL_AT_APPROACH, FLY_TO_APPROACH);
    return;
  case FLY_TO_APPROACH:
    FlyToApproachPoint(_guest);
    return;
  case AIM_ROLL_AT_STATION:
    AimRoll(_guest, false, ROLL_TO_STATION);
    return;
  case ROLL_TO_STATION:
    rollToTarget(PITCH_TO_STATION);
    return;
  case PITCH_TO_STATION:
    PitchToTarget(_guest, false, STATION_PITCH_TOLERANCE, STATION_PITCH_STEP, AIM_ROLL_AT_STATION, CLOSE_IN);
    return;
  case CLOSE_IN:
    CloseIn(_guest);
    return;
  case MATCH_SPIN:
    MatchSpin(_guest);
    return;
  case ROLL_WITH_SPIN:
    RollWithSpin(_guest, 0);
    return;
  case ROLL_WITH_SPIN_TURNED:
    RollWithSpin(_guest, HALF_TURN);
    return;
  default:
    return;
  }
}

void CancelDockingComputer(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.dockingComputerOn) != 1)
  {
    return;
  }
  _guest.Set(DS.dockingComputerOn, 0);
  _guest.Set(DS.dataA137, 0);
  _guest.Set(DS.viewLocked, 0);
  const std::uint16_t saved = regs.ax;
  _guest.Call(STOP_ALL_SOUND);
  regs.ax = saved;
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

} // namespace

void CheckDockingAlignmentEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetFlag(FLAG_CARRY, CheckDockingAlignment(_guest.State(), ObjectSlot(_guest.State(), regs.di), regs.bx));
  _guest.Clobber(ALIGNMENT);
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
  NativeEntry{0x1AA0, "DrawTunnelRectangle", &DrawTunnelRectangle, CLOBBERS_ALL},
  NativeEntry{0x2D0F, "CheckDockingAlignment", &CheckDockingAlignmentEntry, ALIGNMENT},
  NativeEntry{0x2D5B, "PlayStationTunnel", &PlayStationTunnel, CLOBBERS_ALL, Machine::NativeReturn::Near, 0, Machine::NativeWait::Always},
  NativeEntry{0x2E0A, "MaskOutsideTunnel", &MaskOutsideTunnelEntry, CLOBBERS_ALL_BUT_SI_ES},
  NativeEntry{0x83B2, "ToggleDockingComputer", &ToggleDockingComputer, PRESERVES_ALL},
  NativeEntry{0x8622, "RunDockingComputer", &RunDockingComputer, CLOBBERS_BX_CX_DX},
  NativeEntry{0x8BAA, "CancelDockingComputer", &CancelDockingComputer, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> DockingEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
