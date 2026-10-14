#include "pch.h"

#include "Docking.h"

#include "Arithmetic.h"
#include "DockingTables.h"
#include "Flight.h"
#include "FlightSnapshot.h"
#include "Maths.h"
#include "Scene.h"
#include "Ships.h"
#include "Sound.h"
#include "Text.h"
#include "Timer.h"
#include "Video.h"

#include <span>

namespace Elite
{

namespace
{

// The station tunnel: ten frames of one to ten of tunnelRectangles' ten rectangles, ten bytes each, and
// ten of ten to one; docked, each frame waits for timer ticks first.
constexpr std::uint16_t TUNNEL_FRAMES = 10;
constexpr std::uint16_t TUNNEL_RECTANGLE_BYTES = 10;
constexpr std::uint8_t TUNNEL_COLOR = 3;
constexpr std::uint16_t DOCKED_FIRST_TICKS = 0x19;
constexpr std::uint16_t TUNNEL_FRAME_TICKS = 0x14;
constexpr std::uint16_t DOCKING_MESSAGE_FRAMES = 0x19;

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
constexpr std::uint16_t VIEW_BUFFER_BYTES = 0x2000; // the buffer, rows 0-127
constexpr std::uint16_t VIEW_LAST_WORD = VIEW_BUFFER_BYTES - 2;

constexpr std::uint16_t HALF_TURN = 0x400; // in 2048ths
constexpr std::uint16_t ANGLE_MASK = 0x7FF;
constexpr std::uint8_t SHIP_TYPE_MASK = 0x1F; // of the slot's first byte, shifted right once

// A point about the view's centre, as DrawLine takes it: x and row from the top-left.
[[nodiscard]] std::uint16_t FromCenter(std::uint16_t _point) noexcept
{
  const auto x = static_cast<std::uint8_t>(Low(_point) + VIEW_CENTER_X);
  const auto row = static_cast<std::uint8_t>(High(_point) + VIEW_CENTER_ROW);
  return static_cast<std::uint16_t>((row << 8) | x);
}

// The space view's buffer, 128 rows of 64 bytes: the drawing buffer (DrawingBuffer).
static_assert(VIEW_BUFFER_BYTES == DRAWING_BUFFER_BYTES);
[[nodiscard]] std::span<std::uint8_t> ViewBuffer(GameState& _state)
{
  return DrawingBuffer(_state);
}

// REP STOSB or REP STOSW into the space view's buffer from its byte _offset, ES = DS: _value, or its low byte, _count times, a byte or a
// word at a time (_bytes), backwards when _backward: MaskOutsideTunnel's STD half. Returns the offset after the last, which the original
// leaves in DI. Every store of every tunnel rectangle is in the buffer, either way (measured from tunnelRectangles: 382 to 7,872), so a
// store outside it throws std::out_of_range (BytesWithin) rather than reaching on into the data segment as the original would (change 3c).
std::uint16_t Store(GameState& _state, std::uint16_t _offset, std::uint16_t _count, std::uint16_t _value, std::uint16_t _bytes,
                    bool _backward)
{
  const std::span<std::uint8_t> buffer = ViewBuffer(_state);
  const auto step = static_cast<std::uint16_t>(_backward ? 0u - _bytes : _bytes);
  std::uint16_t at = _offset;
  for (std::uint16_t left = _count; left != 0; --left)
  {
    const std::span<std::uint8_t> stored = BytesWithin(buffer, at, _bytes);
    if (_bytes == 2)
    {
      SetWordIn(stored, 0, _value);
    }
    else
    {
      stored[0] = Low(_value);
    }
    at = Offset(at, step);
  }
  return at;
}

// Rectangle _rectangle of tunnelRectangles, its five points of two signed bytes, x and y. The original holds the rectangle's
// address in SI. No loop reaches the one past the last: the last frames draw from 0-9 to the tenth.
[[nodiscard]] std::span<const std::uint8_t> TunnelRectangle(std::size_t _rectangle)
{
  return std::span<const std::uint8_t>(TUNNEL_RECTANGLES).subspan(_rectangle * TUNNEL_RECTANGLE_BYTES, TUNNEL_RECTANGLE_BYTES);
}

// _count of tunnelRectangles from _first, each by DrawTunnelRectangle in the tunnel's colour, in the LOOP.
void DrawTunnelRectangles(GameState& _state, std::size_t _first, std::uint16_t _count)
{
  std::size_t next = _first;
  std::uint16_t count = _count;
  for (;;)
  {
    _state.Data().drawing.drawColor = TUNNEL_COLOR;
    DrawTunnelRectangle(_state, next);
    ++next;
    if (--count == 0)
    {
      return;
    }
  }
}

// MOV CX,_ticks, then WaitForTimerTick and the LOOP back to it: about _ticks milliseconds. The original leaves CX = 0.
[[nodiscard]] Task<void> WaitTimerTicks(GameState& _state, Host& _host, std::uint16_t _ticks)
{
  std::uint16_t count = _ticks;
  for (;;)
  {
    co_await WaitForTimerTick(_state, _host);
    if (--count == 0)
    {
      co_return;
    }
  }
}

// _message posted for 25 frames.
void PostDockingMessage(GameState& _state, Message _message)
{
  _state.Data().messages.messagePointer = _message;
  _state.Data().messages.messageFrames = DOCKING_MESSAGE_FRAMES;
}

// The station's position, z moved out to the approach point when _approach: what the original loads into AX, BX
// and CX, with DI the station's slot.
[[nodiscard]] Vector LoadStationPosition(GameState& _state, bool _approach)
{
  const SpaceObject& station = _state.Slot(SlotIndex::Station);
  const std::uint16_t z = station.z;
  return Vector{static_cast<std::int16_t>(station.x), static_cast<std::int16_t>(station.y),
                static_cast<std::int16_t>(_approach ? Offset(z, APPROACH_DISTANCE) : z)};
}

// dockingComputerSteering and rollRate both _steering.
void Steer(GameState& _state, std::uint16_t _steering)
{
  _state.Data().docking.dockingComputerSteering = _steering;
  _state.Data().playerShip.rollRate = _steering;
}

// playerSpeed up by 4, and then held at 48: ADD and a MOV over it.
void SpeedUp(GameState& _state)
{
  const auto speed = static_cast<std::uint16_t>(_state.Data().playerShip.playerSpeed + SPEED_STEP);
  _state.Data().playerShip.playerSpeed = speed;
  if (speed > MOST_SPEED)
  {
    _state.Data().playerShip.playerSpeed = MOST_SPEED;
  }
}

// playerSpeed down by 4, and 4 again where that reaches 0: SUB and a MOV over it.
void SlowDown(GameState& _state)
{
  const auto speed = static_cast<std::uint16_t>(_state.Data().playerShip.playerSpeed - SPEED_STEP);
  _state.Data().playerShip.playerSpeed = speed;
  if (speed == 0)
  {
    _state.Data().playerShip.playerSpeed = LEAST_SPEED;
  }
}

// SAR r,1 twice: a rotated coordinate quartered for ArcTangent2.
[[nodiscard]] std::int16_t Quarter(std::int16_t _value) noexcept
{
  return static_cast<std::int16_t>(Sar(static_cast<std::uint16_t>(_value), 2));
}

// States 1 and 5: dockingTargetAngle, the roll that brings the target (the approach point, or the
// station) into the pitch plane, whichever of up or down is nearer; then on to state _next.
void AimRoll(GameState& _state, bool _approach, std::uint8_t _next)
{
  LoadPlayerAngles(_state);
  const Vector target = RotatePitchYawRoll(_state, LoadStationPosition(_state, _approach));
  const std::uint16_t angle = ArcTangent2(Quarter(target.x), Quarter(target.y));
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
  roll = static_cast<std::uint16_t>(Offset(roll, _state.Data().playerShip.playerRollAngle) & ANGLE_MASK);
  _state.Data().docking.dockingTargetAngle = roll;
  _state.Data().docking.dockingComputerState = _next;
}

// An 11-bit angle sign-extended to 16 bits, as AngleWithinTolerance leaves its second.
[[nodiscard]] std::uint16_t SignExtendAngle(std::uint16_t _angle) noexcept
{
  const auto angle = static_cast<std::uint16_t>(_angle & ANGLE_MASK);
  return (angle & ANGLE_SIGN) != 0 ? static_cast<std::uint16_t>(angle | ~ANGLE_MASK) : angle;
}

// States 2 and 6: roll at 9 a frame towards dockingTargetAngle, and onto it, sign-extended, once within 19; then on to
// state _next.
void RollToTarget(GameState& _state, std::uint8_t _next)
{
  const std::uint16_t target = _state.Data().docking.dockingTargetAngle;
  if (AngleWithinTolerance(_state.Data().playerShip.playerRollAngle, target, ROLL_TOLERANCE))
  {
    _state.Data().playerShip.playerRollAngle = SignExtendAngle(target);
    _state.Data().docking.dockingComputerState = _next;
    _state.Data().docking.dockingComputerSteering = 0;
    _state.Data().playerShip.rollRate = 0;
    return;
  }
  const auto turn = static_cast<std::uint16_t>(_state.Data().docking.dockingTargetAngle - _state.Data().playerShip.playerRollAngle);
  Steer(_state, Join(0, (turn & ANGLE_SIGN) != 0 ? Negate(ROLL_STEP) : ROLL_STEP));
}

// States 3 and 7: pitch towards the target by _step a frame, or by half the angle once within
// _tolerance; the second time that happens, on to state _next at speed 4, else back to state _again.
void PitchToTarget(GameState& _state, bool _approach, std::uint16_t _tolerance, std::uint8_t _step, std::uint8_t _again, std::uint8_t _next)
{
  if (!_approach)
  {
    LoadPlayerAngles(_state);
  }
  const Vector target = RotatePitchYawRoll(_state, LoadStationPosition(_state, _approach));
  const std::uint16_t angle = ArcTangent2(Quarter(target.y), Quarter(target.z));
  if (!AngleWithinTolerance(angle, 0, _tolerance))
  {
    // MOV AL,step / TEST AX,400h / NEG AL / MOV AH,AL / XOR AL,AL: the step, against the angle's sign, in the high byte.
    Steer(_state, Join((angle & ANGLE_SIGN) != 0 ? Negate(_step) : _step, 0));
    return;
  }
  // SAR AX,1 / MOV AH,AL / XOR AL,AL: half the angle, its low byte in the high byte.
  Steer(_state, Join(Low(Sar(angle, 1)), 0));
  if (_state.Data().docking.dockingAlignPasses != 1)
  {
    _state.Data().docking.dockingAlignPasses = static_cast<std::uint8_t>(_state.Data().docking.dockingAlignPasses + 1);
    _state.Data().docking.dockingComputerState = _again;
    return;
  }
  _state.Data().docking.dockingComputerState = _next;
  _state.Data().playerShip.playerSpeed = LEAST_SPEED;
  _state.Data().playerShip.velocityDirty = 1;
}

// CWD / IDIV BX: _coordinate over _frames, the frames left; with no frame left it divides by 0 into the game's trap.
[[nodiscard]] std::uint16_t VelocityOver(std::uint16_t _coordinate, std::uint16_t _frames)
{
  const auto dividend = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(_coordinate)));
  return DivideSignedWord(dividend, _frames).quotient;
}

