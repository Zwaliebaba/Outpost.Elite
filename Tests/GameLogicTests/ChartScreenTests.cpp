#include "pch.h"

#include "ChartScreen.h"
#include "GameRig.h"
#include "Replay.h"

#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The keys the tests press, as scan codes.
constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_BACKSPACE = 0x0E;
constexpr std::uint8_t SCAN_R = 0x13;
constexpr std::uint8_t SCAN_ENTER = 0x1C;
constexpr std::uint8_t SCAN_D = 0x20;
constexpr std::uint8_t SCAN_F = 0x21;
constexpr std::uint8_t SCAN_SPACE = 0x39;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_KEYPAD_5 = 0x4C;

// Systems of the first galaxy, by index.
constexpr std::uint8_t LAVE = 7;
constexpr std::uint8_t RIEDQUAT = 46;
constexpr std::uint8_t LEESTI = 55;

// _key pressed on a chart, Find open (_finding) or not.
[[nodiscard]] Elite::ChartInput Key(std::uint8_t _scanCode, bool _finding)
{
  return Elite::ChartKey(Elite::KeyPress{.taken = true, .scanCode = _scanCode, .shift = false}, _finding);
}

// _screen with _action, and _character for Type, applied.
[[nodiscard]] Elite::ChartScreen Step(const Elite::ChartScreen& _screen, Elite::ChartAction _action, char _character = 0)
{
  return Elite::StepFind(_screen, Elite::ChartInput{.action = _action, .character = static_cast<std::uint8_t>(_character)});
}

// _steps, in Replay.h's syntax, played on _rig; each must run to its end.
void Play(GameRig& _rig, std::string_view _steps)
{
  std::vector<Elite::Step> steps;
  std::string error;
  Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), std::wstring(error.begin(), error.end()).c_str());
  Elite::ReplayPlayer player(_rig.Host(), _rig.State());
  for (const Elite::Step& step : steps)
  {
    Elite::Checkpoint checkpoint;
    Assert::IsTrue(player.Play(step, checkpoint) == Elite::ExpectedStop(step), L"the step runs");
  }
}

// The chart shown, which must be.
[[nodiscard]] const Elite::ChartView& Shown(GameRig& _rig)
{
  const Elite::ChartView* const view = _rig.Host().ShownChart();
  Assert::IsTrue(view != nullptr, L"a chart is shown");
  static const Elite::ChartView NONE;
  return view != nullptr ? *view : NONE;
}

// From power-on to the status screen at Lave, as a new game docks.
constexpr std::string_view TO_THE_STATUS_SCREEN = "wait 3\nkey space; wait 4";

} // namespace

