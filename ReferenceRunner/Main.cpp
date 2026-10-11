#include "pch.h"

#include "DirectoryFileStore.h"
#include "Game.h"
#include "Outcome.h"
#include "Platform.h"
#include "PngWriter.h"
#include "Reference.h"
#include "Replay.h"

#include <array>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Plays the reference headless on the native platform (Elite::Platform, D33): a replay (ADR-008), or steps given on the
// command line:
//
//   ReferenceRunner [--out DIR] [--steps TEXT | --replay FILE]
//
// It loads the reference from its generated image (D3), so it reads no ELITES.EXE, applies the D5 byte in memory, puts
// the native routines where the original's entries are (ADR-010), and follows the steps (Replay.h) in paced time: shots
// go to DIR/NAME.png, digests to standard output. DOS's files live in DIR/files, which must be empty when the run
// starts, as a replay's conditions require (ADR-008); a run refuses one that is not. A file step copies from the replay
// file's own directory, or from Replays/ in the repository for --steps.
//
// A digest with an expected value that does not match fails the run. Nothing writes a digest back: since D7 deleted the
// interpreter there is no original to record one from, and a digest changes only by a ruling (ADR-008 item 7).
//
// A digest's picture and outcome lines (Replay.h) are checked too (D33). A picture that differs is reported: it prints both
// values and does not fail the run. An outcome of the player's progress that differs fails the run; a timing outcome that
// differs is reported (Elite::OutcomeKind). The last lines sum what was checked and what differs. A digest that names its
// expected value but has no recorded picture or outcomes fails the run, naming the line.
//
// Exit status: 0 the steps ran and every digest matched, 1 the program stopped them (a refused call, its end, a spin, or
// an interrupt to unported code), a digest or an outcome of progress did not match, or a checked digest has no picture or
// outcomes, 2 usage or file errors. A picture or a timing outcome that differs does not
// change it.
// Only an end step may end the program, and it must (Elite::ExpectedStop): a replay that leaves for DOS ends with one,
// and its digests after the end read what the program left.

