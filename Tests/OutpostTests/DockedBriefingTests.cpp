#include "pch.h"

#include "BriefingView.h"
#include "Canvas.h"
#include "DockedBriefing.h"
#include "GoldenImage.h"
#include "Renderer.h"

#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace OutpostTests
{

namespace
{

constexpr std::uint32_t DESIGN_WIDTH_PIXELS = 1920;
constexpr std::uint32_t DESIGN_HEIGHT_PIXELS = 1080;
constexpr std::uint32_t BACKGROUND = 0x070605;

// The supernova's briefing at Lave over Jameson's status, as the game publishes it: its news and its question, Y or N.
[[nodiscard]] Elite::BriefingView SupernovaBriefing()
{
  Elite::BriefingView view;
  view.sequence = 1;
  view.status.sequence = 1;
  view.status.systemName = "LAVE";
  view.status.title = "COMMANDER";
  view.status.commanderName = "JAMESON";
  view.status.presentSystem = "LAVE";
  view.status.hyperspaceTarget = "DISO";
  view.status.fuelTenths = 70;
  view.status.creditsTenths = 12500;
  view.status.equipment = {Elite::FittedItem{.kind = Elite::FittedKind::Missiles, .count = 3, .mounts = 0},
                           Elite::FittedItem{.kind = Elite::FittedKind::PulseLaser, .count = 1, .mounts = Elite::MOUNT_FORE}};
  view.status.bayTonnes = 20;
  view.mission = 1;
  view.kind = Elite::BriefingKind::Briefing;
  view.paragraphs = {"Our sun is going supernova! You are advised to hyper-space out of this system immediately!",
                     "Will you help us (Y/N)?"};
  view.answer = Elite::BriefingAnswer::YesOrNo;
  return view;
}

// The invasion's briefing, as the game words it.
[[nodiscard]] Elite::BriefingView InvasionBriefing()
{
  Elite::BriefingView view = SupernovaBriefing();
  view.mission = 3;
  view.paragraphs = {"Intelligence reports that a near-by system has been invaded by alien thargoid ships and that the coriolis "
                     "orbiting space station is under attack. We suggest that you destroy the space station and not try to dock with "
                     "it if you come across heavy alien concentrations in that system!"};
  view.answer = Elite::BriefingAnswer::Acknowledge;
  return view;
}

// The supernova's debriefing with a full hold of refugees: 1000.0 credits.
[[nodiscard]] Elite::BriefingView SupernovaDebriefing()
{
  Elite::BriefingView view = SupernovaBriefing();
  view.kind = Elite::BriefingKind::Debriefing;
  view.paragraphs = {"Thank you for saving us. Please accept 1000 Credits!"};
  view.answer = Elite::BriefingAnswer::Acknowledge;
  view.reward = Elite::BriefingReward::Credits;
  view.rewardTenths = 10000;
  view.status.creditsTenths = 22500;
  return view;
}

// _view drawn on WARP at the design's size, over D08.
[[nodiscard]] GoldenImage::Picture Drawn(const Elite::BriefingView& _view)
{
  Engine::Renderer renderer;
  Assert::IsTrue(renderer.Create(Engine::Renderer::Desc{.warp = true}), L"WARP");
  Assert::IsTrue(renderer.HasBahnschrift(), L"Bahnschrift installed: the design's face");
  Engine::Canvas canvas;
  const Outpost::TextMeasure measure = [&renderer](const Engine::CanvasText& _text) { return renderer.MeasureText(_text); };
  Outpost::DockedBriefing::Draw(_view, std::nullopt, measure, canvas);
  GoldenImage::Picture picture{.width = DESIGN_WIDTH_PIXELS,
                               .height = DESIGN_HEIGHT_PIXELS,
                               .pixels = renderer.Capture(BACKGROUND, DESIGN_WIDTH_PIXELS, DESIGN_HEIGHT_PIXELS, nullptr, &canvas)};
  Assert::AreEqual(std::size_t{DESIGN_WIDTH_PIXELS} * DESIGN_HEIGHT_PIXELS, picture.pixels.size(), L"the whole picture captured");
  return picture;
}

} // namespace

// D09, a mission's briefing and debriefing, as the shell draws it (ADR-068, D82, D101, D105): on WARP, against its golden images
// (§6.5, D94); the keys its buttons send; and its words.
TEST_CLASS(DockedBriefingTests)
{
public:
  // The supernova's question, Y or N, which D09's drawing does not show.
  TEST_METHOD(TheSupernovaAsks)
  {
    GoldenImage::Check("D09-Supernova-Briefing", Drawn(SupernovaBriefing()));
  }

  // The invasion's briefing, in D09's layout with this build's mission (D101).
  TEST_METHOD(TheInvasionBriefing)
  {
    GoldenImage::Check("D09-Invasion-Briefing", Drawn(InvasionBriefing()));
  }

  // The supernova's debriefing, with what it pays.
  TEST_METHOD(TheSupernovaDebriefing)
  {
    GoldenImage::Check("D09-Supernova-Debriefing", Drawn(SupernovaDebriefing()));
  }

  // A click on a button sends its key: Y and N while the supernova asks, Space otherwise; nothing elsewhere.
  TEST_METHOD(AClickSendsTheButtonsKey)
  {
    using Outpost::DockedBriefing;
    const Elite::BriefingView asks = SupernovaBriefing();
    Assert::AreEqual(std::uint8_t{0x15}, DockedBriefing::ScanCodeAt(asks, Engine::Point{.x = 1300.0f, .y = 865.0f}).value_or(0), L"Y");
    Assert::AreEqual(std::uint8_t{0x31}, DockedBriefing::ScanCodeAt(asks, Engine::Point{.x = 1000.0f, .y = 865.0f}).value_or(0), L"N");
    Assert::IsFalse(DockedBriefing::ScanCodeAt(asks, Engine::Point{.x = 700.0f, .y = 500.0f}).has_value(), L"the text");
    const Elite::BriefingView acknowledges = InvasionBriefing();
    Assert::AreEqual(std::uint8_t{0x39}, DockedBriefing::ScanCodeAt(acknowledges, Engine::Point{.x = 1300.0f, .y = 865.0f}).value_or(0),
                     L"Space");
    Assert::IsFalse(DockedBriefing::ScanCodeAt(acknowledges, Engine::Point{.x = 1000.0f, .y = 865.0f}).has_value(), L"no N");
  }

  // The titles and senders the game's texts do not give, and what each debriefing gives.
  TEST_METHOD(TheWordsAreTheBriefings)
  {
    using Outpost::DockedBriefing;
    Elite::BriefingView view = SupernovaBriefing();
    Assert::AreEqual(std::wstring(L"Supernova"), DockedBriefing::TitleOf(view));
    Assert::AreEqual(std::wstring(L"THE PEOPLE OF LAVE"), DockedBriefing::SenderOf(view));
    Assert::AreEqual(std::wstring(), DockedBriefing::RewardOf(view), L"a briefing gives nothing");
    view = SupernovaDebriefing();
    Assert::AreEqual(std::wstring(L"1000.0 credits"), DockedBriefing::RewardOf(view));
    view.mission = 2;
    view.reward = Elite::BriefingReward::MaskingDevice;
    Assert::AreEqual(std::wstring(L"The masking device"), DockedBriefing::TitleOf(view));
    Assert::AreEqual(std::wstring(L"NAVY INTELLIGENCE"), DockedBriefing::SenderOf(view));
    Assert::AreEqual(std::wstring(L"Masking device fitted"), DockedBriefing::RewardOf(view));
    view.reward = Elite::BriefingReward::None;
    Assert::AreEqual(std::wstring(), DockedBriefing::RewardOf(view), L"the ship destroyed or fled from gives nothing");
  }
};

} // namespace OutpostTests
