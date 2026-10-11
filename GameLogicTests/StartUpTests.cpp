#include "pch.h"

#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// Sets the byte _member names to _value on the twin.
template <typename Struct> void SetBoth(TwinRig& _rig, DataByte<Struct> _member, std::uint8_t _value)
{
  _rig.Both([_member, _value](Elite::Platform&, Elite::GameState& _state) { DataByteIn(_state, _member) = _value; });
}

// The twin is back at the title, out of flight.
void AssertAtTheTitle(TwinRig& _rig)
{
  _rig.Both(
    [](Elite::Platform&, Elite::GameState& _state)
    {
      Assert::AreEqual(std::uint8_t{0}, std::uint8_t{_state.Data().titleShown}, L"the title runs again");
      Assert::AreEqual(std::uint8_t{0}, std::uint8_t{_state.Data().inFlight}, L"out of flight");
    });
}

} // namespace

// Twins for start-up and GameLoop: the start moments and ways out no replay gives (ADR-016).
TEST_CLASS(StartUpTests)
{
public:
  // Start when DOS's clock reads 55 seconds, where the start-up wait's target second wraps
  // past the minute; then the disc menu's Exit to DOS: WipeProgram and ExitToDos, to the end of the program.
  TEST_METHOD(StartAgreesLateInAMinuteAndOnTheWayOut)
  {
    TwinRig twin("TwinStartAndExit", {.startMoment = Elite::DosDateTime{1980, 1, 1, 0, 0, 55, 0}, .fromPowerOn = true});
    twin.Play("wait 3\ndigest title\nkey space; wait 3.2\nkey Escape; wait 0.2\nkey e; wait 0.2\ndigest exit-asked");
    twin.Play("key y; wait 0.5", Elite::StopReason::Terminated);
  }

  // GameLoop after a death: RunFlight returns 40 frames after GAME OVER, and the title runs
  // again.
  TEST_METHOD(GameLoopAgreesAfterADeath)
  {
    TwinRig rig("TwinDeath");
    rig.Play("key space; wait 3.2");
    SetBoth(rig, &Elite::GameData::playerDead, 1);
    rig.Play("key F1; wait 4.5\ndigest title-again");
    AssertAtTheTitle(rig);
  }

  // GameLoop after the pause screen's abort, which drops two return addresses and returns into it past
  // RunFlight: it carries on as the original does, to the title.
  TEST_METHOD(GameLoopAgreesAfterThePauseScreensAbort)
  {
    TwinRig rig("TwinAbort");
    rig.Play("key space; wait 3.2\nkey F1; wait 2\ndown Escape; wait 0.1; up Escape; wait 0.3\nkey a; wait 0.5\ndigest title-again");
    AssertAtTheTitle(rig);
  }
};

} // namespace GameLogicTests
