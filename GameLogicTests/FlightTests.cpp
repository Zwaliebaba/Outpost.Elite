#include "pch.h"

#include "DataOverlay.h"
#include "Guest.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

// An object slot's bytes.
constexpr std::uint16_t SLOT_BYTES = 0x40;

[[nodiscard]] std::uint16_t Slot(std::uint16_t _index) noexcept
{
  return static_cast<std::uint16_t>(Elite::DS.shipSlots.offset + _index * SLOT_BYTES);
}

// A byte of the data segment set on the twin.
void SetBoth(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Both([&](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _field.offset, _value); });
}

// Launches from Lave, and flies on for 6 seconds.
void Launch(TwinRig& _rig)
{
  _rig.Play("key space; wait 4\nkey F1; wait 6");
}

} // namespace

// Twins for the flight states the replays do not reach (ADR-016).
TEST_CLASS(FlightTests)
{
public:
  // Dead with the cheat on, the flight goes on; with it off, GAME OVER for 40 frames, then the title.
  TEST_METHOD(RunFlightAgreesThroughGameOver)
  {
    TwinRig rig("TwinGameOver");
    Launch(rig);
    SetBoth(rig, DS.cheatEnabled, 1);
    SetBoth(rig, DS.playerDead, 1);
    rig.Play("wait 0.5");
    SetBoth(rig, DS.cheatEnabled, 0);
    rig.Play("wait 5\ndigest game-over");
  }

  // GAME OVER with cargo aboard and every ship slot taken: the wreckage's barrel needs a slot reclaimed.
  TEST_METHOD(RunFlightAgreesThroughGameOverWithCargo)
  {
    TwinRig rig("TwinGameOverCargo");
    Launch(rig);
    SetBoth(rig, DS.cargoUsedTonnes, 1);
    rig.Both(
      [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
      {
        Elite::Guest guest(_pc, _program.loadSegment, Elite::DataSegment(_program));
        for (std::uint16_t index = 3; index < 20; ++index)
        {
          const std::uint16_t slot = Slot(index);
          guest.SetByte(slot, 0x85); // a Sidewinder, active
          for (std::uint16_t offset = 1; offset < SLOT_BYTES; ++offset)
            guest.SetByte(static_cast<std::uint16_t>(slot + offset), 0);
          guest.SetWord(static_cast<std::uint16_t>(slot + 4), static_cast<std::uint16_t>(index * 0x100));
          guest.SetWord(static_cast<std::uint16_t>(slot + 8), 0x2000);
          guest.SetByte(static_cast<std::uint16_t>(slot + 0x1E), 2); // its blip drawn
        }
      });
    SetBoth(rig, DS.playerDead, 1);
    rig.Play("wait 5\ndigest game-over");
  }

  // When the escape pod arrives TickEscapePod returns past RunFlight, to GameLoop, which docks.
  TEST_METHOD(RunFlightAgreesWhenTheEscapePodArrives)
  {
    TwinRig rig("TwinEscapePod");
    Launch(rig);
    SetBoth(rig, DS.escapePodFrames, 3);
    rig.Play("wait 3\ndigest docked");
  }

  // The pause screen's every key: the five options toggled, a key it ignores, two frame rates, and A,
  // which leaves RunFlight for the title.
  TEST_METHOD(PauseScreenAgreesOnEveryKey)
  {
    TwinRig rig("TwinPause");
    Launch(rig);
    rig.Play("down Escape; wait 0.1; up Escape; wait 1\ndigest paused");
    rig.Play("key r; wait 0.5\nkey d; wait 0.5\nkey y; wait 0.5\nkey b; wait 0.5\nkey s; wait 0.5\nkey x; wait 0.5\n"
             "key F3; wait 0.5\nkey F10; wait 0.5\ndigest options");
    rig.Play("key a; wait 2\ndigest title");
  }

  // A screen left by Escape, which waits for a key at 0BF3; in witch space the short-range chart at the
  // last of the countdown, F9 past the navigation computer, Escape back to it, and the charts refused.
  TEST_METHOD(FlightScreensAgreeInWitchSpace)
  {
    TwinRig rig("TwinWitchScreens");
    Launch(rig);
    rig.Play("down F10; wait 0.1; up F10; wait 1\nkey Escape; wait 1\nkey F1; wait 1\ndigest after-escape");
    SetBoth(rig, DS.witchspaceCountdown, 1);
    rig.Play("down F6; wait 0.1; up F6; wait 1\nkey F9; wait 1\nkey Escape; wait 1\ndigest witch-chart");
    SetBoth(rig, DS.witchspaceCountdown, 2);
    rig.Play("down F6; wait 0.1; up F6; wait 0.5\ndown F10; wait 0.1; up F10; wait 1\nkey F1; wait 1\ndigest witch-inventory");
  }
};

} // namespace GameLogicTests
