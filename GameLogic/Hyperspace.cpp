#include "pch.h"

#include "Hyperspace.h"

#include "Arithmetic.h"
#include "DataOverlay.h"

#include <algorithm>

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::Registers;

// The routines these call through their entries: the original's, or a native routine hooked there.
constexpr std::uint16_t FINISH_SPACE_VIEW_FRAME = 0x0570;
constexpr std::uint16_t NEXT_RANDOM = 0x061C;
constexpr std::uint16_t FIND_NEAREST_SYSTEM = 0x1292;
constexpr std::uint16_t SELECT_SYSTEM_AT_CURSOR = 0x1199;
constexpr std::uint16_t LOAD_SYSTEM_SEEDS = 0x139C;
constexpr std::uint16_t DRAW_CIRCLE = 0x1AC1;
constexpr std::uint16_t SET_UP_LOCAL_SPACE = 0x29D0;
constexpr std::uint16_t ARRIVE_IN_SYSTEM = 0x2B5A;
constexpr std::uint16_t IN_SAFE_ZONE = 0x2E63;
constexpr std::uint16_t UPDATE_MESSAGE_LINE = 0x35A3;
constexpr std::uint16_t IS_OBJECT_NEAR = 0x3B9A;
constexpr std::uint16_t ERASE_COMPASS_AND_BLIPS = 0x4594;
constexpr std::uint16_t COMPLETE_HYPERSPACE_JUMP = 0x4707;
constexpr std::uint16_t DRAW_HYPERSPACE_RINGS = 0x48C0;
constexpr std::uint16_t PLAY_HYPERSPACE_TUNNEL = 0x4906;
constexpr std::uint16_t ENTER_WITCH_SPACE = 0x4917;
constexpr std::uint16_t UPDATE_MISSION_SCHEDULE = 0x4953;
constexpr std::uint16_t COMPUTE_ANGLES_TO_OBJECT = 0x4ECF;
constexpr std::uint16_t START_BEEP = 0x7A57;
constexpr std::uint16_t SHOW_HYPERSPACE_COUNTDOWN = 0x8C62;

// IsMassLocked: the ships that do not lock the jump drive, by type, and the slot flag of a ship on the
// scanner.
constexpr std::array<std::uint8_t, 4> UNLOCKING_TYPES = {5, 0x11, 6, 0x0B}; // Asteroid, Boulder, Barrel, Splinter
constexpr std::uint8_t SHIP_TYPE_MASK = 0x1F;
constexpr std::uint16_t SLOT_FLAGS = 0x1E;
constexpr std::uint8_t FLAG_BLIP_DRAWN = 0x02;
constexpr std::uint8_t FIRST_SHIP_SLOT_INDEX = 3;

// CompleteHyperspaceJump.
constexpr std::uint16_t ENGAGED_MESSAGE_FRAMES = 0x32;
constexpr std::uint16_t ARRIVAL_MESSAGE_FRAMES = 0x1E;
constexpr std::uint8_t LEGAL_STATUS_PER_JUMP = 5;
constexpr std::uint16_t MISJUMP_ODDS = 0xC8;       // in 65536
constexpr std::uint16_t NINTH_GALAXY_ODDS = 0x12C; // in 65536, of staying there
constexpr std::uint8_t NINTH_GALAXY = 8;
constexpr std::uint8_t GALAXY_DIGIT_ONE = 0x31;
constexpr std::uint8_t WITCHSPACE_FRAMES = 0x64;
constexpr std::uint8_t CHART_CENTER_X = 0x50;
constexpr std::uint8_t CHART_CENTER_Y = 0x40;
constexpr std::uint8_t FUEL_LEAK_DELAY_FRAMES = 0x32;
constexpr std::uint16_t HYPERSPACE_TUNNEL_FRAMES = 0x32;

// ArriveInSystem: the slots it moves (sun, planet, station), 64 bytes apart.
constexpr std::uint8_t ARRIVAL_SLOTS = 3;
constexpr std::uint16_t SLOT_BYTES = 0x40;
constexpr std::uint16_t ARRIVAL_OFFSET_MASK = 0x1FF;
constexpr std::uint16_t ARRIVAL_OFFSET_LEAST = 0x200;
constexpr std::uint16_t ANGLE_MASK = 0x7FF;

