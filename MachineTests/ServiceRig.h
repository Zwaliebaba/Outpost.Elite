// MachineTests/ServiceRig.h
#pragma once

#include "Cpu.h"
#include "DirectoryFileStore.h"
#include "Firmware.h"
#include "Memory.h"
#include "PcServices.h"
#include "PortBus.h"
#include "Timing.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace MachineTests
{

/// One OUT the bus saw. A plain record (R8).
struct PortWrite
{
  std::uint16_t port = 0;
  std::uint8_t value = 0;

  bool operator==(const PortWrite&) const = default;
};

/// A port bus that records every OUT and answers IN from a table (FFh for a port nobody set, as an empty bus reads).
class RecordingBus final : public Machine::PortBus
{
public:
  [[nodiscard]] std::uint8_t In8(std::uint16_t _port) override
  {
    m_reads.push_back(_port);
    const auto found = m_inputs.find(_port);
    return found == m_inputs.end() ? std::uint8_t{0xFF} : found->second;
  }

  void Out8(std::uint16_t _port, std::uint8_t _value) override
  {
    m_writes.push_back(PortWrite{_port, _value});
  }

  void SetInput(std::uint16_t _port, std::uint8_t _value)
  {
    m_inputs[_port] = _value;
  }

  [[nodiscard]] const std::vector<PortWrite>& Writes() const noexcept
  {
    return m_writes;
  }

  [[nodiscard]] const std::vector<std::uint16_t>& Reads() const noexcept
  {
    return m_reads;
  }

  void Clear() noexcept
  {
    m_writes.clear();
    m_reads.clear();
  }

private:
  std::map<std::uint16_t, std::uint8_t> m_inputs;
  std::vector<PortWrite> m_writes;
  std::vector<std::uint16_t> m_reads;
};

/// An empty directory under the system's temporary directory, removed again when the test ends.
class ScratchDirectory
{
public:
  explicit ScratchDirectory(std::string_view _name)
    : m_path(std::filesystem::temp_directory_path() / ("OutpostEliteMachineTests-" + std::string(_name)))
  {
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
    std::filesystem::create_directories(m_path, error);
  }

  ScratchDirectory(const ScratchDirectory&) = delete;
  ScratchDirectory& operator=(const ScratchDirectory&) = delete;

  ~ScratchDirectory()
  {
    try
    {
      std::error_code error;
      std::filesystem::remove_all(m_path, error);
    }
    catch (...)
    {
      // Left behind; the next run removes it before it starts.
      return;
    }
  }

  [[nodiscard]] const std::filesystem::path& Path() const noexcept
  {
    return m_path;
  }

  void WriteFile(std::string_view _name, std::string_view _contents) const
  {
    std::ofstream stream(m_path / _name, std::ios::binary);
    stream.write(_contents.data(), static_cast<std::streamsize>(_contents.size()));
  }

  [[nodiscard]] std::string ReadFile(std::string_view _name) const
  {
    std::ifstream stream(m_path / _name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  }

  [[nodiscard]] bool Exists(std::string_view _name) const
  {
    std::error_code error;
    return std::filesystem::exists(m_path / _name, error);
  }

private:
  std::filesystem::path m_path;
};

/// A powered-on PC: memory with the firmware installed, the services attached to a CPU, a recording port bus, and DOS's
/// files in a scratch directory. Code runs at CODE_SEGMENT:0000 with a stack at STACK_SEGMENT:STACK_TOP, and data
/// goes in DATA_SEGMENT.
class ServiceRig
{
public:
  static constexpr std::uint16_t CODE_SEGMENT = 0x2000;
  static constexpr std::uint16_t DATA_SEGMENT = 0x3000;
  static constexpr std::uint16_t STACK_SEGMENT = 0x4000;
  static constexpr std::uint16_t STACK_TOP = 0x0100;
  static constexpr std::uint16_t PSP_SEGMENT = 0x1000;

  explicit ServiceRig(std::string_view _name, bool _mousePresent = false, const Machine::Dos::DateTime& _start = Machine::Dos::DateTime{})
    : m_files(_name),
      m_store(m_files.Path()),
      m_services(m_memory, m_bus, m_store, m_clock, Machine::PcServices::Desc{_start, _mousePresent}),
      m_cpu(m_memory, m_bus)
  {
    m_services.PowerOn();
    m_services.StartProgram(PSP_SEGMENT);
    m_cpu.SetHostServices(&m_services);
    Machine::Registers& regs = m_cpu.Regs();
    regs.cs = CODE_SEGMENT;
    regs.ip = 0;
    regs.ds = DATA_SEGMENT;
    regs.es = DATA_SEGMENT;
    regs.ss = STACK_SEGMENT;
    regs.sp = STACK_TOP;
    regs.flags = static_cast<std::uint16_t>(Machine::FLAGS_FIXED_ONES | Machine::FLAG_INTERRUPT);
    m_bus.Clear();
  }

  [[nodiscard]] Machine::Memory& Ram() noexcept
  {
    return m_memory;
  }

  [[nodiscard]] RecordingBus& Bus() noexcept
  {
    return m_bus;
  }

  [[nodiscard]] Machine::Cpu& Processor() noexcept
  {
    return m_cpu;
  }

  [[nodiscard]] Machine::Registers& Regs() noexcept
  {
    return m_cpu.Regs();
  }

  [[nodiscard]] Machine::PcServices& Services() noexcept
  {
    return m_services;
  }

  [[nodiscard]] Machine::Cycles& Clock() noexcept
  {
    return m_clock;
  }

  [[nodiscard]] const ScratchDirectory& Files() const noexcept
  {
    return m_files;
  }

  /// Runs INT _vector from CODE_SEGMENT:0000, one step.
  void Interrupt(std::uint8_t _vector)
  {
    m_memory.Write8(CODE_SEGMENT, 0, 0xCD);
    m_memory.Write8(CODE_SEGMENT, 1, _vector);
    m_cpu.Regs().cs = CODE_SEGMENT;
    m_cpu.Regs().ip = 0;
    (void)m_cpu.Step();
  }

  /// Int 21h with AH = _function and AL = _subfunction.
  void Dos(std::uint8_t _function, std::uint8_t _subfunction = 0)
  {
    m_cpu.Regs().ax = static_cast<std::uint16_t>((_function << 8) | _subfunction);
    Interrupt(0x21);
  }

  /// Puts an ASCIIZ (or '$'-terminated, if the text says so) string in DATA_SEGMENT and points DS:DX at it.
  void PutString(std::uint16_t _offset, std::string_view _text, bool _terminate = true)
  {
    for (std::size_t index = 0; index < _text.size(); ++index)
    {
      m_memory.Write8(DATA_SEGMENT, static_cast<std::uint16_t>(_offset + index), static_cast<std::uint8_t>(_text[index]));
    }
    if (_terminate)
    {
      m_memory.Write8(DATA_SEGMENT, static_cast<std::uint16_t>(_offset + _text.size()), 0);
    }
    m_cpu.Regs().ds = DATA_SEGMENT;
    m_cpu.Regs().dx = _offset;
  }

  /// Whether the services recorded a fault of this kind.
  [[nodiscard]] bool Faulted(Machine::FaultKind _kind) const
  {
    const std::optional<Machine::ServiceFault>& fault = m_services.Fault();
    return fault.has_value() && fault->kind == _kind;
  }

  [[nodiscard]] bool Carry() const noexcept
  {
    return (m_cpu.Regs().flags & Machine::FLAG_CARRY) != 0;
  }

  [[nodiscard]] bool Zero() const noexcept
  {
    return (m_cpu.Regs().flags & Machine::FLAG_ZERO) != 0;
  }

  /// Steps until CS:IP is _segment:_offset, at most _limit instructions; returns whether it got there.
  bool RunUntil(std::uint16_t _segment, std::uint16_t _offset, int _limit = 1000)
  {
    for (int step = 0; step < _limit; ++step)
    {
      if (m_cpu.Regs().cs == _segment && m_cpu.Regs().ip == _offset)
      {
        return true;
      }
      (void)m_cpu.Step();
    }
    return m_cpu.Regs().cs == _segment && m_cpu.Regs().ip == _offset;
  }

private:
  Machine::Memory m_memory;
  RecordingBus m_bus;
  Machine::Cycles m_clock = 0;
  ScratchDirectory m_files;
  Machine::DirectoryFileStore m_store;
  Machine::PcServices m_services;
  Machine::Cpu m_cpu;
};

/// IsMouseDriverInstalled, CS:02D4-02EF of ELITEL.EXE, byte for byte: ZF=0 when the int 33h vector is non-zero and
/// its first byte is not CF (IRET). ExeLoaderTests checks these against the file.
inline constexpr std::array<std::uint8_t, 28> IS_MOUSE_DRIVER_INSTALLED = {
  0x50,                         // push ax
  0x53,                         // push bx
  0x33, 0xC0,                   // xor ax, ax
  0x8E, 0xC0,                   // mov es, ax
  0x26, 0x8B, 0x1E, 0xCC, 0x00, // mov bx, es:[00CCh]
  0x26, 0xA1, 0xCE, 0x00,       // mov ax, es:[00CEh]
  0x8E, 0xC0,                   // mov es, ax
  0x0B, 0xC3,                   // or ax, bx
  0x74, 0x04,                   // je +4
  0x26, 0x80, 0x3F, 0xCF,       // cmp byte es:[bx], CFh
  0x5B,                         // pop bx
  0x58,                         // pop ax
  0xC3,                         // ret
};

/// ELITEL.EXE, found by walking up from the working directory to the repository root, or else beside the source
/// tree this file was compiled from (the parent of MachineTests/). Empty if neither has it.
inline std::vector<std::uint8_t> ReadReferenceBinary()
{
  std::vector<std::filesystem::path> candidates;
  std::error_code error;
  for (std::filesystem::path directory = std::filesystem::current_path(error); !error && !directory.empty();
       directory = directory.parent_path())
  {
    candidates.push_back(directory / "ELITEL.EXE");
    if (directory == directory.parent_path())
    {
      break;
    }
  }
  const std::filesystem::path source = std::source_location::current().file_name();
  candidates.push_back(source.parent_path().parent_path() / "ELITEL.EXE");
  for (const std::filesystem::path& candidate : candidates)
  {
    std::error_code found;
    if (std::filesystem::is_regular_file(candidate, found))
    {
      std::ifstream stream(candidate, std::ios::binary);
      return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
  }
  return {};
}

} // namespace MachineTests
