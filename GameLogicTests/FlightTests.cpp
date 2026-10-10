#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Flight.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

using Bytes = std::initializer_list<std::uint8_t>;
using Words = std::initializer_list<std::uint16_t>;

constexpr std::uint16_t UPDATE_STARDUST = 0x068F;
constexpr std::uint16_t COMPUTE_STARDUST_SHIFT = 0x0887;
constexpr std::uint16_t COMPUTE_DUST_STRIP_MASK = 0x08B9;
constexpr std::uint16_t RESPAWN_DUST_AT_VERTICAL_EDGE = 0x0969;
constexpr std::uint16_t RESPAWN_DUST_ANYWHERE = 0x09A3;
constexpr std::uint16_t IS_DUST_ON_SCREEN = 0x09D6;
constexpr std::uint16_t SCALE_DUST_STEP = 0x0A06;
constexpr std::uint16_t DUST_TO_SCREEN = 0x0A28;
constexpr std::uint16_t HANDLE_FLIGHT_FUNCTION_KEYS = 0x0BB3;
constexpr std::uint16_t UPDATE_DASHBOARD = 0x254F;
constexpr std::uint16_t UPDATE_ENERGY_AND_LASER_HEAT = 0x2882;
constexpr std::uint16_t SET_UP_LOCAL_SPACE = 0x29D0;
constexpr std::uint16_t CHECK_COLLISIONS = 0x2BC5;
constexpr std::uint16_t UPDATE_SAFE_ZONE = 0x2E69;
constexpr std::uint16_t UPDATE_WARNINGS = 0x36B6;
constexpr std::uint16_t UPDATE_COMPASS = 0x418F;
constexpr std::uint16_t UPDATE_FUEL_LEAK = 0x499F;
constexpr std::uint16_t TICK_ESCAPE_POD = 0x7F69;
constexpr std::uint16_t PROCESS_FLIGHT_KEYS = 0x7FA8;
constexpr std::uint16_t UPDATE_PLAYER_MOTION = 0x8472;
constexpr std::uint16_t UPDATE_PLAYER_VELOCITY = 0x8599;
constexpr std::uint16_t ROLL_STARDUST = 0x0927;
constexpr std::uint16_t SHIFT_STARDUST_VERTICALLY = 0x0940;
constexpr std::uint16_t LOAD_DUST_POSITION = 0x0A13;
constexpr std::uint16_t STORE_DUST_POSITION = 0x0A19;
constexpr std::uint16_t STORE_PREVIOUS_DUST_POSITION = 0x0A1F;
constexpr std::uint16_t RESET_STARDUST = 0x0A43;
constexpr std::uint16_t INVALIDATE_DASHBOARD = 0x2540;
constexpr std::uint16_t DRAW_FIVE_LINE_BAR = 0x2645;
constexpr std::uint16_t DRAW_SIGNED_INDICATOR = 0x26BE;
constexpr std::uint16_t DRAW_THREE_LINE_BAR = 0x273E;
constexpr std::uint16_t DRAW_MISSILE_ICONS = 0x2799;
constexpr std::uint16_t DRAW_MISSILE_LOCK_INDICATOR = 0x2804;
constexpr std::uint16_t DRAW_ENERGY_BANKS = 0x283B;
constexpr std::uint16_t DRAW_CONDITION_LIGHT = 0x290C;
constexpr std::uint16_t UPDATE_CONDITION_COLOR = 0x2959;
constexpr std::uint16_t IN_SAFE_ZONE = 0x2E63;
constexpr std::uint16_t UPDATE_SCANNER_BLIP = 0x40EC;
constexpr std::uint16_t XOR_COMPASS_DOT = 0x42A4;
constexpr std::uint16_t ERASE_SCANNER_BLIP = 0x42D6;
constexpr std::uint16_t XOR_SCANNER_BLIP = 0x42F6;
constexpr std::uint16_t XOR_DASHBOARD_PIXEL = 0x43C4;
constexpr std::uint16_t ERASE_COMPASS_AND_BLIPS = 0x4594;
constexpr std::uint16_t MOVE_OBJECTS_BY_VELOCITY = 0x85EC;

constexpr std::uint16_t VIDEO_SEGMENT = 0xB800;

constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F5 = 0x3F;

// The slots: the sun, the planet, the station, then the ships.
constexpr std::uint16_t SLOT_BYTES = 0x40;
constexpr std::uint16_t STATION_SLOT = 0x69B0;
constexpr std::uint16_t SHIP_SLOT = 0x69F0;

[[nodiscard]] std::uint16_t Data(ComparisonRig& _rig) noexcept
{
  return Elite::DataSegment(_rig.Program());
}

// A byte or word of the data segment, at an offset and of a value the test writes as plain numbers.
void SetByte(ComparisonRig& _rig, int _offset, int _value)
{
  _rig.Host().Ram().Write8(Data(_rig), static_cast<std::uint16_t>(_offset), static_cast<std::uint8_t>(_value));
}

void SetWord(ComparisonRig& _rig, int _offset, int _value)
{
  _rig.Host().Ram().Write16(Machine::Memory::Linear(Data(_rig), static_cast<std::uint16_t>(_offset)), static_cast<std::uint16_t>(_value));
}

void Set(ComparisonRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  SetByte(_rig, _field.offset, _value);
}

void Set(ComparisonRig& _rig, Elite::DataField<std::uint16_t> _field, std::uint16_t _value)
{
  SetWord(_rig, _field.offset, _value);
}

