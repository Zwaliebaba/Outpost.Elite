#include "pch.h"

#include "Hyperspace.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Flight.h"
#include "Galaxy.h"
#include "Maths.h"
#include "Ships.h"
#include "Sound.h"
#include "Text.h"
#include "Video.h"

#include <algorithm>

namespace Elite
{

namespace
{

// The backward jumps the jump and the tunnel take: CompleteHyperspaceJump's tail, which GalacticJump jumps back to, and its copy's
// loop; the mis-jump, which a forced one jumps back to; and the tunnel's frames.
constexpr std::uint16_t DESTINATION_COPY = 0x4733;
constexpr std::uint16_t DESTINATION_COPY_LOOP = 0x474C;
constexpr std::uint16_t MIS_JUMP = 0x4781;
constexpr std::uint16_t TUNNEL_FRAME_LOOP = 0x4909;

// IsMassLocked: the ships that do not lock the jump drive, by type, and the slot flag of a ship on the
// scanner.
constexpr std::array<std::uint8_t, 4> UNLOCKING_TYPES = {5, 0x11, 6, 0x0B}; // Asteroid, Boulder, Barrel, Splinter
constexpr std::uint8_t SHIP_TYPE_MASK = 0x1F;
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

// NextRandom made an offset of 200h-3FFh, negated when the number's top bit is set (CS:2B69, CS:2B7C: mov dh,ah; shl dh,1
// leaves that bit in CF). Nothing reads the DH it leaves: ArriveInSystem's CWD (CS:2B8E) loads DX next.
[[nodiscard]] std::uint16_t RandomArrivalOffset(GameState& _state)
{
  const std::uint16_t random = NextRandom(_state);
  const auto offset = static_cast<std::uint16_t>((random & ARRIVAL_OFFSET_MASK) + ARRIVAL_OFFSET_LEAST);
  return (random & 0x8000) != 0 ? Negate(offset) : offset;
}

// GalacticJump (0x485B), which CompleteHyperspaceJump jumps to and which jumps back to 0x4733: the next galaxy, the ninth now
// and then after the eighth, and the system nearest a random point of its galactic chart selected (FindNearestSystem, then
// SelectSystemAtCursor). _countIfNone is BP, what FindNearestSystem makes the index from when no system is on the chart.
void GalacticJump(GameState& _state, std::uint16_t _countIfNone)
{
  if (_state.Get(DS.galaxyNumber) == NINTH_GALAXY)
  {
    _state.Set(DS.galaxyNumber, 0);
  }
  else
  {
    _state.Set(DS.galaxyNumber, static_cast<std::uint8_t>(_state.Get(DS.galaxyNumber) + 1));
    if (_state.Get(DS.galaxyNumber) == NINTH_GALAXY && NextRandom(_state) >= NINTH_GALAXY_ODDS)
    {
      _state.Set(DS.galaxyNumber, 0);
    }
  }
  _state.SetByte(DS.arrivalGalaxyDigit.offset, static_cast<std::uint8_t>(_state.Get(DS.galaxyNumber) + GALAXY_DIGIT_ONE));
  // AND AL,3Fh / ADD AL,60h, and AND AH,1Fh / ADD AH,30h: a random point about the middle of the chart.
  const std::uint16_t random = NextRandom(_state);
  _state.Set(DS.chartCursorX, static_cast<std::uint8_t>((Low(random) & 0x3F) + 0x60));
  _state.Set(DS.chartCursorY, static_cast<std::uint8_t>((High(random) & 0x1F) + 0x30));
  _state.Set(DS.chartIsShortRange, 0);
  // The galactic chart holds every system, and none is far enough from that point for its distance to carry, so the search
  // always finds one, and leaves BP its loop's count there, 100h less its index, for SelectSystemAtCursor's own search.
  const std::uint8_t index = FindNearestSystem(_state, _countIfNone);
  SelectSystemAtCursor(_state, Negate(index));
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

ScreenChange ArriveInSystem(GameState& _state, Hardware& _hardware, bool _backward)
{
  const ScreenChange change = SetUpLocalSpace(_state, _hardware, _backward);
  if (_state.Get(DS.witchspaceCountdown) != 0)
  {
    return change;
  }
  // A random word for z, in CX, then the offsets for y, in BX, and x, in AX.
  const std::uint16_t zOffset = NextRandom(_state);
  const std::uint16_t yOffset = RandomArrivalOffset(_state);
  const std::uint16_t xOffset = RandomArrivalOffset(_state);
  // XCHG CX,AX / CWD / XCHG CX,AX: DL is z's sign, which carries into its top byte; DH counts the three slots.
  const std::uint8_t zTop = (zOffset & 0x8000) != 0 ? std::uint8_t{0xFF} : std::uint8_t{0};
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint8_t slots = ARRIVAL_SLOTS; slots != 0; --slots)
  {
    AddCarried(_state, static_cast<std::uint16_t>(slot + 5), static_cast<std::uint16_t>(slot + 1), xOffset);
    AddCarried(_state, static_cast<std::uint16_t>(slot + 7), static_cast<std::uint16_t>(slot + 2), yOffset);
    const auto z = static_cast<std::uint16_t>(slot + 8);
    const std::uint32_t sum = std::uint32_t{_state.Word(z)} + zOffset;
    _state.SetWord(z, static_cast<std::uint16_t>(sum));
    const auto top = static_cast<std::uint16_t>(slot + 3);
    _state.SetByte(top, static_cast<std::uint8_t>(_state.Byte(top) + zTop + (sum >> 16)));
    slot = static_cast<std::uint16_t>(slot + SLOT_BYTES);
  }
  const Angles toStation = ComputeAnglesToObject(_state, ObjectSlot(_state, DS.stationSlot.offset));
  _state.Set(DS.playerPitchAngle, toStation.first);
  _state.Set(DS.playerYawAngle, toStation.second);
  _state.Set(DS.playerRollAngle, static_cast<std::uint16_t>(NextRandom(_state) & ANGLE_MASK));
  return change;
}

MassLock IsMassLocked(GameState& _state)
{
  MassLock lock{true, InSafeZone(_state), std::nullopt, std::nullopt, std::nullopt};
  if (lock.zone.inside)
  {
    return lock;
  }
  lock.sun = IsObjectNear(_state, ObjectSlot(_state, DS.shipSlots.offset));
  if (lock.sun->nearby)
  {
    return lock;
  }
  lock.planet = IsObjectNear(_state, ObjectSlot(_state, Offset(DS.shipSlots.offset, ObjectSlot::BYTES)));
  if (lock.planet->nearby)
  {
    return lock;
  }
  // Any ship on the scanner but the rocks, barrels and splinters: MOV CL,objectSlotCount / XOR CH,CH / SUB CL,3 / JG, else RET
  // with the SUB's borrow.
  const std::uint8_t slots = _state.Get(DS.objectSlotCount);
  ShipScan scan{DS.firstShipSlot.offset, static_cast<std::uint8_t>(slots - FIRST_SHIP_SLOT_INDEX), std::nullopt};
  if (static_cast<std::int8_t>(slots) <= FIRST_SHIP_SLOT_INDEX)
  {
    lock.locked = slots < FIRST_SHIP_SLOT_INDEX;
    lock.ships = scan;
    return lock;
  }
  do
  {
    const ObjectSlot ship(_state, scan.slot);
    scan.lastLooked = scan.slot;
    const auto type = static_cast<std::uint8_t>((ship.Get(SlotByte::Type) >> 1) & SHIP_TYPE_MASK);
    const bool unlocking = std::find(UNLOCKING_TYPES.begin(), UNLOCKING_TYPES.end(), type) != UNLOCKING_TYPES.end();
    if ((ship.Get(SlotByte::Type) & ObjectSlot::ACTIVE) != 0 && !unlocking && (ship.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) != 0)
    {
      lock.ships = scan;
      return lock;
    }
    scan.slot = Offset(scan.slot, ObjectSlot::BYTES);
  } while (--scan.slotsLeft != 0);
  lock.locked = false;
  lock.ships = scan;
  return lock;
}

void CompleteHyperspaceJump(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward)
{
  (void)EraseCompassAndBlips(_state);
  _state.Set(DS.messagePointer, DS.hyperspaceEngagedText.offset);
  _state.Set(DS.messageFrames, ENGAGED_MESSAGE_FRAMES);
  if (_state.Get(DS.galacticJumpPending) == 1)
  {
    GalacticJump(_state, _countIfNone);
    // JMP 4733h, back into the shared tail below: not a loop, so its turn carries nothing.
    _hardware.LoopTurn(DESTINATION_COPY, {});
  }
  else
  {
    // SUB [fuel],AL; then SUB [legalStatus],5, and 0 written over it on a borrow.
    _state.Set(DS.fuel, static_cast<std::uint8_t>(_state.Get(DS.fuel) - _state.Get(DS.hyperspaceFuelCost)));
    const std::uint8_t legal = _state.Get(DS.legalStatus);
    _state.Set(DS.legalStatus, static_cast<std::uint8_t>(legal - LEGAL_STATUS_PER_JUMP));
    if (legal < LEGAL_STATUS_PER_JUMP)
    {
      _state.Set(DS.legalStatus, 0);
    }
  }

  // 0x4733: the destination becomes the current system, a byte at a time, LOOP from CX = systemRecordBytes: a count of 0 copies
  // 65,536. Each turn carries the count, SI and DI.
  _state.Set(DS.selectedDistanceTenthsLy, 0);
  const bool galactic = _state.Get(DS.galacticJumpPending) == 1;
  std::uint16_t from = galactic ? DS.selectedSystemName.offset : DS.hyperspaceTargetRecord.offset;
  std::uint16_t to = DS.currentSystemName.offset;
  std::uint16_t bytes = _state.Get(DS.systemRecordBytes);
  for (;;)
  {
    _state.SetByte(to, _state.Byte(from));
    from = Offset(from, 1);
    to = Offset(to, 1);
    if (--bytes == 0)
    {
      break;
    }
    _hardware.LoopTurn(DESTINATION_COPY_LOOP, {bytes, from, to});
  }
  _state.Set(DS.marketQuantitiesSet, 0);
  LoadSystemSeeds(_state, _state.Get(galactic ? DS.selectedSystemIndex : DS.hyperspaceTargetIndex));

  // A mis-jump into witch space: 200 in 65536 outside the missions, or when forceMisjump asks.
  bool misjump = false;
  if (_state.Get(DS.galacticJumpPending) != 1)
  {
    misjump = NextRandom(_state) < MISJUMP_ODDS && _state.Get(DS.missionNumber) == 0;
  }
  if (!misjump && _state.Get(DS.forceMisjump) == 1)
  {
    // JE MisJump, back into the code above: not a loop, so its turn carries nothing.
    _hardware.LoopTurn(MIS_JUMP, {});
    misjump = true;
  }
  if (misjump)
  {
    _state.Set(DS.forceMisjump, 0);
    EnterWitchSpace(_state);
  }
  else
  {
    _state.Set(DS.witchspaceCountdown, 0);
    const std::uint8_t x = _state.Get(DS.systemX);
    _state.Set(DS.currentSystemX, x);
    _state.Set(DS.chartCursorX, x);
    _state.Set(DS.galacticCursorX, x);
    _state.Set(DS.shortRangeCursorX, CHART_CENTER_X);
    const auto y = static_cast<std::uint8_t>(_state.Get(DS.systemY) >> 1);
    _state.Set(DS.currentSystemChartY, y);
    _state.Set(DS.chartCursorY, y);
    _state.Set(DS.galacticCursorY, y);
    _state.Set(DS.shortRangeCursorY, CHART_CENTER_Y);
    if (_state.Get(DS.chartIsShortRange) == 1)
    {
      _state.Set(DS.chartCursorX, CHART_CENTER_X);
      _state.Set(DS.chartCursorY, CHART_CENTER_Y);
    }
  }

  // The tunnel's frames end with FinishSpaceViewFrame's CLD, so the arrival copies forwards.
  PlayHyperspaceTunnel(_state, _hardware, _backward);
  (void)ArriveInSystem(_state, _hardware, false);
  (void)UpdateMissionSchedule(_state);
  _state.Set(DS.supernovaHeat, 0);
  _state.Set(DS.supernovaFrames, 0);
  _state.Set(DS.jumpedSinceBriefing, 1);
  (void)PostArrival(_state);
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

bool DrawHyperspaceRings(GameState& _state)
{
  // LOOP over the ten rings, three bytes each: the delay, the radius and the colour. PUSH CX and PUSH SI keep the count and
  // the ring round DrawCircle.
  bool filled = false;
  std::uint16_t ring = DS.hyperspaceRings.offset;
  for (std::uint16_t rings = HYPERSPACE_RING_COUNT; rings != 0; --rings)
  {
    const std::uint8_t delay = _state.Byte(ring);
    if (delay != 0)
    {
      _state.SetByte(ring, static_cast<std::uint8_t>(delay - 1));
    }
    else if (const std::uint8_t radius = _state.Byte(Offset(ring, 1)); radius < HYPERSPACE_RING_LARGEST)
    {
      // Grows by an eighth, at least 1, and is drawn from 20 on in its colour about the space view's centre.
      const auto eighth = static_cast<std::uint8_t>(radius >> 3);
      const auto grown = static_cast<std::uint8_t>(radius + (eighth == 0 ? 1 : eighth));
      _state.SetByte(Offset(ring, 1), grown);
      if (grown >= HYPERSPACE_RING_SMALLEST_DRAWN)
      {
        _state.Set(DS.drawColor, _state.Byte(Offset(ring, 2)));
        if (DrawCircle(_state, grown, SPACE_VIEW_CENTER_X, SPACE_VIEW_CENTER_ROW))
        {
          filled = true;
        }
      }
    }
    ring = Offset(ring, HYPERSPACE_RING_BYTES);
  }
  return filled;
}

void PlayHyperspaceTunnel(GameState& _state, Hardware& _hardware, bool _backward)
{
  // PUSH CX / POP CX keep the frames left round the frame's work, and each turn carries them. Every frame ends with
  // FinishSpaceViewFrame's CLD, so only the first message line runs by _backward.
  bool backward = _backward;
  for (std::uint16_t frames = HYPERSPACE_TUNNEL_FRAMES;;)
  {
    (void)UpdateMessageLine(_state, backward);
    (void)DrawHyperspaceRings(_state);
    FinishSpaceViewFrame(_state, _hardware);
    backward = false;
    if (--frames == 0)
    {
      return;
    }
    _hardware.LoopTurn(TUNNEL_FRAME_LOOP, {frames});
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

bool TickHyperspaceCountdown(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward)
{
  if (_state.Get(DS.galacticDriveReadyFrames) != 0)
  {
    _state.Set(DS.galacticDriveReadyFrames, static_cast<std::uint8_t>(_state.Get(DS.galacticDriveReadyFrames) - 1));
  }
  if (_state.Get(DS.hyperspaceCountdown) == 0)
  {
    return false;
  }
  _state.Set(DS.hyperspaceCountdownFrames, static_cast<std::uint8_t>(_state.Get(DS.hyperspaceCountdownFrames) - 1));
  if (_state.Get(DS.hyperspaceCountdownFrames) != 0)
  {
    return false;
  }
  _state.Set(DS.hyperspaceCountdownFrames, COUNTDOWN_STEP_FRAMES);
  _state.Set(DS.hyperspaceCountdown, static_cast<std::uint8_t>(_state.Get(DS.hyperspaceCountdown) - 1));
  ShowHyperspaceCountdown(_state);
  if (_state.Get(DS.hyperspaceCountdown) != 0)
  {
    return false;
  }
  CompleteHyperspaceJump(_state, _hardware, _countIfNone, _backward);
  return true;
}

void ShowHyperspaceCountdown(GameState& _state)
{
  StartBeep(_state);
  const std::uint8_t countdown = _state.Get(DS.hyperspaceCountdown);
  // Two characters: "10", or a space and the digit.
  _state.Set(DS.hyperspaceCountdownDigits,
             countdown == COUNTDOWN_TEN ? MakeWord('1', '0') : MakeWord(' ', static_cast<std::uint8_t>(countdown + '0')));
  _state.Set(DS.messagePointer, DS.hyperspaceCountdownMessage.offset);
  _state.Set(DS.messageFrames, COUNTDOWN_MESSAGE_FRAMES);
  _state.Set(DS.messageShown, 0);
}

} // namespace Elite