// The galactic and the short-range charts, D04 to D07 (D82, D98 to D100, D109 to D111): their keys and Find as values, and the
// charts as the game runs them, docked and in flight.
TEST_CLASS(ChartScreenTests)
{
public:
  // Space and keypad 5 recentre (D98), D finds the nearest system and F opens Find; Esc and the F-keys are the screens'. In Find,
  // its characters, Backspace, Enter and Esc, and nothing else.
  TEST_METHOD(TheKeysAreTheFootersKeys)
  {
    Assert::IsTrue(Key(SCAN_SPACE, false).action == Elite::ChartAction::Recenter, L"Space");
    Assert::IsTrue(Key(SCAN_KEYPAD_5, false).action == Elite::ChartAction::Recenter, L"keypad 5");
    Assert::IsTrue(Key(SCAN_D, false).action == Elite::ChartAction::Nearest, L"D");
    Assert::IsTrue(Key(SCAN_F, false).action == Elite::ChartAction::OpenFind, L"F");
    Assert::IsTrue(Key(SCAN_ESCAPE, false).action == Elite::ChartAction::None, L"Esc is the screens'");
    Assert::IsTrue(Key(SCAN_F1, false).action == Elite::ChartAction::None, L"and F1");

    Assert::IsTrue(Key(SCAN_ESCAPE, true).action == Elite::ChartAction::CloseFind, L"Esc closes Find");
    Assert::IsTrue(Key(SCAN_ENTER, true).action == Elite::ChartAction::Submit, L"Enter");
    Assert::IsTrue(Key(SCAN_BACKSPACE, true).action == Elite::ChartAction::Erase, L"Backspace");
    const Elite::ChartInput typed = Key(SCAN_R, true);
    Assert::IsTrue(typed.action == Elite::ChartAction::Type && typed.character == 'r', L"a letter is typed");
    Assert::IsTrue(Key(SCAN_D, true).action == Elite::ChartAction::Type, L"D is a letter in Find");
    Assert::IsTrue(Key(SCAN_F1, true).action == Elite::ChartAction::None, L"F1 does nothing in Find");
  }

  // Find opens empty, takes eight letters and no more, erases one at a time and no further than empty, and Esc closes it.
  TEST_METHOD(FindTakesAName)
  {
    Elite::ChartScreen screen = Step(Elite::ChartScreen{}, Elite::ChartAction::OpenFind);
    Assert::IsTrue(screen.find == Elite::FindState::Typing && screen.typed.empty(), L"opened, empty");
    for (const char letter : std::string_view("riedquatx"))
    {
      screen = Step(screen, Elite::ChartAction::Type, letter);
    }
    Assert::AreEqual("riedquat", screen.typed.c_str(), L"eight letters");
    screen = Step(screen, Elite::ChartAction::Erase);
    Assert::AreEqual("riedqua", screen.typed.c_str(), L"one erased");
    for (int erase = 0; erase < 10; ++erase)
    {
      screen = Step(screen, Elite::ChartAction::Erase);
    }
    Assert::IsTrue(screen.typed.empty() && screen.find == Elite::FindState::Typing, L"erased to empty, and still open");
    screen = Step(Step(screen, Elite::ChartAction::Type, 'l'), Elite::ChartAction::CloseFind);
    Assert::IsTrue(screen.find == Elite::FindState::Closed && screen.typed.empty(), L"Esc closes it");
  }

  // F4 opens the galactic chart at Lave with its 256 systems and the fuel's ring, 7.0 light years. A click moves the cursor, and
  // the target is the system nearest it, with its range, while the game's own selection waits for the chart to close (R9).
  // Space brings the cursor back, and Esc closes the chart, selecting the system at the cursor.
  TEST_METHOD(TheGalacticChartRunsDocked)
  {
    GameRig rig("GalacticChart");
    Play(rig, TO_THE_STATUS_SCREEN);
    Play(rig, "key F4; wait 0.2");
    const Elite::ChartView& opened = Shown(rig);
    Assert::IsTrue(opened.kind == Elite::ChartKind::Galactic && !opened.flight, L"the galactic chart, docked");
    Assert::AreEqual(std::size_t{256}, opened.stars.size(), L"every system");
    Assert::IsTrue(opened.currentX == 20 && opened.currentRow == 86 && opened.cursorX == 20 && opened.cursorRow == 86, L"at Lave");
    Assert::AreEqual(std::uint8_t{70}, opened.reachTenths, L"a full tank reaches 7.0 light years");
    Assert::AreEqual(17.5f, opened.ringRadius, L"in the chart's units, four to a light year");
    Assert::AreEqual(LAVE, opened.target.facts.index, L"Lave, the system at the cursor");
    Assert::IsFalse(opened.target.inRange, L"which is no jump");

    Play(rig, "click 13 93; wait 0.05");
    const Elite::ChartView& leesti = Shown(rig);
    Assert::IsTrue(leesti.cursorX == 13 && leesti.cursorRow == 93, L"the cursor where it was clicked");
    Assert::AreEqual(LEESTI, leesti.target.facts.index, L"Leesti, the nearest");
    Assert::AreEqual(std::uint8_t{10}, leesti.target.facts.techLevel, L"of tech level 10, as shown");
    Assert::IsTrue(leesti.target.inRange && leesti.target.shortTenths == 0, L"in range");
    Assert::AreEqual(LAVE, rig.State().Data().commander.selectedSystem.index, L"nothing selected while the chart is open");

    Play(rig, "click 200 40; wait 0.05");
    const Elite::ChartView& far = Shown(rig);
    Assert::IsFalse(far.target.inRange, L"a system across the galaxy is out of range");
    Assert::AreEqual(static_cast<std::uint16_t>(far.target.facts.distanceTenths - 70), far.target.shortTenths, L"by its distance less 7.0");

    Play(rig, "click 255 255; wait 0.05");
    Assert::IsTrue(Shown(rig).cursorRow < 128, L"a click below the chart is held to its rows");

    Play(rig, "click 13 93; wait 0.05\nkey space; wait 0.05");
    Assert::IsTrue(Shown(rig).cursorX == 20 && Shown(rig).cursorRow == 86, L"Space brings it back to Lave");
    Play(rig, "click 13 93; wait 0.05\nkey Escape; wait 0.3");
    Assert::IsTrue(rig.Host().ShownChart() == nullptr, L"Esc closes it");
    Assert::AreEqual(LEESTI, rig.State().Data().commander.selectedSystem.index, L"and selects Leesti");
    rig.Host().Click(Elite::ChartClick{.x = 1, .row = 1});
    Assert::IsFalse(rig.Host().TakeChartClick().has_value(), L"a click with no chart is dropped");
  }

  // F5 opens the short-range chart about Lave, its systems named. Find takes a name on the chart and moves the cursor onto it;
  // a name off the chart leaves it, saying so, until a click; D the system nearest the cursor.
  TEST_METHOD(TheShortRangeChartFindsByName)
  {
    GameRig rig("ShortRangeChart");
    Play(rig, TO_THE_STATUS_SCREEN);
    Play(rig, "key F5; wait 0.2");
    const Elite::ChartView& opened = Shown(rig);
    Assert::IsTrue(opened.kind == Elite::ChartKind::ShortRange, L"the short-range chart");
    Assert::IsTrue(opened.currentX == 80 && opened.currentRow == 64, L"Lave at its centre");
    Assert::AreEqual(61.25f, opened.ringRadius, L"7.0 light years, 3.5 of its units to a galaxy unit");
    Assert::IsTrue(opened.stars.size() > 1, L"Lave and its neighbors");
    for (const Elite::ChartStar& star : opened.stars)
    {
      Assert::IsFalse(star.name.empty(), L"each named");
    }

    Play(rig, "key f; wait 0.05; key r; key i; key e; key d; wait 0.05");
    Assert::IsTrue(Shown(rig).find == Elite::FindState::Typing && Shown(rig).typed == "ried", L"typing");
    Play(rig, "key q; key u; key a; key t; key Return; wait 0.05");
    const Elite::ChartView& found = Shown(rig);
    Assert::IsTrue(found.find == Elite::FindState::Closed, L"found, Find closed");
    Assert::AreEqual(RIEDQUAT, found.target.facts.index, L"Riedquat");
    Assert::IsTrue(found.cursorX == found.target.x && found.cursorRow == found.target.row, L"the cursor on it");
    Assert::IsTrue(found.target.x == 20 && found.target.row == 78, L"where the chart shows it");
    Assert::AreEqual(std::uint16_t{68}, found.target.facts.distanceTenths, L"6.8 light years");

    Play(rig, "key f; wait 0.05; key t; key i; key b; key e; key d; key i; key e; key d; key Return; wait 0.05");
    Assert::IsTrue(Shown(rig).find == Elite::FindState::NotOnChart && Shown(rig).typed == "tibedied", L"Tibedied is not on this chart");
    Assert::AreEqual(RIEDQUAT, Shown(rig).target.facts.index, L"and the cursor stays");
    Play(rig, "click 80 64; wait 0.05");
    Assert::IsTrue(Shown(rig).find == Elite::FindState::Closed, L"a click clears it");
    Play(rig, "click 22 76; wait 0.05\nkey d; wait 0.05");
    Assert::IsTrue(Shown(rig).cursorX == 20 && Shown(rig).cursorRow == 78, L"D, the cursor onto the nearest");

    Play(rig, "key F1; wait 1");
    Assert::IsTrue(rig.Host().ShownChart() == nullptr, L"F1 closes it");
    Assert::AreEqual(RIEDQUAT, rig.State().Data().commander.selectedSystem.index, L"Riedquat selected for the jump");
  }

  // The two charts share the system selected (D124): Riedquat picked on the galactic chart is the short-range chart's target as it
  // opens, its cursor on it, and closing it unmoved keeps it; a click there picks anew.
  TEST_METHOD(TheChartsShareTheSelectedSystem)
  {
    GameRig rig("SharedSelection");
    Play(rig, TO_THE_STATUS_SCREEN);
    Play(rig, "key F4; wait 0.2\nclick 4 89; wait 0.05\nkey F5; wait 0.2");
    Assert::AreEqual(RIEDQUAT, rig.State().Data().commander.selectedSystem.index, L"Riedquat selected on the galactic chart");
    const Elite::ChartView& opened = Shown(rig);
    Assert::IsTrue(opened.kind == Elite::ChartKind::ShortRange, L"the short-range chart");
    Assert::AreEqual(RIEDQUAT, opened.target.facts.index, L"its target Riedquat");
    Assert::IsTrue(opened.cursorX == opened.target.x && opened.cursorRow == opened.target.row, L"the cursor on it");
    Play(rig, "key F4; wait 0.2");
    Assert::AreEqual(RIEDQUAT, rig.State().Data().commander.selectedSystem.index, L"closed unmoved: still Riedquat");
    Assert::AreEqual(RIEDQUAT, Shown(rig).target.facts.index, L"and the galactic chart's target");
    Play(rig, "key F5; wait 0.2\nclick 80 64; wait 0.05\nkey F6; wait 0.3");
    Assert::AreEqual(LAVE, rig.State().Data().commander.selectedSystem.index, L"a click picks anew");
  }

  // In flight, once the countdown runs, F6 shows the target locked: its countdown, the latched system whatever the cursor, and
  // closing the chart selects nothing (D07).
  TEST_METHOD(TheTargetLocksInFlight)
  {
    GameRig rig("LockedChart");
    Play(rig, TO_THE_STATUS_SCREEN);
    Play(rig, "key F5; wait 0.2\nclick 20 78; wait 0.05\nkey F1; wait 8\ndown h; wait 0.2; up h; wait 0.3");
    Assert::AreNotEqual(std::uint8_t{0}, rig.State().Data().jump.hyperspaceCountdown, L"the countdown runs");
    Play(rig, "down F6; wait 0.1; up F6; wait 0.3");
    const Elite::ChartView& locked = Shown(rig);
    Assert::IsTrue(locked.flight && locked.locked && locked.countdown != 0, L"locked, counting down");
    Assert::AreEqual(RIEDQUAT, locked.target.facts.index, L"on Riedquat");
    Play(rig, "click 80 64; wait 0.05");
    Assert::IsTrue(Shown(rig).cursorX == 80 && Shown(rig).target.facts.index == RIEDQUAT, L"the cursor moves, the target holds");
    Play(rig, "key F1; wait 0.3");
    Assert::IsTrue(rig.Host().ShownChart() == nullptr, L"F1 back to the front view");
    Assert::AreEqual(RIEDQUAT, rig.State().Data().commander.selectedSystem.index, L"nothing selected while the countdown runs");
  }
};

} // namespace GameLogicTests