[[nodiscard]] std::uint8_t Byte(ComparisonRig& _rig, int _offset)
{
  return _rig.Host().Ram().Read8(Data(_rig), static_cast<std::uint16_t>(_offset));
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

[[nodiscard]] const Machine::NativeCode::Hook& HookAt(ComparisonRig& _rig, std::uint16_t _entry)
{
  const auto& hooks = _rig.Host().Native().Hooks();
  const auto found = hooks.find(Machine::Memory::Linear(_rig.Program().loadSegment, _entry));
  Assert::IsTrue(found != hooks.end(), L"the routine is ported");
  return found->second;
}

// The calls of the flight entries so far that could not be compared.
[[nodiscard]] std::uint64_t FlightUnverifiable(ComparisonRig& _rig)
{
  std::uint64_t total = 0;
  for (const Elite::NativeEntry& entry : Elite::FlightEntries())
  {
    total += HookAt(_rig, entry.offset).unverifiable;
  }
  return total;
}

// ComparisonRig::Call with ES at video memory, as the dashboard's drawing routines are called.
void CallOnScreen(ComparisonRig& _rig, std::uint16_t _entry, const Inputs& _inputs)
{
  Machine::Registers& regs = _rig.Host().Processor().Regs();
  const Machine::Registers saved = regs;
  regs.ax = _inputs.ax;
  regs.bx = _inputs.bx;
  regs.cx = _inputs.cx;
  regs.dx = _inputs.dx;
  regs.si = _inputs.si;
  regs.di = _inputs.di;
  regs.bp = _inputs.bp;
  regs.ds = Data(_rig);
  regs.es = VIDEO_SEGMENT;
  _rig.Host().CallNear(_entry);
  regs = saved;
}

// One call of the routine at _entry, compared with the original, which agreed. ES is the data segment,
// or video memory when _onScreen.
void CallVerified(ComparisonRig& _rig, std::uint16_t _entry, const Inputs& _inputs = {}, bool _onScreen = false)
{
  const Machine::NativeCode::Hook& hook = HookAt(_rig, _entry);
  const std::uint64_t calls = hook.calls;
  const std::uint64_t verified = hook.verified;
  if (_onScreen)
  {
    CallOnScreen(_rig, _entry, _inputs);
  }
  else
  {
    _rig.Call(_entry, _inputs);
  }
  AssertNoMismatch(_rig);
  Assert::AreEqual(calls + 1, hook.calls);
  Assert::AreEqual(verified + 1, hook.verified, L"compared, not unverifiable");
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

// Launches from Lave as launch-and-dock.replay does, and flies on for 6 seconds: the state the flight
// routines run in.
void Launch(ComparisonRig& _rig)
{
  Play(_rig, "key space; wait 4; key F1; wait 6");
}

// A slot's 24-bit position and its camera-frame copy, both _x, _y, _z.
void SetPosition(ComparisonRig& _rig, std::uint16_t _slot, std::int16_t _x, std::int16_t _y, std::int16_t _z)
{
  const std::array<std::int16_t, 3> position = {_x, _y, _z};
  for (std::uint16_t axis = 0; axis < 3; ++axis)
  {
    const auto value = static_cast<std::uint16_t>(position[axis]);
    SetByte(_rig, static_cast<std::uint16_t>(_slot + 1 + axis), position[axis] < 0 ? 0xFF : 0);
    SetWord(_rig, static_cast<std::uint16_t>(_slot + 4 + axis * 2), value);
    SetWord(_rig, static_cast<std::uint16_t>(_slot + 0x10 + axis * 2), value);
  }
}

// A ship of _type at _x, _y, _z, active and in view, not yet collided with, no blip drawn, no bounty.
void PlaceShip(ComparisonRig& _rig, std::uint16_t _slot, std::uint8_t _type, std::int16_t _x, std::int16_t _y, std::int16_t _z)
{
  SetByte(_rig, _slot, static_cast<std::uint8_t>(0x81 | (_type << 1)));
  SetPosition(_rig, _slot, _x, _y, _z);
  SetByte(_rig, static_cast<std::uint16_t>(_slot + 0x0C), 0);
  SetByte(_rig, static_cast<std::uint16_t>(_slot + 0x1E), 0);
  SetByte(_rig, static_cast<std::uint16_t>(_slot + 0x31), 0);
}

[[nodiscard]] std::uint16_t Slot(std::uint16_t _index) noexcept
{
  return static_cast<std::uint16_t>(Elite::DS.shipSlots.offset + _index * SLOT_BYTES);
}

} // namespace

// Constructed inputs for the ported flight routines (plan §6.3): the views, keys and states the replays
// do not reach.
TEST_CLASS(FlightTests)
{
public:
  // The leaves on their own, at the edges of their arithmetic: stardustShift's divide traps once
  // playerSpeed passes 34h.
  TEST_METHOD(StardustLeavesAgreeAtTheirEdges)
  {
    ComparisonRig rig("StardustLeaves");
    for (const std::uint16_t speed : Words{0, 0x30, 0x34, 0x35, 0x80, 0xFFFF})
    {
      Set(rig, DS.playerSpeed, speed);
      for (const std::uint8_t jump : Bytes{0, 1})
      {
        Set(rig, DS.jumpDriveEngaged, jump);
        CallVerified(rig, COMPUTE_STARDUST_SHIFT, {.bx = 0x1234});
        CallVerified(rig, SCALE_DUST_STEP, {.ax = 0x8765, .bx = 0x1234});
      }
    }
    for (const std::uint16_t step : Words{0, 1, 2, 3, 0x7FFF, 0x8000, 0xFFFF, 0xFFFE, 0x0400, 0xF800})
      CallVerified(rig, COMPUTE_DUST_STRIP_MASK, {.dx = step});
    for (const std::uint16_t coordinate : Words{0x0000, 0x1FFF, 0x2000, 0xE000, 0xDFFF, 0x0FFF, 0x1000, 0xF000, 0xEFFF, 0x8000, 0x7FFF})
    {
      CallVerified(rig, IS_DUST_ON_SCREEN, {.ax = coordinate, .bx = 0});
      CallVerified(rig, IS_DUST_ON_SCREEN, {.ax = 0, .bx = coordinate});
      CallVerified(rig, DUST_TO_SCREEN, {.ax = coordinate, .bx = static_cast<std::uint16_t>(~coordinate & 0xFFFF)});
    }
    for (const std::uint16_t step : Words{0x0100, 0xFF00})
    {
      CallVerified(rig, RESPAWN_DUST_AT_VERTICAL_EDGE, {.dx = step, .si = DS.stardust.At(3), .bp = 0x0FFF});
      CallVerified(rig, RESPAWN_DUST_ANYWHERE, {.si = DS.stardust.At(4)});
    }
  }

  TEST_METHOD(UpdateStardustAgreesInEveryViewAndWithTheJumpDrive)
  {
    ComparisonRig rig("UpdateStardust");
    Launch(rig);
    for (const std::uint16_t rollRate : Words{0xF00C, 0x10F4, 0x0000})
    {
      Set(rig, DS.rollRate, rollRate);
      for (const std::uint16_t view : Words{0x000, 0x200, 0x400, 0x600})
      {
        Set(rig, DS.viewAngle, view);
        for (const std::uint8_t jump : Bytes{0, 1, 1, 0})
        {
          Set(rig, DS.jumpDriveEngaged, jump);
          Set(rig, DS.playerSpeed, jump != 0 ? std::uint16_t{0x30} : std::uint16_t{0x10});
          CallVerified(rig, UPDATE_STARDUST);
        }
        Set(rig, DS.playerSpeed, 0);
        CallVerified(rig, UPDATE_STARDUST);
      }
    }
    // Reversed controls flip the roll and the pitch.
    Set(rig, DS.reverseYControl, 1);
    Set(rig, DS.reverseXAndY, 1);
    Set(rig, DS.rollRate, 0xF00C);
    for (const std::uint16_t view : Words{0x000, 0x200, 0x400, 0x600})
    {
      Set(rig, DS.viewAngle, view);
      CallVerified(rig, UPDATE_STARDUST);
    }
    // Streaks one to three pixels long along the centre row, from every pixel of a byte.
    Set(rig, DS.reverseYControl, 0);
    Set(rig, DS.reverseXAndY, 0);
    Set(rig, DS.rollRate, 0);
    Set(rig, DS.viewAngle, 0);
    Set(rig, DS.playerSpeed, 0x30);
    for (std::uint16_t particle = 0; particle < 30; ++particle)
    {
      const auto x = static_cast<std::uint16_t>(0x80 + particle * 0x14);
      SetWord(rig, DS.stardust.At(particle), particle % 2 == 0 ? x : static_cast<std::uint16_t>(0 - x));
      SetWord(rig, static_cast<std::uint16_t>(DS.stardust.At(particle) + 2), 0);
      SetWord(rig, static_cast<std::uint16_t>(DS.stardust.At(particle) + 4), 0x0030);
    }
    Set(rig, DS.jumpDriveEngaged, 1);
    CallVerified(rig, UPDATE_STARDUST);
  }

  // F1-F4 come straight back; in witch space F5, F6, F7 and F8 post the navigation computer's error.
  TEST_METHOD(HandleFlightFunctionKeysAgreesWhereNoScreenWaits)
  {
    ComparisonRig rig("FunctionKeys");
    Launch(rig);
    for (std::uint8_t key = SCAN_F1; key < SCAN_F5; ++key)
    {
      SetByte(rig, DS.keyDown.At(key), 1);
      CallVerified(rig, HANDLE_FLIGHT_FUNCTION_KEYS);
    }
    Set(rig, DS.witchspaceCountdown, 2);
    for (std::uint8_t key = SCAN_F5; key < SCAN_F5 + 4; ++key)
    {
      SetByte(rig, DS.keyDown.At(key), 1);
      CallVerified(rig, HANDLE_FLIGHT_FUNCTION_KEYS);
    }
  }

  // The ECM glyph, both indicators against their stops, low energy flashing the condition light and
  // emptying the lower banks, and the safe zone with no station.
  TEST_METHOD(UpdateDashboardAgreesAtItsLimits)
  {
    ComparisonRig rig("Dashboard");
    Launch(rig);
    Set(rig, DS.ecmFired, 1);
    Set(rig, DS.rollRate, 0x807F);
    CallVerified(rig, UPDATE_DASHBOARD);
    Set(rig, DS.rollRate, 0x7F80);
    Set(rig, DS.playerEnergy, 0x0080);
    for (const std::uint16_t milliseconds : Words{0x0000, 0x0200, 0x0400})
    {
      Set(rig, DS.millisecondCounter, milliseconds);
      CallVerified(rig, UPDATE_DASHBOARD);
    }
    SetByte(rig, STATION_SLOT, static_cast<std::uint8_t>(Byte(rig, STATION_SLOT) & 0xFE));
    CallVerified(rig, UPDATE_DASHBOARD);
    CallVerified(rig, UPDATE_SAFE_ZONE);
  }

  // Energy capped at 3FFh, and below one bank the equipment loss, of a fitted item and of one not.
  TEST_METHOD(UpdateEnergyAndLaserHeatAgreesOnLossAndCap)
  {
    ComparisonRig rig("Energy");
    Launch(rig);
    Set(rig, DS.energyUnitFitted, 1);
    Set(rig, DS.playerEnergy, 0x03FE);
    CallVerified(rig, UPDATE_ENERGY_AND_LASER_HEAT);
    Set(rig, DS.playerEnergy, 0x0050);
    for (const std::uint16_t third : Words{0x0000, 0x0050})
    {
      // NextRandom returns state0 + state1, then state1 + state2.
      Set(rig, DS.randomState0, 0);
      Set(rig, DS.randomState1, 0x10);
      Set(rig, DS.randomState2, third);
      Set(rig, DS.missileCount, 3);
      CallVerified(rig, UPDATE_ENERGY_AND_LASER_HEAT);
    }
  }

  TEST_METHOD(SetUpLocalSpaceAgreesInWitchSpaceAndUnderInvasion)
  {
    ComparisonRig rig("LocalSpace");
    Launch(rig);
    Set(rig, DS.witchspaceCountdown, 3);
    CallVerified(rig, SET_UP_LOCAL_SPACE);
    Set(rig, DS.witchspaceCountdown, 0);
    Set(rig, DS.currentTechLevel, 12);
    Set(rig, DS.thargoidInvasionActive, 1);
    CallVerified(rig, SET_UP_LOCAL_SPACE);
  }

  TEST_METHOD(UpdateWarningsAndFuelLeakAgree)
  {
    ComparisonRig rig("Warnings");
    Launch(rig);
    Set(rig, DS.warningFrames, 3);
    CallVerified(rig, UPDATE_WARNINGS);
    Set(rig, DS.fuelLeakDelayFrames, 2);
    CallVerified(rig, UPDATE_FUEL_LEAK);
    CallVerified(rig, UPDATE_FUEL_LEAK);
    Set(rig, DS.fuel, 3);
    CallVerified(rig, UPDATE_FUEL_LEAK);
    CallVerified(rig, UPDATE_FUEL_LEAK);
    Set(rig, DS.titleShown, 0);
    CallVerified(rig, UPDATE_COMPASS);
  }

  // When the pod arrives TickEscapePod drops its return address, so it returns to its caller's caller:
  // here a second FFFFh pushed below the rig's.
  TEST_METHOD(TickEscapePodAgreesWhenThePodArrives)
  {
    ComparisonRig rig("EscapePod");
    Launch(rig);
    Machine::Registers& regs = rig.Host().Processor().Regs();
    for (const std::uint8_t frames : Bytes{2, 1})
    {
      Set(rig, DS.escapePodFrames, frames);
      const std::uint16_t stackPointer = regs.sp;
      regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
      rig.Host().Ram().Write16(Machine::Memory::Linear(regs.ss, regs.sp), 0xFFFF);
      CallVerified(rig, TICK_ESCAPE_POD);
      regs.sp = stackPointer;
    }
  }

  // A ship rammed, and the station met aligned, nearly aligned and not at all.
  TEST_METHOD(CheckCollisionsAgreesOnEveryOutcome)
  {
    ComparisonRig rig("Collisions");
    Launch(rig);
    PlaceShip(rig, SHIP_SLOT, 2, 0, 0, 0x20);
    CallVerified(rig, CHECK_COLLISIONS);

    // The station at the origin, its slot open, approached at each tolerance.
    const std::array<std::array<std::uint16_t, 3>, 4> approaches = {
      {{0, 0x400, 0}, {0x80, 0x400, 0}, {0x200, 0x200, 0x200}, {0x400, 0, 0x400}}};
    for (const auto& angles : approaches)
    {
      PlaceShip(rig, STATION_SLOT, 1, 0, 0, 0x10);
      SetWord(rig, STATION_SLOT + 0x0E, 0);
      Set(rig, DS.playerPitchAngle, angles[0]);
      Set(rig, DS.playerYawAngle, angles[1]);
      Set(rig, DS.playerRollAngle, angles[2]);
      Set(rig, DS.playerDocked, 0);
      CallVerified(rig, CHECK_COLLISIONS);
    }
    PlaceShip(rig, STATION_SLOT, 1, 0, 0, 0x10);
    Set(rig, DS.thargoidInvasionActive, 1);
    Set(rig, DS.playerPitchAngle, 0);
    Set(rig, DS.playerYawAngle, 0x400);
    Set(rig, DS.playerRollAngle, 0);
    CallVerified(rig, CHECK_COLLISIONS);
    PlaceShip(rig, STATION_SLOT, 1, 0x50, 0, 0x10);
    Set(rig, DS.thargoidInvasionActive, 0);
    Set(rig, DS.playerPitchAngle, 0x60);
    CallVerified(rig, CHECK_COLLISIONS);
  }

  TEST_METHOD(ProcessFlightKeysAgreesOnEveryKeyThatDoesNotWait)
  {
    ComparisonRig rig("FlightKeys");
    Launch(rig);
    Set(rig, DS.gameOverFrames, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.gameOverFrames, 0);
    Set(rig, DS.escapePodFrames, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.escapePodFrames, 0);

    // The views, and a view locked.
    for (const std::uint8_t key : Bytes{0x3C, 0x3C, 0x3D, 0x3E, 0x3B})
    {
      SetByte(rig, DS.keyDown.At(key), 1);
      CallVerified(rig, PROCESS_FLIGHT_KEYS);
    }
    Set(rig, DS.viewLocked, 1);
    SetByte(rig, DS.keyDown.At(0x3C), 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.viewLocked, 0);

    // G, then H in each of its refusals and with the galactic drive ready.
    Set(rig, DS.keyDownG, 1);
    Set(rig, DS.galacticHyperdriveFitted, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownG, 0);
    Set(rig, DS.keyDownH, 1);
    Set(rig, DS.invadedStationDestroyed, 0);
    Set(rig, DS.thargoidInvasionActive, 1);
    Set(rig, DS.jumpedSinceBriefing, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.thargoidInvasionActive, 0);
    for (const std::uint16_t distance : Words{0, 0x50, 0x46})
    {
      Set(rig, DS.galacticDriveReadyFrames, 0);
      Set(rig, DS.selectedDistanceTenthsLy, distance);
      Set(rig, DS.fuel, 0x10);
      CallVerified(rig, PROCESS_FLIGHT_KEYS);
    }
    Set(rig, DS.galacticDriveReadyFrames, 0x20);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownH, 0);

    // D: refused in mission 3, cancelled by a countdown, else the docking computer toggles.
    Set(rig, DS.dockingComputerFitted, 1);
    Set(rig, DS.missionNumber, 3);
    Set(rig, DS.dockingKeyReleased, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.missionNumber, 0);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.hyperspaceCountdown, 0);
    Set(rig, DS.dockingKeyReleased, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);

    // J.
    Set(rig, DS.keyDownJ, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownJ, 0);

    // T primes a missile, which locks on a ship ahead; the target goes, and U unarms; M jams, then
    // launches.
    PlaceShip(rig, SHIP_SLOT, 2, 0, 0, 0x800);
    Set(rig, DS.missileCount, 4);
    Set(rig, DS.missileState, 0);
    Set(rig, DS.shipIdRequested, 0);
    Set(rig, DS.keyDownT, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownT, 0);
    SetByte(rig, SHIP_SLOT, 0);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.missileState, 1);
    Set(rig, DS.keyDownU, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownU, 0);
    PlaceShip(rig, SHIP_SLOT, 2, 0, 0, 0x800);
    Set(rig, DS.missileTarget, SHIP_SLOT);
    Set(rig, DS.keyDownM, 1);
    for (const std::uint16_t random : Words{0x0010, 0x8000})
    {
      Set(rig, DS.missileState, 2);
      Set(rig, DS.missileJammed, 0);
      Set(rig, DS.randomState0, 0);
      Set(rig, DS.randomState1, random);
      CallVerified(rig, PROCESS_FLIGHT_KEYS);
    }
    Set(rig, DS.keyDownM, 0);

    // Fire with the laser too hot.
    Set(rig, DS.keyDownSpace, 1);
    Set(rig, DS.laserTemperature, 0xF4);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownSpace, 0);

    // E, B, C, I, N and L.
    Set(rig, DS.ecmFitted, 1);
    Set(rig, DS.keyDownE, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownE, 0);
    Set(rig, DS.energyBombFitted, 1);
    Set(rig, DS.keyDownB, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownB, 0);
    PlaceShip(rig, SHIP_SLOT, 2, 0, 0, 0x800);
    Set(rig, DS.missileState, 0);
    Set(rig, DS.shipIdRequested, 0);
    Set(rig, DS.missileCount, 2);
    Set(rig, DS.keyDownI, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownI, 0);
    Set(rig, DS.maskingDeviceFitted, 1);
    Set(rig, DS.keyDownN, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownN, 0);
    Set(rig, DS.antiEcmEmulatorFitted, 1);
    Set(rig, DS.keyDownL, 1);
    Set(rig, DS.playerEnergy, 0);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownL, 0);
    Set(rig, DS.playerEnergy, 0x3FF);
    Set(rig, DS.escapePodFitted, 1);
    Set(rig, DS.keyDownC, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
  }

  // GAME OVER's first frame and the later ones, the escape pod, the docking computer, the speed keys
  // at their stops, and the jump drive's one frame of 32 times the speed.
  TEST_METHOD(UpdatePlayerMotionAgreesInEveryMode)
  {
    ComparisonRig rig("PlayerMotion");
    Launch(rig);
    for (const std::uint8_t frames : Bytes{0x28, 0x27})
    {
      Set(rig, DS.gameOverFrames, frames);
      CallVerified(rig, UPDATE_PLAYER_MOTION);
    }
    Set(rig, DS.gameOverFrames, 0);
    Set(rig, DS.escapePodFrames, 5);
    CallVerified(rig, UPDATE_PLAYER_MOTION);
    Set(rig, DS.escapePodFrames, 0);
    for (const std::uint16_t speed : Words{0x2E, 0x06, 0x20})
    {
      Set(rig, DS.playerSpeed, speed);
      Set(rig, DS.keyDownPeriod, static_cast<std::uint8_t>(speed > 0x20 ? 1 : 0));
      Set(rig, DS.keyDownComma, static_cast<std::uint8_t>(speed < 0x20 ? 1 : 0));
      CallVerified(rig, UPDATE_PLAYER_MOTION);
    }
    Set(rig, DS.keyDownPeriod, 0);
    Set(rig, DS.keyDownComma, 0);
    Set(rig, DS.dockingComputerOn, 1);
    for (int frame = 0; frame < 4; ++frame)
      CallVerified(rig, UPDATE_PLAYER_MOTION);
    Set(rig, DS.dockingComputerOn, 0);
    Set(rig, DS.velocityDirty, 1);
    Set(rig, DS.jumpDriveEngaged, 1);
    CallVerified(rig, UPDATE_PLAYER_VELOCITY);
    // The joysticks: the Amstrad's, read as keys, and an IBM stick that is not there.
    for (const std::uint8_t amstrad : Bytes{1, 0})
    {
      Set(rig, DS.inputDevice, 1);
      Set(rig, DS.joystickIsAmstrad, amstrad);
      Set(rig, DS.keyDownAmstradFire1, 1);
      CallVerified(rig, UPDATE_PLAYER_MOTION);
      CallVerified(rig, PROCESS_FLIGHT_KEYS);
    }
    // The Amstrad stick held up and right, then down and left: the rates ramp while it is held.
    Set(rig, DS.joystickIsAmstrad, 1);
    for (const std::uint8_t upRight : Bytes{1, 1, 1, 0, 0, 0})
    {
      Set(rig, DS.keyDownAmstradUp, upRight);
      Set(rig, DS.keyDownAmstradRight, upRight);
      Set(rig, DS.keyDownAmstradDown, static_cast<std::uint8_t>(upRight ^ 1));
      Set(rig, DS.keyDownAmstradLeft, static_cast<std::uint8_t>(upRight ^ 1));
      CallVerified(rig, UPDATE_PLAYER_MOTION);
    }
    Set(rig, DS.inputDevice, 0);
  }

  // A scrape at the slot's edge, a Dodo station, the escape pod taking no damage, and the bounties: a
  // trader, a Thargoid, police inside the safe zone and out, kills in witch space, a missile's target.
  TEST_METHOD(CheckCollisionsAgreesOnScrapesAndBounties)
  {
    ComparisonRig rig("Bounties");
    Launch(rig);
    PlaceShip(rig, STATION_SLOT, 1, 0x80, 0, 0x10);
    Set(rig, DS.playerPitchAngle, 0x80);
    Set(rig, DS.playerYawAngle, 0x400);
    Set(rig, DS.playerRollAngle, 0);
    CallVerified(rig, CHECK_COLLISIONS);
    PlaceShip(rig, STATION_SLOT, 0, 0, 0, 0x10);
    SetWord(rig, STATION_SLOT + 0x0E, 0x40);
    Set(rig, DS.playerPitchAngle, 0);
    Set(rig, DS.playerRollAngle, 0x7C0);
    Set(rig, DS.playerDocked, 0);
    CallVerified(rig, CHECK_COLLISIONS);
    SetByte(rig, STATION_SLOT, 0);

    Set(rig, DS.escapePodFrames, 3);
    PlaceShip(rig, Slot(3), 2, 0, 0, 0x20);
    CallVerified(rig, CHECK_COLLISIONS);
    Set(rig, DS.escapePodFrames, 0);

    struct Kill
    {
      std::uint8_t type;
      std::uint8_t bounty;
      std::uint8_t police;
    };
    const std::array<Kill, 5> kills = {{{2, 0x10, 0}, {0x16, 0xFF, 0}, {0x1C, 0xFF, 1}, {0x1C, 0xFF, 0}, {7, 0x20, 0}}};
    for (const std::uint8_t safeZone : Bytes{1, 0})
    {
      Set(rig, DS.safeZoneFlags, safeZone);
      Set(rig, DS.killCount, 0xFE);
      Set(rig, DS.legalStatus, 0xFE);
      Set(rig, DS.playerEnergy, 0x3FF);
      for (std::size_t index = 0; index < kills.size(); ++index)
      {
        const std::uint16_t slot = Slot(static_cast<std::uint16_t>(3 + index));
        PlaceShip(rig, slot, kills[index].type, 0, 0, static_cast<std::int16_t>(0x10 + index));
        SetByte(rig, static_cast<std::uint16_t>(slot + 0x31), kills[index].bounty);
        SetWord(rig, static_cast<std::uint16_t>(slot + 0x3A), kills[index].police);
      }
      Set(rig, DS.missileState, 2);
      Set(rig, DS.missileTarget, Slot(3));
      CallVerified(rig, CHECK_COLLISIONS);
    }
    for (const std::uint8_t type : Bytes{7, 0x16, 2})
    {
      for (const std::uint8_t countdown : Bytes{0x30, 4, 5})
      {
        Set(rig, DS.witchspaceCountdown, countdown);
        Set(rig, DS.playerEnergy, 0x3FF);
        PlaceShip(rig, Slot(3), type, 0, 0, 0x10);
        SetByte(rig, Slot(3) + 0x31, 0x20);
        CallVerified(rig, CHECK_COLLISIONS);
      }
    }
  }

  // The station near on every axis on the negative side, or far by an inconsistent high byte; the
  // compass in a side view, with the planet behind and below.
  TEST_METHOD(SafeZoneAndCompassAgreeOnNegativeAndFarPositions)
  {
    ComparisonRig rig("NearAndFar");
    Launch(rig);
    SetPosition(rig, STATION_SLOT, -0x10, -0x10, -0x10);
    CallVerified(rig, UPDATE_SAFE_ZONE);
    CallVerified(rig, UPDATE_DASHBOARD);
    SetPosition(rig, STATION_SLOT, -0x10, -0x10, -0x2000);
    SetByte(rig, STATION_SLOT + 1, 0xFF);
    SetWord(rig, STATION_SLOT + 4, 0x0010);
    CallVerified(rig, UPDATE_SAFE_ZONE);
    SetPosition(rig, STATION_SLOT, -0x10, 0x10, -0x10);
    SetByte(rig, STATION_SLOT + 2, 0xFF);
    CallVerified(rig, UPDATE_SAFE_ZONE);
    Set(rig, DS.titleShown, 1);
    for (const std::uint16_t view : Words{0x200, 0x400, 0x600})
    {
      Set(rig, DS.viewAngle, view);
      SetPosition(rig, Slot(1), -0x3000, -0x2000, -0x1000);
      CallVerified(rig, UPDATE_COMPASS);
      SetPosition(rig, Slot(1), -0x3000, -0x2000, 0x1000);
      SetByte(rig, Slot(1) + 3, 0xFF);
      CallVerified(rig, UPDATE_COMPASS);
      SetPosition(rig, Slot(1), 0x3000, 0x2000, 0x1000);
      SetByte(rig, Slot(1) + 2, 0xFF);
      CallVerified(rig, UPDATE_COMPASS);
      SetPosition(rig, Slot(1), 0x3000, -0x2000, 0x1000);
      SetByte(rig, Slot(1) + 2, 0);
      CallVerified(rig, UPDATE_COMPASS);
    }
  }

  // D toggles the docking computer off, and is refused outside the safe zone and by a hostile station;
  // J engages the jump drive, or is refused below full speed or mass-locked; E and N with too little
  // energy; B in the safe zone with ships about; C and M with every slot taken; I on each kind of ship.
  TEST_METHOD(ProcessFlightKeysAgreesOnTheEquipmentAtItsLimits)
  {
    ComparisonRig rig("Equipment");
    Launch(rig);
    Set(rig, DS.dockingComputerFitted, 1);
    for (const std::uint8_t on : Bytes{1, 0, 0})
    {
      Set(rig, DS.dockingComputerOn, on);
      Set(rig, DS.dockingKeyReleased, 1);
      Set(rig, DS.playerSpeed, 0);
      CallVerified(rig, PROCESS_FLIGHT_KEYS);
      Set(rig, DS.safeZoneFlags, 0);
    }
    Set(rig, DS.safeZoneFlags, 1);
    SetByte(rig, STATION_SLOT + 0x1E, static_cast<std::uint8_t>(Byte(rig, STATION_SLOT + 0x1E) | 1));
    Set(rig, DS.dockingComputerOn, 0);
    Set(rig, DS.dockingKeyReleased, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.dockingComputerOn, 0);

    Set(rig, DS.keyDownJ, 1);
    Set(rig, DS.playerSpeed, 0x20);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.playerSpeed, 0x30);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.safeZoneFlags, 0);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    SetPosition(rig, Slot(0), 0x10, 0, 0);
    SetByte(rig, Slot(0) + 1, 0x10);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    SetPosition(rig, Slot(1), 0x10, 0, 0);
    SetByte(rig, Slot(1) + 1, 0x10);
    for (std::uint16_t index = 3; index < 20; ++index)
      SetByte(rig, Slot(index), 0);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    for (const std::uint8_t type : Bytes{5, 0x11, 6, 0x0B, 2, 2})
    {
      PlaceShip(rig, Slot(3), type, 0, 0, 0x200);
      SetByte(rig, Slot(3) + 0x1E, type == 2 ? 2 : 0);
      CallVerified(rig, PROCESS_FLIGHT_KEYS);
    }
    Set(rig, DS.objectSlotCount, 3);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.objectSlotCount, 0x14);
    Set(rig, DS.keyDownJ, 0);
    Set(rig, DS.jumpDriveEngaged, 0);

    Set(rig, DS.ecmFitted, 1);
    Set(rig, DS.keyDownE, 1);
    Set(rig, DS.playerEnergy, 0x10);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownE, 0);
    Set(rig, DS.maskingDeviceFitted, 1);
    Set(rig, DS.keyDownN, 1);
    Set(rig, DS.playerEnergy, 5);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownN, 0);
    Set(rig, DS.playerEnergy, 0x3FF);

    Set(rig, DS.safeZoneFlags, 1);
    Set(rig, DS.legalStatus, 0xF0);
    for (std::uint16_t index = 3; index < 6; ++index)
    {
      PlaceShip(rig, Slot(index), 2, static_cast<std::int16_t>(index * 0x100), 0, 0x400);
      SetByte(rig, static_cast<std::uint16_t>(Slot(index) + 0x1E), 2);
    }
    SetByte(rig, Slot(4) + 0x2D, 2); // debris pieces
    SetByte(rig, Slot(5), static_cast<std::uint8_t>(Byte(rig, Slot(5)) & 0x7F));
    Set(rig, DS.energyBombFitted, 1);
    Set(rig, DS.keyDownB, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownB, 0);

    // Every ship slot taken, and every one with a blip, so a slot has to be reclaimed.
    for (std::uint16_t index = 3; index < 20; ++index)
    {
      PlaceShip(rig, Slot(index), 2, static_cast<std::int16_t>(index * 0x40), 0x100, 0x2000);
      SetByte(rig, static_cast<std::uint16_t>(Slot(index) + 0x1E), 2);
    }
    Set(rig, DS.missileCount, 2);
    Set(rig, DS.missileState, 2);
    Set(rig, DS.missileJammed, 0);
    Set(rig, DS.missileTarget, Slot(4));
    Set(rig, DS.randomState0, 0x4000);
    Set(rig, DS.randomState1, 0x4000);
    Set(rig, DS.keyDownM, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownM, 0);
    for (std::uint16_t index = 3; index < 20; ++index)
      SetByte(rig, static_cast<std::uint16_t>(Slot(index) + 0x1E), 2);
    Set(rig, DS.escapePodFitted, 1);
    Set(rig, DS.escapePodFrames, 0);
    Set(rig, DS.keyDownC, 1);
    CallVerified(rig, PROCESS_FLIGHT_KEYS);
    Set(rig, DS.keyDownC, 0);
    Set(rig, DS.escapePodFrames, 0);
    Set(rig, DS.viewLocked, 0);
    Set(rig, DS.viewAngle, 0);

    // I on a rock, a simple class 3 ship, a Hermit and a Viper, off-centre to either side.
    struct Sighting
    {
      std::uint8_t type;
      std::uint8_t kind;
      std::int16_t x;
    };
    const std::array<Sighting, 4> sightings = {{{5, 3, -4}, {9, 3, 4}, {5, 4, -4}, {0x1C, 1, 4}}};
    for (const Sighting& sighting : sightings)
    {
      for (std::uint16_t index = 3; index < 20; ++index)
        SetByte(rig, Slot(index), 0);
      PlaceShip(rig, Slot(3), sighting.type, sighting.x, sighting.x, 0x800);
      SetByte(rig, Slot(3) + 0x33, sighting.kind);
      Set(rig, DS.missileState, 0);
      Set(rig, DS.shipIdRequested, 0);
      Set(rig, DS.hyperspaceCountdown, 0);
      Set(rig, DS.keyDownI, 1);
      CallVerified(rig, PROCESS_FLIGHT_KEYS);
    }
  }

  // The leaves the corpus reaches only from native callers, each called on its own and held to its own
  // contract.
  TEST_METHOD(LeavesAgreeOnTheirOwnContracts)
  {
    ComparisonRig rig("Leaves");
    Launch(rig);

    // The bars, the indicators, the missiles, the condition light and the energy banks.
    for (const std::uint8_t value : Bytes{0, 1, 3, 4, 5, 0x7F, 0x80, 0xFE, 0xFF})
    {
      CallVerified(rig, DRAW_FIVE_LINE_BAR, {.ax = value, .di = 0x3E38}, true);
      CallVerified(rig, DRAW_THREE_LINE_BAR, {.ax = value, .di = 0x3B8C}, true);
    }
    for (const std::uint8_t value : Bytes{0x80, 0xE8, 0xE9, 0xF0, 0, 0x10, 0x17, 0x18, 0x7F})
      CallVerified(rig, DRAW_SIGNED_INDICATOR, {.ax = value, .di = 0x37F8}, true);
    for (const std::uint8_t count : Bytes{0, 1, 2, 3, 4, 4})
    {
      Set(rig, DS.missileCount, count);
      CallVerified(rig, DRAW_MISSILE_ICONS, {}, true);
    }
    for (const std::uint8_t state : Bytes{0, 1, 2, 3, 3, 0})
    {
      Set(rig, DS.missileState, state);
      CallVerified(rig, DRAW_MISSILE_LOCK_INDICATOR, {}, true);
    }
    for (const std::uint16_t energy : Words{0x3FF, 0x2FF, 0x180, 0x80, 0, 0x3FF})
    {
      Set(rig, DS.playerEnergy, energy);
      CallVerified(rig, UPDATE_CONDITION_COLOR, {.ax = 0x1234});
      CallVerified(rig, DRAW_CONDITION_LIGHT, {}, true);
      CallVerified(rig, DRAW_ENERGY_BANKS, {}, true);
    }
    CallVerified(rig, INVALIDATE_DASHBOARD);
    for (const std::uint8_t flags : Bytes{0, 1, 2, 3})
    {
      Set(rig, DS.safeZoneFlags, flags);
      CallVerified(rig, IN_SAFE_ZONE, {.ax = 0x1234});
    }

    // The scanner and the compass.
    CallVerified(rig, XOR_DASHBOARD_PIXEL, {.bx = 0x1234, .dx = 0x2010});
    CallVerified(rig, XOR_DASHBOARD_PIXEL, {.bx = 0x1234, .dx = 0x2F3F});
    for (const std::uint16_t blip : Words{0x0000, 0x7F7F, 0x8080, 0x10F0, 0xF010})
      CallVerified(rig, XOR_SCANNER_BLIP, {.ax = blip, .bx = blip, .cx = static_cast<std::uint16_t>(~blip & 0xFFFF)});
    for (const std::uint16_t front : Words{0, 0x20})
      CallVerified(rig, XOR_COMPASS_DOT, {.dx = 0x27CF, .di = STATION_SLOT, .bp = front});
    for (std::uint16_t index = 3; index < 7; ++index)
    {
      const auto offset = static_cast<std::int16_t>(index * 0x300 - 0x1000);
      PlaceShip(rig, Slot(index), 2, offset, static_cast<std::int16_t>(-offset), 0x800);
      const Inputs camera = {
        .ax = static_cast<std::uint16_t>(offset), .bx = static_cast<std::uint16_t>(-offset), .cx = 0x0800, .di = Slot(index)};
      CallVerified(rig, UPDATE_SCANNER_BLIP, camera);
      CallVerified(rig, UPDATE_SCANNER_BLIP, camera);
      CallVerified(rig, ERASE_SCANNER_BLIP, {.ax = 0x1234, .di = Slot(index)});
      CallVerified(rig, ERASE_SCANNER_BLIP, {.ax = 0x1234, .di = Slot(index)});
    }
    for (const std::uint16_t slot : Words{0x6E30, STATION_SLOT, 0x6970})
    {
      CallVerified(rig, UPDATE_SCANNER_BLIP, {.ax = 0x100, .bx = 0x200, .cx = 0x300, .di = slot});
      CallVerified(rig, ERASE_SCANNER_BLIP, {.ax = 0x1234, .di = slot});
    }
    CallVerified(rig, ERASE_COMPASS_AND_BLIPS);

    // The stardust.
    for (const std::uint16_t step : Words{0x0400, 0xFC00, 0x0040, 0})
      CallVerified(rig, SHIFT_STARDUST_VERTICALLY, {.dx = step, .di = 0x1234});
    CallVerified(rig, ROLL_STARDUST, {.di = 0x1234, .bp = 0x5678});
    CallVerified(rig, LOAD_DUST_POSITION, {.si = DS.stardust.At(5)});
    CallVerified(rig, STORE_DUST_POSITION, {.ax = 0x1234, .bx = 0xF321, .si = DS.stardust.At(5)});
    CallVerified(rig, STORE_PREVIOUS_DUST_POSITION, {.ax = 0x1234, .bx = 0xF321, .si = DS.stardust.At(5)});
    for (const std::uint16_t frames : Words{0x0100, 0x0005})
    {
      Set(rig, DS.messageFrames, frames);
      CallVerified(rig, RESET_STARDUST);
    }

    // The player's motion, and the warnings, each in turn.
    Set(rig, DS.velocityDirty, 1);
    CallVerified(rig, UPDATE_PLAYER_VELOCITY, {.cx = 0x1234, .di = 0x5678});
    CallVerified(rig, UPDATE_PLAYER_VELOCITY);
    CallVerified(rig, MOVE_OBJECTS_BY_VELOCITY, {.bx = 0x1234});
    Set(rig, DS.incomingMissileAlert, 1);
    Set(rig, DS.altitude, 0x10);
    Set(rig, DS.cabinTemperature, 0xF0);
    Set(rig, DS.playerEnergy, 0x80);
    for (const std::uint8_t last : Bytes{3, 0, 1, 2, 3})
    {
      Set(rig, DS.warningFrames, 0);
      Set(rig, DS.warningIndex, last);
      CallVerified(rig, UPDATE_WARNINGS);
    }
  }

  // The docking computer, turned on as D's release would, flies the ship back in, with every call of a
  // flight entry on the way compared.
  TEST_METHOD(UpdatePlayerMotionAgreesWhileTheDockingComputerFlies)
  {
    ComparisonRig rig("DockingComputer");
    Launch(rig);
    Set(rig, DS.dockingComputerFitted, 1);
    Set(rig, DS.dockingKeyReleased, 1);
    const std::uint64_t unverifiable = FlightUnverifiable(rig);
    Play(rig, "wait 40");
    Assert::AreEqual(unverifiable, FlightUnverifiable(rig), L"every call of a flight entry compared");
  }
};

} // namespace GameLogicTests
