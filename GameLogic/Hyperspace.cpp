#include "pch.h"

#include "Hyperspace.h"

#include "Arithmetic.h"
#include "Flight.h"
#include "FlightSnapshot.h"
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
constexpr std::size_t ARRIVAL_GALAXY_DIGIT = 24; // arrivalGalaxyText's digit, after 'Hyperspaced into galaxy '

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

// NextRandom made an offset of 200h-3FFh, negated when the number's top bit is set. The original
// leaves a shifted byte behind in testing that bit, which nothing reads.
[[nodiscard]] std::uint16_t RandomArrivalOffset(GameState& _state)
{
  const std::uint16_t random = NextRandom(_state);
  const auto offset = static_cast<std::uint16_t>((random & ARRIVAL_OFFSET_MASK) + ARRIVAL_OFFSET_LEAST);
  return (random & 0x8000) != 0 ? Negate(offset) : offset;
}

// GalacticJump, which CompleteHyperspaceJump jumps to and which jumps back to the shared tail: the next galaxy, the ninth now
// and then after the eighth, and the system nearest a random point of its galactic chart selected (FindNearestSystem, then
// SelectSystemAtCursor).
void GalacticJump(GameState& _state)
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
  _state.Data().hyperspace.arrivalGalaxyText[ARRIVAL_GALAXY_DIGIT] =
    static_cast<std::uint8_t>(_state.Data().commander.galaxyNumber + GALAXY_DIGIT_ONE);
  // The low byte's bottom six bits plus 60h, and the high byte's bottom five plus 30h: a random point about the middle of the chart.
  const std::uint16_t random = NextRandom(_state);
  _state.Data().commander.chartCursorX = static_cast<std::uint8_t>((Low(random) & 0x3F) + 0x60);
  _state.Data().commander.chartCursorY = static_cast<std::uint8_t>((High(random) & 0x1F) + 0x30);
  _state.Data().commander.chartIsShortRange = 0;
  // The galactic chart holds every system, and none is far enough from that point for its distance to carry, so the search
  // always finds one, and moves the cursor onto it for SelectSystemAtCursor's own search.
  (void)FindNearestSystem(_state);
  SelectSystemAtCursor(_state);
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

// _value added to the top 16 bits of a coordinate, _low's high byte and _high, a byte at a time: the low byte's carry goes into
// _high with the high byte.
void AddCarried(std::uint16_t& _low, std::uint8_t& _high, std::uint16_t _value)
{
  const auto sum = static_cast<unsigned>(High(_low) + Low(_value));
  _low = WithHigh(_low, static_cast<std::uint8_t>(sum));
  _high = static_cast<std::uint8_t>(_high + High(_value) + (sum >> 8));
}

} // namespace

void ArriveInSystem(GameState& _state, Host& _host)
{
  (void)SetUpLocalSpace(_state, _host);
  if (_state.Data().commander.witchspaceCountdown != 0)
  {
    return;
  }
  // A random word for z, then the offsets for y and x.
  const std::uint16_t zOffset = NextRandom(_state);
  const std::uint16_t yOffset = RandomArrivalOffset(_state);
  const std::uint16_t xOffset = RandomArrivalOffset(_state);
  // z's sign, extended to a byte, carries into its top byte; the loop counts the three slots.
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
  lock.sun = IsObjectNear(_state.Slot(SlotIndex::Sun));
  if (lock.sun->nearby)
  {
    return lock;
  }
  lock.planet = IsObjectNear(_state.Slot(SlotIndex::Planet));
  if (lock.planet->nearby)
  {
    return lock;
  }
  // Any ship on the scanner but the rocks, barrels and splinters: objectSlotCount less 3, as a signed byte; when that is not above
  // 0, no ship is looked at, and the jump drive is locked only when taking 3 borrowed.
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

[[nodiscard]] Task<void> CompleteHyperspaceJump(GameState& _state, Host& _host)
{
  (void)EraseCompassAndBlips(_state);
  _state.Data().messages.messagePointer = Message::HyperspaceEngaged;
  _state.Data().messages.messageFrames = ENGAGED_MESSAGE_FRAMES;
  if (_state.Data().jump.galacticJumpPending == 1)
  {
    GalacticJump(_state);
    // Back into the shared tail below.
  }
  else
  {
    // The fuel less the jump's cost; then legalStatus less 5, and 0 written over it on a borrow.
    _state.Data().commander.fuel = static_cast<std::uint8_t>(_state.Data().commander.fuel - _state.Data().flight.hyperspaceFuelCost);
    const std::uint8_t legal = _state.Data().commander.legalStatus;
    _state.Data().commander.legalStatus = static_cast<std::uint8_t>(legal - LEGAL_STATUS_PER_JUMP);
    if (legal < LEGAL_STATUS_PER_JUMP)
    {
      _state.Data().commander.legalStatus = 0;
    }
  }

  // the destination becomes the current system, its whole record, which the original copies a byte at a time.
  _state.Data().commander.selectedSystem.distanceTenthsLy = 0;
  const bool galactic = _state.Data().jump.galacticJumpPending == 1;
  _state.Data().commander.currentSystem = galactic ? _state.Data().commander.selectedSystem : _state.Data().hyperspace.hyperspaceTarget;
  _state.Data().commander.marketQuantitiesSet = 0;
  CommanderState& commander = _state.Data().commander;
  LoadSystemSeeds(_state, galactic ? commander.selectedSystem.index : commander.hyperspaceTargetIndex);

  // A mis-jump into witch space: 200 in 65536 outside the missions, or when forceMisjump asks.
  bool misjump = false;
  if (_state.Data().jump.galacticJumpPending != 1)
  {
    misjump = NextRandom(_state) < MISJUMP_ODDS && _state.Data().commander.missionNumber == 0;
  }
  if (!misjump && _state.Data().jump.forceMisjump == 1)
  {
    // Back into the mis-jump above.
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

  // The tunnel's frames end with FinishSpaceViewFrame setting the direction forwards, so the arrival copies forwards.
  co_await PlayHyperspaceTunnel(_state, _host);
  ArriveInSystem(_state, _host);
  UpdateMissionSchedule(_state);
  _state.Data().commander.supernovaHeat = 0;
  _state.Data().commander.supernovaFrames = 0;
  _state.Data().commander.jumpedSinceBriefing = 1;
  PostArrival(_state);
}

void ResetHyperspaceRings(GameState& _state)
{
  // A word at a time within the data segment, from the tables' starts: hyperspaceRingStart's bytes into hyperspaceRings, upwards,
  // as the direction flag is clear wherever the game calls it: the only backward copy, MaskOutsideTunnel's, sets it forwards
  // again before the routine returns (change 3c).
  static_assert(sizeof(HyperspaceState::hyperspaceRings) == std::size_t{HYPERSPACE_RING_WORDS} * 2);
  for (std::size_t ring = 0; ring < HYPERSPACE_RING_START.size(); ++ring)
  {
    for (std::size_t byte = 0; byte < HYPERSPACE_RING_START[ring].size(); ++byte)
    {
      _state.Data().hyperspace.hyperspaceRings[ring][byte] = HYPERSPACE_RING_START[ring][byte];
    }
  }
}

SnapshotTunnel DrawHyperspaceRings(GameState& _state)
{
  SnapshotTunnel drawn{.kind = TunnelKind::Hyperspace, .centerX = SPACE_VIEW_CENTER_X, .centerRow = SPACE_VIEW_CENTER_ROW};
  // The ten rings, three bytes each: the delay, the radius and the colour.
  for (std::uint8_t(&ring)[HYPERSPACE_RING_BYTES] : _state.Data().hyperspace.hyperspaceRings)
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
        if (drawn.shapeCount < drawn.shapes.size())
        {
          drawn.shapes[drawn.shapeCount++] = SnapshotTunnelShape{.halfWidth = grown, .halfHeight = grown, .color = std::uint8_t{ring[2]}};
        }
      }
    }
  }
  return drawn;
}

