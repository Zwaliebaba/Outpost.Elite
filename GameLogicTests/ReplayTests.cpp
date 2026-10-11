#include "pch.h"

#include "ReferenceRig.h"
#include "Replay.h"

#include <format>
#include <fstream>
#include <map>
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
    Elite::Cycles ends[2] = {};
    for (int run = 0; run < 2; ++run)
    {
      ReferenceRig rig("Waits");
      Elite::ReplayPlayer player(rig.Host(), rig.State());
      Elite::Checkpoint checkpoint;
      for (const Elite::Step& step : run == 0 ? one : many)
        Assert::IsTrue(player.Play(step, checkpoint) == Elite::StopReason::Reached, L"runs");
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
    Elite::ReplayRecorder recorder("a test session");
    const Elite::Cycles start = live.Host().Clock();
    std::uint64_t now = 0;
    for (const Press& press : presses)
    {
      // The wall clock moves in uneven steps between the presses.
      for (std::uint64_t step = 13; now + step < press.milliseconds; step += 7)
      {
        now += step;
        Assert::IsTrue(live.Host().RunUntil(Elite::ReplayCycle(start, now)) == Elite::StopReason::Reached, L"runs");
      }
      now = press.milliseconds;
      Assert::IsTrue(live.Host().RunUntil(Elite::ReplayCycle(start, now)) == Elite::StopReason::Reached, L"runs");
      Assert::IsTrue(recorder.Key(now, press.scanCode, press.down), L"an XT key");
      live.Host().KeyboardController().Inject(static_cast<std::uint8_t>(press.scanCode | (press.down ? 0 : 0x80)));
    }
    Assert::IsTrue(live.Host().RunUntil(Elite::ReplayCycle(start, END_MILLISECONDS)) == Elite::StopReason::Reached, L"runs");
    const Elite::Checkpoint expected = Elite::TakeCheckpoint(live.State(), live.Host().Video());
    recorder.Digest(END_MILLISECONDS, "end", expected);

    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(recorder.Text(), steps, error), L"the recording parses");
    ReferenceRig replay("RecordReplay");
    Elite::ReplayPlayer player(replay.Host(), replay.State());
    Assert::AreEqual(expected.picture.c_str(), steps.back().expectedPicture.c_str(), false, L"the digest's picture line");
    Assert::AreEqual(expected.outcomes.c_str(), steps.back().expectedOutcomes.c_str(), false, L"and its outcome line");
    Elite::Checkpoint checkpoint;
    for (const Elite::Step& step : steps)
      Assert::IsTrue(player.Play(step, checkpoint) == Elite::StopReason::Reached, L"plays");
    Assert::AreEqual(expected.digest.c_str(), checkpoint.digest.c_str());
    Assert::AreEqual(expected.picture.c_str(), checkpoint.picture.c_str());
    Assert::AreEqual(expected.outcomes.c_str(), checkpoint.outcomes.c_str());
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
    fails("end", "a step is a verb and one argument");
    fails("end 0.0001", "end takes seconds, with at most three decimals");
    fails("end 1; key y", "only digest and shot may follow end");
    fails("end 1\nwait 1", "line 2");
    fails("end 1; end 1", "only digest and shot may follow end");
    fails("picture boot 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "follows the digest step it names");
    fails("digest boot\npicture boot 1234", "64 lowercase hex digits");
    fails("digest boot\npicture boot", "picture takes the label of the digest before it and a value");
    fails("digest boot\nwait 1\npicture boot 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "line 3");
    fails("digest boot\npicture title 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
          "follows the digest step it names");
    fails("digest boot\npicture boot 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
          "picture boot 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
          "has its picture already");
    fails("outcome boot kills=0", "follows the digest step it names");
    fails("digest boot\noutcome boot", "outcome takes the label of the digest before it and its outcomes");
    fails("digest boot\noutcome boot kills=0 fuelTonnes=3", "'fuelTonnes' is not an outcome");
    fails("digest boot\noutcome boot kills=0 kills=1", "'kills' is given twice");
    fails("digest boot\noutcome boot kills", "not a name=value pair");
    fails("digest boot\nkey F1\noutcome boot kills=0", "line 3");
    fails("digest boot\noutcome boot kills=0\noutcome boot fuel=70", "has its outcomes already");
  }

  // A picture or outcome line (Replay.h) is not a step: it gives the picture digest or the outcomes the digest step just
  // before it must give, even after an end step, and a replay with such lines has the same steps as one without.
  TEST_METHOD(APictureLineBelongsToTheDigestBeforeIt)
  {
    constexpr std::string_view DIGEST = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    constexpr std::string_view PICTURE = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    std::vector<Elite::Step> steps;
    std::string error;
    const std::string text =
      std::format("wait 1\ndigest boot {}\npicture boot {}   # recorded\noutcome boot kills=0  fuel=70\nend 1; digest dos\n"
                  "outcome dos kills=1\npicture dos {}\n",
                  DIGEST, PICTURE, PICTURE);
    Assert::IsTrue(Elite::ParseSteps(text, steps, error), L"parses");
    Assert::AreEqual(std::size_t{4}, steps.size(), L"wait, digest, end, digest: the pictures and outcomes are not steps");
    Assert::AreEqual("kills=0 fuel=70", steps[1].expectedOutcomes.c_str(), false, L"separated by single spaces");
    Assert::AreEqual("kills=1", steps[3].expectedOutcomes.c_str(), false, L"before the picture line too");
    Assert::AreEqual(std::string(DIGEST).c_str(), steps[1].expectedDigest.c_str());
    Assert::AreEqual(std::string(PICTURE).c_str(), steps[1].expectedPicture.c_str());
    Assert::IsTrue(steps[3].expectedDigest.empty(), L"a digest with no value");
    Assert::AreEqual(std::string(PICTURE).c_str(), steps[3].expectedPicture.c_str(), false, L"may still have its picture");
  }

  // Recording a corpus's pictures and outcomes (RecordCheckpoints) writes them after their digest's line, ending as that line
  // does, and replaces the picture and outcome lines there were; every other line, comments and blank lines included, is
  // kept byte for byte.
  TEST_METHOD(RecordingAPictureKeepsEveryOtherLine)
  {
    const std::string digest(64, 'a');
    const std::string old(64, 'b');
    const std::string picture(64, 'c');
    const std::string text = "# a replay\r\nwait 3\r\ndigest boot " + digest + "\r\npicture boot " + old + "\r\noutcome boot kills=1" +
                             "\r\n\nkey F1; wait 1  # launch\n" + "digest flight " + digest;
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(text, steps, error), L"parses");
    const Elite::Checkpoint checkpoint{digest, picture, "kills=2 fuel=70"};
    const std::map<std::size_t, Elite::Checkpoint> checkpoints = {{3, checkpoint}, {8, checkpoint}};
    std::string recorded;
    Assert::IsTrue(Elite::RecordCheckpoints(text, checkpoints, recorded, error), L"records");
    const std::string expected = "# a replay\r\nwait 3\r\ndigest boot " + digest + "\r\npicture boot " + picture +
                                 "\r\noutcome boot kills=2 fuel=70\r\n\nkey F1; wait 1  # launch\ndigest flight " + digest +
                                 "\npicture flight " + picture + "\noutcome flight kills=2 fuel=70\n";
    Assert::AreEqual(expected.c_str(), recorded.c_str());
    std::string again;
    Assert::IsTrue(Elite::RecordCheckpoints(recorded, checkpoints, again, error), L"records again");
    Assert::AreEqual(recorded.c_str(), again.c_str(), false, L"a second recording changes nothing");

    Assert::IsFalse(Elite::RecordCheckpoints(text, {{2, checkpoint}}, recorded, error), L"line 2 is a wait");
    Assert::IsTrue(error.find("line 2") != std::string::npos, L"names the line");
    Assert::IsFalse(Elite::RecordCheckpoints("key F1; digest boot\n", {{1, checkpoint}}, recorded, error), L"not alone");
    Assert::IsFalse(Elite::RecordCheckpoints(text, {{9, checkpoint}}, recorded, error), L"past the end");
  }

  // Re-basing a corpus's digests (RecordDigests) writes each over the one its line holds, and keeps every other byte, each
  // line's end and a comment after the digest included.
  TEST_METHOD(RecordingADigestKeepsEveryOtherByte)
  {
    const std::string old(64, 'a');
    const std::string digest(64, 'b');
    const std::string text = "# a replay\r\nwait 3\r\ndigest boot " + old + "\r\npicture boot " + old + "\r\n\nkey F1; wait 1\n" +
                             "digest flight " + old + "  # launched\n" + "digest last " + old;
    std::string recorded;
    std::string error;
    Assert::IsTrue(Elite::RecordDigests(text, {{3, digest}, {7, digest}, {8, digest}}, recorded, error), L"records");
    const std::string expected = "# a replay\r\nwait 3\r\ndigest boot " + digest + "\r\npicture boot " + old +
                                 "\r\n\nkey F1; wait 1\ndigest flight " + digest + "  # launched\ndigest last " + digest;
    Assert::AreEqual(expected.c_str(), recorded.c_str());

    Assert::IsFalse(Elite::RecordDigests(text, {{2, digest}}, recorded, error), L"line 2 is a wait");
    Assert::IsTrue(error.find("line 2") != std::string::npos, L"names the line");
    Assert::IsFalse(Elite::RecordDigests(text, {{9, digest}}, recorded, error), L"past the end");
  }

  // An end step (Replay.h) is the last that runs the machine, and the only one in which the program may end, as it must
  // there: only digests and shots, which look at what the program left, may follow it, in the same text or a later one.
  TEST_METHOD(AnEndStepIsTheLastThatRunsTheMachine)
  {
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps("key y; end 1.5; digest dos; shot dos", steps, error), L"parses");
    Assert::AreEqual(std::size_t{4}, steps.size());
    Assert::IsTrue(steps[1].kind == Elite::StepKind::End);
    Assert::AreEqual(std::uint64_t{1'500}, steps[1].waitMilliseconds);
    Assert::IsTrue(Elite::ExpectedStop(steps[1]) == Elite::StopReason::Terminated, L"the program must end in an end step");
    for (const std::size_t other : {std::size_t{0}, std::size_t{2}, std::size_t{3}})
      Assert::IsTrue(Elite::ExpectedStop(steps[other]) == Elite::StopReason::Reached, L"and in no other");
    Assert::IsTrue(Elite::ParseSteps("digest again", steps, error), L"a digest may follow, in a later text too");
    Assert::IsFalse(Elite::ParseSteps("wait 1", steps, error), L"a wait may not");
    Assert::IsTrue(error.find("only digest and shot may follow end") != std::string::npos, L"says why");
  }

  // Leaving for DOS: E on the disc menu asks, Y answers, and the program ends (int 20h) in the next step that runs the
  // machine. In an end step that is what must happen, and digests after it read what the program left; in a wait it stops
  // the run, as only an end step may; and an end step in which the program does not end, here because nothing answers
  // the question, fails as surely (ExpectedStop).
  TEST_METHOD(OnlyAnEndStepMayEndTheProgram)
  {
    // Any key, the disc menu, and E: "Are you sure (Y/N)?".
    constexpr std::string_view TO_THE_QUESTION = "wait 0.5; key space; wait 3.5; key Escape; wait 0.3; key e; wait 0.3";
    struct Ending
    {
      std::string_view steps;
      Elite::StopReason stop; // what its step that runs the machine returns
      bool accepted;          // whether that is what the step must return
    };
    const Ending endings[] = {{"key y; end 1; digest dos; digest again", Elite::StopReason::Terminated, true},
                              {"key y; wait 1", Elite::StopReason::Terminated, false},
                              {"end 1", Elite::StopReason::Reached, false}};
    for (const Ending& ending : endings)
    {
      ReferenceRig rig("EndStep");
      std::vector<Elite::Step> steps;
      std::string error;
      Assert::IsTrue(Elite::ParseSteps(TO_THE_QUESTION, steps, error), L"parses");
      Elite::ReplayPlayer player(rig.Host(), rig.State());
      Elite::Checkpoint checkpoint;
      for (const Elite::Step& step : steps)
        Assert::IsTrue(player.Play(step, checkpoint) == Elite::StopReason::Reached, L"the menus run");
      steps.clear();
      Assert::IsTrue(Elite::ParseSteps(ending.steps, steps, error), L"parses");
      std::vector<std::string> digests;
      for (const Elite::Step& step : steps)
      {
        const Elite::StopReason stop = player.Play(step, checkpoint);
        if (step.kind == Elite::StepKind::Wait || step.kind == Elite::StepKind::End)
        {
          Assert::IsTrue(stop == ending.stop, L"Y ends the program, and nothing else does");
          Assert::AreEqual(ending.accepted, stop == Elite::ExpectedStop(step), L"only an end step may end it, and it must");
        }
        else
          Assert::IsTrue(stop == Elite::StopReason::Reached, L"a key or a digest stops nothing");
        if (step.kind == Elite::StepKind::Digest)
          digests.push_back(checkpoint.digest);
      }
      if (!digests.empty())
      {
        Assert::AreEqual(std::size_t{64}, digests[0].size(), L"a digest after the end reads what the program left");
        Assert::IsTrue(digests[0] == digests[1], L"which nothing changes");
      }
    }
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
    Elite::ReplayPlayer player(rig.Host(), rig.State(), rig.Store(), sources.Path());
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps("file pilot.cdr first.cdr", steps, error), L"parses");
    const Elite::Cycles before = rig.Host().Clock();
    Elite::Checkpoint checkpoint;
    Assert::IsTrue(player.Play(steps[0], checkpoint) == Elite::StopReason::Reached, L"copies");
    Assert::AreEqual(before, rig.Host().Clock(), L"no time passes");
    Assert::IsTrue(Elite::ReadWholeFile(rig.Files() / "PILOT.CDR") == first, L"the copy, under its DOS name");

    std::uint8_t attribute = 0xFF;
    Assert::IsTrue(rig.Store().Attribute("PILOT.CDR", attribute) == Elite::DosError::None, L"DOS finds it");
    Assert::AreEqual(0u, std::uint32_t{attribute}, L"no attribute, not even archive");
    const std::vector<Elite::FileEntry> listed = rig.Store().List();
    Assert::AreEqual(std::size_t{1}, listed.size());
    Assert::AreEqual(std::uint32_t{0x0021}, std::uint32_t{listed[0].stamp.date}, L"1980-01-01");
    Assert::AreEqual(std::uint32_t{0}, std::uint32_t{listed[0].stamp.time}, L"00:00:00");
    Assert::AreEqual(std::uint32_t{7}, listed[0].sizeBytes);

    steps.clear();
    Assert::IsTrue(Elite::ParseSteps("file PILOT.CDR second.cdr", steps, error), L"parses");
    Assert::IsTrue(player.Play(steps[0], checkpoint) == Elite::StopReason::Reached, L"copies again");
    Assert::IsTrue(Elite::ReadWholeFile(rig.Files() / "PILOT.CDR") == second, L"the second copy replaces the first");
  }

  // A file step that cannot be done is the replay's fault, not the game's: it throws, naming the step.
  TEST_METHOD(AFileStepThatCannotBeDoneThrows)
  {
    ScratchDirectory sources("FileStepMissing");
    ReferenceRig rig("FileStepFails");
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps("wait 0.5\nfile PILOT.CDR missing.cdr", steps, error), L"parses");
    const auto refusal = [&](Elite::ReplayPlayer& _player)
    {
      Elite::Checkpoint checkpoint;
      std::string message;
      try
      {
        static_cast<void>(_player.Play(steps[1], checkpoint));
      }
      catch (const std::runtime_error& failure)
      {
        message = failure.what();
      }
      return message;
    };

    Elite::ReplayPlayer copiesNothing(rig.Host(), rig.State());
    const std::string noDirectory = refusal(copiesNothing);
    Assert::IsTrue(noDirectory.find("line 2: file PILOT.CDR missing.cdr") != std::string::npos, L"names the step");
    Assert::IsTrue(noDirectory.find("no DOS directory") != std::string::npos, L"says why");

    Elite::ReplayPlayer player(rig.Host(), rig.State(), rig.Store(), sources.Path());
    const std::string noSource = refusal(player);
    Assert::IsTrue(noSource.find("cannot read") != std::string::npos, L"a source that is not there");
    Assert::IsTrue(rig.Store().List().empty(), L"and nothing was created");
  }
};

} // namespace GameLogicTests
