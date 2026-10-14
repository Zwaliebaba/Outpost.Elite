#include "pch.h"

#include "PlatformRig.h"
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

// PerformDiskRequest's requests.
constexpr std::uint8_t LOAD_COMMANDER = 1;
constexpr std::uint8_t SAVE_COMMANDER = 2;

// keyBuffer is a ring of 16 codes: the write index wraps after its last.
constexpr std::uint16_t KEY_BUFFER_WRAP_MASK = 0x000F;

// Alt and PrtSc held for a moment: GetKey saves a screenshot while both are down. PrtSc also types '*' at a prompt.
// On a chart, which reads its keys once a frame, 117 ms on the IBM PC (D18): held for longer than a frame, so
// that GetKey sees both keys down and takes the screenshot.
constexpr std::string_view CHART_SCREENSHOT_KEYS = "down Alt_L; down KP_Multiply; wait 0.15; up KP_Multiply; up Alt_L; wait 0.05";
constexpr std::string_view QUICK_SCREENSHOT_KEYS = "down Alt_L; down KP_Multiply; wait 0.002; up KP_Multiply; up Alt_L; wait 0.05";

// _code at the end of keyBuffer, as ReadScanCode puts a key there: how a test sends a code no replay can name, such as
// the Amstrad joystick's 77h-7Ch.
void QueueScanCode(Elite::GameState& _state, std::uint8_t _code)
{
  const std::uint16_t write = _state.Data().input.keyBufferWrite;
  _state.Data().input.keyBuffer[write] = _code;
  _state.Data().input.keyBufferWrite = static_cast<std::uint16_t>((write + 1) & KEY_BUFFER_WRAP_MASK);
  _state.Data().input.keyBufferCount = static_cast<std::uint8_t>(_state.Data().input.keyBufferCount + 1);
}

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
// The disc menu's: A.CDR and ABCDEFI.CDR, the same commander saved under two names.
constexpr std::array<WrittenFile, 2> DISC_MENU_FILES = {{
  {"A.CDR", "87b5b23a48fee9234ff2ed89cc580f3059890ab9fe7d1cc1a7d091371437aa62"},
  {"ABCDEFI.CDR", "87b5b23a48fee9234ff2ed89cc580f3059890ab9fe7d1cc1a7d091371437aa62"},
}};
// The screenshots' and the commander saved before leaving for DOS, beside the two directories that stood in their way.
constexpr std::array<WrittenFile, 5> SCREENSHOT_FILES = {{
  {"ELITE00.HI", DIRECTORY},
  {"ELITE01.HI", "17b7f6a85f96fc70301256d3a943efeb21e9edc79f4513fd53316d0bcdadbda3"},
  {"ELITE10.LO", "88279d4b0951320971b62cb8c28a9f4752941464a6d8e36ed9d2e7922106a3bf"},
  {"ELITE11.LO", DIRECTORY},
  {"X.CDR", "a71fb1c01109def521b285ec6cdb25f045be9e74ce4298f533f794981ebd5cdd"},
}};
// The screenshot in flight.
constexpr std::array<WrittenFile, 1> FLIGHT_SCREENSHOT_FILES = {{
  {"ELITE01.HI", "6ddc6d729059be522ea65076eb0c078e16ab4b6e1e482df9805d8870bcc7bc00"},
}};

