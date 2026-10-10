// MachineTests/ServiceRig.h
#pragma once

#include "DirectoryFileStore.h"
#include "Firmware.h"
#include "Memory.h"
#include "PcServices.h"
#include "PortBus.h"
#include "Registers.h"
#include "Timing.h"

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

/// A powered-on PC's services: memory with the firmware installed, the BIOS, DOS and mouse services, a recording port bus,
/// DOS's files in a scratch directory, and the registers a call is made with. Calls are made at CODE_SEGMENT:0000 with a
/// stack at STACK_SEGMENT:STACK_TOP, and data goes in DATA_SEGMENT.
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
      m_services(m_memory, m_bus, m_store, m_clock, Machine::PcServices::Desc{_start, _mousePresent})
  {
    m_services.PowerOn();
    m_services.StartProgram(PSP_SEGMENT);
    m_regs.cs = CODE_SEGMENT;
    m_regs.ip = 0;
    m_regs.ds = DATA_SEGMENT;
    m_regs.es = DATA_SEGMENT;
    m_regs.ss = STACK_SEGMENT;
    m_regs.sp = STACK_TOP;
    m_regs.flags = static_cast<std::uint16_t>(Machine::FLAGS_FIXED_ONES | Machine::FLAG_INTERRUPT);
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

  [[nodiscard]] Machine::Registers& Regs() noexcept
  {
    return m_regs;
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

  /// INT _vector, made at CODE_SEGMENT:0000 as Pc::CallInterrupt makes it for native code: the services are given the
  /// registers with IP past the two-byte instruction. Returns whether they served the call; one they do not serve goes
  /// through the interrupt table, which is the firmware's (FirmwareTests), and nothing here follows it.
  bool Interrupt(std::uint8_t _vector)
  {
    m_regs.cs = CODE_SEGMENT;
    m_regs.ip = INT_BYTES;
    return m_services.ServiceInterrupt(m_regs, _vector);
  }

  /// Int 21h with AH = _function and AL = _subfunction.
  void Dos(std::uint8_t _function, std::uint8_t _subfunction = 0)
  {
    m_regs.ax = static_cast<std::uint16_t>((_function << 8) | _subfunction);
    (void)Interrupt(0x21);
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
    m_regs.ds = DATA_SEGMENT;
    m_regs.dx = _offset;
  }

  /// Whether the services recorded a fault of this kind.
  [[nodiscard]] bool Faulted(Machine::FaultKind _kind) const
  {
    const std::optional<Machine::ServiceFault>& fault = m_services.Fault();
    return fault.has_value() && fault->kind == _kind;
  }

  [[nodiscard]] bool Carry() const noexcept
  {
    return (m_regs.flags & Machine::FLAG_CARRY) != 0;
  }

  [[nodiscard]] bool Zero() const noexcept
  {
    return (m_regs.flags & Machine::FLAG_ZERO) != 0;
  }

private:
  static constexpr std::uint16_t INT_BYTES = 2; // CD nn

  Machine::Memory m_memory;
  RecordingBus m_bus;
  Machine::Cycles m_clock = 0;
  ScratchDirectory m_files;
  Machine::DirectoryFileStore m_store;
  Machine::PcServices m_services;
  Machine::Registers m_regs{};
};

/// ELITES.EXE, found by walking up from the working directory to the repository root, or else beside the source
/// tree this file was compiled from (the parent of MachineTests/). Empty if neither has it.
inline std::vector<std::uint8_t> ReadReferenceBinary()
{
  std::vector<std::filesystem::path> candidates;
  std::error_code error;
  for (std::filesystem::path directory = std::filesystem::current_path(error); !error && !directory.empty();
       directory = directory.parent_path())
  {
    candidates.push_back(directory / "ELITES.EXE");
    if (directory == directory.parent_path())
    {
      break;
    }
  }
  const std::filesystem::path source = std::source_location::current().file_name();
  candidates.push_back(source.parent_path().parent_path() / "ELITES.EXE");
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
