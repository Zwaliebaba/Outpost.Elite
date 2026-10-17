#include "pch.h"

#include "Docked.h"
#include "GameRig.h"
#include "Replay.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The supernova's refugees, a full hold of them, which its debriefing pays 1000.0 credits for.
constexpr std::uint8_t REFUGEE_TONNES = 0x14;
constexpr std::uint32_t FULL_HOLD_REWARD_TENTHS = 10000;

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

// D09 shown, which must be.
[[nodiscard]] const Elite::BriefingView& Shown(GameRig& _rig)
{
  const Elite::BriefingView* const view = _rig.Host().ShownBriefing();
  Assert::IsTrue(view != nullptr, L"D09 is shown");
  static const Elite::BriefingView NONE;
  return view != nullptr ? *view : NONE;
}

// From power-on to the status screen at Lave, as a new game docks.
constexpr std::string_view TO_THE_STATUS_SCREEN = "wait 3\nkey space; wait 4";

// The status screen again, which ignores its own F6 (D96): the market first.
constexpr std::string_view STATUS_AGAIN = "key F2; wait 0.3\nkey F6; wait 0.5";

} // namespace

// D09, a mission's briefing and debriefing over D08 (ADR-068): the game's text as paragraphs, and the view the original routines
// publish while they wait for their key, which moves nothing of the game's.
TEST_CLASS(BriefingViewTests)
{
public:
  // The lines between blank ones join into a paragraph, by a space or after a hyphen by nothing, and "Press any key to continue!"
  // is left out.
  TEST_METHOD(TheLinesJoinIntoParagraphs)
  {
    constexpr std::array<std::string_view, 6> LINES = {
      "Intelligence reports that a near-", "by system has been invaded!", " ", "Will you help us (Y/N)?", " ",
      "Press any key to continue!"};
    const std::vector<std::string> paragraphs = Elite::BriefingParagraphs(LINES);
    Assert::AreEqual(std::size_t{2}, paragraphs.size(), L"two paragraphs");
    Assert::AreEqual("Intelligence reports that a near-by system has been invaded!", paragraphs[0].c_str(), L"joined");
    Assert::AreEqual("Will you help us (Y/N)?", paragraphs[1].c_str(), L"the question");
  }

  // The supernova's briefing asks Y or N over D08; Y takes the refugees and the reply asks only to be acknowledged; Space closes
  // it, and D08 shows. Its debriefing, with a full hold of refugees, pays 1000.0 credits.
  TEST_METHOD(TheSupernovaAsksThenPays)
  {
    GameRig rig("BriefingSupernova");
    Play(rig, TO_THE_STATUS_SCREEN);
    rig.State().Data().commander.missionNumber = 1;
    rig.State().Data().flight.playerDocked = 1;
    Play(rig, STATUS_AGAIN);
    const Elite::BriefingView& asked = Shown(rig);
    Assert::IsTrue(asked.mission == 1 && asked.kind == Elite::BriefingKind::Briefing, L"mission 1's briefing");
    Assert::IsTrue(asked.answer == Elite::BriefingAnswer::YesOrNo, L"which asks Y or N");
    Assert::AreEqual(std::size_t{2}, asked.paragraphs.size(), L"the news and the question");
    Assert::AreEqual("Our sun is going supernova! You are advised to hyper-space out of this system immediately!",
                     asked.paragraphs[0].c_str());
    Assert::IsTrue(asked.status.commanderName == "JAMESON", L"over D08");
    Assert::IsTrue(rig.Host().ShownStatus() == nullptr, L"which waits for it");

    Play(rig, "key space; wait 0.2");
    Assert::IsTrue(Shown(rig).answer == Elite::BriefingAnswer::YesOrNo, L"Space is no answer");
    Play(rig, "key y; wait 0.2");
    const Elite::BriefingView& accepted = Shown(rig);
    Assert::IsTrue(accepted.answer == Elite::BriefingAnswer::Acknowledge, L"answered");
    Assert::AreEqual(std::size_t{3}, accepted.paragraphs.size(), L"and the reply after the question");
    Assert::IsTrue(accepted.paragraphs[2].starts_with("Thank you! I'm afraid that we've had to dump any cargo"), L"Thank you!");
    Assert::AreEqual(REFUGEE_TONNES, accepted.status.refugeesTonnes, L"D08 under it holds the refugees");

    Play(rig, "key space; wait 0.5");
    Assert::IsTrue(rig.Host().ShownBriefing() == nullptr && rig.Host().ShownStatus() != nullptr, L"acknowledged: D08");

    rig.State().Data().commander.jumpedSinceBriefing = 1;
    Play(rig, STATUS_AGAIN);
    const Elite::BriefingView& paid = Shown(rig);
    Assert::IsTrue(paid.kind == Elite::BriefingKind::Debriefing && paid.reward == Elite::BriefingReward::Credits, L"paid");
    Assert::AreEqual(FULL_HOLD_REWARD_TENTHS, paid.rewardTenths, L"1000.0 credits for the full hold");
    Assert::AreEqual("Thank you for saving us. Please accept 1000 Credits!", paid.paragraphs.front().c_str());
    Assert::AreEqual(std::uint32_t{1000} + FULL_HOLD_REWARD_TENTHS, paid.status.creditsTenths, L"D08 with them");
    Play(rig, "key space; wait 0.5");
    Assert::IsTrue(rig.Host().ShownBriefing() == nullptr, L"acknowledged");
    Assert::AreEqual(std::uint8_t{0}, rig.State().Data().commander.missionNumber, L"and the mission over");
  }

  // The mask ship's debriefing with the device recovered fits it; the invasion's gives the anti-ECM and the title.
  TEST_METHOD(TheDebriefingsSayWhatTheyGive)
  {
    GameRig rig("BriefingRewards");
    Play(rig, TO_THE_STATUS_SCREEN);
    Elite::CommanderState& commander = rig.State().Data().commander;
    commander.missionNumber = 2;
    commander.missionStage = 1;
    commander.maskShipDestroyed = 1;
    commander.maskingDeviceRecovered = 1;
    rig.State().Data().flight.playerDocked = 1;
    Play(rig, STATUS_AGAIN);
    Assert::IsTrue(Shown(rig).mission == 2 && Shown(rig).reward == Elite::BriefingReward::MaskingDevice, L"the masking device");
    Assert::AreEqual("This masking device looks very interesting. We will fit a modified version to your space craft as a reward! "
                     "Press 'N' to activate it but be careful as it uses a lot of energy!",
                     Shown(rig).paragraphs.front().c_str());
    Play(rig, "key space; wait 0.5");

    commander.missionNumber = 3;
    Play(rig, STATUS_AGAIN);
    Assert::IsTrue(Shown(rig).kind == Elite::BriefingKind::Briefing && Shown(rig).reward == Elite::BriefingReward::None, L"briefed");
    Play(rig, "key space; wait 0.5");
    Play(rig, STATUS_AGAIN);
    const Elite::BriefingView& done = Shown(rig);
    Assert::IsTrue(done.kind == Elite::BriefingKind::Debriefing && done.reward == Elite::BriefingReward::AntiEcmAndArchangel,
                   L"the anti-ECM and Archangel");
    Assert::AreEqual(std::size_t{1}, done.paragraphs.size(), L"the thanks and what they fitted, as one");
    Assert::IsTrue(done.paragraphs.front().ends_with("awarded you the title Archangel!"), L"to its last line");
    Play(rig, "key space; wait 0.5");
  }
};

} // namespace GameLogicTests
