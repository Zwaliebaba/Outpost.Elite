#include "pch.h"

#include "Cpu.h"
#include "DirectoryFileStore.h"
#include "Dispatcher.h"
#include "NativeRoutines.h"
#include "Pc.h"
#include "PngWriter.h"
#include "Reference.h"
#include "Replay.h"
#include "TraceWriter.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
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

// Runs the reference headless on Machine::Pc and records what ADR-003 compares (ADR-006), or plays a
// replay (ADR-008):
//
//   ReferenceRunner [--exe FILE] [--out DIR] [--steps TEXT | --replay FILE [--update]] [--paced]
//                   [--trace FILE] [--trace-until OFFSET] [--coverage FILE] [--native | --compare]
//                   [--native-report FILE]
//
// It checks that FILE (default ELITES.EXE) is the binary ADR-001 names, loads it, applies the D5 byte
// in memory, and follows the steps (Replay.h), from --steps or from a replay file: shots go to
// DIR/NAME.png, digests to standard output. DOS's files live in DIR/files. A file step copies from the
// replay file's own directory, or from Replays/ in the repository for --steps.
//
// Time is clocked, as on the real machine, unless --paced is given; a replay is always paced. A digest
// with an expected value that does not match fails the run. --update writes each digest's value into the
// replay file instead: what recording a replay ends with, and never a way to make a failing one pass
// (ADR-008).
//
// --trace writes the boot trace, every instruction from the entry until the first one at OFFSET in the
// program's code segment (default 0x7616, GetKey). --coverage writes the offsets in that segment of every
// instruction start the run executed, one per line in hex.
//
// --native runs the native routines in place of the original's (ADR-010), on a Dispatcher, which
// interprets nothing (ADR-011): a run that reaches code no native routine stands in for stops there.
// --compare runs them on the interpreter instead, and compares each call with the original as it is
// made. Either prints each native routine's calls and what comparing found, and every mismatch; --native-report writes the same per routine as a table,
// with the offsets the original executed while it was compared, for Tools/RoutineCoverage.py.
//
// Exit status: 0 the steps ran, 1 the program stopped them (a refused call, its end, a deadlock, a spin
// or unported code), a digest did not match, or a native routine did not match the original, 2 usage or file
// errors.

namespace
{

constexpr char USAGE[] = "usage: ReferenceRunner [--exe FILE] [--out DIR] [--steps TEXT | --replay FILE [--update]] [--paced] "
                         "[--trace FILE] [--trace-until OFFSET] [--coverage FILE] [--native | --compare] [--native-report FILE]\n";

// A trace with no steps to run runs until it ends, or for at most this long.
constexpr std::uint64_t TRACE_LIMIT_MILLISECONDS = 60'000;

struct Options
{
  std::filesystem::path exe = Elite::REFERENCE_FILE_NAME;
  std::filesystem::path out = ".";
  std::string steps;
  std::filesystem::path replay;
  bool update = false;
  bool paced = false;
  std::filesystem::path trace;
  std::uint16_t traceUntil = Elite::GET_KEY_OFFSET;
  std::filesystem::path coverage;
  bool native = false;
  bool compare = false;
  std::filesystem::path nativeReport;
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
    if (argument == "--paced")
    {
      _options.paced = true;
      continue;
    }
    if (argument == "--update")
    {
      _options.update = true;
      continue;
    }
    if (argument == "--native" || argument == "--compare")
    {
      _options.native = true;
      _options.compare = _options.compare || argument == "--compare";
      continue;
    }
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
    else if (argument == "--trace")
      _options.trace = value;
    else if (argument == "--coverage")
      _options.coverage = value;
    else if (argument == "--native-report")
      _options.nativeReport = value;
    else if (argument == "--trace-until")
    {
      const std::string_view digits = value.starts_with("0x") ? value.substr(2) : value;
      const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), _options.traceUntil, 16);
      if (error != std::errc{} || end != digits.data() + digits.size())
        return false;
    }
    else
      return false;
  }
  if (!_options.replay.empty())
  {
    if (!_options.steps.empty())
      return false;
    _options.paced = true;
  }
  if (!_options.nativeReport.empty() && !_options.native)
    return false;
  return !_options.update || !_options.replay.empty();
}