constexpr std::uint16_t HYPERSPACE_RING_WORDS = 15;
constexpr std::uint16_t HYPERSPACE_RING_COUNT = 10;
constexpr std::uint16_t HYPERSPACE_RING_BYTES = 3;
constexpr std::uint8_t HYPERSPACE_RING_LARGEST = 0x96;
constexpr std::uint8_t HYPERSPACE_RING_SMALLEST_DRAWN = 0x14;
// The space view's centre, where the rings are drawn.
constexpr std::uint16_t SPACE_VIEW_CENTER_X = 0x80;
constexpr std::uint16_t SPACE_VIEW_CENTER_ROW = 0x40;

// missionJumpCount's milestones, and the mission each starts.
constexpr std::uint8_t FIRST_MISSION_JUMPS = 0x20;
constexpr std::uint8_t SECOND_MISSION_JUMPS = 0x40;
constexpr std::uint8_t THIRD_MISSION_JUMPS = 0x80;
constexpr std::uint8_t FIRST_MISSION = 1;
constexpr std::uint8_t SECOND_MISSION = 2;
constexpr std::uint8_t THIRD_MISSION = 3;

constexpr std::uint8_t COUNTDOWN_TEN = 10;
constexpr std::uint8_t COUNTDOWN_STEP_FRAMES = 10;
constexpr std::uint16_t COUNTDOWN_MESSAGE_FRAMES = 10;

[[nodiscard]] constexpr std::uint16_t MakeWord(std::uint8_t _low, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>(_low | (_high << 8));
}

// The mission UpdateMissionSchedule picks for a jump count, as the original does: the first at 20h, the second at
// 40h, and the third at any other, which only 80h stores.
[[nodiscard]] std::uint8_t MissionStartedAt(std::uint8_t _jumps) noexcept
{
  return _jumps == FIRST_MISSION_JUMPS ? FIRST_MISSION : _jumps == SECOND_MISSION_JUMPS ? SECOND_MISSION : THIRD_MISSION;
}

// NextRandom made an offset of 200h-3FFh in AX, negated when the number's top bit is set (mov dh,ah;
// shl dh,1 leaves that bit in CF).
void RandomArrivalOffset(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(NEXT_RANDOM);
  const std::uint8_t top = High(regs.ax);
  SetHigh(regs.dx, static_cast<std::uint8_t>(top << 1));
  regs.ax = static_cast<std::uint16_t>((regs.ax & ARRIVAL_OFFSET_MASK) + ARRIVAL_OFFSET_LEAST);
  if ((top & 0x80) != 0)
  {
    regs.ax = static_cast<std::uint16_t>(0u - regs.ax);
  }
}

// GalacticJump (0x485B): the next galaxy, the ninth now and then after the eighth, and the system
// nearest a random point of its chart selected.
void GalacticJump(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.galaxyNumber) == NINTH_GALAXY)
  {
    _guest.Set(DS.galaxyNumber, 0);
  }
  else
  {
    _guest.Set(DS.galaxyNumber, static_cast<std::uint8_t>(_guest.Get(DS.galaxyNumber) + 1));
    if (_guest.Get(DS.galaxyNumber) == NINTH_GALAXY)
    {
      _guest.Call(NEXT_RANDOM);
      if (regs.ax >= NINTH_GALAXY_ODDS)
      {
        _guest.Set(DS.galaxyNumber, 0);
      }
    }
  }
  SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.galaxyNumber) + GALAXY_DIGIT_ONE));
  _guest.SetByte(DS.arrivalGalaxyDigit.offset, Low(regs.ax));
  _guest.Call(NEXT_RANDOM);
  SetLow(regs.ax, static_cast<std::uint8_t>((Low(regs.ax) & 0x3F) + 0x60));
  _guest.Set(DS.chartCursorX, Low(regs.ax));
  SetHigh(regs.ax, static_cast<std::uint8_t>((High(regs.ax) & 0x1F) + 0x30));
  _guest.Set(DS.chartCursorY, High(regs.ax));
  _guest.Set(DS.chartIsShortRange, 0);
  _guest.Call(FIND_NEAREST_SYSTEM);
  _guest.Call(SELECT_SYSTEM_AT_CURSOR);
}

