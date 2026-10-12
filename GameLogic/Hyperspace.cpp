#include "pch.h"

#include "Hyperspace.h"

#include "Arithmetic.h"
#include "Flight.h"
#include "Galaxy.h"
#include "HyperspaceTables.h"
#include "Maths.h"
#include "Ships.h"
#include "Sound.h"
#include "Text.h"
#include "Video.h"

#include <algorithm>
#include <utility>

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
  if (_state.Data().commander.galaxyNumber == NINTH_GALAXY)
  {
    _state.Data().commander.galaxyNumber = 0;
  }
  else
  {
    _state.Data().commander.galaxyNumber = static_cast<std::uint8_t>(_state.Data().commander.galaxyNumber + 1);
    if (_state.Data().commander.galaxyNumber == NINTH_GALAXY && NextRandom(_state) >= NINTH_GALAXY_ODDS)
    {
      _state.Data().commander.galaxyNumber = 0;
    }
  }
  _state.Data().hyperspace.arrivalGalaxyText.arrivalGalaxyDigit[0] =
    static_cast<std::uint8_t>(_state.Data().commander.galaxyNumber + GALAXY_DIGIT_ONE);
  // AND AL,3Fh / ADD AL,60h, and AND AH,1Fh / ADD AH,30h: a random point about the middle of the chart.
  const std::uint16_t random = NextRandom(_state);
  _state.Data().commander.chartCursorX = static_cast<std::uint8_t>((Low(random) & 0x3F) + 0x60);
  _state.Data().commander.chartCursorY = static_cast<std::uint8_t>((High(random) & 0x1F) + 0x30);
  _state.Data().commander.chartIsShortRange = 0;
  // The galactic chart holds every system, and none is far enough from that point for its distance to carry, so the search
  // always finds one, and leaves BP its loop's count there, 100h less its index, for SelectSystemAtCursor's own search.
  const std::uint8_t index = FindNearestSystem(_state, _countIfNone);
  SelectSystemAtCursor(_state, Negate(index));
}

// The arrival's message, and a fuel leak armed for missions 1 and 3 at their stages.
void PostArrival(GameState& _state)
{
  Message message = Message::ArrivalSystem;
  if (_state.Data().jump.galacticJumpPending != 0)
  {
    message = _state.Data().commander.galaxyNumber == NINTH_GALAXY ? Message::NinthGalaxy : Message::ArrivalGalaxy;
    _state.Data().commander.legalStatus = 0;
    _state.Data().commander.galacticHyperdriveFitted = 0;
  }
  if (_state.Data().commander.witchspaceCountdown == 1)
  {
    message = Message::WitchSpaceArrival;
  }
  _state.Data().messages.messagePointer = message;
  _state.Data().messages.messageFrames = ARRIVAL_MESSAGE_FRAMES;
  _state.Data().jump.galacticJumpPending = 0;
  if (_state.Data().commander.witchspaceCountdown == 1)
  {
    return;
  }
  const std::uint8_t mission = _state.Data().commander.missionNumber;
  const std::uint8_t stage = _state.Data().commander.missionStage;
  if ((mission == 1 && stage == 0) || (mission == 3 && stage == 1 && _state.Data().commander.invadedStationDestroyed != 1))
  {
    _state.Data().commander.fuelLeakDelayFrames = FUEL_LEAK_DELAY_FRAMES;
  }
}

// add [_low], low byte of _value; adc [_high], high byte: _value added to the top 16 bits of a coordinate, _low's high byte
// and _high.
void AddCarried(Counted<std::uint16_t>& _low, Counted<std::uint8_t>& _high, std::uint16_t _value)
{
  const auto sum = static_cast<unsigned>(High(_low) + Low(_value));
  _low = WithHigh(_low, static_cast<std::uint8_t>(sum));
  _high = static_cast<std::uint8_t>(_high + High(_value) + (sum >> 8));
}

} // namespace

