#include "pch.h"

#include "DirectoryFileStore.h"
#include "Pc.h"
#include "PngWriter.h"
#include "Sha256.h"
#include "StepScript.h"
#include "TraceWriter.h"

#include <array>
#include <charconv>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Runs the reference headless on Machine::Pc and records what ADR-003 compares (ADR-006):
//
//   ReferenceRunner [--exe FILE] [--out DIR] [--steps TEXT] [--trace FILE] [--trace-until OFFSET]
//                   [--coverage FILE]
//
// It checks that FILE (default ELITES.EXE) is the binary ADR-001 names, loads it, applies the D5 byte
// in memory, and follows the steps Tools/ReferenceScreens.py also reads (StepScript.h): shots go to
// DIR/NAME.png, digests to standard output. DOS's files live in DIR/files.
//
// --trace writes the boot trace, every instruction from the entry until the first one at OFFSET in the
// program's code segment (default 0x7616, GetKey). --coverage writes the offsets in that segment of every
// instruction start the run executed, one per line in hex. Exit status: 0 the steps ran, 1 the program
// stopped them (a refused call, its end, or a deadlock), 2 usage or file errors.

namespace
{

constexpr char USAGE[] = "usage: ReferenceRunner [--exe FILE] [--out DIR] [--steps TEXT] [--trace FILE] [--trace-until OFFSET] "
                         "[--coverage FILE]\n";

// ADR-001: the reference binary, and the one byte the host changes in memory (D5): the protection's
// "already shown" flag at DS:25E4, in the data segment's image paragraph 08F4.
constexpr std::string_view REFERENCE_SHA256 = "18b5076a54733dea2d45b1e3b280fd1377067b6cb1b27166d3873aa7e7744363";
constexpr std::uint16_t D5_SEGMENT = 0x08F4;
constexpr std::uint16_t D5_OFFSET = 0x25E4;

// Where DOSBox-X puts the PSP, measured from its trace, so the two boot traces compare register for
// register (ADR-003). The clock starts as a PC/XT without a clock card does, as Tools/ReferenceScreens.py
// sets DOSBox-X's.
constexpr std::uint16_t PSP_SEGMENT = 0x0813;
constexpr Machine::Dos::DateTime START_MOMENT = {1980, 1, 1, 0, 0, 0, 0};

constexpr std::uint16_t GET_KEY_OFFSET = 0x7616;
constexpr std::uint16_t CODE_SEGMENT_BYTES = 0x8F40;
// A trace with no steps to run runs until it ends, or for at most this long.
constexpr std::uint64_t TRACE_LIMIT_MILLISECONDS = 60'000;

struct Options
{
  std::filesystem::path exe = "ELITES.EXE";
  std::filesystem::path out = ".";
  std::string steps;
  std::filesystem::path trace;
  std::uint16_t traceUntil = GET_KEY_OFFSET;
  std::filesystem::path coverage;
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
    else if (argument == "--trace")
      _options.trace = value;
    else if (argument == "--coverage")
      _options.coverage = value;
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
  return true;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& _path)
{
  std::ifstream stream(_path, std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// SHA-256 of all of memory and then the fourteen registers, little-endian: two runs that agree on this
// agree on everything the program can see.
std::string StateDigest(Machine::Pc& _pc)
{
  Machine::Sha256 hash;
  hash.Update(_pc.Ram().Bytes());
  const Machine::Registers& regs = _pc.Processor().Regs();
  const std::array<std::uint16_t, 14> words = {regs.cs, regs.ip, regs.ax, regs.bx, regs.cx, regs.dx, regs.si,
                                               regs.di, regs.bp, regs.sp, regs.ds, regs.es, regs.ss, regs.flags};
  for (const std::uint16_t word : words)
  {
    const std::array<std::uint8_t, 2> bytes = {static_cast<std::uint8_t>(word & 0xFF), static_cast<std::uint8_t>(word >> 8)};
    hash.Update(bytes);
  }
  return Machine::Sha256::ToHex(hash.Finish());
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
  case Machine::StopReason::Deadlocked:
    return "the CPU halted with interrupts off";
  case Machine::StopReason::Reached:
    break;
  }
  return "running";
}

int Run(int _argc, char** _argv)
{
  Options options;
  std::vector<ReferenceRunner::Step> steps;
  std::string error;
  if (!ParseOptions(_argc, _argv, options) || !ReferenceRunner::ParseSteps(options.steps, steps, error))
  {
    if (!error.empty())
      std::fprintf(stderr, "ReferenceRunner: %s\n", error.c_str());
    std::fputs(USAGE, stderr);
    return 2;
  }

  const std::vector<std::uint8_t> file = ReadFile(options.exe);
  const std::string digest = Machine::Sha256::ToHex(Machine::Sha256::Of(file));
  if (digest != REFERENCE_SHA256)
  {
    std::fprintf(stderr, "ReferenceRunner: %s is not the reference binary (SHA-256 %s); see ADR-001\n", options.exe.string().c_str(),
                 digest.c_str());
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
  desc.startMoment = START_MOMENT;
  const auto pc = std::make_unique<Machine::Pc>(files, desc);
  Machine::ExeLoader::Desc load;
  load.pspSegment = PSP_SEGMENT;
  Machine::LoadedProgram program;
  if (pc->Load(file, load, program) != Machine::LoadError::None ||
      !Machine::ExeLoader::PatchByte(pc->Ram(), program, D5_SEGMENT, D5_OFFSET, 0x00, 0x01))
  {
    std::fputs("ReferenceRunner: the reference did not load\n", stderr);
    return 2;
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
  for (const ReferenceRunner::Step& step : steps)
  {
    if (step.kind == ReferenceRunner::StepKind::Wait)
      reason = pc->RunUntil(pc->Clock() + Machine::MicrosecondsToCycles(step.waitMilliseconds * 1000));
    else if (step.kind == ReferenceRunner::StepKind::Key)
    {
      pc->KeyboardController().Inject(step.scanCode);
      pc->KeyboardController().Inject(static_cast<std::uint8_t>(step.scanCode | 0x80));
    }
    else if (step.kind == ReferenceRunner::StepKind::Shot)
    {
      const std::filesystem::path path = options.out / (step.name + ".png");
      if (!WriteShot(*pc, path))
      {
        std::fprintf(stderr, "ReferenceRunner: cannot write %s\n", path.string().c_str());
        return 2;
      }
      Print(std::format("shot\t{}\n", path.string()));
    }
    else
      Print(std::format("digest\t{}\t{}\n", step.name, StateDigest(*pc)));
    if (reason != Machine::StopReason::Reached)
      break;
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
    std::size_t count = 0;
    const std::uint32_t base = Machine::Memory::Linear(program.loadSegment, 0);
    for (std::uint32_t offset = 0; offset < CODE_SEGMENT_BYTES; ++offset)
    {
      if (executed[base + offset] != 0)
      {
        out << std::format("{:04X}\n", offset);
        ++count;
      }
    }
    Print(std::format("coverage\t{}\t{} instruction starts\n", options.coverage.string(), count));
  }
  for (const auto& [port, counts] : pc->Ports().Unmapped())
    Print(std::format("unmapped\tport {:04X}h\t{} reads\t{} writes\n", port, counts.reads, counts.writes));
  Print(std::format("ran\t{} instructions\t{} cycles\t{:.3f} s\t{}\n", pc->Processor().InstructionCount(), pc->Clock(),
                    static_cast<double>(pc->Clock()) * Machine::CPU_CLOCK_DIVISOR / static_cast<double>(Machine::CRYSTAL_HZ),
                    Describe(reason, *pc)));
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
