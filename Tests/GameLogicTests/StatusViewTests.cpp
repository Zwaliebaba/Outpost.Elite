#include "pch.h"

#include "Docked.h"
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

constexpr std::uint8_t FOOD = 0;
constexpr std::uint8_t GEM_STONES = 15;

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

// From power-on to the status screen at Lave, as a new game docks.
constexpr std::string_view TO_THE_STATUS_SCREEN = "wait 3\nkey space; wait 4";

} // namespace

// D08, the commander's status (ADR-063): the view the status screen publishes while docked, with the legal status, the rating,
// the equipment with each laser's mounts and the cargo as the original's status screen and inventory print them.
TEST_CLASS(StatusViewTests)
{
public:
  // The rating is the first of ratingThresholds above the kills: 2, 4, 9, 20, 35, 90, 155 and 230 kills begin the next.
  TEST_METHOD(TheRatingIsTheFirstThresholdAboveTheKills)
  {
    struct Case
    {
      std::uint8_t kills;
      std::uint8_t rating;
    };
    const std::vector<Case> cases = {{0, 0},  {1, 0},  {2, 1},  {3, 1},  {4, 2},   {8, 2},   {9, 3},   {19, 3},  {20, 4},
                                     {34, 4}, {35, 5}, {89, 5}, {90, 6}, {154, 6}, {155, 7}, {229, 7}, {230, 8}, {254, 8}};
    for (const auto& [kills, rating] : cases)
    {
      Assert::AreEqual(rating, Elite::RatingOf(kills), std::to_wstring(kills).c_str());
    }
  }

  // Clean below 1, Offender below 40, Fugitive from 40.
  TEST_METHOD(TheLegalStatusHasThreeNames)
  {
    Assert::IsTrue(Elite::LegalStandingOf(0) == Elite::LegalStanding::Clean, L"0");
    Assert::IsTrue(Elite::LegalStandingOf(1) == Elite::LegalStanding::Offender, L"1");
    Assert::IsTrue(Elite::LegalStandingOf(39) == Elite::LegalStanding::Offender, L"39");
    Assert::IsTrue(Elite::LegalStandingOf(40) == Elite::LegalStanding::Fugitive, L"40");
    Assert::IsTrue(Elite::LegalStandingOf(255) == Elite::LegalStanding::Fugitive, L"255");
  }

  // A new game at Lave: Jameson, Clean and Harmless, with 7.0 light years, 100.0 credits, three missiles and a pulse laser on
  // the fore mount, and an empty hold of 20 tonnes.
  TEST_METHOD(ANewGameIsJamesonAtLave)
  {
    GameRig rig("StatusNewGame");
    Play(rig, TO_THE_STATUS_SCREEN);
    const Elite::StatusView view = Elite::StatusViewOf(rig.State());
    Assert::IsTrue(view.title == "COMMANDER", L"the title");
    Assert::IsTrue(view.commanderName == "JAMESON", L"the name");
    Assert::IsTrue(view.presentSystem == "LAVE" && view.systemName == "LAVE" && !view.dodo, L"Lave, a Coriolis");
    Assert::IsFalse(view.witchSpace, L"not in witch space");
    Assert::AreEqual(std::uint8_t{70}, view.fuelTenths, L"7.0 light years");
    Assert::AreEqual(1000u, view.creditsTenths, L"100.0 credits");
    Assert::IsTrue(view.legal == Elite::LegalStanding::Clean, L"Clean");
    Assert::AreEqual(std::uint8_t{0}, view.rating, L"Harmless");
    Assert::AreEqual(std::size_t{2}, view.equipment.size(), L"two items");
    Assert::IsTrue(view.equipment[0].kind == Elite::FittedKind::Missiles && view.equipment[0].count == 3, L"three missiles");
    Assert::IsTrue(view.equipment[1].kind == Elite::FittedKind::PulseLaser && view.equipment[1].mounts == Elite::MOUNT_FORE,
                   L"a pulse laser on the fore mount");
    Assert::AreEqual(std::uint8_t{0}, view.usedTonnes, L"an empty hold");
    Assert::AreEqual(std::uint8_t{20}, view.bayTonnes, L"of 20 tonnes");
  }

  // Lasers on several mounts, each listed under its own type; the extension's 35 tonnes; cargo and refugees; and the
  // statuses past Clean and Harmless.
  TEST_METHOD(EachLaserListsItsMounts)
  {
    GameRig rig("StatusLasers");
    Play(rig, TO_THE_STATUS_SCREEN);
    Elite::CommanderState& commander = rig.State().Data().commander;
    // Fore pulse (type 0), aft beam (1), right military (3), left beam (1).
    commander.laserMountsFitted = 0x0F;
    commander.laserMountTypes = static_cast<std::uint8_t>(0x0 | (0x1 << 2) | (0x3 << 4) | (0x1 << 6));
    commander.pulseLaserCount = 1;
    commander.beamLaserCount = 2;
    commander.militaryLaserCount = 1;
    commander.largeCargoBayFitted = 1;
    commander.cargoHold.lots[FOOD].held = 4;
    commander.cargoHold.lots[GEM_STONES].held = 6;
    commander.cargoHold.refugeesTonnes = 3;
    commander.cargoHold.cargoUsedTonnes = 7;
    commander.legalStatus = 50;
    commander.killCount = 100;
    const Elite::StatusView view = Elite::StatusViewOf(rig.State());
    const auto find = [&view](Elite::FittedKind _kind)
    {
      for (const Elite::FittedItem& item : view.equipment)
      {
        if (item.kind == _kind)
        {
          return item;
        }
      }
      Assert::Fail(L"the item is listed");
      return Elite::FittedItem{};
    };
    Assert::AreEqual(Elite::MOUNT_FORE, find(Elite::FittedKind::PulseLaser).mounts, L"the pulse laser fore");
    Assert::AreEqual(static_cast<std::uint8_t>(Elite::MOUNT_AFT | Elite::MOUNT_LEFT), find(Elite::FittedKind::BeamLaser).mounts,
                     L"the beam lasers aft and left");
    Assert::AreEqual(Elite::MOUNT_RIGHT, find(Elite::FittedKind::MilitaryLaser).mounts, L"the military laser right");
    Assert::AreEqual(std::uint8_t{0}, find(Elite::FittedKind::CargoBayExtension).mounts, L"no mounts for what is not a laser");
    Assert::AreEqual(std::uint8_t{35}, view.bayTonnes, L"35 tonnes with the extension");
    Assert::AreEqual(std::uint8_t{4}, view.held[FOOD], L"food");
    Assert::AreEqual(std::uint8_t{6}, view.held[GEM_STONES], L"gem-stones");
    Assert::AreEqual(std::uint8_t{3}, view.refugeesTonnes, L"refugees");
    Assert::IsTrue(view.legal == Elite::LegalStanding::Fugitive, L"a fugitive");
    Assert::AreEqual(std::uint8_t{6}, view.rating, L"Dangerous");
  }

  // Docked, the status screen publishes its view while it waits, a new one each time it opens, and takes it down as it closes;
  // in flight it publishes none.
  TEST_METHOD(TheStatusScreenShowsItsViewDocked)
  {
    GameRig rig("StatusShown");
    Play(rig, TO_THE_STATUS_SCREEN);
    const Elite::StatusView* view = rig.Host().ShownStatus();
    Assert::IsTrue(view != nullptr, L"shown on the status screen");
    const std::uint64_t first = view->sequence;
    Play(rig, "key F2; wait 0.3");
    Assert::IsTrue(rig.Host().ShownStatus() == nullptr, L"taken down for the market");
    Play(rig, "key F6; wait 0.5");
    view = rig.Host().ShownStatus();
    Assert::IsTrue(view != nullptr && view->sequence == first + 1, L"shown again, a new view");
    Play(rig, "key F1; wait 2\nkey F9; wait 0.5");
    Assert::IsTrue(rig.Host().ShownStatus() == nullptr, L"none in flight");
  }
};

} // namespace GameLogicTests