void ArriveInSystem(GameState& _state, Hardware& _hardware, bool _backward)
{
  (void)SetUpLocalSpace(_state, _hardware, _backward);
  if (_state.Data().commander.witchspaceCountdown != 0)
  {
    return;
  }
  // A random word for z, in CX, then the offsets for y, in BX, and x, in AX.
  const std::uint16_t zOffset = NextRandom(_state);
  const std::uint16_t yOffset = RandomArrivalOffset(_state);
  const std::uint16_t xOffset = RandomArrivalOffset(_state);
  // XCHG CX,AX / CWD / XCHG CX,AX: DL is z's sign, which carries into its top byte; DH counts the three slots.
  const std::uint8_t zTop = (zOffset & 0x8000) != 0 ? std::uint8_t{0xFF} : std::uint8_t{0};
  SlotIndex index = SlotIndex::Sun;
  for (std::uint8_t slots = ARRIVAL_SLOTS; slots != 0; --slots)
  {
    SpaceObject& slot = _state.Slot(index);
    AddCarried(slot.x, slot.xHigh, xOffset);
    AddCarried(slot.y, slot.yHigh, yOffset);
    const std::uint32_t sum = std::uint32_t{slot.z} + zOffset;
    slot.z = static_cast<std::uint16_t>(sum);
    slot.zHigh = static_cast<std::uint8_t>(slot.zHigh + zTop + (sum >> 16));
    index = SlotAfter(index);
  }
  const Angles toStation = ComputeAnglesToObject(_state, _state.Slot(SlotIndex::Station));
  _state.Data().playerShip.playerPitchAngle = toStation.first;
  _state.Data().playerShip.playerYawAngle = toStation.second;
  _state.Data().playerShip.playerRollAngle = static_cast<std::uint16_t>(NextRandom(_state) & ANGLE_MASK);
}

MassLock IsMassLocked(GameState& _state)
{
  MassLock lock{true, InSafeZone(_state), std::nullopt, std::nullopt, std::nullopt};
  if (lock.zone.inside)
  {
    return lock;
  }
  lock.sun = IsObjectNear(_state, _state.Slot(SlotIndex::Sun));
  if (lock.sun->nearby)
  {
    return lock;
  }
  lock.planet = IsObjectNear(_state, _state.Slot(SlotIndex::Planet));
  if (lock.planet->nearby)
  {
    return lock;
  }
  // Any ship on the scanner but the rocks, barrels and splinters: MOV CL,objectSlotCount / XOR CH,CH / SUB CL,3 / JG, else RET
  // with the SUB's borrow.
  const std::uint8_t slots = _state.Data().flight.objectSlotCount;
  ShipScan scan{SlotIndex::FirstShip, static_cast<std::uint8_t>(slots - FIRST_SHIP_SLOT_INDEX)};
  if (static_cast<std::int8_t>(slots) <= FIRST_SHIP_SLOT_INDEX)
  {
    lock.locked = slots < FIRST_SHIP_SLOT_INDEX;
    lock.ships = scan;
    return lock;
  }
  do
  {
    const SpaceObject& ship = _state.Slot(scan.slot);
    const auto type = static_cast<std::uint8_t>((ship.type >> 1) & SHIP_TYPE_MASK);
    const bool unlocking = std::find(UNLOCKING_TYPES.begin(), UNLOCKING_TYPES.end(), type) != UNLOCKING_TYPES.end();
    if ((ship.type & SpaceObject::ACTIVE) != 0 && !unlocking && (ship.flags & FLAG_BLIP_DRAWN) != 0)
    {
      lock.ships = scan;
      return lock;
    }
    scan.slot = SlotAfter(scan.slot);
  } while (--scan.slotsLeft != 0);
  lock.locked = false;
  lock.ships = scan;
  return lock;
}