// State 4: fly straight to the approach point, faster while 350 or more away (SpeedUp, SlowDown), at the velocity
// that gets there in the frames left, the distance over the speed: XOR DX,DX / DIV playerSpeed, a speed of 0 dividing into the
// game's trap. On the last frame there, the ship stops on it and goes on to state 5.
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
  const std::uint16_t frames = DivideWord(distance, _state.Data().playerShip.playerSpeed).quotient;
  if (frames == 1)
  {
    _state.Data().playerShip.playerSpeed = 0;
    _state.Data().playerShip.velocityDirty = 1;
    _state.Data().playerShip.playerVelocityX = static_cast<std::uint16_t>(approach.x);
    _state.Data().playerShip.playerVelocityY = static_cast<std::uint16_t>(approach.y);
    _state.Data().playerShip.playerVelocityZ = static_cast<std::uint16_t>(approach.z);
    MoveObjectsByVelocity(_state);
    _state.Data().docking.dockingComputerState = AIM_ROLL_AT_STATION;
    _state.Data().docking.dockingAlignPasses = 0;
    return;
  }
  // MOV AX,[DI+axis] (and ADD AX,7D0h for z), then the divide, for x, y and z.
  _state.Data().playerShip.playerVelocityX = VelocityOver(static_cast<std::uint16_t>(approach.x), frames);
  _state.Data().playerShip.playerVelocityY = VelocityOver(static_cast<std::uint16_t>(approach.y), frames);
  _state.Data().playerShip.playerVelocityZ = VelocityOver(static_cast<std::uint16_t>(approach.z), frames);
  MoveObjectsByVelocity(_state);
}