// The arrival's message, and a fuel leak armed for missions 1 and 3 at their stages. Returns the message, which the
// original leaves in SI.
[[nodiscard]] std::uint16_t PostArrival(GameState& _state)
{
  std::uint16_t message = DS.arrivalSystemText.offset;
  if (_state.Get(DS.galacticJumpPending) != 0)
  {
    message = _state.Get(DS.galaxyNumber) == NINTH_GALAXY ? DS.ninthGalaxyText.offset : DS.arrivalGalaxyText.offset;
    _state.Set(DS.legalStatus, 0);
    _state.Set(DS.galacticHyperdriveFitted, 0);
  }
  if (_state.Get(DS.witchspaceCountdown) == 1)
  {
    message = DS.witchSpaceArrivalText.offset;
  }
  _state.Set(DS.messagePointer, message);
  _state.Set(DS.messageFrames, ARRIVAL_MESSAGE_FRAMES);
  _state.Set(DS.galacticJumpPending, 0);
  if (_state.Get(DS.witchspaceCountdown) == 1)
  {
    return message;
  }
  const std::uint8_t mission = _state.Get(DS.missionNumber);
  const std::uint8_t stage = _state.Get(DS.missionStage);
  if ((mission == 1 && stage == 0) || (mission == 3 && stage == 1 && _state.Get(DS.invadedStationDestroyed) != 1))
  {
    _state.Set(DS.fuelLeakDelayFrames, FUEL_LEAK_DELAY_FRAMES);
  }
  return message;
}

// add [_low], low byte of _value; adc [_high], high byte.
void AddCarried(GameState& _state, std::uint16_t _low, std::uint16_t _high, std::uint16_t _value)
{
  const auto sum = static_cast<unsigned>(_state.Byte(_low) + Low(_value));
  _state.SetByte(_low, static_cast<std::uint8_t>(sum));
  _state.SetByte(_high, static_cast<std::uint8_t>(_state.Byte(_high) + High(_value) + (sum >> 8)));
}

} // namespace

void ArriveInSystem(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(SET_UP_LOCAL_SPACE);
  if (_guest.Get(DS.witchspaceCountdown) != 0)
  {
    return;
  }
  _guest.Call(NEXT_RANDOM);
  regs.cx = regs.ax;
  RandomArrivalOffset(_guest);
  regs.bx = regs.ax;
  RandomArrivalOffset(_guest);
  // xchg cx,ax; cwd; xchg cx,ax: DL is the first number's sign, which carries into the z offset's top.
  regs.dx = (regs.cx & 0x8000) != 0 ? static_cast<std::uint16_t>(0xFFFF) : static_cast<std::uint16_t>(0);
  SetHigh(regs.dx, ARRIVAL_SLOTS);
  regs.di = DS.shipSlots.offset;
  do
  {
    AddCarried(_guest.State(), static_cast<std::uint16_t>(regs.di + 5), static_cast<std::uint16_t>(regs.di + 1), regs.ax);
    AddCarried(_guest.State(), static_cast<std::uint16_t>(regs.di + 7), static_cast<std::uint16_t>(regs.di + 2), regs.bx);
    const auto z = static_cast<std::uint16_t>(regs.di + 8);
    const std::uint32_t sum = std::uint32_t{_guest.Word(z)} + regs.cx;
    _guest.SetWord(z, static_cast<std::uint16_t>(sum));
    const auto top = static_cast<std::uint16_t>(regs.di + 3);
    _guest.SetByte(top, static_cast<std::uint8_t>(_guest.Byte(top) + Low(regs.dx) + (sum >> 16)));
    regs.di = static_cast<std::uint16_t>(regs.di + SLOT_BYTES);
    SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) - 1));
  } while (High(regs.dx) != 0);
  regs.di = DS.stationSlot.offset;
  _guest.Call(COMPUTE_ANGLES_TO_OBJECT);
  _guest.Set(DS.playerPitchAngle, regs.ax);
  _guest.Set(DS.playerYawAngle, regs.bx);
  _guest.Call(NEXT_RANDOM);
  regs.ax = static_cast<std::uint16_t>(regs.ax & ANGLE_MASK);
  _guest.Set(DS.playerRollAngle, regs.ax);
}

