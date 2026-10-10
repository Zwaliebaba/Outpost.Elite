#include "pch.h"

#include "ComparisonRig.h"
#include "NativeRoutines.h"
#include "ReferenceRig.h"
#include "Replay.h"

#include <algorithm>
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

std::string ReadText(const std::filesystem::path& _path)
{
  std::ifstream stream(_path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  return text.str();
}

// How the corpus runs the reference.
enum class Running : std::uint8_t
{
  Original, // interpreted throughout
  Native,   // with every ported routine in place of the original's (ADR-010)
  Compared  // likewise, each call compared with the original as it is made
};

// Every Replays/*.replay, played as _running says. Every digest it names must come out as recorded;
// _check then looks at the machine after each replay.
template <typename Check> void PlayEveryReplay(Running _running, Check _check)
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

    // What the interpreted run executes, which the native runs' digests are compared with. Made before
    // the machine that marks it, so that it outlives it.
    std::vector<std::uint8_t> executed;
    ReferenceRig rig("Corpus");
    Assert::IsTrue(rig.Loaded(), L"ELITES.EXE at the repository root");
    if (_running == Running::Original)
    {
      rig.Host().Processor().SetExecutionMap(&executed);
    }
    else
    {
      Elite::InstallNativeRoutines(rig.Host(), rig.Program());
      rig.Host().Native().SetVerifying(_running == Running::Compared);
    }
    Elite::ReplayPlayer player(rig.Host(), rig.Program());
    std::size_t checked = 0;
    for (const Elite::Step& step : steps)
    {
      std::string digest;
      const Machine::StopReason reason = player.Play(step, digest);
      const std::wstring where = name + L" line " + std::to_wstring(step.line);
      if (reason == Machine::StopReason::Overran)
        Assert::Fail(
          (where + L": native " + Widen(rig.Host().Native().Overran()) + L" waited, and is not hooked as a routine that waits").c_str());
      if (reason != Machine::StopReason::Reached)
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
    if (_running == Running::Original)
      SaveExecutedOffsets("Corpus-" + path.stem().string(), executed, rig.Program());
    if (_running == Running::Compared)
      SaveNativeReport("Corpus-" + path.stem().string(), rig.Host().Native());
    _check(name, rig.Host());
  }
}

} // namespace

TEST_CLASS(CorpusTests)
{
public:
  // The replay corpus (ADR-008): every Replays/*.replay plays on the reference in paced time, and every
  // digest it names comes out as recorded. Phases 2 and 3 never change one (plan §6.2).
  TEST_METHOD(EveryReplayReproducesItsDigests)
  {
    PlayEveryReplay(Running::Original, [](const std::wstring&, Machine::Pc&) {});
  }

  // Phase 3 (ADR-010): with the ported routines in place of the original's, every digest is unchanged.
  TEST_METHOD(NativeCodeKeepsEveryDigest)
  {
    PlayEveryReplay(Running::Native,
                    [](const std::wstring& _name, Machine::Pc& _pc)
                    {
                      std::uint64_t calls = 0;
                      for (const auto& [linear, hook] : _pc.Native().Hooks())
                        calls += hook.calls;
                      Assert::IsTrue(calls > 0, (_name + L": no native routine ran").c_str());
                    });
  }

  // Phase 3's acceptance (plan §5, §6.3): run on the original's every call in the corpus, each ported
  // routine agrees with it on everything its contract names, and the digests are unchanged.
  TEST_METHOD(NativeCodeAgreesWithTheOriginalOnEveryCall)
  {
    PlayEveryReplay(
      Running::Compared,
      [](const std::wstring& _name, Machine::Pc& _pc)
      {
        const std::vector<Machine::NativeCode::Mismatch>& mismatches = _pc.Native().Mismatches();
        if (!mismatches.empty())
        {
          const Machine::NativeCode::Mismatch& first = mismatches.front();
          Assert::Fail(
            (_name + L": " + Widen(first.routine) + L" call " + std::to_wstring(first.call) + L": " + Widen(first.difference)).c_str());
        }
      });
  }
};

} // namespace GameLogicTests