// State 8: straight in along z, slowing within 1000, until within 650: then the front view, locked, and the spin is
// matched next.
void CloseIn(GameState& _state)
{
  const std::uint16_t distance = Negate(_state.Slot(SlotIndex::Station).z);
  if (distance < STOP_CLOSING_DISTANCE)
  {
    _state.Data().docking.dockingComputerState = MATCH_SPIN;
    if (_state.Data().playerShip.viewAngle != 0)
    {
      _state.Data().playerShip.viewAngle = 0;
    }
    (void)ResetStardust(_state);
    _state.Data().playerShip.viewLocked = 1;
    return;
  }
  if (distance < CLOSE_SLOWLY_DISTANCE)
  {
    SlowDown(_state);
  }
  else
  {
    SpeedUp(_state);
  }
  _state.Data().playerShip.playerVelocityX = 0;
  _state.Data().playerShip.playerVelocityY = 0;
  _state.Data().playerShip.playerVelocityZ = Negate(_state.Data().playerShip.playerSpeed);
  MoveObjectsByVelocity(_state);
  _state.Data().playerShip.velocityDirty = 1;
}

// The station's spin angle plus _turn, negated for a Dodo (IsStation's CF).
[[nodiscard]] std::uint16_t LoadStationSpin(GameState& _state, std::uint16_t _turn)
{
  const SpaceObject& station = _state.Slot(SlotIndex::Station);
  const auto spin = Offset(station.roll, _turn);
  return IsStation(station).dodo ? Negate(spin) : spin;
}