void IsMassLocked(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(IN_SAFE_ZONE);
  if (_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  regs.di = DS.shipSlots.offset;
  _guest.Call(IS_OBJECT_NEAR);
  if (_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  regs.di = Offset(regs.di, SLOT_BYTES);
  _guest.Call(IS_OBJECT_NEAR);
  if (_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  // Any ship on the scanner but the rocks, barrels and splinters: sub cl,3; jg, else RET with its borrow.
  regs.di = DS.firstShipSlot.offset;
  const std::uint8_t slots = _guest.Get(DS.objectSlotCount);
  regs.cx = static_cast<std::uint8_t>(slots - FIRST_SHIP_SLOT_INDEX);
  if (static_cast<std::int8_t>(slots) <= FIRST_SHIP_SLOT_INDEX)
  {
    _guest.SetFlag(FLAG_CARRY, slots < FIRST_SHIP_SLOT_INDEX);
    return;
  }
  do
  {
    const std::uint8_t slot = _guest.Byte(regs.di);
    SetLow(regs.ax, static_cast<std::uint8_t>(slot >> 1));
    if ((slot & 1) != 0)
    {
      SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & SHIP_TYPE_MASK));
      const bool unlocking = std::find(UNLOCKING_TYPES.begin(), UNLOCKING_TYPES.end(), Low(regs.ax)) != UNLOCKING_TYPES.end();
      if (!unlocking && (_guest.Byte(Offset(regs.di, SLOT_FLAGS)) & FLAG_BLIP_DRAWN) != 0)
      {
        _guest.SetFlag(FLAG_CARRY, true);
        return;
      }
    }
    regs.di = Offset(regs.di, SLOT_BYTES);
  } while (--regs.cx != 0);
  _guest.SetFlag(FLAG_CARRY, false);
}

void CompleteHyperspaceJump(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(ERASE_COMPASS_AND_BLIPS);
  regs.ax = DS.hyperspaceEngagedText.offset;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, ENGAGED_MESSAGE_FRAMES);
  if (_guest.Get(DS.galacticJumpPending) == 1)
  {
    GalacticJump(_guest);
    _guest.JumpBack(0x4733);
  }
  else
  {
    SetLow(regs.ax, _guest.Get(DS.hyperspaceFuelCost));
    _guest.Set(DS.fuel, static_cast<std::uint8_t>(_guest.Get(DS.fuel) - Low(regs.ax)));
    const std::uint8_t legal = _guest.Get(DS.legalStatus);
    _guest.Set(DS.legalStatus, legal < LEGAL_STATUS_PER_JUMP ? std::uint8_t{0} : static_cast<std::uint8_t>(legal - LEGAL_STATUS_PER_JUMP));
  }

  // 0x4733: the destination becomes the current system.
  regs.ax = 0;
  _guest.Set(DS.selectedDistanceTenthsLy, regs.ax);
  const bool galactic = _guest.Get(DS.galacticJumpPending) == 1;
  regs.si = galactic ? DS.selectedSystemName.offset : DS.hyperspaceTargetRecord.offset;
  regs.di = DS.currentSystemName.offset;
  regs.cx = _guest.Get(DS.systemRecordBytes);
  for (;;)
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.si;
    ++regs.di;
    if (--regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(0x474C);
  }
  _guest.Set(DS.marketQuantitiesSet, 0);
  SetLow(regs.cx, _guest.Get(galactic ? DS.selectedSystemIndex : DS.hyperspaceTargetIndex));
  _guest.Call(LOAD_SYSTEM_SEEDS);

  // A mis-jump into witch space: 200 in 65536 outside the missions, or when forceMisjump asks.
  bool misjump = false;
  if (_guest.Get(DS.galacticJumpPending) != 1)
  {
    _guest.Call(NEXT_RANDOM);
    misjump = regs.ax < MISJUMP_ODDS && _guest.Get(DS.missionNumber) == 0;
  }
  if (!misjump && _guest.Get(DS.forceMisjump) == 1)
  {
    _guest.JumpBack(0x4781); // MisJump
    misjump = true;
  }
  if (misjump)
  {
    _guest.Set(DS.forceMisjump, 0);
    _guest.Call(ENTER_WITCH_SPACE);
  }
  else
  {
    _guest.Set(DS.witchspaceCountdown, 0);
    SetLow(regs.ax, _guest.Get(DS.systemX));
    _guest.Set(DS.currentSystemX, Low(regs.ax));
    _guest.Set(DS.chartCursorX, Low(regs.ax));
    _guest.Set(DS.galacticCursorX, Low(regs.ax));
    _guest.Set(DS.shortRangeCursorX, CHART_CENTER_X);
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.systemY) >> 1));
    _guest.Set(DS.currentSystemChartY, Low(regs.ax));
    _guest.Set(DS.chartCursorY, Low(regs.ax));
    _guest.Set(DS.galacticCursorY, Low(regs.ax));
    _guest.Set(DS.shortRangeCursorY, CHART_CENTER_Y);
    if (_guest.Get(DS.chartIsShortRange) == 1)
    {
      _guest.Set(DS.chartCursorX, CHART_CENTER_X);
      _guest.Set(DS.chartCursorY, CHART_CENTER_Y);
    }
  }

  _guest.Call(PLAY_HYPERSPACE_TUNNEL);
  _guest.Call(ARRIVE_IN_SYSTEM);
  _guest.Call(UPDATE_MISSION_SCHEDULE);
  _guest.Set(DS.supernovaHeat, 0);
  _guest.Set(DS.supernovaFrames, 0);
  _guest.Set(DS.jumpedSinceBriefing, 1);
  regs.si = PostArrival(_guest.State());
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
}

