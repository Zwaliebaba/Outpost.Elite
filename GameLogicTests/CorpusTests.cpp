#include "pch.h"

#include "ReferenceRig.h"
#include "Replay.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

std::wstring Widen(std::string_view _text)
{
  return std::wstring(_text.begin(), _text.end());
}

// CS:IP, as the listing writes an address.
std::wstring Address(const Machine::Registers& _registers)
{
  return Widen(std::format("{:04X}:{:04X}", _registers.cs, _registers.ip));
}

std::string ReadText(const std::filesystem::path& _path)
{
  std::ifstream stream(_path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  return text.str();
}

// Every Replays/*.replay, played on the native routines (ReferenceRig). Every digest it names must come out as
// recorded; _check then looks at the machine after each replay.
template <typename Check> void PlayEveryReplay(Check _check)
{
  const std::filesystem::path directory = Elite::FindInRepository("Replays");
  Assert::IsFalse(directory.empty(), L"Replays/ at the repository root");
  std::vector<std::filesystem::path> replays;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory))
  {
    if (entry.path().extension() == ".replay")
      replays.push_back(entry.path());
  }
  std::sort(replays.begin(), replays.end());
  Assert::IsFalse(replays.empty(), L"at least one replay");

  for (const std::filesystem::path& path : replays)
  {
    const std::wstring name = path.filename().wstring();
    std::vector<Elite::Step> steps;
    std::string error;
    if (!Elite::ParseSteps(ReadText(path), steps, error))
      Assert::Fail((name + L": " + Widen(error)).c_str());

    ReferenceRig rig("Corpus");
    Assert::IsTrue(rig.Loaded(), L"ELITES.EXE at the repository root");
    Elite::ReplayPlayer player(rig.Host(), rig.Program(), rig.Store(), directory); // file steps copy from Replays/
    std::size_t checked = 0;
    for (const Elite::Step& step : steps)
    {
      std::string digest;
      const Machine::StopReason reason = player.Play(step, digest);
      const std::wstring where = name + L" line " + std::to_wstring(step.line);
      if (reason == Machine::StopReason::Unported)
        Assert::Fail((where + L": reached code no native routine stands in for, at " + Address(rig.Host().Processor().Regs())).c_str());
      if (reason == Machine::StopReason::Overran)
        Assert::Fail(
          (where + L": native " + Widen(rig.Host().Native().Overran()) + L" waited, and is not hooked as a routine that waits").c_str());
      // Only an end step may end the program, and it must (Replay.h): a replay that leaves for DOS ends with one.
      if (step.kind == Elite::StepKind::End && reason == Machine::StopReason::Reached)
        Assert::Fail((where + L": the program has not ended by the end of its end step").c_str());
      if (reason == Machine::StopReason::Terminated && reason != Elite::ExpectedStop(step))
        Assert::Fail((where + L": the program ended, and only a replay's end step may end it").c_str());
      if (reason != Elite::ExpectedStop(step))
        Assert::Fail((where + L": the run stopped").c_str());
      if (step.kind != Elite::StepKind::Digest)
        continue;
      if (step.expectedDigest.empty())
        Assert::Fail((where + L": a corpus digest must say what it expects").c_str());
      if (digest != step.expectedDigest)
        Assert::Fail((where + L": digest " + Widen(step.name) + L" is " + Widen(digest)).c_str());
      ++checked;
    }
    Assert::IsTrue(checked > 0, (name + L": checks no digest").c_str());
    _check(name, rig.Host());
  }
}

} // namespace

TEST_CLASS(CorpusTests)
{
public:
  // The replay corpus (ADR-008): every Replays/*.replay plays on the native routines in paced time, and every digest it
  // names comes out as it was recorded from the original. It runs on a Dispatcher (ADR-011), which interprets nothing, so
  // no instruction of the original runs. Since D7 deleted the interpreter, the corpus and the twins' known answers are what
  // the port is held to (ADR-003 item 5).
  TEST_METHOD(NativeCodeKeepsEveryDigest)
  {
    PlayEveryReplay(
      [](const std::wstring& _name, Machine::Pc& _pc)
      {
        std::uint64_t calls = 0;
        for (const auto& [linear, hook] : _pc.Native().Hooks())
          calls += hook.calls;
        Assert::IsTrue(calls > 0, (_name + L": no native routine ran").c_str());
      });
  }
};

} // namespace GameLogicTests
