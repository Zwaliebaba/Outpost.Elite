#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Guest.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t CHECK_DOCKING_ALIGNMENT = 0x2D0F;
constexpr std::uint16_t TOGGLE_DOCKING_COMPUTER = 0x83B2;
constexpr std::uint16_t RUN_DOCKING_COMPUTER = 0x8622;
constexpr std::uint16_t CANCEL_DOCKING_COMPUTER = 0x8BAA;
constexpr std::uint16_t STATION = 0x20F5;          // commanderFileList, free for a made-up station's slot
constexpr std::uint16_t STATION_SPIN_ANGLE = 0x0E; // in the slot
constexpr std::uint16_t STATION_SPIN = 0x100;
constexpr std::uint16_t TOLERANCE = 0x20;
constexpr std::uint16_t SLOT_FLAGS = 0x1E;

// Every register given a value of its own, so that each one the routine leaves is seen.
constexpr Inputs ALL_REGISTERS = {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777};

// The player's angles, and the station's ship type.
struct Approach
{
  std::uint16_t pitch;
  std::uint16_t yaw;
  std::uint16_t roll;
  std::uint8_t type;
};

// Where the station is from the player, its ship type (0 a Dodo, 1 a Coriolis) and its spin angle.
struct Station
{
  std::int16_t x;
  std::int16_t y;
  std::int16_t z;
  std::uint8_t type = 1;
  std::uint16_t spin = 0;
};

[[nodiscard]] Elite::Guest GuestOf(ComparisonRig& _rig)
{
  return Elite::Guest(_rig.Host(), _rig.Program().loadSegment, Elite::DataSegment(_rig.Program()));
}

void AssertNoMismatch(ComparisonRig& _rig)
{
  const std::vector<Machine::NativeCode::Mismatch>& mismatches = _rig.Host().Native().Mismatches();
  if (!mismatches.empty())
  {
    const Machine::NativeCode::Mismatch& first = mismatches.front();
    const std::string text = first.routine + " call " + std::to_string(first.call) + ": " + first.difference;
    Assert::Fail(std::wstring(text.begin(), text.end()).c_str());
  }
}

// Plays _steps on from where the reference is, every native call on the way compared.
void Play(ComparisonRig& _rig, std::string_view _steps)
{
  std::vector<Elite::Step> steps;
  std::string error;
  Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), L"parses");
  Elite::ReplayPlayer player(_rig.Host(), _rig.Program());
  std::string digest;
  for (const Elite::Step& step : steps)
  {
    Assert::IsTrue(player.Play(step, digest) == Machine::StopReason::Reached, L"plays");
  }
  AssertNoMismatch(_rig);
}

// Launches from Lave and flies on for 6 seconds, run by the original with no hook in force, so that the
// test's calls are not made while a native RunFlight waits (FlightTests' Launch says why).
void Launch(ComparisonRig& _rig)
{
  Machine::Processor& processor = _rig.Host().Processor();
  processor.SetHookMap(nullptr);
  Play(_rig, "key space; wait 4; key F1; wait 6");
  processor.SetHookMap(&_rig.Host().Native().Map());
}

// The station's slot: active, of _station's type, at its position (and its camera-frame copy), with its
// spin angle.
void PlaceStation(Elite::Guest& _guest, const Station& _station)
{
  const std::uint16_t slot = DS.stationSlot.offset;
  _guest.SetByte(slot, static_cast<std::uint8_t>(0x81 | (_station.type << 1)));
  const std::array<std::int16_t, 3> position = {_station.x, _station.y, _station.z};
  for (std::uint16_t axis = 0; axis < 3; ++axis)
  {
    const auto value = static_cast<std::uint16_t>(position[axis]);
    _guest.SetByte(static_cast<std::uint16_t>(slot + 1 + axis), static_cast<std::uint8_t>(position[axis] < 0 ? 0xFF : 0));
    _guest.SetWord(static_cast<std::uint16_t>(slot + 4 + axis * 2), value);
    _guest.SetWord(static_cast<std::uint16_t>(slot + 0x10 + axis * 2), value);
  }
  _guest.SetWord(static_cast<std::uint16_t>(slot + STATION_SPIN_ANGLE), _station.spin);
}

} // namespace

