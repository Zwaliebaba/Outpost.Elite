#include "pch.h"

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