// State 9: inside, matching the station's spin: MoveObjectsByVelocity, then to state 10 once the roll is within 11
// of the spin (AngleWithinTolerance), or to 11 once it is within 11 of it turned half round.
void MatchSpin(GameState& _state)
{
  MoveObjectsByVelocity(_state);
  const std::uint16_t spin = LoadStationSpin(_state, 0);
  const std::uint16_t roll = _state.Data().playerShip.playerRollAngle;
  if (AngleWithinTolerance(spin, roll, SPIN_TOLERANCE))
  {
    _state.Data().docking.dockingComputerState = ROLL_WITH_SPIN;
  }
  else if (AngleWithinTolerance(spin, Offset(roll, HALF_TURN), SPIN_TOLERANCE))
  {
    // ADD CX,400h on the roll the first test left sign-extended in CX, which the second masks off again.
    _state.Data().docking.dockingComputerState = ROLL_WITH_SPIN_TURNED;
  }
}

// States 10 and 11: MoveObjectsByVelocity, then the roll steered by half its difference from the station's
// spin plus _turn, negated as a byte: NEG AL / XOR AH,AH.
void RollWithSpin(GameState& _state, std::uint16_t _turn)
{
  MoveObjectsByVelocity(_state);
  const auto spin = static_cast<std::uint16_t>(LoadStationSpin(_state, _turn) & ANGLE_MASK);
  const auto roll = static_cast<std::uint16_t>(_state.Data().playerShip.playerRollAngle & ANGLE_MASK);
  Steer(_state, Negate(Low(Sar(static_cast<std::uint16_t>(spin - roll), 1))));
}

} // namespace

void DrawTunnelRectangle(GameState& _state, std::size_t _rectangle)
{
  const std::span<const std::uint8_t> points = TunnelRectangle(_rectangle);
  std::size_t point = 0;
  for (std::uint16_t edge = 0; edge < TUNNEL_EDGES; ++edge)
  {
    const std::uint16_t from = FromCenter(WordIn(points, point));
    const std::uint16_t to = FromCenter(WordIn(points, point + 2));
    DrawLine(_state, Low(from), High(from), Low(to), High(to));
    point += 2;
  }
}

bool CheckDockingAlignment(const GameState& _state, const SpaceObject& _station, std::uint16_t _tolerance)
{
  // Pitch near 0 wants yaw near a half turn, and pitch near a half turn wants yaw near 0.
  const std::uint16_t pitch = _state.Data().playerShip.playerPitchAngle;
  std::uint16_t yaw = HALF_TURN;
  if (!AngleWithinTolerance(pitch, 0, _tolerance))
  {
    if (!AngleWithinTolerance(pitch, HALF_TURN, _tolerance))
    {
      return false;
    }
    yaw = 0;
  }
  if (!AngleWithinTolerance(_state.Data().playerShip.playerYawAngle, yaw, _tolerance))
  {
    return false;
  }

  // Roll near the station's spin, negated for ship type 0, or half a turn from it.
  const std::uint16_t roll = _state.Data().playerShip.playerRollAngle;
  std::uint16_t spin = _station.roll;
  if (((_station.type >> 1) & SHIP_TYPE_MASK) == 0)
  {
    spin = Negate(spin);
  }
  spin &= ANGLE_MASK;
  if (AngleWithinTolerance(roll, spin, _tolerance))
  {
    return true;
  }
  return AngleWithinTolerance(roll, static_cast<std::uint16_t>((spin + HALF_TURN) & ANGLE_MASK), _tolerance);
}

