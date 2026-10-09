#include "pch.h"

#include "TestRunner.h"

#include <array>
#include <cstring>
#include <format>
#include <string_view>

namespace CpuConformance
{

namespace
{

// FLAGS bits by name, most significant first, as debuggers print them.
constexpr std::string_view FLAG_LETTERS = "oditszapc";
constexpr std::array<std::uint16_t, 9> FLAG_BITS = {0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0x0040, 0x0010, 0x0004, 0x0001};

std::string DescribeFlags(std::uint16_t _flags)
{
  std::string text;
  for (std::size_t index = 0; index < FLAG_LETTERS.size(); ++index)
  {
    text += (_flags & FLAG_BITS[index]) != 0 ? static_cast<char>(FLAG_LETTERS[index] - 'a' + 'A') : '.';
  }
  return text;
}

constexpr std::size_t STRAY_REPORT_LIMIT = 4;

} // namespace

TestRunner::TestRunner()
  : m_cpu(m_memory, m_ports),
    m_zeros(Machine::Memory::SIZE_BYTES, 0)
{
}

std::vector<std::string> TestRunner::Run(const TestCase& _test)
{
  std::vector<std::string> mismatches;

  for (const MemoryByte& byte : _test.initialMemory)
  {
    m_memory.Write8(byte.address, byte.value);
  }
  m_cpu.Reset();
  m_cpu.Regs() = _test.initial;
  (void)m_cpu.Step();

  const Machine::Registers& actual = m_cpu.Regs();
  for (std::size_t index = 0; index < REGISTER_ORDER.size(); ++index)
  {
    const auto field = REGISTER_ORDER[index];
    std::uint16_t wanted = _test.expected.*field;
    std::uint16_t got = actual.*field;
    if (field == &Machine::Registers::flags)
    {
      wanted = static_cast<std::uint16_t>(wanted & _test.flagsMask);
      got = static_cast<std::uint16_t>(got & _test.flagsMask);
      if (wanted != got)
      {
        mismatches.push_back(std::format("flags: expected {:04X} {} got {:04X} {} (mask {:04X}, initial {:04X})", wanted,
                                         DescribeFlags(wanted), got, DescribeFlags(got), _test.flagsMask, _test.initial.flags));
      }
    }
    else if (wanted != got)
    {
      mismatches.push_back(
        std::format("{}: expected {:04X} got {:04X} (initial {:04X})", REGISTER_NAMES[index], wanted, got, _test.initial.*field));
    }
  }

  // Every byte the test names, then everything else, which must still be zero.
  for (const MemoryByte& byte : _test.expectedMemory)
  {
    const std::uint8_t got = m_memory.Read8(byte.address);
    if (got != byte.value)
    {
      mismatches.push_back(std::format("ram[{:05X}]: expected {:02X} got {:02X}", byte.address, byte.value, got));
    }
    m_memory.Write8(byte.address, 0);
  }
  for (const MemoryByte& byte : _test.initialMemory)
  {
    m_memory.Write8(byte.address, 0);
  }
  const auto bytes = m_memory.Bytes();
  if (std::memcmp(bytes.data(), m_zeros.data(), bytes.size()) != 0)
  {
    std::size_t reported = 0;
    for (std::size_t address = 0; address < bytes.size(); ++address)
    {
      if (bytes[address] != 0)
      {
        if (reported < STRAY_REPORT_LIMIT)
        {
          mismatches.push_back(std::format("ram[{:05X}]: written {:02X}, which the test does not expect", address, bytes[address]));
        }
        ++reported;
        bytes[address] = 0;
      }
    }
  }
  return mismatches;
}

} // namespace CpuConformance
