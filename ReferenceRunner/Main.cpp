#include "pch.h"

#include "DirectoryFileStore.h"
#include "NativeRoutines.h"
#include "Pc.h"
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

// Plays the reference headless on Machine::Pc (ADR-006), natively: a replay (ADR-008), or steps given on the command
// line:
//
//   ReferenceRunner [--exe FILE] [--out DIR] [--steps TEXT | --replay FILE]
//
// It checks that FILE (default ELITES.EXE) is the binary ADR-001 names, loads it, applies the D5 byte in memory, puts
// the native routines in place of the original's (ADR-010) on the Pc's Dispatcher, which interprets nothing (ADR-011),
// and follows the steps (Replay.h) in paced time: shots go to DIR/NAME.png, digests to standard output. DOS's files live
// in DIR/files, which must be empty when the run starts, as a replay's conditions require (ADR-008); a run refuses one
// that is not. A file step copies from the replay file's own directory, or from Replays/ in the repository for --steps.
//
// A digest with an expected value that does not match fails the run. Nothing writes a digest back: since D7 deleted the
// interpreter there is no original to record one from, and a digest changes only by a ruling (ADR-008 item 7).
//
// Exit status: 0 the steps ran and every digest matched, 1 the program stopped them (a refused call, its end, a spin, a
// native routine that waited where it cannot, or unported code) or a digest did not match, 2 usage or file errors.
// Only an end step may end the program, and it must (Elite::ExpectedStop): a replay that leaves for DOS ends with one,
// and its digests after the end read what the program left.

namespace
{

constexpr char USAGE[] = "usage: ReferenceRunner [--exe FILE] [--out DIR] [--steps TEXT | --replay FILE]\n";

struct Options
{
  std::filesystem::path exe = Elite::REFERENCE_FILE_NAME;
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
    if (argument == "--exe")
      _options.exe = value;
    else if (argument == "--out")
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

bool WriteShot(Machine::Pc& _pc, const std::filesystem::path& _path)
{
  const auto image = std::make_unique<Machine::FrameImage>();
  _pc.Video().Render(_pc.Ram(), *image);
  std::array<std::uint32_t, 16> palette = {};
  const auto colors = Machine::CgaPalette();
  for (std::size_t index = 0; index < palette.size(); ++index)
    palette[index] = (std::uint32_t{colors[index].red} << 16) | (std::uint32_t{colors[index].green} << 8) | colors[index].blue;
  return ReferenceRunner::WritePalettePng(_path, Machine::FrameImage::WIDTH_PIXELS, Machine::FrameImage::HEIGHT_PIXELS, image->pixels,
                                          palette);
}

std::string Describe(Machine::StopReason _reason, const Machine::Pc& _pc)
{
  switch (_reason)
  {
  case Machine::StopReason::Fault:
    if (const std::optional<Machine::ServiceFault>& fault = _pc.Services().Fault(); fault.has_value())
      return std::format("refused call: int {:02X}h AH={:02X}h AL={:02X}h at {:04X}:{:04X}", fault->vector, fault->ah, fault->al, fault->cs,
                         fault->ip);
    break;
  case Machine::StopReason::Terminated:
    return "the program ended";
  case Machine::StopReason::Spinning:
    return std::format("{} steps without waiting, at {:04X}:{:04X}", _pc.SpinLimit(), _pc.Processor().Regs().cs, _pc.Processor().Regs().ip);
  case Machine::StopReason::Unported:
    return std::format("code no native routine stands in for, at {:04X}:{:04X}", _pc.Processor().Regs().cs, _pc.Processor().Regs().ip);
  case Machine::StopReason::Overran:
    return std::format("native {} waited past the end of a run, and is not hooked as a routine that waits", _pc.Native().Overran());
  case Machine::StopReason::Reached:
    break;
  }
  return "running";
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
  Machine::DirectoryFileStore files(options.out / "files");
  Machine::Pc::Desc desc;
  desc.startMoment = Elite::START_MOMENT;
  const auto pc = std::make_unique<Machine::Pc>(files, desc);
  Machine::LoadedProgram program;
  switch (Elite::LoadReference(*pc, Elite::ReadWholeFile(options.exe), Elite::PSP_SEGMENT, program))
  {
  case Elite::ReferenceFailure::NotTheReference:
    std::fprintf(stderr, "ReferenceRunner: %s is not the reference binary; see ADR-001 and ADR-007\n", options.exe.string().c_str());
    return 2;
  case Elite::ReferenceFailure::DidNotLoad:
    std::fputs("ReferenceRunner: the reference did not load\n", stderr);
    return 2;
  case Elite::ReferenceFailure::None:
    break;
  }
  pc->SetTimeMode(Machine::TimeMode::Paced);
  Elite::InstallNativeRoutines(*pc, program);

  // reason: why the last step that ran the machine returned; asExpected: every step so far left the run as it must
  // (Elite::ExpectedStop), which for an end step is with the program ended.
  Machine::StopReason reason = Machine::StopReason::Reached;
  bool asExpected = true;
  const std::filesystem::path sources = options.replay.empty() ? Elite::FindInRepository("Replays") : options.replay.parent_path();
  Elite::ReplayPlayer player(*pc, program, files, sources);
  std::size_t mismatches = 0;
  for (std::size_t index = 0; index < steps.size() && asExpected; ++index)
  {
    const Elite::Step& step = steps[index];
    std::string digest;
    const Machine::StopReason stop = player.Play(step, digest);
    asExpected = stop == Elite::ExpectedStop(step);
    if (step.kind == Elite::StepKind::Wait || step.kind == Elite::StepKind::End)
      reason = stop;
    if (!asExpected && stop == Machine::StopReason::Reached)
      std::fprintf(stderr, "ReferenceRunner: line %zu: the program has not ended by the end of its end step\n", step.line);
    if (step.kind == Elite::StepKind::Shot)
    {
      const std::filesystem::path path = options.out / (step.name + ".png");
      if (!WriteShot(*pc, path))
      {
        std::fprintf(stderr, "ReferenceRunner: cannot write %s\n", path.string().c_str());
        return 2;
      }
      Print(std::format("shot\t{}\n", path.string()));
    }
    else if (step.kind == Elite::StepKind::Digest)
    {
      const bool checked = !step.expectedDigest.empty();
      const bool matches = digest == step.expectedDigest;
      if (checked && !matches)
        ++mismatches;
      Print(std::format("digest\t{}\t{}{}\n", step.name, digest, !checked ? "" : matches ? "\tmatches" : "\tDOES NOT MATCH"));
    }
  }

  for (const auto& [port, counts] : pc->Ports().Unmapped())
    Print(std::format("unmapped\tport {:04X}h\t{} reads\t{} writes\n", port, counts.reads, counts.writes));
  Print(std::format("ran\t{} instructions\t{} cycles\t{:.3f} s\t{}\n", pc->Processor().InstructionCount(), pc->Clock(),
                    static_cast<double>(pc->Clock()) * Machine::CPU_CLOCK_DIVISOR / static_cast<double>(Machine::CRYSTAL_HZ),
                    Describe(reason, *pc)));
  if (mismatches > 0)
  {
    std::fprintf(stderr, "ReferenceRunner: %zu digest(s) did not match\n", mismatches);
    return 1;
  }
  return asExpected ? 0 : 1;
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