void MaskOutsideTunnel(GameState& _state, std::size_t _rectangle)
{
  // The margins: x/4 bytes either side, and the rows above and below the rectangle, in the space-view buffer,
  // through ES = DS.
  const std::span<const std::uint8_t> corner = TunnelRectangle(_rectangle);
  const auto sideBytes = static_cast<std::uint16_t>(static_cast<std::uint8_t>(corner[0] + VIEW_CENTER_X) >> 2);
  const std::uint8_t top = corner[1];
  const auto rows = static_cast<std::uint16_t>(static_cast<std::uint8_t>(Negate(top) << 1));
  const auto marginWords = static_cast<std::uint16_t>(static_cast<std::uint8_t>(top + VIEW_CENTER_ROW) << 5);
  // The rows above, then the sides of each row of the rectangle, forwards: the direction flag is clear wherever the game calls it (change
  // 3c). DEC BX and JNE count the rows, 65,536 for 0.
  std::uint16_t row = Store(_state, 0, marginWords, 0, 2, false);
  std::uint16_t left = rows;
  do
  {
    Store(_state, row, sideBytes, 0, 1, false);
    row = Offset(row, VIEW_ROW_BYTES);
  } while (--left != 0);

  // The same from the end of the buffer, backwards (STD).
  row = Offset(Store(_state, VIEW_LAST_WORD, marginWords, 0, 2, true), 1);
  left = rows;
  do
  {
    Store(_state, row, sideBytes, 0, 1, true);
    row = static_cast<std::uint16_t>(row - VIEW_ROW_BYTES);
  } while (--left != 0);
}

[[nodiscard]] Task<void> PlayStationTunnel(GameState& _state, Host& _host)
{
  // MOV AX,leavingStationMessage, or autoDockMessage when docked, for one frame.
  _state.Data().messages.messagePointer = _state.Data().flight.playerDocked != 0 ? Message::AutoDock : Message::LeavingStation;
  _state.Data().messages.messageFrames = 1;
  UpdateMessageLine(_state);
  UpdateDashboard(_state);

  // Frame n of the first ten draws the first n rectangles, n in AX, pushed round the frame and popped for its INC. Leaving, the world moves on behind the tunnel, which masks the view outside the first rectangle; docking, each frame waits
  // for the timer first.
  for (std::uint16_t frame = 1;;)
  {
    if (_state.Data().flight.playerDocked == 0)
    {
      TransformAndDrawObjects(_state, _host);
      MoveObjectsByVelocity(_state);
      UpdateStardust(_state);
      MaskOutsideTunnel(_state, 0);
    }
    else
    {
      co_await WaitTimerTicks(_state, _host, DOCKED_FIRST_TICKS);
    }
    DrawTunnelRectangles(_state, 0, frame);
    co_await FinishSpaceViewFrame(_state, _host);
    // The frame as the game drew it, for a renderer that shows it in the cockpit (D68).
    _host.PublishSpaceViewPicture(WithoutScene(TakeFlightSnapshot(_state)));
    co_await WaitTimerTicks(_state, _host, TUNNEL_FRAME_TICKS);
    // INC AX / CMP AL,0Bh.
    frame = Offset(frame, 1);
    if (Low(frame) == TUNNEL_FRAMES + 1)
    {
      break;
    }
  }

  // The last ten draw ten rectangles less one each frame, from one further along each time: CX and SI, pushed twice round the
  // frame.
  std::size_t first = 0;
  for (std::uint16_t frames = TUNNEL_FRAMES;;)
  {
    if (_state.Data().flight.playerDocked == 0)
    {
      TransformAndDrawObjects(_state, _host);
      UpdateStardust(_state);
      MoveObjectsByVelocity(_state);
      MaskOutsideTunnel(_state, first);
    }
    else
    {
      co_await WaitTimerTicks(_state, _host, TUNNEL_FRAME_TICKS);
    }
    DrawTunnelRectangles(_state, first, frames);
    co_await FinishSpaceViewFrame(_state, _host);
    _host.PublishSpaceViewPicture(WithoutScene(TakeFlightSnapshot(_state)));
    ++first;
    if (--frames == 0)
    {
      co_return;
    }
  }
}