namespace
{

constexpr char USAGE[] = "usage: ReferenceRunner [--out DIR] [--steps TEXT | --replay FILE]\n";

struct Options
{
  std::filesystem::path out = ".";
  std::string steps;
  std::filesystem::path replay;
};

void Print(const std::string& _text)
{
  std::fputs(_text.c_str(), stdout);
}

bool ParseOptions(int _argc, char** _argv, Options& _options)
{
  for (int index = 1; index < _argc; ++index)
  {
    const std::string_view argument = _argv[index];
    if (index + 1 >= _argc)
      return false;
    const std::string_view value = _argv[++index];
    if (argument == "--out")
      _options.out = value;
    else if (argument == "--steps")
      _options.steps = value;
    else if (argument == "--replay")
      _options.replay = value;
    else
      return false;
  }
  return _options.replay.empty() || _options.steps.empty();
}

std::optional<std::string> ReadText(const std::filesystem::path& _path)
{
  std::ifstream stream(_path, std::ios::binary);
  if (!stream)
    return std::nullopt;
  std::ostringstream text;
  text << stream.rdbuf();
  return text.str();
}

bool WriteShot(const Elite::Platform& _platform, const std::filesystem::path& _path)
{
  const auto image = std::make_unique<Elite::FrameImage>();
  _platform.Render(*image);
  std::array<std::uint32_t, 16> palette = {};
  const auto colors = Elite::CgaPalette();
  for (std::size_t index = 0; index < palette.size(); ++index)
    palette[index] = (std::uint32_t{colors[index].red} << 16) | (std::uint32_t{colors[index].green} << 8) | colors[index].blue;
  return ReferenceRunner::WritePalettePng(_path, Elite::FrameImage::WIDTH_PIXELS, Elite::FrameImage::HEIGHT_PIXELS, image->pixels, palette);
}

std::string Describe(Elite::StopReason _reason, const Elite::Platform& _platform)
{
  switch (_reason)
  {
  case Elite::StopReason::Terminated:
    return "the program ended";
  case Elite::StopReason::Spinning:
    return std::format("{} turns without waiting", _platform.SpinLimit());
  case Elite::StopReason::Reached:
    break;
  }
  return "running";
}

// What the guard's checks found over the run (D33).
struct GuardTally
{
  std::size_t unrecorded = 0; // checked digests with no picture, or no outcomes, recorded
  std::size_t picturesChecked = 0;
  std::size_t picturesDiffer = 0;
  std::size_t outcomesChecked = 0; // digests whose outcomes were checked
  std::size_t progressDiffer = 0;  // outcomes of the player's progress that differ, or are not recorded: these fail the run
  std::size_t timingDiffer = 0;    // timing outcomes that differ: these are reported
};

// _step's picture line against _checkpoint's picture, printed. A difference is reported; when the digest is _checked, a
// picture not recorded fails the run.
void CheckPicture(const Elite::Step& _step, const Elite::Checkpoint& _checkpoint, bool _checked, GuardTally& _tally)
{
  if (_step.expectedPicture.empty())
  {
    Print(std::format("picture\t{}\t{}\n", _step.name, _checkpoint.picture));
    if (_checked)
    {
      ++_tally.unrecorded;
      std::fprintf(stderr, "ReferenceRunner: line %zu: digest %s has no recorded picture\n", _step.line, _step.name.c_str());
    }
    return;
  }
  ++_tally.picturesChecked;
  const bool same = _checkpoint.picture == _step.expectedPicture;
  _tally.picturesDiffer += same ? 0 : 1;
  Print(std::format("picture\t{}\t{}\t{}\n", _step.name, _checkpoint.picture,
                    same ? "matches" : std::format("DIFFERS from the recorded {}", _step.expectedPicture)));
}

// _step's outcome line against _checkpoint's outcomes, each difference printed with its kind (Elite::OutcomeKind). An outcome
// of the player's progress that differs, or is not recorded, fails the run; a timing outcome that differs is reported. When
// the digest is _checked, outcomes not recorded at all fail the run.
void CheckOutcomes(const Elite::Step& _step, const Elite::Checkpoint& _checkpoint, bool _checked, GuardTally& _tally)
{
  if (_step.expectedOutcomes.empty())
  {
    Print(std::format("outcome\t{}\t{}\n", _step.name, _checkpoint.outcomes));
    if (_checked)
    {
      ++_tally.unrecorded;
      std::fprintf(stderr, "ReferenceRunner: line %zu: digest %s has no recorded outcomes\n", _step.line, _step.name.c_str());
    }
    return;
  }
  ++_tally.outcomesChecked;
  const std::vector<Elite::OutcomeDifference> differences = Elite::CompareOutcomes(_step.expectedOutcomes, _checkpoint.outcomes);
  for (const Elite::OutcomeDifference& difference : differences)
  {
    const bool gates = difference.kind == Elite::OutcomeKind::Progress || !difference.recorded;
    (gates ? _tally.progressDiffer : _tally.timingDiffer) += 1;
    Print(std::format("outcome\t{}\t{}={}\t{}\t{}\n", _step.name, difference.name, difference.actual,
                      difference.recorded ? std::format("DIFFERS from the recorded {}", *difference.recorded) : "NOT RECORDED",
                      gates ? "progress: fails the run" : "timing: reported"));
  }
  if (differences.empty())
    Print(std::format("outcome\t{}\tmatches\n", _step.name));
}

int Run(int _argc, char** _argv)
{
  Options options;
  std::vector<Elite::Step> steps;
  std::string error;
  std::string replayText;
  if (!ParseOptions(_argc, _argv, options))
  {
    std::fputs(USAGE, stderr);
    return 2;
  }
  if (!options.replay.empty())
  {
    const std::optional<std::string> text = ReadText(options.replay);
    if (!text)
    {
      std::fprintf(stderr, "ReferenceRunner: cannot read %s\n", options.replay.string().c_str());
      return 2;
    }
    replayText = *text;
  }
  if (!Elite::ParseSteps(options.replay.empty() ? std::string_view{options.steps} : std::string_view{replayText}, steps, error))
  {
    std::fprintf(stderr, "ReferenceRunner: %s\n", error.c_str());
    return 2;
  }

  std::error_code created;
  std::filesystem::create_directories(options.out / "files", created);
  if (created)
  {
    std::fprintf(stderr, "ReferenceRunner: cannot create %s\n", (options.out / "files").string().c_str());
    return 2;
  }
  // A run starts with DOS's files in an empty directory (ADR-008 item 4). A file left by an earlier run would change what
  // the disc menu catalogues, so the run refuses rather than deleting what it did not write.
  std::error_code listed;
  if (!std::filesystem::is_empty(options.out / "files", listed) || listed)
  {
    std::fprintf(stderr, "ReferenceRunner: %s is not empty: empty it, or name another --out\n", (options.out / "files").string().c_str());
    return 2;
  }
  Elite::DirectoryFileStore files(options.out / "files");
  Elite::Platform::Desc desc;
  desc.startMoment = Elite::START_MOMENT;
  // The GameState owns the game's memory and the platform runs the native routines over it (D19, D33).
  const auto game = std::make_unique<Elite::Game>(files, desc);
  if (!game->Load(Elite::PSP_SEGMENT))
  {
    std::fputs("ReferenceRunner: the reference did not load\n", stderr);
    return 2;
  }
  Elite::Platform& platform = game->Host();

  // reason: why the last step that ran the machine returned; asExpected: every step so far left the run as it must
  // (Elite::ExpectedStop), which for an end step is with the program ended.
  Elite::StopReason reason = Elite::StopReason::Reached;
  bool asExpected = true;
  const std::filesystem::path sources = options.replay.empty() ? Elite::FindInRepository("Replays") : options.replay.parent_path();
  Elite::ReplayPlayer player(platform, game->State(), files, sources);
  std::size_t mismatches = 0;
  GuardTally tally;
  for (std::size_t index = 0; index < steps.size() && asExpected; ++index)
  {
    const Elite::Step& step = steps[index];
    Elite::Checkpoint checkpoint;
    const Elite::StopReason stop = player.Play(step, checkpoint);
    asExpected = stop == Elite::ExpectedStop(step);
    if (step.kind == Elite::StepKind::Wait || step.kind == Elite::StepKind::End)
      reason = stop;
    if (!asExpected && stop == Elite::StopReason::Reached)
      std::fprintf(stderr, "ReferenceRunner: line %zu: the program has not ended by the end of its end step\n", step.line);
    if (step.kind == Elite::StepKind::Shot)
    {
      const std::filesystem::path path = options.out / (step.name + ".png");
      if (!WriteShot(platform, path))
      {
        std::fprintf(stderr, "ReferenceRunner: cannot write %s\n", path.string().c_str());
        return 2;
      }
      Print(std::format("shot\t{}\n", path.string()));
    }
    else if (step.kind == Elite::StepKind::Digest)
    {
      const bool checked = !step.expectedDigest.empty();
      const bool matches = checkpoint.digest == step.expectedDigest;
      if (checked && !matches)
        ++mismatches;
      Print(std::format("digest\t{}\t{}{}\n", step.name, checkpoint.digest, !checked ? "" : matches ? "\tmatches" : "\tDOES NOT MATCH"));
      CheckPicture(step, checkpoint, checked, tally);
      CheckOutcomes(step, checkpoint, checked, tally);
    }
  }

  Print(std::format("ran\t{} interrupts\t{} cycles\t{:.3f} s\t{}\n", platform.InterruptsTaken(), platform.Clock(),
                    static_cast<double>(platform.Clock()) * Elite::CPU_CLOCK_DIVISOR / static_cast<double>(Elite::CRYSTAL_HZ),
                    Describe(reason, platform)));
  Print(std::format("pictures\t{} checked\t{} differ\n", tally.picturesChecked, tally.picturesDiffer));
  Print(std::format("outcomes\t{} checked\t{} differ in progress\t{} differ in timing\n", tally.outcomesChecked, tally.progressDiffer,
                    tally.timingDiffer));
  if (tally.picturesDiffer > 0 || tally.timingDiffer > 0)
    std::fprintf(stderr,
                 "ReferenceRunner: %zu picture(s) and %zu timing outcome(s) differ from those recorded (reported, not failing the run)\n",
                 tally.picturesDiffer, tally.timingDiffer);
  if (mismatches > 0)
  {
    std::fprintf(stderr, "ReferenceRunner: %zu digest(s) did not match\n", mismatches);
    return 1;
  }
  if (tally.progressDiffer > 0)
  {
    std::fprintf(stderr, "ReferenceRunner: %zu outcome(s) of the player's progress differ from those recorded\n", tally.progressDiffer);
    return 1;
  }
  return asExpected && tally.unrecorded == 0 ? 0 : 1;
}

} // namespace

int main(int _argc, char** _argv)
{
  try
  {
    return Run(_argc, _argv);
  }
  catch (const std::exception& exception)
  {
    std::fprintf(stderr, "ReferenceRunner: %s\n", exception.what());
  }
  catch (...)
  {
    std::fputs("ReferenceRunner: unexpected exception\n", stderr);
  }
  return 2;
}