// Each native routine's books, every mismatch, and the report file if one was asked for. Returns the
// number of calls that did not match the original.
std::uint64_t ReportNativeCode(const Machine::Pc& _pc, const Options& _options)
{
  if (!_options.nativeReport.empty())
  {
    std::ofstream report(_options.nativeReport, std::ios::trunc);
    Elite::WriteNativeReport(_pc.Native(), report);
  }
  for (const auto& [linear, hook] : _pc.Native().Hooks())
  {
    Print(std::format("native\t{:04X}\t{}\t{} calls\t{} verified\t{} unverifiable\t{} mismatches\n", hook.offset, hook.name, hook.calls,
                      hook.verified, hook.unverifiable, hook.mismatches));
  }
  for (const Machine::NativeCode::Mismatch& mismatch : _pc.Native().Mismatches())
    Print(std::format("mismatch\t{}\tcall {}\tcycle {}\t{}\n", mismatch.routine, mismatch.call, mismatch.clock, mismatch.difference));
  return _pc.Native().Mismatches().size();
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

// Writes each digest's value into the replay's text, on the line its step came from.
std::string WithDigests(std::string_view _text, const std::vector<Elite::Step>& _steps, const std::vector<std::string>& _digests)
{
  std::vector<std::string> lines;
  for (std::size_t start = 0; start <= _text.size();)
  {
    const std::size_t end = _text.find('\n', start);
    lines.emplace_back(_text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  for (std::size_t index = 0; index < _steps.size(); ++index)
  {
    const Elite::Step& step = _steps[index];
    if (step.kind != Elite::StepKind::Digest || _digests[index].empty() || step.line == 0 || step.line > lines.size())
      continue;
    std::string& line = lines[step.line - 1];
    const bool crlf = !line.empty() && line.back() == '\r';
    if (crlf)
      line.pop_back();
    const std::size_t comment = line.find('#');
    const std::string tail = comment == std::string::npos ? std::string{} : "  " + line.substr(comment);
    line = std::format("digest {} {}{}", step.name, _digests[index], tail);
    if (crlf)
      line.push_back('\r');
  }
  std::string text;
  for (std::size_t index = 0; index < lines.size(); ++index)
  {
    text += lines[index];
    if (index + 1 < lines.size())
      text += '\n';
  }
  return text;
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
  case Machine::StopReason::Deadlocked:
    return "the CPU halted with interrupts off";
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
  Machine::DirectoryFileStore files(options.out / "files");
  Machine::Pc::Desc desc;
  desc.startMoment = Elite::START_MOMENT;
  const bool detached = options.native && !options.compare;
  const auto pc = std::make_unique<Machine::Pc>(files, desc, detached ? &Machine::MakeDispatcher : &Machine::MakeCpu);
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
  if (options.paced)
    pc->SetTimeMode(Machine::TimeMode::Paced);
  if (options.native)
  {
    Elite::InstallNativeRoutines(*pc, program);
    pc->Native().SetVerifying(options.compare);
  }

  std::vector<std::uint8_t> executed;
  pc->Processor().SetExecutionMap(&executed);
  std::unique_ptr<ReferenceRunner::TraceWriter> trace;
  if (!options.trace.empty())
  {
    trace = std::make_unique<ReferenceRunner::TraceWriter>(options.trace, program.loadSegment, options.traceUntil);
    if (!trace->IsOpen())
    {
      std::fprintf(stderr, "ReferenceRunner: cannot write %s\n", options.trace.string().c_str());
      return 2;
    }
    pc->Processor().SetInstructionObserver(trace.get());
  }

  Machine::StopReason reason = Machine::StopReason::Reached;
  const std::filesystem::path sources = options.replay.empty() ? Elite::FindInRepository("Replays") : options.replay.parent_path();
  Elite::ReplayPlayer player(*pc, program, files, sources);
  std::vector<std::string> digests(steps.size());
  std::size_t mismatches = 0;
  for (std::size_t index = 0; index < steps.size() && reason == Machine::StopReason::Reached; ++index)
  {
    const Elite::Step& step = steps[index];
    reason = player.Play(step, digests[index]);
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
      const bool checked = !step.expectedDigest.empty() && !options.update;
      const bool matches = digests[index] == step.expectedDigest;
      if (checked && !matches)
        ++mismatches;
      Print(std::format("digest\t{}\t{}{}\n", step.name, digests[index], !checked ? "" : matches ? "\tmatches" : "\tDOES NOT MATCH"));
    }
  }
  if (trace && !trace->Finished() && reason == Machine::StopReason::Reached)
  {
    const Machine::Cycles limit = pc->Clock() + Machine::MicrosecondsToCycles(TRACE_LIMIT_MILLISECONDS * 1000);
    while (!trace->Finished() && reason == Machine::StopReason::Reached && pc->Clock() < limit)
      reason = pc->RunUntil(pc->Clock() + 1);
  }
  pc->Processor().SetInstructionObserver(nullptr);

  if (trace)
    Print(std::format("trace\t{}\t{} instructions{}\n", options.trace.string(), trace->Records(),
                      trace->Finished() ? "" : ", stop address not reached"));
  if (!options.coverage.empty())
  {
    std::ofstream out(options.coverage, std::ios::trunc);
    Elite::WriteExecutedOffsets(executed, program, out);
    const std::uint32_t base = Machine::Memory::Linear(program.loadSegment, 0);
    const auto count = static_cast<std::size_t>(std::count_if(executed.begin() + base, executed.begin() + base + Elite::CODE_SEGMENT_BYTES,
                                                              [](std::uint8_t _byte) { return _byte != 0; }));
    Print(std::format("coverage\t{}\t{} instruction starts\n", options.coverage.string(), count));
  }
  if (options.update && reason == Machine::StopReason::Reached)
  {
    std::ofstream out(options.replay, std::ios::binary | std::ios::trunc);
    out << WithDigests(replayText, steps, digests);
    if (!out)
    {
      std::fprintf(stderr, "ReferenceRunner: cannot write %s\n", options.replay.string().c_str());
      return 2;
    }
    Print(std::format("updated\t{}\n", options.replay.string()));
  }
  const std::uint64_t nativeFailures = options.native ? ReportNativeCode(*pc, options) : 0;
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
  if (nativeFailures > 0)
  {
    std::fprintf(stderr, "ReferenceRunner: native code did not match the original\n");
    return 1;
  }
  return reason == Machine::StopReason::Reached ? 0 : 1;
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