void ResetHyperspaceRings(GameState& _state, bool _backward)
{
  // REP MOVSW within the data segment, a word at a time.
  const auto step = static_cast<std::uint16_t>(_backward ? 0xFFFE : 2);
  std::uint16_t from = DS.hyperspaceRingStart.offset;
  std::uint16_t to = DS.hyperspaceRings.offset;
  for (std::uint16_t word = 0; word < HYPERSPACE_RING_WORDS; ++word)
  {
    _state.SetWord(to, _state.Word(from));
    from = Offset(from, step);
    to = Offset(to, step);
  }
}

void DrawHyperspaceRings(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.hyperspaceRings.offset;
  for (regs.cx = HYPERSPACE_RING_COUNT; regs.cx != 0; --regs.cx)
  {
    const std::uint8_t delay = _guest.Byte(regs.si);
    const auto radius = static_cast<std::uint16_t>(regs.si + 1);
    if (delay != 0)
    {
      _guest.SetByte(regs.si, static_cast<std::uint8_t>(delay - 1));
    }
    else
    {
      SetLow(regs.bx, _guest.Byte(radius));
      if (Low(regs.bx) < HYPERSPACE_RING_LARGEST)
      {
        // Grows by an eighth, at least 1.
        SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.bx) >> 3));
        if (Low(regs.ax) == 0)
        {
          SetLow(regs.ax, 1);
        }
        SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + Low(regs.ax)));
        _guest.SetByte(radius, Low(regs.bx));
        if (Low(regs.bx) >= HYPERSPACE_RING_SMALLEST_DRAWN)
        {
          const std::uint16_t count = regs.cx;
          const std::uint16_t ring = regs.si;
          regs.cx = SPACE_VIEW_CENTER_X;
          regs.dx = SPACE_VIEW_CENTER_ROW;
          SetLow(regs.ax, _guest.Byte(static_cast<std::uint16_t>(ring + 2)));
          _guest.Set(DS.drawColor, Low(regs.ax));
          _guest.Call(DRAW_CIRCLE);
          regs.si = ring;
          regs.cx = count;
        }
      }
    }
    regs.si = static_cast<std::uint16_t>(regs.si + HYPERSPACE_RING_BYTES);
  }
}

void PlayHyperspaceTunnel(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.cx = HYPERSPACE_TUNNEL_FRAMES;
  for (;;)
  {
    const std::uint16_t frames = regs.cx;
    _guest.Call(UPDATE_MESSAGE_LINE);
    _guest.Call(DRAW_HYPERSPACE_RINGS);
    _guest.Call(FINISH_SPACE_VIEW_FRAME);
    regs.cx = frames;
    if (--regs.cx == 0)
    {
      return;
    }
    _guest.JumpBack(0x4909);
  }
}