// The Disc/Control menu's every path but leaving for DOS: the catalogue empty and with a name, the version, keys that do
// nothing, a mouse driver absent and present, the IBM stick and the Amstrad's, saving, deleting, a load that DOS fails
// and its retry, names cut at eight characters and edited, and every kind of bad name, each rejected into the menu's
// key loop from inside PromptCommanderFileName.
void PlayEveryDiscMenuPath(TwinRig& _rig)
{
  _rig.Play("key space; wait 3.3\nkey Escape; wait 0.1\nkey c; wait 0.1\ndigest no-names");
  _rig.Play("key v; wait 0.05\nkey q; wait 0.05\nkey Next; wait 0.05\nkey m; wait 0.05\ndigest no-mouse");
  // A mouse driver, loaded with no mouse behind it: what the game's test of int 33h's vector finds once a driver points it at its
  // entry.
  const bool mouse = _rig.Host().MouseDriverLoaded();
  _rig.Both([](Elite::Platform& _platform, Elite::GameState&) { _platform.SetMouseDriverLoaded(true); });
  _rig.Play("key m; wait 0.05\ndigest mouse\nkey k; wait 0.05");
  _rig.Both([mouse](Elite::Platform& _platform, Elite::GameState&) { _platform.SetMouseDriverLoaded(mouse); });
  // The IBM stick, with nothing in the port and then with a stick in it.
  _rig.Play("key j; wait 0.05; key q; wait 0.05; key i; wait 0.05; key space; wait 0.1\ndigest no-stick");
  constexpr std::array<std::size_t, 2> AXES = {0, 1};
  _rig.Both(
    [&AXES](Elite::Platform& _platform, Elite::GameState&)
    {
      for (const std::size_t axis : AXES)
        _platform.Joystick().SetAxisResistance(axis, Elite::GamePort::MAXIMUM_OHMS / 2);
    });
  _rig.Play("key j; wait 0.05; key i; wait 0.05; key space; wait 0.1\ndigest stick\nkey k; wait 0.05");
  _rig.Both(
    [&AXES](Elite::Platform& _platform, Elite::GameState&)
    {
      for (const std::size_t axis : AXES)
        _platform.Joystick().SetAxisResistance(axis, Elite::GamePort::AXIS_DISCONNECTED);
    });
  // The Amstrad's, which never moves, and then sends codes from beyond the keyboard's before a key.
  _rig.Play("key j; wait 0.05; key a; wait 0.05; key q; wait 0.05\ndigest amstrad-still\nkey j; wait 0.05; key a; wait 0.05");
  _rig.Both(
    [](Elite::Platform&, Elite::GameState& _state)
    {
      constexpr std::array<std::uint8_t, 3> CODES = {0x60, 0x7D, 0x77};
      for (const std::uint8_t code : CODES)
        QueueScanCode(_state, code);
    });
  _rig.Play("key q; wait 0.05\ndigest amstrad-moved\nkey k; wait 0.05");
  // Leaving for DOS, thought better of.
  _rig.Play("key e; wait 0.05; key q; wait 0.05; key n; wait 0.05\ndigest stayed");
  // AB1 saved, catalogued and deleted; then Z, which is not there, loaded, retried and given up.
  _rig.Play("key s; wait 0.05; key a; down Shift_L; key b; up Shift_L; key 1; key Return; wait 0.1\ndigest saved\n"
            "key c; wait 0.1\ndigest catalogued\n"
            "key d; wait 0.05; key a; key b; key 1; key Return; wait 0.1\ndigest deleted\n"
            "key l; wait 0.05; key z; key Return; wait 0.1\nkey q; wait 0.05; key y; wait 0.1\nkey n; wait 0.05\ndigest not-loaded");
  // Nine characters typed keep eight, and a backspace takes one back: ABCDEFGI.
  _rig.Play("key s; wait 0.05; key a; key b; key c; key d; key e; key f; key g; key h; key i; key BackSpace; key BackSpace; key i;"
            " key Return; wait 0.1\ndigest long-name");
  // Bad names: none, a digit or a bracket first, and then a minus, a semicolon or a bracket after a letter.
  _rig.Play("key s; wait 0.05; key Return; wait 0.05\nkey s; wait 0.05; key 1; key Return; wait 0.05\n"
            "key s; wait 0.05; key bracketleft; key Return; wait 0.05\nkey s; wait 0.05; key a; key minus; key Return; wait 0.05\n"
            "key s; wait 0.05; key a; key semicolon; key Return; wait 0.05\n"
            "key s; wait 0.05; key a; key bracketleft; key Return; wait 0.05\ndigest bad-names");
  // From inside the last prompt's frame: a good name, saved; then, back in the menu after another bad one, the
  // catalogue, and a function key out of the menu.
  _rig.Play("key s; wait 0.05; key a; key Return; wait 0.1\ndigest saved-a\nkey s; wait 0.05; key Return; wait 0.05\n"
            "key c; wait 0.1\ndigest catalogued-again\nkey s; wait 0.05; key Return; wait 0.05\nkey F9; wait 0.1\ndigest status");
  Assert::IsTrue(HoldsExactly(_rig.Files(), DISC_MENU_FILES), L"the files the original wrote");
}

} // namespace