void ToggleDockingComputer(GameState& _state, Host& _host)
{
  _state.Data().playerShip.dockingKeyReleased = 0;
  if (_state.Data().playerShip.dockingComputerOn == 1)
  {
    StopAllSound(_state, _host);
    _state.Data().playerShip.dockingComputerOn = 0;
    _state.Data().playerShip.viewLocked = 0;
    if (_state.Data().playerShip.playerSpeed == 0)
    {
      _state.Data().playerShip.playerSpeed = LEAST_SPEED;
      _state.Data().playerShip.velocityDirty = 1;
    }
    StartLowBeep(_state);
    PostDockingMessage(_state, Message::DockingComputerOff);
    return;
  }
  if (!InSafeZone(_state).inside)
  {
    StartLowBeep(_state);
    PostDockingMessage(_state, Message::StationOutOfRange);
    return;
  }
  if ((_state.Slot(SlotIndex::Station).flags & STATION_SHOT) != 0)
  {
    StartLowBeep(_state);
    PostDockingMessage(_state, Message::DockingDenied);
    return;
  }
  _state.Data().docking.dockingComputerSteering = 0;
  _state.Data().docking.dockingComputerState = SLOW_TO_STOP;
  _state.Data().playerShip.dockingComputerOn = 1;
  StartMusic(_state, _host);
  StartBeep(_state);
  PostDockingMessage(_state, Message::DockingComputerOn);
}

void RunDockingComputer(GameState& _state)
{
  Steer(_state, 0);
  const std::uint8_t state = _state.Data().docking.dockingComputerState;
  switch (state)
  {
  case SLOW_TO_STOP:
    // SUB WORD playerSpeed,4 while it is not 0, then on to state 1.
    if (_state.Data().playerShip.playerSpeed != 0)
    {
      _state.Data().playerShip.playerSpeed = static_cast<std::uint16_t>(_state.Data().playerShip.playerSpeed - SPEED_STEP);
      _state.Data().playerShip.velocityDirty = 1;
      break;
    }
    _state.Data().docking.dockingComputerState = AIM_ROLL_AT_APPROACH;
    _state.Data().docking.dockingAlignPasses = 0;
    break;
  case AIM_ROLL_AT_APPROACH:
    AimRoll(_state, true, ROLL_TO_APPROACH);
    break;
  case ROLL_TO_APPROACH:
  case ROLL_TO_STATION:
    RollToTarget(_state, state == ROLL_TO_APPROACH ? PITCH_TO_APPROACH : PITCH_TO_STATION);
    break;
  case PITCH_TO_APPROACH:
    PitchToTarget(_state, true, APPROACH_PITCH_TOLERANCE, APPROACH_PITCH_STEP, AIM_ROLL_AT_APPROACH, FLY_TO_APPROACH);
    break;
  case FLY_TO_APPROACH:
    FlyToApproachPoint(_state);
    break;
  case AIM_ROLL_AT_STATION:
    AimRoll(_state, false, ROLL_TO_STATION);
    break;
  case PITCH_TO_STATION:
    PitchToTarget(_state, false, STATION_PITCH_TOLERANCE, STATION_PITCH_STEP, AIM_ROLL_AT_STATION, CLOSE_IN);
    break;
  case CLOSE_IN:
    CloseIn(_state);
    break;
  case MATCH_SPIN:
    MatchSpin(_state);
    break;
  case ROLL_WITH_SPIN:
    RollWithSpin(_state, 0);
    break;
  case ROLL_WITH_SPIN_TURNED:
    RollWithSpin(_state, HALF_TURN);
    break;
  default:
    break;
  }
}

void CancelDockingComputer(GameState& _state, Host& _host)
{
  if (_state.Data().playerShip.dockingComputerOn != 1)
  {
    return;
  }
  _state.Data().playerShip.dockingComputerOn = 0;
  _state.Data().playerShip.viewLocked = 0;
  // PUSH AX / POP AX round it: StopAllSound's AX does not come out.
  StopAllSound(_state, _host);
}

} // namespace Elite