void EnterWitchSpace(GameState& _state)
{
  _state.Set(DS.witchspaceCountdown, WITCHSPACE_FRAMES);
  // Halfway along the jump: x the mean of the two, y the mean of the chart's (the destination's halved).
  const auto x = static_cast<std::uint8_t>((_state.Get(DS.systemX) + _state.Get(DS.currentSystemX)) >> 1);
  _state.Set(DS.currentSystemX, x);
  _state.Set(DS.chartCursorX, x);
  _state.Set(DS.galacticCursorX, x);
  _state.Set(DS.shortRangeCursorX, CHART_CENTER_X);
  // add al, [currentSystemChartY]; shr al,1: a byte sum, its carry lost.
  const auto sum = static_cast<std::uint8_t>((_state.Get(DS.systemY) >> 1) + _state.Get(DS.currentSystemChartY));
  const auto y = static_cast<std::uint8_t>(sum >> 1);
  _state.Set(DS.currentSystemChartY, y);
  _state.Set(DS.chartCursorY, y);
  _state.Set(DS.galacticCursorY, y);
  _state.Set(DS.shortRangeCursorY, CHART_CENTER_Y);
}

bool UpdateMissionSchedule(GameState& _state)
{
  if (_state.Get(DS.witchspaceCountdown) != 0)
  {
    return false;
  }
  if (_state.Get(DS.maskSystemJumps) != 0)
  {
    const auto maskJumps = static_cast<std::uint8_t>(_state.Get(DS.maskSystemJumps) - 1);
    _state.Set(DS.maskSystemJumps, maskJumps);
    if (maskJumps == 0)
    {
      _state.Set(DS.fledMaskShip, 1);
    }
  }
  if (_state.Get(DS.galaxyNumber) == 0 && _state.Get(DS.data7629) != 1)
  {
    return false;
  }
  const auto jumps = static_cast<std::uint8_t>(_state.Get(DS.missionJumpCount) + 1);
  _state.Set(DS.missionJumpCount, jumps);
  if (jumps == FIRST_MISSION_JUMPS || jumps == SECOND_MISSION_JUMPS || jumps == THIRD_MISSION_JUMPS)
  {
    _state.Set(DS.missionNumber, MissionStartedAt(jumps));
  }
  return true;
}

void LatchHyperspaceTarget(GameState& _state)
{
  _state.Set(DS.hyperspaceTargetIndex, _state.Get(DS.selectedSystemIndex));
  // LOOP: a count of 0 copies 65536 bytes.
  const std::uint32_t bytes = LoopCount(_state.Get(DS.systemRecordBytes));
  for (std::uint32_t copied = 0; copied < bytes; ++copied)
  {
    const auto at = static_cast<std::uint16_t>(copied);
    _state.SetByte(Offset(DS.hyperspaceTargetRecord.offset, at), _state.Byte(Offset(DS.selectedSystemName.offset, at)));
  }
}

void TickHyperspaceCountdown(Guest& _guest)
{
  if (_guest.Get(DS.galacticDriveReadyFrames) != 0)
  {
    _guest.Set(DS.galacticDriveReadyFrames, static_cast<std::uint8_t>(_guest.Get(DS.galacticDriveReadyFrames) - 1));
  }
  if (_guest.Get(DS.hyperspaceCountdown) == 0)
  {
    return;
  }
  _guest.Set(DS.hyperspaceCountdownFrames, static_cast<std::uint8_t>(_guest.Get(DS.hyperspaceCountdownFrames) - 1));
  if (_guest.Get(DS.hyperspaceCountdownFrames) != 0)
  {
    return;
  }
  _guest.Set(DS.hyperspaceCountdownFrames, COUNTDOWN_STEP_FRAMES);
  _guest.Set(DS.hyperspaceCountdown, static_cast<std::uint8_t>(_guest.Get(DS.hyperspaceCountdown) - 1));
  _guest.Call(SHOW_HYPERSPACE_COUNTDOWN);
  if (_guest.Get(DS.hyperspaceCountdown) == 0)
  {
    _guest.Call(COMPLETE_HYPERSPACE_JUMP);
  }
}

void ShowHyperspaceCountdown(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(START_BEEP);
  const std::uint8_t countdown = _guest.Get(DS.hyperspaceCountdown);
  // Two characters: "10", or a space and the digit.
  regs.ax = countdown == COUNTDOWN_TEN ? MakeWord('1', '0') : MakeWord(' ', static_cast<std::uint8_t>(countdown + '0'));
  _guest.Set(DS.hyperspaceCountdownDigits, regs.ax);
  regs.ax = DS.hyperspaceCountdownMessage.offset;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, COUNTDOWN_MESSAGE_FRAMES);
  _guest.Set(DS.messageShown, 0);
}

