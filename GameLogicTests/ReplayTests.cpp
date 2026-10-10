#include "pch.h"

#include "ReferenceRig.h"
#include "Replay.h"

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