[[nodiscard]] Task<void> PlayHyperspaceTunnel(GameState& _state, Host& _host)
{
  // The frames left, counted down after each frame.
  for (std::uint16_t frames = HYPERSPACE_TUNNEL_FRAMES;;)
  {
    UpdateMessageLine(_state);
    const SnapshotTunnel rings = DrawHyperspaceRings(_state);
    co_await FinishSpaceViewFrame(_state, _host);
    // The frame's rings, which the renderer draws in the cockpit (ADR-069).
    _host.PublishTunnel(WithoutScene(TakeFlightSnapshot(_state)), rings);
    if (--frames == 0)
    {
      co_return;
    }
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
  // The destination's y, halved, plus currentSystemChartY in 8 bits, its carry lost, then halved.
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
  if (_state.Data().commander.galaxyNumber == 0 && _state.Data().commander.missionsInFirstGalaxy != 1)
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
  _state.Data().commander.hyperspaceTargetIndex = std::uint8_t{_state.Data().commander.selectedSystem.index};
  // The selected system's record, which the original copies a byte at a time.
  _state.Data().hyperspace.hyperspaceTarget = _state.Data().commander.selectedSystem;
}

[[nodiscard]] Task<bool> TickHyperspaceCountdown(GameState& _state, Host& _host)
{
  if (_state.Data().jump.galacticDriveReadyFrames != 0)
  {
    _state.Data().jump.galacticDriveReadyFrames = static_cast<std::uint8_t>(_state.Data().jump.galacticDriveReadyFrames - 1);
  }
  if (_state.Data().jump.hyperspaceCountdown == 0)
  {
    co_return false;
  }
  _state.Data().jump.hyperspaceCountdownFrames = static_cast<std::uint8_t>(_state.Data().jump.hyperspaceCountdownFrames - 1);
  if (_state.Data().jump.hyperspaceCountdownFrames != 0)
  {
    co_return false;
  }
  _state.Data().jump.hyperspaceCountdownFrames = COUNTDOWN_STEP_FRAMES;
  _state.Data().jump.hyperspaceCountdown = static_cast<std::uint8_t>(_state.Data().jump.hyperspaceCountdown - 1);
  ShowHyperspaceCountdown(_state);
  if (_state.Data().jump.hyperspaceCountdown != 0)
  {
    co_return false;
  }
  co_await CompleteHyperspaceJump(_state, _host);
  co_return true;
}

void ShowHyperspaceCountdown(GameState& _state)
{
  StartBeep(_state);
  const std::uint8_t countdown = _state.Data().jump.hyperspaceCountdown;
  // Two characters: "10", or a space and the digit.
  std::uint8_t(&digits)[2] = _state.Data().hyperspace.hyperspaceCountdownDigits;
  digits[0] = countdown == COUNTDOWN_TEN ? '1' : ' ';
  digits[1] = countdown == COUNTDOWN_TEN ? '0' : static_cast<std::uint8_t>(countdown + '0');
  _state.Data().messages.messagePointer = Message::HyperspaceCountdown;
  _state.Data().messages.messageFrames = COUNTDOWN_MESSAGE_FRAMES;
  _state.Data().messages.messageShown = Message::None;
}

} // namespace Elite