void CompleteHyperspaceJump(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward)
{
  (void)EraseCompassAndBlips(_state);
  _state.Data().messages.messagePointer = Message::HyperspaceEngaged;
  _state.Data().messages.messageFrames = ENGAGED_MESSAGE_FRAMES;
  if (_state.Data().jump.galacticJumpPending == 1)
  {
    GalacticJump(_state, _countIfNone);
    // JMP 4733h, back into the shared tail below: not a loop, so its turn carries nothing.
    _hardware.LoopTurn(DESTINATION_COPY, {});
  }
  else
  {
    // SUB [fuel],AL; then SUB [legalStatus],5, and 0 written over it on a borrow.
    _state.Data().commander.fuel = static_cast<std::uint8_t>(_state.Data().commander.fuel - _state.Data().flight.hyperspaceFuelCost);
    const std::uint8_t legal = _state.Data().commander.legalStatus;
    _state.Data().commander.legalStatus = static_cast<std::uint8_t>(legal - LEGAL_STATUS_PER_JUMP);
    if (legal < LEGAL_STATUS_PER_JUMP)
    {
      _state.Data().commander.legalStatus = 0;
    }
  }

  // 0x4733: the destination becomes the current system, a byte at a time, LOOP from CX = systemRecordBytes: a count of 0 copies
  // 65,536. Each turn carries the count, SI and DI.
  _state.Data().commander.selectedDistanceTenthsLy = 0;
  const bool galactic = _state.Data().jump.galacticJumpPending == 1;
  const std::uint32_t bytes = LoopCount(SYSTEM_RECORD_BYTES);
  const std::span<const Counted<std::uint8_t>> from =
    galactic ? BytesWithin(BytesOf(std::as_const(_state).Data().commander), COMMANDER_STATE_OFFSET(selectedSystemName), bytes)
             : BytesWithin(BytesOf(_state.Data().hyperspace.hyperspaceTargetRecord), 0, bytes);
  const std::span<Counted<std::uint8_t>> to =
    BytesWithin(BytesOf(_state.Data().commander), COMMANDER_STATE_OFFSET(currentSystemName), bytes);
  for (std::uint32_t copied = 0;;)
  {
    to[copied] = std::uint8_t{from[copied]};
    ++copied;
    if (copied == bytes)
    {
      break;
    }
    _hardware.LoopTurn(DESTINATION_COPY_LOOP,
                       {static_cast<std::uint16_t>(bytes - copied), _state.PlaceOf(from[copied]), _state.PlaceOf(to[copied])});
  }
  _state.Data().commander.marketQuantitiesSet = 0;
  CommanderState& commander = _state.Data().commander;
  LoadSystemSeeds(_state, galactic ? commander.selectedSystemIndex : commander.hyperspaceTargetIndex);

  // A mis-jump into witch space: 200 in 65536 outside the missions, or when forceMisjump asks.
  bool misjump = false;
  if (_state.Data().jump.galacticJumpPending != 1)
  {
    misjump = NextRandom(_state) < MISJUMP_ODDS && _state.Data().commander.missionNumber == 0;
  }
  if (!misjump && _state.Data().jump.forceMisjump == 1)
  {
    // JE MisJump, back into the code above: not a loop, so its turn carries nothing.
    _hardware.LoopTurn(MIS_JUMP, {});
    misjump = true;
  }
  if (misjump)
  {
    _state.Data().jump.forceMisjump = 0;
    EnterWitchSpace(_state);
  }
  else
  {
    _state.Data().commander.witchspaceCountdown = 0;
    const std::uint8_t x = SystemX(_state);
    _state.Data().commander.currentSystemX = x;
    _state.Data().commander.chartCursorX = x;
    _state.Data().commander.galacticCursorX = x;
    _state.Data().commander.shortRangeCursorX = CHART_CENTER_X;
    const auto y = static_cast<std::uint8_t>(SystemY(_state) >> 1);
    _state.Data().commander.currentSystemChartY = y;
    _state.Data().commander.chartCursorY = y;
    _state.Data().commander.galacticCursorY = y;
    _state.Data().commander.shortRangeCursorY = CHART_CENTER_Y;
    if (_state.Data().commander.chartIsShortRange == 1)
    {
      _state.Data().commander.chartCursorX = CHART_CENTER_X;
      _state.Data().commander.chartCursorY = CHART_CENTER_Y;
    }
  }

  // The tunnel's frames end with FinishSpaceViewFrame's CLD, so the arrival copies forwards.
  PlayHyperspaceTunnel(_state, _hardware, _backward);
  ArriveInSystem(_state, _hardware, false);
  UpdateMissionSchedule(_state);
  _state.Data().commander.supernovaHeat = 0;
  _state.Data().commander.supernovaFrames = 0;
  _state.Data().commander.jumpedSinceBriefing = 1;
  PostArrival(_state);
}

