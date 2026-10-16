#include "pch.h"

#include "DockedKeys.h"
#include "GameRig.h"
#include "Replay.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_A = 0x1E;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F8 = 0x42;
constexpr std::uint8_t SCAN_F9 = 0x43;
constexpr std::uint8_t SCAN_F10 = 0x44;

// Each key the docked host shows a screen for, as the key bar names them (D96), and F7 until the charts take it in.
struct KeyedScreen
{
  std::uint8_t scanCode;
  Elite::DockedScreen screen;
};

constexpr std::array<KeyedScreen, 7> KEYED_SCREENS = {{
  {SCAN_ESCAPE, Elite::DockedScreen::Disc},
  {0x3C, Elite::DockedScreen::Market},
  {0x3D, Elite::DockedScreen::Equip},
  {0x3E, Elite::DockedScreen::Galaxy},
  {0x3F, Elite::DockedScreen::Local},
  {0x40, Elite::DockedScreen::Status},
  {0x41, Elite::DockedScreen::Data},
}};

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

// What the screen shows now, as its picture's digest.
[[nodiscard]] std::string Picture(GameRig& _rig)
{
  return Elite::TakeCheckpoint(_rig.State(), _rig.Host().Video()).picture;
}

[[nodiscard]] bool Shows(GameRig& _rig, Elite::DockedScreen _screen)
{
  return _rig.Host().ShownDockedScreen() == _screen;
}

} // namespace

// The docked host's keys (D96): the key bar's F1 to F6 and Esc, F7 until the charts take it in, and F8, F9 and F10, which do
// nothing docked; and where a screen is closed by them.
TEST_CLASS(DockedKeysTests)
{
public:
  // Every key that shows a screen shows the one the key bar names, and is the key that screen is shown by; F1 launches; F8, F9,
  // F10 and every other key do nothing.
  TEST_METHOD(TheKeysAreTheKeyBars)
  {
    for (const KeyedScreen& keyed : KEYED_SCREENS)
    {
      const Elite::DockedTarget target = Elite::DockedTargetOf(keyed.scanCode);
      Assert::IsTrue(!target.launches && target.screen == keyed.screen, L"the screen its key shows");
      Assert::AreEqual(keyed.scanCode, Elite::DockedKeyOf(keyed.screen), L"the key that shows it");
    }
    const Elite::DockedTarget launch = Elite::DockedTargetOf(SCAN_F1);
    Assert::IsTrue(launch.launches && !launch.screen.has_value(), L"F1 launches");
    for (const std::uint8_t idle : {SCAN_F8, SCAN_F9, SCAN_F10, SCAN_A, std::uint8_t{0}})
    {
      const Elite::DockedTarget target = Elite::DockedTargetOf(idle);
      Assert::IsTrue(!target.launches && !target.screen.has_value(), L"nothing");
    }
  }

  // Docked, a screen closes on the keys the docked host acts on; in flight, on Esc and every F-key, as in the original.
  TEST_METHOD(AScreenClosesOnTheKeysOfItsPlace)
  {
    for (std::uint8_t key = SCAN_F1; key <= SCAN_F10; ++key)
    {
      const bool acted = key != SCAN_F8 && key != SCAN_F9 && key != SCAN_F10;
      Assert::AreEqual(acted, Elite::ClosesScreen(key, Elite::ScreenPlace::Docked), L"docked");
      Assert::IsTrue(Elite::ClosesScreen(key, Elite::ScreenPlace::Flight), L"in flight");
    }
    Assert::IsTrue(
      Elite::ClosesScreen(SCAN_ESCAPE, Elite::ScreenPlace::Docked) && Elite::ClosesScreen(SCAN_ESCAPE, Elite::ScreenPlace::Flight), L"Esc");
    Assert::IsFalse(Elite::ClosesScreen(SCAN_A, Elite::ScreenPlace::Docked) || Elite::ClosesScreen(SCAN_A, Elite::ScreenPlace::Flight),
                    L"a letter");
  }

  // The docked host as the game runs it: from the status screen, each key shows its screen and the host says which; F8, F9 and
  // F10 leave the screen as it is; F1 launches, and the host shows none.
  TEST_METHOD(TheDockedHostShowsEachScreenByItsKey)
  {
    GameRig rig("DockedKeys");
    Assert::IsFalse(rig.Host().ShownDockedScreen().has_value(), L"none before the dock");
    Play(rig, "wait 3\nkey space; wait 4");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Status), L"the status screen first");

    const std::string status = Picture(rig);
    Play(rig, "key F8; wait 0.3\nkey F9; wait 0.3\nkey F10; wait 0.3");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Status), L"F8, F9 and F10 show no other screen");
    Assert::AreEqual(status, Picture(rig), L"and leave the status screen as it was");

    Play(rig, "key F3; wait 0.8");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Equip), L"F3, the equipment");
    const std::string equip = Picture(rig);
    Play(rig, "key F8; wait 0.3\nkey F9; wait 0.3\nkey F10; wait 0.3");
    Assert::AreEqual(equip, Picture(rig), L"F8, F9 and F10 do nothing on the equipment either");

    Play(rig, "key F4; wait 0.5");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Galaxy), L"F4, the galactic chart");
    Play(rig, "key F5; wait 0.5");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Local), L"F5, the short-range chart");
    Play(rig, "key F7; wait 0.5");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Data), L"F7, Data on System, until the charts take it in");
    Play(rig, "key Escape; wait 0.5");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Disc), L"Esc, the disc menu");
    const std::string disc = Picture(rig);
    Play(rig, "key F8; wait 0.3\nkey F9; wait 0.3\nkey F10; wait 0.3");
    Assert::AreEqual(disc, Picture(rig), L"F8, F9 and F10 do nothing on the disc menu");
    Play(rig, "key F2; wait 0.3");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Market) && rig.Host().ShownMarket() != nullptr, L"F2, the market");
    Play(rig, "key F6; wait 0.5");
    Assert::IsTrue(Shows(rig, Elite::DockedScreen::Status), L"F6, the status screen");

    Play(rig, "key F1; wait 2");
    Assert::IsFalse(rig.Host().ShownDockedScreen().has_value(), L"F1 launches, and no docked screen is shown");
    Assert::AreEqual(std::uint8_t{1}, rig.State().Data().flow.inFlight, L"in flight");
  }
};

} // namespace GameLogicTests