// Constructed inputs for docking (plan §6.3): the alignments no replay flies, and the docking computer
// in every state.
TEST_CLASS(DockingTests)
{
public:
  // Either pitch, the roll against the spin and half a turn from it, a type 0 station's negated spin, and
  // misses at each test.
  TEST_METHOD(CheckDockingAlignmentAgreesOnEveryAlignment)
  {
    ComparisonRig rig("CheckDockingAlignment");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    ram.Write16(data, static_cast<std::uint16_t>(STATION + STATION_SPIN_ANGLE), STATION_SPIN);
    const std::initializer_list<Approach> approaches = {{0x010, 0x400, 0x700, 0}, {0x7F0, 0x3F0, 0x300, 0}, {0x000, 0x400, 0x500, 0},
                                                        {0x400, 0x000, 0x100, 2}, {0x410, 0x7F0, 0x500, 2}, {0x200, 0x000, 0x000, 2},
                                                        {0x000, 0x200, 0x000, 2}, {0x400, 0x400, 0x100, 2}};
    for (const Approach& approach : approaches)
    {
      ram.Write16(data, DS.playerPitchAngle.offset, approach.pitch);
      ram.Write16(data, DS.playerYawAngle.offset, approach.yaw);
      ram.Write16(data, DS.playerRollAngle.offset, approach.roll);
      ram.Write8(data, STATION, static_cast<std::uint8_t>(approach.type << 1));
      rig.Call(CHECK_DOCKING_ALIGNMENT, {.bx = TOLERANCE, .di = STATION});
    }
    rig.AssertAllAgreed(CHECK_DOCKING_ALIGNMENT, approaches.size());
  }

  // D switches the docking computer off, stopped and moving; and on, refused out of range and by a
  // station that has been shot. Cancelling it, on and off.
  TEST_METHOD(DockingComputerSwitchesAgree)
  {
    ComparisonRig rig("DockingComputerSwitches");
    Launch(rig);
    Elite::Guest guest = GuestOf(rig);
    for (const std::uint16_t speed : {std::uint16_t{0}, std::uint16_t{0x10}})
    {
      guest.Set(DS.dockingComputerOn, 1);
      guest.Set(DS.playerSpeed, speed);
      rig.Call(TOGGLE_DOCKING_COMPUTER, ALL_REGISTERS);
    }
    guest.Set(DS.safeZoneFlags, 0);
    rig.Call(TOGGLE_DOCKING_COMPUTER, ALL_REGISTERS);
    guest.Set(DS.safeZoneFlags, 1);
    const auto flags = static_cast<std::uint16_t>(DS.stationSlot.offset + SLOT_FLAGS);
    const std::uint8_t unshot = guest.Byte(flags);
    guest.SetByte(flags, static_cast<std::uint8_t>(unshot | 1));
    rig.Call(TOGGLE_DOCKING_COMPUTER, ALL_REGISTERS);
    guest.SetByte(flags, static_cast<std::uint8_t>(unshot & 0xFE));
    rig.Call(TOGGLE_DOCKING_COMPUTER, ALL_REGISTERS);
    rig.AssertAllAgreed(TOGGLE_DOCKING_COMPUTER, 5);

    for (const std::uint8_t on : {std::uint8_t{1}, std::uint8_t{0}})
    {
      guest.Set(DS.dockingComputerOn, on);
      rig.Call(CANCEL_DOCKING_COMPUTER, ALL_REGISTERS);
    }
    rig.AssertAllAgreed(CANCEL_DOCKING_COMPUTER, 2);
  }

  // Each state of the docking computer, from a constructed station: stopping; the roll that brings the
  // approach point or the station into the pitch plane, up or down; rolling to it either way and onto
  // it; pitching to it either way, and within its tolerance on the first pass and the second; the flight
  // to the approach point from far and near, on its last frame, and with no frame left, where the divide
  // traps; closing in fast, slowly and to the end, from the front view and another; the spin matched
  // either way round or not, for a Coriolis and a Dodo; rolling with it; and a state there is not.
  TEST_METHOD(RunDockingComputerAgreesInEveryState)
  {
    ComparisonRig rig("RunDockingComputer");
    Launch(rig);
    Elite::Guest guest = GuestOf(rig);
    std::uint64_t calls = 0;
    const auto run = [&](std::uint8_t _state)
    {
      guest.Set(DS.dockingComputerState, _state);
      rig.Call(RUN_DOCKING_COMPUTER, ALL_REGISTERS);
      ++calls;
    };
    guest.Set(DS.playerPitchAngle, 0);
    guest.Set(DS.playerYawAngle, 0);

    guest.Set(DS.playerSpeed, 8);
    run(0);
    guest.Set(DS.playerSpeed, 0);
    run(0);

    for (const Station& station : {Station{0x400, 0, -0xC00}, Station{0, 0x400, -0xC00}, Station{-0x400, -0x400, -0xC00},
                                   Station{0x200, -0x800, -0xC00}, Station{-0x100, 0x600, 0x800}})
    {
      for (const std::uint16_t roll : {std::uint16_t{0}, std::uint16_t{0x300}, std::uint16_t{0x5C0}})
      {
        PlaceStation(guest, station);
        guest.Set(DS.playerRollAngle, roll);
        run(1);
        run(5);
      }
    }

    struct Roll
    {
      std::uint16_t roll;
      std::uint16_t target;
    };
    for (const Roll& roll :
         {Roll{0x100, 0x100}, Roll{0x110, 0x100}, Roll{0x100, 0x200}, Roll{0x100, 0x000}, Roll{0x100, 0x700}, Roll{0x7F8, 0x004}})
    {
      for (const std::uint8_t state : {std::uint8_t{2}, std::uint8_t{6}})
      {
        guest.Set(DS.playerRollAngle, roll.roll);
        guest.Set(DS.dockingTargetAngle, roll.target);
        run(state);
      }
    }

    guest.Set(DS.playerRollAngle, 0);
    for (const Station& station : {Station{0, 0, 0x1000}, Station{0, 0x10, 0x1000}, Station{0, 0x800, 0x400}, Station{0, -0x800, 0x400},
                                   Station{0, 0, -0x1000}, Station{0, -0x40, -0x1800}})
    {
      for (const std::uint8_t passes : {std::uint8_t{0}, std::uint8_t{1}})
      {
        for (const std::uint8_t state : {std::uint8_t{3}, std::uint8_t{7}})
        {
          PlaceStation(guest, station);
          guest.Set(DS.dockingAlignPasses, passes);
          run(state);
        }
      }
    }

    struct Flight
    {
      Station station;
      std::uint16_t speed;
    };
    for (const Flight& flight :
         {Flight{{0x100, -0x200, 0x1800}, 0x30}, Flight{{0x100, -0x200, 0x1800}, 0x10}, Flight{{0, 0, -0x700}, 8},
          Flight{{0, 0, -0x700}, 4}, Flight{{0, 0, -0x7CB}, 8}, Flight{{0, 0, -0x7CE}, 8}, Flight{{-0x50, 0x30, -0x7D0}, 2}})
    {
      PlaceStation(guest, flight.station);
      guest.Set(DS.playerSpeed, flight.speed);
      run(4);
    }

    struct Closing
    {
      std::int16_t z;
      std::uint16_t speed;
      std::uint16_t view;
    };
    for (const Closing& closing : {Closing{-0x800, 0x30, 0}, Closing{-0x800, 0x10, 0}, Closing{-0x300, 8, 0}, Closing{-0x300, 4, 0},
                                   Closing{-0x200, 4, 0}, Closing{-0x200, 4, 0x400}})
    {
      PlaceStation(guest, Station{0, 0, closing.z});
      guest.Set(DS.playerSpeed, closing.speed);
      guest.Set(DS.viewAngle, closing.view);
      run(8);
    }
    guest.Set(DS.viewAngle, 0);

    for (const std::uint8_t type : {std::uint8_t{1}, std::uint8_t{0}})
    {
      for (const std::uint16_t roll : {std::uint16_t{0x105}, std::uint16_t{0x505}, std::uint16_t{0x300}, std::uint16_t{0x6FB}})
      {
        PlaceStation(guest, Station{0, 0, -0x100, type, 0x100});
        guest.Set(DS.playerRollAngle, roll);
        run(9);
        run(10);
        run(11);
      }
    }
    run(12);
    rig.AssertAllAgreed(RUN_DOCKING_COMPUTER, calls);
  }

  // Engaged far from the station, where no replay engages it: having flown away at full speed for 8
  // seconds, the docking computer stops, turns to the approach point, flies there and turns to the
  // station, every call of the native routines on the way compared.
  TEST_METHOD(DockingComputerAgreesFromFarAway)
  {
    ComparisonRig rig("DockingFromFarAway");
    Launch(rig);
    Elite::Guest guest = GuestOf(rig);
    guest.Set(DS.playerSpeed, 0x30);
    guest.Set(DS.velocityDirty, 1);
    Play(rig, "wait 8");
    guest.Set(DS.dockingComputerFitted, 1);
    guest.Set(DS.dockingKeyReleased, 1);
    Play(rig, "wait 30");
    Assert::IsTrue(guest.Get(DS.dockingComputerState) >= 5, L"the docking computer reached the approach point");
  }
};

} // namespace GameLogicTests
