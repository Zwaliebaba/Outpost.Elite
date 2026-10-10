#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Firmware.h"
#include "StateDigest.h"
#include "TwinRig.h"

#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t INSTALL_TIMER_INTERRUPT = 0x00C6;
constexpr std::uint16_t INSTALL_DIVIDE_AND_KEYBOARD_INTERRUPTS = 0x0105;
constexpr std::uint16_t RESTORE_DIVIDE_AND_KEYBOARD_INTERRUPTS = 0x0148;
constexpr std::uint16_t CHECK_CHEAT_ARGUMENT = 0x02A5;
constexpr std::uint16_t COPY_PROTECTION = 0x04A3;
constexpr std::uint16_t WIPE_PROGRAM = 0x0554;
constexpr std::uint16_t SAVE_STARTUP_COMMANDER = 0x4660;
constexpr std::uint16_t COMMAND_TAIL = 0x80; // in the PSP: its length, then the text
constexpr std::uint16_t INTERRUPT_TABLE_SEGMENT = 0;

// The scan code and character of 'q' in the BIOS's key buffer.
constexpr std::uint16_t BIOS_KEY_Q = 0x1071;

// Sets the byte _field to _value on both twins.
void SetBoth(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Both([_field, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _field.offset, _value); });
}

// ComparisonRig::Call with ES on the interrupt table, as Start leaves it for InstallDivideAndKeyboardInterrupts.
void CallOnTheInterruptTable(ComparisonRig& _rig, std::uint16_t _entry)
{
  Machine::Registers& regs = _rig.Host().Processor().Regs();
  const Machine::Registers saved = regs;
  regs.ds = Elite::DataSegment(_rig.Program());
  regs.es = INTERRUPT_TABLE_SEGMENT;
  _rig.Host().CallNear(_entry);
  regs = saved;
}

// Both twins are back at the title, out of flight.
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

// Constructed inputs for start-up (plan §6.3): the command tails, start moments and ways out no replay
// gives.
TEST_CLASS(StartUpTests)
{
public:
  // ' cheat' itself, and a tail of the same length that differs only in its last letter.
  TEST_METHOD(CheckCheatArgumentAgreesOnTheCheatAndANearMiss)
  {
    ComparisonRig rig("CheckCheatArgument");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::uint16_t psp = ram.Read16(data, Elite::DS.pspSegment.offset);
    for (const std::string_view tail : {std::string_view(" cheat"), std::string_view(" cheaT")})
    {
      ram.Write8(psp, COMMAND_TAIL, static_cast<std::uint8_t>(tail.size()));
      for (std::size_t index = 0; index < tail.size(); ++index)
        ram.Write8(psp, static_cast<std::uint16_t>(COMMAND_TAIL + 1 + index), static_cast<std::uint8_t>(tail[index]));
      rig.Call(CHECK_CHEAT_ARGUMENT, {});
      Assert::IsTrue((ram.Read8(data, Elite::DS.cheatEnabled.offset) == 1) == (tail == " cheat"), L"only ' cheat' enables it");
    }
    rig.AssertAllAgreed(CHECK_CHEAT_ARGUMENT, 2);
  }

  // What Start does round GameLoop, from the title: the start-up commander kept, int 0 and int 9 put back, the timer's and their
  // interrupts installed again, the protection that shows once passed over, and the program wiped on the way out. Start calls them
  // as value routines since level 5 of the de-assembly (ADR-012 item 12), so only calls like these compare them with the original.
  TEST_METHOD(StartUpRoutinesAgree)
  {
    ComparisonRig rig("StartUpRoutines");
    rig.Host().Ram().Write8(Elite::DataSegment(rig.Program()), Elite::DS.protectionShown.offset, 1);
    rig.Call(SAVE_STARTUP_COMMANDER, {});
    rig.Call(RESTORE_DIVIDE_AND_KEYBOARD_INTERRUPTS, {});
    rig.Call(INSTALL_TIMER_INTERRUPT, {});
    rig.Call(COPY_PROTECTION, {});
    CallOnTheInterruptTable(rig, INSTALL_DIVIDE_AND_KEYBOARD_INTERRUPTS);
    rig.Call(WIPE_PROGRAM, {});
    for (const std::uint16_t entry : {SAVE_STARTUP_COMMANDER, RESTORE_DIVIDE_AND_KEYBOARD_INTERRUPTS, INSTALL_TIMER_INTERRUPT,
                                      COPY_PROTECTION, INSTALL_DIVIDE_AND_KEYBOARD_INTERRUPTS, WIPE_PROGRAM})
    {
      rig.AssertAllAgreed(entry, 1);
    }
  }

  // Start (ADR-010 item 5) when DOS's clock reads 55 seconds, where the start-up wait's target second wraps
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

  // GameLoop (ADR-010 item 5) after a death: RunFlight returns 40 frames after GAME OVER, and the title runs
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