void ResetHyperspaceRings(GameState& _state)
{
  // REP MOVSW within the data segment, a word at a time, SI and DI from the tables' starts: hyperspaceRingStart's bytes into
  // hyperspaceRings, each counted when it changes, upwards, as the direction flag is clear wherever the game calls it: the only STD,
  // MaskOutsideTunnel's, has its CLD before the routine returns (change 3c).
  static_assert(sizeof(HyperspaceState::hyperspaceRings) == std::size_t{HYPERSPACE_RING_WORDS} * 2);
  for (std::size_t ring = 0; ring < HYPERSPACE_RING_START.size(); ++ring)
  {
    for (std::size_t byte = 0; byte < HYPERSPACE_RING_START[ring].size(); ++byte)
    {
      _state.Data().hyperspace.hyperspaceRings[ring][byte] = HYPERSPACE_RING_START[ring][byte];
    }
  }
}

void DrawHyperspaceRings(GameState& _state)
{
  // LOOP over the ten rings, three bytes each: the delay, the radius and the colour. PUSH CX and PUSH SI keep the count and
  // the ring round DrawCircle.
  for (Counted<std::uint8_t>(&ring)[HYPERSPACE_RING_BYTES] : _state.Data().hyperspace.hyperspaceRings)
  {
    const std::uint8_t delay = ring[0];
    if (delay != 0)
    {
      ring[0] = static_cast<std::uint8_t>(delay - 1);
    }
    else if (const std::uint8_t radius = ring[1]; radius < HYPERSPACE_RING_LARGEST)
    {
      // Grows by an eighth, at least 1, and is drawn from 20 on in its colour about the space view's centre.
      const auto eighth = static_cast<std::uint8_t>(radius >> 3);
      const auto grown = static_cast<std::uint8_t>(radius + (eighth == 0 ? 1 : eighth));
      ring[1] = grown;
      if (grown >= HYPERSPACE_RING_SMALLEST_DRAWN)
      {
        _state.Data().drawing.drawColor = std::uint8_t{ring[2]};
        DrawCircle(_state, grown, SPACE_VIEW_CENTER_X, SPACE_VIEW_CENTER_ROW);
      }
    }
  }
}

