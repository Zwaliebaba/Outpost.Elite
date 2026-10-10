#include "pch.h"

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

} // namespace

TEST_CLASS(CorpusTests)
{
public:
  // The replay corpus (ADR-008): every Replays/*.replay plays on the reference in paced time, and every
  // digest it names comes out as recorded. Phases 2 and 3 never change one (plan §6.2).
  TEST_METHOD(EveryReplayReproducesItsDigests)
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
      std::size_t checked = 0;
      for (const Elite::Step& step : steps)
      {
        std::string digest;
        const Machine::StopReason reason = Elite::PlayStep(rig.Host(), rig.Program(), step, digest);
        const std::wstring where = name + L" line " + std::to_wstring(step.line);
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
    }
  }
};

} // namespace GameLogicTests
