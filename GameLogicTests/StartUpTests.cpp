#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "DirectoryFileStore.h"
#include "Firmware.h"
#include "StateDigest.h"
#include "TwinRig.h"

#include <memory>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t CHECK_CHEAT_ARGUMENT = 0x02A5;
constexpr std::uint16_t COMMAND_TAIL = 0x80; // in the PSP: its length, then the text

// The scan code and character of 'q' in the BIOS's key buffer.
constexpr std::uint16_t BIOS_KEY_Q = 0x1071;

// Sets the byte _field to _value on both twins.
void SetBoth(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Both([_field, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _field.offset, _value); });
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

// The reference loaded as ReferenceRig loads it, but with DOS's clock starting at a moment of the test's
// own.
class MomentRig
{
public:
  MomentRig(std::string_view _name, const Machine::Dos::DateTime& _moment)
    : m_directory(_name),
      m_files(m_directory.Path()),
      m_pc(std::make_unique<Machine::Pc>(m_files, Desc(_moment)))
  {
    m_pc->SetTimeMode(Machine::TimeMode::Paced);
    const std::vector<std::uint8_t> file = Elite::ReadWholeFile(Elite::FindInRepository(Elite::REFERENCE_FILE_NAME));
    m_loaded = Elite::LoadReference(*m_pc, file, Elite::PSP_SEGMENT, m_program) == Elite::ReferenceFailure::None;
  }

  [[nodiscard]] bool Loaded() const noexcept
  {
    return m_loaded;
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return *m_pc;
  }

  [[nodiscard]] const Machine::LoadedProgram& Program() const noexcept
  {
    return m_program;
  }

private:
  [[nodiscard]] static Machine::Pc::Desc Desc(const Machine::Dos::DateTime& _moment) noexcept
  {
    Machine::Pc::Desc desc;
    desc.startMoment = _moment;
    return desc;
  }

  ScratchDirectory m_directory;
  Machine::DirectoryFileStore m_files;
  std::unique_ptr<Machine::Pc> m_pc;
  Machine::LoadedProgram m_program;
  bool m_loaded = false;
};

// TwinRig's two machines, from the power-on of a machine whose clock starts at _moment, and as far as
// the end of the program: how a test drives Start through paths no replay takes. Both twins boot with
// what they run recorded, the native one with every work call compared; each step must stop both for
// the same reason, every digest must agree, and so must the state they end in.
class StartUpTwin
{
public:
  StartUpTwin(std::string_view _name, const Machine::Dos::DateTime& _moment)
    : m_name(_name),
      m_original(m_name + "-Original", _moment),
      m_native(m_name + "-Native", _moment),
      m_originalPlayer(m_original.Host(), m_original.Program()),
      m_nativePlayer(m_native.Host(), m_native.Program())
  {
    Assert::IsTrue(m_original.Loaded() && m_native.Loaded(), L"ELITES.EXE at the repository root");
    m_original.Host().Processor().SetExecutionMap(&m_executed);
    Elite::InstallNativeRoutines(m_native.Host(), m_native.Program());
    m_native.Host().Native().SetVerifying(true);
  }

  StartUpTwin(const StartUpTwin&) = delete;
  StartUpTwin& operator=(const StartUpTwin&) = delete;
  StartUpTwin(StartUpTwin&&) = delete;
  StartUpTwin& operator=(StartUpTwin&&) = delete;

  ~StartUpTwin()
  {
    SaveExecutedOffsets(m_name, m_executed, m_original.Program());
    SaveNativeReport(m_name, m_native.Host().Native());
  }

  /// As TwinRig::Both.
  template <typename Change> void Both(Change _change)
  {
    _change(m_original.Host(), m_original.Program());
    _change(m_native.Host(), m_native.Program());
  }

  /// Plays _steps on both machines: each must reach its end, but for the last, which must stop both
  /// with _end.
  void Play(std::string_view _steps, Machine::StopReason _end = Machine::StopReason::Reached)
  {
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), L"parses");
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
      std::string original;
      std::string native;
      const Machine::StopReason expected = index + 1 == steps.size() ? _end : Machine::StopReason::Reached;
      Assert::IsTrue(m_originalPlayer.Play(steps[index], original) == expected, L"the interpreted run stops as it should");
      Assert::IsTrue(m_nativePlayer.Play(steps[index], native) == expected, L"the native run stops as it should");
      Assert::IsTrue(original == native, L"the digests agree");
    }
    Assert::IsTrue(Elite::GameStateDigest(m_original.Host(), m_original.Program()) ==
                     Elite::GameStateDigest(m_native.Host(), m_native.Program()),
                   L"the two end in one state");
    Assert::IsTrue(m_native.Host().Native().Mismatches().empty(), L"every compared call agreed");
  }

private:
  std::string m_name;
  std::vector<std::uint8_t> m_executed; // made before the machine that marks it, so that it outlives it
  MomentRig m_original;
  MomentRig m_native;
  Elite::ReplayPlayer m_originalPlayer;
  Elite::ReplayPlayer m_nativePlayer;
};

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

  // Start (ADR-010 item 5) when DOS's clock reads 55 seconds, where the start-up wait's target second wraps
  // past the minute; then the disc menu's Exit to DOS, with a key left in the BIOS's buffer to empty:
  // WipeProgram and ExitToDos, to the end of the program.
  TEST_METHOD(StartAgreesLateInAMinuteAndOnTheWayOut)
  {
    StartUpTwin twin("TwinStartAndExit", Machine::Dos::DateTime{1980, 1, 1, 0, 0, 55, 0});
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
