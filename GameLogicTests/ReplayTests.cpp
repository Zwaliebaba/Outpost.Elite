#include "pch.h"

#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDigest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

TEST_CLASS(ReplayTests)
{
public:
  TEST_METHOD(EveryVerbParses)
  {
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps("wait 1.25; key F1; down Up; up Up; shot title; digest boot", steps, error), L"parses");
    Assert::AreEqual(std::size_t{6}, steps.size());
    Assert::IsTrue(steps[0].kind == Elite::StepKind::Wait);
    Assert::AreEqual(std::uint64_t{1'250}, steps[0].waitMilliseconds);
    Assert::IsTrue(steps[1].kind == Elite::StepKind::Key);
    Assert::AreEqual(0x3Bu, std::uint32_t{steps[1].scanCode});
    Assert::IsTrue(steps[2].kind == Elite::StepKind::Down);
    Assert::AreEqual(0x48u, std::uint32_t{steps[2].scanCode});
    Assert::IsTrue(steps[3].kind == Elite::StepKind::Up);
    Assert::IsTrue(steps[4].kind == Elite::StepKind::Shot);
    Assert::AreEqual("title", steps[4].name.c_str());
    Assert::IsTrue(steps[5].kind == Elite::StepKind::Digest);
    Assert::IsTrue(steps[5].expectedDigest.empty());
  }

  // A replay file: one step or several to a line, comments, blank lines, and the line each came from.
  TEST_METHOD(LinesAndCommentsAreKept)
  {
    const std::string_view text = "# the title\n"
                                  "wait 3   # it draws\n"
                                  "\n"
                                  "key space; wait 0.5\n"
                                  "digest docked 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(text, steps, error), L"parses");
    Assert::AreEqual(std::size_t{4}, steps.size());
    Assert::AreEqual(std::size_t{2}, steps[0].line);
    Assert::AreEqual(std::size_t{4}, steps[1].line);
    Assert::AreEqual(std::size_t{4}, steps[2].line);
    Assert::AreEqual(std::size_t{5}, steps[3].line);
    Assert::AreEqual("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", steps[3].expectedDigest.c_str());
  }

  // Time in a replay is whole milliseconds from its start, converted once (ReplayCycle): a thousand waits
  // of a millisecond end on the same cycle as one wait of a second, which is what lets a recorder that
  // samples time as it likes and a player that sums the waits agree to the cycle.
  TEST_METHOD(WaitsAddUpExactly)
  {
    std::vector<Elite::Step> one;
    std::vector<Elite::Step> many;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps("wait 1", one, error), L"parses");
    for (int index = 0; index < 1000; ++index)
      Assert::IsTrue(Elite::ParseSteps("wait 0.001", many, error), L"parses");
    Machine::Cycles ends[2] = {};
    for (int run = 0; run < 2; ++run)
    {
      ReferenceRig rig("Waits");
      Assert::IsTrue(rig.Loaded(), L"ELITES.EXE at the repository root");
      Elite::ReplayPlayer player(rig.Host(), rig.Program());
      std::string digest;
      for (const Elite::Step& step : run == 0 ? one : many)
        Assert::IsTrue(player.Play(step, digest) == Machine::StopReason::Reached, L"runs");
      Assert::AreEqual(std::uint64_t{1'000}, player.ElapsedMilliseconds());
      ends[run] = rig.Host().Clock();
    }
    Assert::AreEqual(ends[0], ends[1]);
    Assert::AreEqual(Elite::ReplayCycle(0, 1'000), ends[0]);
  }

  // What the shell does (ADR-008): it runs the machine to whatever millisecond the wall clock says, sends
  // keys there and records them. The recording, played back, reaches the same state.
  TEST_METHOD(ARecordedSessionPlaysBack)
  {
    struct Press
    {
      std::uint64_t milliseconds;
      std::uint8_t scanCode;
      bool down;
    };
    // Any key to start; F1 to launch; a pitch held across frames; F2 for the rear view.
    const Press presses[] = {{3'017, 0x39, true},  {3'101, 0x39, false},  {7'333, 0x3B, true},  {7'401, 0x3B, false},
                             {12'000, 0x50, true}, {12'777, 0x50, false}, {13'005, 0x3C, true}, {13'090, 0x3C, false}};
    constexpr std::uint64_t END_MILLISECONDS = 15'500;

    ReferenceRig live("RecordLive");
    Assert::IsTrue(live.Loaded(), L"ELITES.EXE at the repository root");
    Elite::ReplayRecorder recorder("a test session");
    const Machine::Cycles start = live.Host().Clock();
    std::uint64_t now = 0;
    for (const Press& press : presses)
    {
      // The wall clock moves in uneven steps between the presses.
      for (std::uint64_t step = 13; now + step < press.milliseconds; step += 7)
      {
        now += step;
        Assert::IsTrue(live.Host().RunUntil(Elite::ReplayCycle(start, now)) == Machine::StopReason::Reached, L"runs");
      }
      now = press.milliseconds;
      Assert::IsTrue(live.Host().RunUntil(Elite::ReplayCycle(start, now)) == Machine::StopReason::Reached, L"runs");
      Assert::IsTrue(recorder.Key(now, press.scanCode, press.down), L"an XT key");
      live.Host().KeyboardController().Inject(static_cast<std::uint8_t>(press.scanCode | (press.down ? 0 : 0x80)));
    }
    Assert::IsTrue(live.Host().RunUntil(Elite::ReplayCycle(start, END_MILLISECONDS)) == Machine::StopReason::Reached, L"runs");
    const std::string expected = Elite::GameStateDigest(live.Host(), live.Program());
    recorder.Digest(END_MILLISECONDS, "end", expected);

    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(recorder.Text(), steps, error), L"the recording parses");
    ReferenceRig replay("RecordReplay");
    Elite::ReplayPlayer player(replay.Host(), replay.Program());
    std::string digest;
    for (const Elite::Step& step : steps)
      Assert::IsTrue(player.Play(step, digest) == Machine::StopReason::Reached, L"plays");
    Assert::AreEqual(expected.c_str(), digest.c_str());
    Assert::AreEqual(live.Host().Clock(), replay.Host().Clock());
    Assert::IsFalse(recorder.Key(END_MILLISECONDS, 0x60, true), L"a code no XT key sends is refused");
  }

  TEST_METHOD(MistakesAreNamed)
  {
    const auto fails = [](std::string_view _text, std::string_view _why)
    {
      std::vector<Elite::Step> steps;
      std::string error;
      Assert::IsFalse(Elite::ParseSteps(_text, steps, error), L"refused");
      Assert::IsTrue(error.find(_why) != std::string::npos, L"says why");
    };
    fails("wait", "a step is a verb and one argument");
    fails("wait 1.2345", "at most three decimals");
    fails("key Hyper", "unknown key name");
    fails("press F1", "steps are");
    fails("digest boot 1234", "64 lowercase hex digits");
    fails("wait 1\nkey F1 F2", "line 2");
  }
};

} // namespace GameLogicTests
