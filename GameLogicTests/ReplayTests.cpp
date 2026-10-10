#include "pch.h"

#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDigest.h"

#include <fstream>
#include <stdexcept>

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

    // A file step names its copy as DOS keys it, upper case, and keeps the source as written.
    Assert::IsTrue(Elite::ParseSteps("file pilot.cdr rich-at-leesti.cdr", steps, error), L"a file step parses");
    Assert::AreEqual(std::size_t{7}, steps.size());
    Assert::IsTrue(steps[6].kind == Elite::StepKind::File);
    Assert::AreEqual("PILOT.CDR", steps[6].name.c_str());
    Assert::AreEqual("rich-at-leesti.cdr", steps[6].source.c_str());
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
    fails("file PILOT.CDR", "file takes a DOS name and the file beside the replay");
    fails("file PILOT.CDR a.cdr b.cdr", "file takes a DOS name and the file beside the replay");
    fails("file LONGERNAME.CDR a.cdr", "DOS 8.3 name");
    fails("file C:PILOT.CDR a.cdr", "DOS 8.3 name");
    fails("file PILOT.CDR ../Replays/a.cdr", "without a directory");
    fails("file PILOT.CDR ..", "without a directory");
    fails("file PILOT.CDR C:a.cdr", "without a directory");
  }

  // A file step (Replay.h) puts a copy of a file beside the replay into DOS's directory, as a file copied there
  // before the run would be: no attributes, which the game's loader requires (LoadCommanderFile), and DOS's
  // earliest stamp. It takes no time, and a second copy replaces the first, as the game's own save would.
  TEST_METHOD(AFileStepCopiesIntoDosDirectory)
  {
    ScratchDirectory sources("FileStepSources");
    const std::vector<std::uint8_t> first = {'C', 'o', 'm', 'm', 0x1A, 0x00, 0xFF};
    const std::vector<std::uint8_t> second = {0x01, 0x02};
    const auto write = [&](const char* _name, const std::vector<std::uint8_t>& _bytes)
    {
      std::ofstream file(sources.Path() / _name, std::ios::binary);
      file.write(reinterpret_cast<const char*>(_bytes.data()), static_cast<std::streamsize>(_bytes.size()));
    };
    write("first.cdr", first);
    write("second.cdr", second);

    ReferenceRig rig("FileStep");
    Assert::IsTrue(rig.Loaded(), L"ELITES.EXE at the repository root");
    Elite::ReplayPlayer player(rig.Host(), rig.Program(), rig.Store(), sources.Path());
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps("file pilot.cdr first.cdr", steps, error), L"parses");
    const Machine::Cycles before = rig.Host().Clock();
    std::string digest;
    Assert::IsTrue(player.Play(steps[0], digest) == Machine::StopReason::Reached, L"copies");
    Assert::AreEqual(before, rig.Host().Clock(), L"no time passes");
    Assert::IsTrue(Elite::ReadWholeFile(rig.Files() / "PILOT.CDR") == first, L"the copy, under its DOS name");

    std::uint8_t attribute = 0xFF;
    Assert::IsTrue(rig.Store().Attribute("PILOT.CDR", attribute) == Machine::DosError::None, L"DOS finds it");
    Assert::AreEqual(0u, std::uint32_t{attribute}, L"no attribute, not even archive");
    const std::vector<Machine::FileEntry> listed = rig.Store().List();
    Assert::AreEqual(std::size_t{1}, listed.size());
    Assert::AreEqual(std::uint32_t{0x0021}, std::uint32_t{listed[0].stamp.date}, L"1980-01-01");
    Assert::AreEqual(std::uint32_t{0}, std::uint32_t{listed[0].stamp.time}, L"00:00:00");
    Assert::AreEqual(std::uint32_t{7}, listed[0].sizeBytes);

    steps.clear();
    Assert::IsTrue(Elite::ParseSteps("file PILOT.CDR second.cdr", steps, error), L"parses");
    Assert::IsTrue(player.Play(steps[0], digest) == Machine::StopReason::Reached, L"copies again");
    Assert::IsTrue(Elite::ReadWholeFile(rig.Files() / "PILOT.CDR") == second, L"the second copy replaces the first");
  }

  // A file step that cannot be done is the replay's fault, not the game's: it throws, naming the step.
  TEST_METHOD(AFileStepThatCannotBeDoneThrows)
  {
    ScratchDirectory sources("FileStepMissing");
    ReferenceRig rig("FileStepFails");
    Assert::IsTrue(rig.Loaded(), L"ELITES.EXE at the repository root");
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps("wait 0.5\nfile PILOT.CDR missing.cdr", steps, error), L"parses");
    const auto refusal = [&](Elite::ReplayPlayer& _player)
    {
      std::string digest;
      std::string message;
      try
      {
        static_cast<void>(_player.Play(steps[1], digest));
      }
      catch (const std::runtime_error& failure)
      {
        message = failure.what();
      }
      return message;
    };

    Elite::ReplayPlayer copiesNothing(rig.Host(), rig.Program());
    const std::string noDirectory = refusal(copiesNothing);
    Assert::IsTrue(noDirectory.find("line 2: file PILOT.CDR missing.cdr") != std::string::npos, L"names the step");
    Assert::IsTrue(noDirectory.find("no DOS directory") != std::string::npos, L"says why");

    Elite::ReplayPlayer player(rig.Host(), rig.Program(), rig.Store(), sources.Path());
    const std::string noSource = refusal(player);
    Assert::IsTrue(noSource.find("cannot read") != std::string::npos, L"a source that is not there");
    Assert::IsTrue(rig.Store().List().empty(), L"and nothing was created");
  }
};

} // namespace GameLogicTests
