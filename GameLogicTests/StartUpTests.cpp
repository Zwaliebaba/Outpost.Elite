#include "pch.h"

#include "DataOverlay.h"
#include "Firmware.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The scan code and character of 'q' in the BIOS's key buffer.
constexpr std::uint16_t BIOS_KEY_Q = 0x1071;

// Sets the byte _field to _value on the twin.
void SetBoth(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Both([_field, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _field.offset, _value); });
}

// The twin is back at the title, out of flight.
void AssertAtTheTitle(TwinRig& _rig)
{
  _rig.Both(
    [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      const std::uint16_t data = Elite::DataSegment(_program);
      Assert::AreEqual(std::uint8_t{0}, _pc.Ram().Read8(data, Elite::DS.titleShown.offset), L"the title runs again");
      Assert::AreEqual(std::uint8_t{0}, _pc.Ram().Read8(data, Elite::DS.inFlight.offset), L"out of flight");
    });
}

} // namespace

// Twins for start-up and GameLoop: the start moments and ways out no replay gives (ADR-016).
TEST_CLASS(StartUpTests)
{
public:
  // Start when DOS's clock reads 55 seconds, where the start-up wait's target second wraps
  // past the minute; then the disc menu's Exit to DOS, with a key left in the BIOS's buffer to empty:
  // WipeProgram and ExitToDos, to the end of the program.
  TEST_METHOD(StartAgreesLateInAMinuteAndOnTheWayOut)
  {
    TwinRig twin("TwinStartAndExit", {.startMoment = Machine::Dos::DateTime{1980, 1, 1, 0, 0, 55, 0}, .fromPowerOn = true});
    twin.Play("wait 3\ndigest title\nkey space; wait 3.2\nkey Escape; wait 0.2\nkey e; wait 0.2\ndigest exit-asked");
    twin.Both(
      [](Machine::Pc& _pc, const Machine::LoadedProgram&)
      {
        using Machine::Firmware;
        Machine::Memory& ram = _pc.Ram();
        const std::uint16_t tail = ram.Read16(Firmware::DATA_SEGMENT, Firmware::KEYBOARD_TAIL);
        const auto next = static_cast<std::uint16_t>(tail + 2 == Firmware::KEYBOARD_BUFFER_END ? Firmware::KEYBOARD_BUFFER : tail + 2);
        ram.Write16(Firmware::DATA_SEGMENT, tail, BIOS_KEY_Q);
        ram.Write16(Firmware::DATA_SEGMENT, Firmware::KEYBOARD_TAIL, next);
      });
    twin.Play("key y; wait 0.5", Machine::StopReason::Terminated);
  }

  // GameLoop after a death: RunFlight returns 40 frames after GAME OVER, and the title runs
  // again.
  TEST_METHOD(GameLoopAgreesAfterADeath)
  {
    TwinRig rig("TwinDeath");
    rig.Play("key space; wait 3.2");
    SetBoth(rig, Elite::DS.playerDead, 1);
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