// Twins of the Disc/Control menu and the screenshots, which reach what no replay does (ADR-016).
TEST_CLASS(SaveLoadTests)
{
public:
  // Every path of the Disc/Control menu but leaving for DOS. Two twins ran this scenario, one with its native calls
  // compared with the original's and one not; they gave the same answers, and this is the one D7 kept.
  TEST_METHOD(DiscMenuAgreesOnEveryPath)
  {
    TwinRig rig("TwinDiscMenu");
    PlayEveryDiscMenuPath(rig);
  }

  // Screenshots with SaveScreenshot's native code: of the galactic chart and of the file name prompt's text page, each
  // also with a directory where DOS must create the file, so that DISC ERROR shows; then E and Y at the menu, which
  // ends the program. The twin must write the files the original wrote.
  TEST_METHOD(ScreenshotsAndLeavingForDosAgree)
  {
    TwinRig twin("TwinScreenshots");
    twin.Play("key space; wait 3.3\nkey F5; wait 0.1");
    twin.Play(CHART_SCREENSHOT_KEYS);
    // The numbers run '99', then '00', the name of a directory.
    twin.Both([](Elite::Platform&, Elite::GameState& _state) { _state.Data().video.screenshotNumber = 0x3939; });
    std::filesystem::create_directories(twin.Files() / "ELITE00.HI");
    twin.Play(CHART_SCREENSHOT_KEYS);
    // GetKey hands the chart one key a frame: the screenshots' Alt and PrtSc go first, then Escape closes it.
    twin.Play("wait 0.6; key Escape; wait 0.15; key s; wait 0.05");
    // '09', then '10'; then '11', a directory.
    twin.Both([](Elite::Platform&, Elite::GameState& _state) { _state.Data().video.screenshotNumber = 0x3930; });
    twin.Play(QUICK_SCREENSHOT_KEYS);
    std::filesystem::create_directories(twin.Files() / "ELITE11.LO");
    twin.Play(QUICK_SCREENSHOT_KEYS);
    twin.Play("key BackSpace; key BackSpace; key x; key Return; wait 0.1\ndigest saved");
    twin.Play("key e; wait 0.05; key y; wait 0.5", Elite::StopReason::Terminated);
    Assert::IsTrue(HoldsExactly(twin.Files(), SCREENSHOT_FILES), L"the files the original wrote");
  }

  // A screenshot in flight, which PollScreenDumpKey takes from RunFlight's loop while Alt and PrtSc are down: its
  // path to SaveScreenshot.
  TEST_METHOD(ScreenshotInFlightAgrees)
  {
    TwinRig twin("TwinFlightScreenshot");
    twin.Play("key space; wait 3.3\nkey F1; wait 2\ndigest launched");
    twin.Play("down Alt_L; down KP_Multiply; wait 0.1; up KP_Multiply; up Alt_L; wait 0.1\ndigest dumped");
    Assert::IsTrue(HoldsExactly(twin.Files(), FLIGHT_SCREENSHOT_FILES), L"the screenshot the original wrote");
  }

  // D43: a commander saved with every field D43 names beyond what the game writes, as only a hand-edited file holds them,
  // loaded through PerformDiskRequest and held to what the game's writers keep.
  TEST_METHOD(LoadedCommanderIsHeldToWhatTheGameWrites)
  {
    PlatformRig rig("SaveLoadClamp");
    rig.Start(
      [&rig]
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
        for (Elite::Field<std::uint8_t>& letter : commander.defaultCommanderName)
          letter = 'A';
        commander.creditsTenths = 1234;
        for (Elite::Field<std::uint8_t>& character : commander.creditBalanceText)
          character = 'X';
        Elite::PerformDiskRequest(state, rig.Native(), SAVE_COMMANDER, 0);
        Elite::PerformDiskRequest(state, rig.Native(), LOAD_COMMANDER, 0);
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
