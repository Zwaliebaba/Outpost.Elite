#include "pch.h"

#include "HostRig.h"
#include "Repository.h"
#include "SaveLoad.h"
#include "Sha256.h"
#include "Text.h"
#include "TwinRig.h"

#include <array>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// A file in DOS's directory: its name, and the SHA-256 of its bytes in lowercase hex, or DIRECTORY for a directory.
struct WrittenFile
{
  std::string_view name;
  std::string_view digest;
};

constexpr std::string_view DIRECTORY = "<directory>";

// Whether _directory holds exactly _files: each by name with its digest, and nothing else.
[[nodiscard]] bool HoldsExactly(const std::filesystem::path& _directory, std::span<const WrittenFile> _files)
{
  std::map<std::string, std::string, std::less<>> expected;
  for (const WrittenFile& file : _files)
  {
    expected.emplace(file.name, file.digest);
  }
  std::map<std::string, std::string, std::less<>> found;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(_directory))
  {
    std::string digest(DIRECTORY);
    if (entry.is_regular_file())
    {
      digest = Elite::Sha256::ToHex(Elite::Sha256::Of(Elite::ReadWholeFile(entry.path())));
    }
    found.emplace(entry.path().filename().string(), digest);
  }
  return found == expected;
}

// The files each twin leaves in DOS's directory, as the interpreted original wrote them for the same scenario, recorded
// before D7 deleted it: what the twins' file comparisons checked, and what the known answers' digests do not cover, since
// the game's state holds no file. Like a known answer (ADR-016 item 4), one changes only by a ruling recorded with its cause.
// The commander saved before leaving for DOS.
constexpr std::array<WrittenFile, 1> QUIT_FILES = {{
  {"X.CDR", "a71fb1c01109def521b285ec6cdb25f045be9e74ce4298f533f794981ebd5cdd"},
}};

} // namespace

// Twins of saving and leaving for DOS, and the clamp of a loaded commander, which reach what no replay does (ADR-016). D10's own
// keys and steps are DiscScreenTests'.
TEST_CLASS(SaveLoadTests)
{
public:
  // A commander saved as X from D10, opened by the chart's Escape, its name typed over the commander's, then 5 and Y, which end
  // the program: the quit (D80), with no farewell. Alt and PrtSc's screenshots, which this twin took on the way until D86, are
  // gone.
  TEST_METHOD(SavingAndLeavingForDosAgree)
  {
    TwinRig twin("TwinQuit");
    twin.Play("key space; wait 3.3\nkey F4; wait 0.1");
    twin.Play("wait 0.6; key Escape; wait 0.15; key 2; wait 0.05");
    twin.Play("key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace; key x;"
              " key Return; wait 0.1\ndigest saved");
    twin.Play("key 5; wait 0.05; key y; wait 0.5", Elite::StopReason::Terminated);
    Assert::IsTrue(HoldsExactly(twin.Files(), QUIT_FILES), L"the commander the game saved");
  }

  // D43: a commander saved with every field D43 names beyond what the game writes, as only a hand-edited file holds them,
  // loaded through PerformDiskRequest and held to what the game's writers keep.
  TEST_METHOD(LoadedCommanderIsHeldToWhatTheGameWrites)
  {
    HostRig rig("SaveLoadClamp");
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
        Elite::GameState& state = rig.State();
        Elite::CommanderState& commander = state.Data().commander;
        constexpr std::string_view NAME = "H.CDR";
        for (std::size_t letter = 0; letter < NAME.size(); ++letter)
          state.Data().saveLoad.commanderFileName[letter] = static_cast<std::uint8_t>(NAME[letter]);
        commander.galaxyNumber = 0x30;
        commander.currentSystemChartY = 0x80;
        commander.chartCursorY = 0xC0;
        commander.shortRangeCursorY = 0x90;
        commander.galacticCursorY = 0xFF;
        commander.currentSystem.government = 9;
        commander.currentSystem.economy = 0xFF;
        commander.selectedSystem.government = 8;
        commander.selectedSystem.economy = 0x80;
        commander.missileCount = 9;
        commander.killCount = 0xFF;
        for (std::uint8_t& letter : commander.defaultCommanderName)
          letter = 'A';
        commander.creditsTenths = 1234;
        for (std::uint8_t& character : commander.creditBalanceText)
          character = 'X';
        Elite::PerformDiskRequest(state, rig.Host(), Elite::DISK_SAVE);
        Elite::PerformDiskRequest(state, rig.Host(), Elite::DISK_LOAD);
        co_return;
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Terminated);
    Elite::GameState& state = rig.State();
    const Elite::CommanderState& commander = state.Data().commander;
    Assert::AreEqual(std::uint32_t{0}, std::uint32_t{state.Data().disk.diskError}, L"saved and loaded");
    Assert::AreEqual(8u, std::uint32_t{commander.galaxyNumber}, L"the ninth galaxy at most");
    Assert::AreEqual(0x7Fu, std::uint32_t{commander.currentSystemChartY}, L"the chart's last row at most");
    Assert::AreEqual(0x7Fu, std::uint32_t{commander.chartCursorY});
    Assert::AreEqual(0x7Fu, std::uint32_t{commander.shortRangeCursorY});
    Assert::AreEqual(0x7Fu, std::uint32_t{commander.galacticCursorY});
    Assert::AreEqual(7u, std::uint32_t{commander.currentSystem.government}, L"the eighth government at most");
    Assert::AreEqual(7u, std::uint32_t{commander.currentSystem.economy}, L"the eighth economy at most");
    Assert::AreEqual(7u, std::uint32_t{commander.selectedSystem.government});
    Assert::AreEqual(7u, std::uint32_t{commander.selectedSystem.economy});
    Assert::AreEqual(4u, std::uint32_t{commander.missileCount}, L"four missiles at most");
    Assert::AreEqual(0xFEu, std::uint32_t{commander.killCount}, L"the kills CreditKill stops at");
    Assert::AreEqual(std::uint32_t{'A'}, std::uint32_t{commander.defaultCommanderName[7]}, L"the name's eight letters kept");
    Assert::AreEqual(0u, std::uint32_t{commander.defaultCommanderName[8]}, L"and its NUL after them");
    Assert::AreEqual(0u, std::uint32_t{commander.creditBalanceText[19]}, L"the cash's text's NUL");
    const std::string_view cash = Elite::TextWithin(commander.creditBalanceText, 0);
    Assert::IsTrue(cash == "      123.4 Credits", L"the cash's text is the cash's");
  }
};

} // namespace GameLogicTests