// ── The entries of the de-assembled routines ──

namespace
{

using Machine::NativeReturn;
using Machine::NativeWait;
using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract CLOBBERS_AX_BX{REGISTER_AX | REGISTER_BX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_SI_DI{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_CX_SI_DI{REGISTER_CX | REGISTER_SI | REGISTER_DI, 0};

} // namespace

void ResetHyperspaceRingsEntry(Guest& _guest)
{
  ResetHyperspaceRings(_guest.State(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Regs().es = Guest::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_AX_CX_SI_DI);
}

void EnterWitchSpaceEntry(Guest& _guest)
{
  EnterWitchSpace(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX);
}

void UpdateMissionScheduleEntry(Guest& _guest)
{
  // The contract keeps AX: once the jump counts, the original leaves AL the mission it picked for the count, stored
  // or not.
  if (UpdateMissionSchedule(_guest.State()))
  {
    SetLow(_guest.Regs().ax, MissionStartedAt(_guest.Get(DS.missionJumpCount)));
  }
  _guest.Clobber(PRESERVES_ALL);
}

void LatchHyperspaceTargetEntry(Guest& _guest)
{
  LatchHyperspaceTarget(_guest.State());
  // The contract keeps AX: the original leaves AL the last byte it copied, which is the last it wrote.
  const auto last = static_cast<std::uint16_t>(DS.hyperspaceTargetRecord.offset + _guest.Get(DS.systemRecordBytes) - 1);
  SetLow(_guest.Regs().ax, _guest.Byte(last));
  _guest.Clobber(CLOBBERS_CX_SI_DI);
}

namespace
{

// CompleteHyperspaceJump and PlayHyperspaceTunnel wait as a rule, for the tunnel's frames.
// TickHyperspaceCountdown waits only on the frame the countdown reaches 0, but it is hooked as a routine
// that always waits: as one that sometimes waits, a compared run would hand that frame's whole jump to
// the original with no hook in force, and none of the work routines the jump calls would be compared.
// What it does on the other frames is a few decrements, and ShowHyperspaceCountdown, compared on its own.
constexpr std::array ENTRIES = {
  NativeEntry{0x2B5A, "ArriveInSystem", &ArriveInSystem, Machine::NativeContract{REGISTER_ALL, 0}},
  NativeEntry{0x4144, "IsMassLocked", &IsMassLocked, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x4707, "CompleteHyperspaceJump", &CompleteHyperspaceJump,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0},
              NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x48AB, "ResetHyperspaceRings", &ResetHyperspaceRingsEntry, CLOBBERS_AX_CX_SI_DI},
  NativeEntry{0x48C0, "DrawHyperspaceRings", &DrawHyperspaceRings, Machine::NativeContract{REGISTER_ALL, 0}},
  NativeEntry{0x4906, "PlayHyperspaceTunnel", &PlayHyperspaceTunnel, Machine::NativeContract{REGISTER_ALL, 0}, NativeReturn::Near, 0,
              NativeWait::Always},
  NativeEntry{0x4917, "EnterWitchSpace", &EnterWitchSpaceEntry, CLOBBERS_AX_BX},
  // UpdateMissionSchedule and LatchHyperspaceTarget clobber AL but keep AH: AX is compared whole, and their entries
  // leave AL as the original does.
  NativeEntry{0x4953, "UpdateMissionSchedule", &UpdateMissionScheduleEntry, PRESERVES_ALL},
  NativeEntry{0x49F6, "LatchHyperspaceTarget", &LatchHyperspaceTargetEntry, CLOBBERS_CX_SI_DI},
  NativeEntry{0x7F79, "TickHyperspaceCountdown", &TickHyperspaceCountdown, Machine::NativeContract{REGISTER_ALL, 0}, NativeReturn::Near, 0,
              NativeWait::Always},
  NativeEntry{0x8C62, "ShowHyperspaceCountdown", &ShowHyperspaceCountdown, Machine::NativeContract{REGISTER_AX, 0}},
};

} // namespace

std::span<const NativeEntry> HyperspaceEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
