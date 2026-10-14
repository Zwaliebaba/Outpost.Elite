#include "pch.h"

#include "AnswerFile.h"
#include "GuardReport.h"
#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDumps.h"

#include <algorithm>
#include <fstream>
#include <map>
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

// The checkpoint of every digest step of the corpus, by replay and by the line the step is on.
using Checkpoints = std::map<std::filesystem::path, std::map<std::size_t, Elite::Checkpoint>>;

// Every Replays/*.replay, played on the native routines (ReferenceRig). Every digest it names must come out as
// recorded, and each is kept in _checkpoints. Unless _recording, each digest's picture and outcomes are checked against
// the ones recorded on the lines after it (_report): a picture or a timing outcome that differs fails nothing, and an
// outcome of the player's progress that differs, or a picture or outcomes not recorded, fails. While the data segment is
// dumped or the digests re-based (StateDumps), each digest point's data segment is dumped or held to the dump before the
// change instead of its digest to the one recorded, and a re-base fails on any picture or outcome that differs.
// _check then looks at the machine after each replay.
template <typename Check> void PlayEveryReplay(bool _recording, GuardReport& _report, Checkpoints& _checkpoints, Check _check)
{
  const StateDumps& dumps = StateDumps::Run();
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
    Elite::ReplayPlayer player(rig.Host(), rig.State(), rig.Store(), directory); // file steps copy from Replays/
    std::size_t checked = 0;
    for (const Elite::Step& step : steps)
    {
      Elite::Checkpoint checkpoint;
      const Elite::StopReason reason = player.Play(step, checkpoint);
      const std::wstring where = name + L" line " + std::to_wstring(step.line);
      // Only an end step may end the program, and it must (Replay.h): a replay that leaves for DOS ends with one.
      if (step.kind == Elite::StepKind::End && reason == Elite::StopReason::Reached)
        Assert::Fail((where + L": the program has not ended by the end of its end step").c_str());
      if (reason == Elite::StopReason::Terminated && reason != Elite::ExpectedStop(step))
        Assert::Fail((where + L": the program ended, and only a replay's end step may end it").c_str());
      if (reason != Elite::ExpectedStop(step))
        Assert::Fail((where + L": the run stopped").c_str());
      if (step.kind != Elite::StepKind::Digest)
        continue;
      if (step.expectedDigest.empty())
        Assert::Fail((where + L": a corpus digest must say what it expects").c_str());
      dumps.Point(rig.State(), "replays", path.stem().string(), step.line);
      if (dumps.Mode() == DigestMode::Check && checkpoint.digest != step.expectedDigest)
        Assert::Fail((where + L": digest " + Widen(step.name) + L" is " + Widen(checkpoint.digest)).c_str());
      ++checked;
      _checkpoints[path][step.line] = checkpoint;
      if (_recording)
        continue;
      if (step.expectedPicture.empty())
        Assert::Fail(
          (where + L": digest " + Widen(step.name) + L" has no recorded picture, and its picture is " + Widen(checkpoint.picture)).c_str());
      const std::wstring point = where + L" (digest " + Widen(step.name) + L")";
      _report.Picture(point, step.expectedPicture, checkpoint.picture);
      if (step.expectedOutcomes.empty())
        Assert::Fail((point + L": no outcomes are recorded, and its outcomes are " + Widen(checkpoint.outcomes)).c_str());
      if (const std::wstring failure = _report.Outcomes(point, step.expectedOutcomes, checkpoint.outcomes); !failure.empty())
        Assert::Fail(failure.c_str());
      if (dumps.Mode() == DigestMode::Rebase &&
          (checkpoint.picture != step.expectedPicture || checkpoint.outcomes != step.expectedOutcomes))
        Assert::Fail((point + L": the picture or the outcomes differ, and a re-base needs every one as recorded").c_str());
    }
    Assert::IsTrue(checked > 0, (name + L": checks no digest").c_str());
    _check(name, rig.Host());
  }
}

// Writes every replay of _checkpoints again with each digest's picture and outcome lines recorded from its checkpoint
// (RecordCheckpoints), keeping every other line. Called only once every digest of every replay has met its recorded value.
void RecordEveryCheckpoint(const Checkpoints& _checkpoints)
{
  for (const auto& [path, checkpoints] : _checkpoints)
  {
    std::string recorded;
    std::string error;
    if (!Elite::RecordCheckpoints(ReadText(path), checkpoints, recorded, error))
      Assert::Fail((path.filename().wstring() + L": " + Widen(error)).c_str());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << recorded;
    file.close();
    Assert::IsFalse(file.fail(), (path.filename().wstring() + L" cannot be written").c_str());
  }
}

// Writes every replay of _checkpoints again with each digest step's state digest from its checkpoint (RecordDigests),
// keeping every other byte. Called only once every digest point of every replay has met the re-base's gate (StateDumps).
void RebaseEveryDigest(const Checkpoints& _checkpoints)
{
  for (const auto& [path, checkpoints] : _checkpoints)
  {
    std::map<std::size_t, std::string> digests;
    for (const auto& [line, checkpoint] : checkpoints)
      digests[line] = checkpoint.digest;
    std::string recorded;
    std::string error;
    if (!Elite::RecordDigests(ReadText(path), digests, recorded, error))
      Assert::Fail((path.filename().wstring() + L": " + Widen(error)).c_str());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << recorded;
    file.close();
    Assert::IsFalse(file.fail(), (path.filename().wstring() + L" cannot be written").c_str());
  }
}

} // namespace

TEST_CLASS(CorpusTests)
{
public:
  // The replay corpus (ADR-008): every Replays/*.replay plays on the native routines in paced time, and every digest it
  // names comes out as it was recorded from the original. It runs on the native platform (D33), which has no processor, so
  // no instruction of the original runs. Since D7 deleted the interpreter, the corpus and the twins' known answers are what
  // the port is held to (ADR-003 item 5).
  //
  // Each digest's picture and outcomes (D33) are checked beside it. A picture, or an outcome that moves with the exact
  // timing of flight, reports: the log names each that differs, with both values, and ends with how many were checked and
  // how many differ. An outcome of the player's progress gates. While the guard is recorded (RecordingGuard), the pictures
  // and outcomes are written into the replays instead, and only once every digest of the corpus has met its recorded value.
  // While the digests are re-based (StateDumps), they are written into the replays once every point has met the gate.
  TEST_METHOD(NativeCodeKeepsEveryDigest)
  {
    const bool recording = RecordingGuard();
    GuardReport report(L"corpus");
    Checkpoints checkpoints;
    PlayEveryReplay(recording, report, checkpoints,
                    [](const std::wstring& _name, Elite::Host& _platform)
                    {
                      const std::uint64_t calls = _platform.Entry().calls + _platform.GameHandler(Elite::Irq::Timer).calls +
                                                  _platform.GameHandler(Elite::Irq::Keyboard).calls;
                      Assert::IsTrue(calls > 0, (_name + L": no native routine ran").c_str());
                    });
    if (recording)
      RecordEveryCheckpoint(checkpoints);
    else
      report.LogSummary();
    if (StateDumps::Run().Mode() == DigestMode::Rebase)
      RebaseEveryDigest(checkpoints);
  }
};

} // namespace GameLogicTests