void PlayHyperspaceTunnel(GameState& _state, Hardware& _hardware, bool _backward)
{
  // PUSH CX / POP CX keep the frames left round the frame's work, and each turn carries them. Every frame ends with
  // FinishSpaceViewFrame's CLD, so only the first message line runs by _backward.
  bool backward = _backward;
  for (std::uint16_t frames = HYPERSPACE_TUNNEL_FRAMES;;)
  {
    UpdateMessageLine(_state, backward);
    DrawHyperspaceRings(_state);
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
  _state.Data().commander.witchspaceCountdown = WITCHSPACE_FRAMES;
  // Halfway along the jump: x the mean of the two, y the mean of the chart's (the destination's halved).
  const auto x = static_cast<std::uint8_t>((SystemX(_state) + _state.Data().commander.currentSystemX) >> 1);
  _state.Data().commander.currentSystemX = x;
  _state.Data().commander.chartCursorX = x;
  _state.Data().commander.galacticCursorX = x;
  _state.Data().commander.shortRangeCursorX = CHART_CENTER_X;
  // add al, [currentSystemChartY]; shr al,1: a byte sum, its carry lost.
  const auto sum = static_cast<std::uint8_t>((SystemY(_state) >> 1) + _state.Data().commander.currentSystemChartY);
  const auto y = static_cast<std::uint8_t>(sum >> 1);
  _state.Data().commander.currentSystemChartY = y;
  _state.Data().commander.chartCursorY = y;
  _state.Data().commander.galacticCursorY = y;
  _state.Data().commander.shortRangeCursorY = CHART_CENTER_Y;
}

void UpdateMissionSchedule(GameState& _state)
{
  if (_state.Data().commander.witchspaceCountdown != 0)
  {
    return;
  }
  if (_state.Data().commander.maskSystemJumps != 0)
  {
    const auto maskJumps = static_cast<std::uint8_t>(_state.Data().commander.maskSystemJumps - 1);
    _state.Data().commander.maskSystemJumps = maskJumps;
    if (maskJumps == 0)
    {
      _state.Data().commander.fledMaskShip = 1;
    }
  }
  if (_state.Data().commander.galaxyNumber == 0 && _state.Data().commander.data7629 != 1)
  {
    return;
  }
  const auto jumps = static_cast<std::uint8_t>(_state.Data().commander.missionJumpCount + 1);
  _state.Data().commander.missionJumpCount = jumps;
  if (jumps == FIRST_MISSION_JUMPS || jumps == SECOND_MISSION_JUMPS || jumps == THIRD_MISSION_JUMPS)
  {
    _state.Data().commander.missionNumber = MissionStartedAt(jumps);
  }
}

void LatchHyperspaceTarget(GameState& _state)
{
  _state.Data().commander.hyperspaceTargetIndex = std::uint8_t{_state.Data().commander.selectedSystemIndex};
  // LOOP: a count of 0 copies 65536 bytes.
  const std::uint32_t bytes = LoopCount(SYSTEM_RECORD_BYTES);
  const std::span<const Counted<std::uint8_t>> from =
    BytesWithin(BytesOf(std::as_const(_state).Data().commander), COMMANDER_STATE_OFFSET(selectedSystemName), bytes);
  const std::span<Counted<std::uint8_t>> to = BytesWithin(BytesOf(_state.Data().hyperspace.hyperspaceTargetRecord), 0, bytes);
  for (std::uint32_t copied = 0; copied < bytes; ++copied)
  {
    to[copied] = std::uint8_t{from[copied]};
  }
}

bool TickHyperspaceCountdown(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward)
{
  if (_state.Data().jump.galacticDriveReadyFrames != 0)
  {
    _state.Data().jump.galacticDriveReadyFrames = static_cast<std::uint8_t>(_state.Data().jump.galacticDriveReadyFrames - 1);
  }
  if (_state.Data().jump.hyperspaceCountdown == 0)
  {
    return false;
  }
  _state.Data().jump.hyperspaceCountdownFrames = static_cast<std::uint8_t>(_state.Data().jump.hyperspaceCountdownFrames - 1);
  if (_state.Data().jump.hyperspaceCountdownFrames != 0)
  {
    return false;
  }
  _state.Data().jump.hyperspaceCountdownFrames = COUNTDOWN_STEP_FRAMES;
  _state.Data().jump.hyperspaceCountdown = static_cast<std::uint8_t>(_state.Data().jump.hyperspaceCountdown - 1);
  ShowHyperspaceCountdown(_state);
  if (_state.Data().jump.hyperspaceCountdown != 0)
  {
    return false;
  }
  CompleteHyperspaceJump(_state, _hardware, _countIfNone, _backward);
  return true;
}

void ShowHyperspaceCountdown(GameState& _state)
{
  StartBeep(_state);
  const std::uint8_t countdown = _state.Data().jump.hyperspaceCountdown;
  // Two characters: "10", or a space and the digit.
  _state.Data().hyperspace.hyperspaceCountdownDigits =
    countdown == COUNTDOWN_TEN ? MakeWord('1', '0') : MakeWord(' ', static_cast<std::uint8_t>(countdown + '0'));
  _state.Data().messages.messagePointer = Message::HyperspaceCountdown;
  _state.Data().messages.messageFrames = COUNTDOWN_MESSAGE_FRAMES;
  _state.Data().messages.messageShown = Message::None;
}

} // namespace Elite
