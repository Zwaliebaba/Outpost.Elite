#include "pch.h"

#include "TestReader.h"

#include <charconv>
#include <format>
#include <string_view>
#include <system_error>

namespace CpuConformance
{

namespace
{

// Splits off the next space-separated token; empty when the line is used up.
std::string_view NextToken(std::string_view& _rest) noexcept
{
  const std::size_t start = _rest.find_first_not_of(' ');
  if (start == std::string_view::npos)
  {
    _rest = {};
    return {};
  }
  _rest.remove_prefix(start);
  const std::size_t end = _rest.find(' ');
  const std::string_view token = _rest.substr(0, end);
  _rest.remove_prefix(end == std::string_view::npos ? _rest.size() : end);
  return token;
}

bool ParseHex(std::string_view _token, std::uint32_t& _value) noexcept
{
  const char* const first = _token.data();
  const char* const last = first + _token.size();
  const auto [end, error] = std::from_chars(first, last, _value, 16);
  return error == std::errc{} && end == last && !_token.empty();
}

bool ParseRegisters(std::string_view _rest, Machine::Registers& _registers) noexcept
{
  for (const auto field : REGISTER_ORDER)
  {
    std::uint32_t value = 0;
    if (!ParseHex(NextToken(_rest), value) || value > 0xFFFF)
    {
      return false;
    }
    _registers.*field = static_cast<std::uint16_t>(value);
  }
  return NextToken(_rest).empty();
}

bool ParseMemory(std::string_view _rest, std::vector<MemoryByte>& _memory)
{
  _memory.clear();
  for (;;)
  {
    const std::string_view addressToken = NextToken(_rest);
    if (addressToken.empty())
    {
      return true;
    }
    std::uint32_t address = 0;
    std::uint32_t value = 0;
    if (!ParseHex(addressToken, address) || !ParseHex(NextToken(_rest), value) || value > 0xFF)
    {
      return false;
    }
    _memory.push_back(MemoryByte{address, static_cast<std::uint8_t>(value)});
  }
}

} // namespace

TestReader::TestReader(const std::string& _path)
  : m_stream(_path)
{
}

bool TestReader::Next(TestCase& _test)
{
  bool started = false;
  while (std::getline(m_stream, m_line))
  {
    ++m_lineNumber;
    if (!m_line.empty() && m_line.back() == '\r')
    {
      m_line.pop_back();
    }
    if (m_line.empty() || m_line.front() == '#')
    {
      continue;
    }
    std::string_view rest = m_line;
    const std::string_view key = NextToken(rest);
    bool parsed = true;
    if (key == "t")
    {
      std::uint32_t index = 0;
      std::uint32_t mask = 0;
      parsed = ParseHex(NextToken(rest), index) && ParseHex(NextToken(rest), mask) && mask <= 0xFFFF;
      _test.index = index;
      _test.flagsMask = static_cast<std::uint16_t>(mask);
      const std::size_t start = rest.find_first_not_of(' ');
      _test.name = start == std::string_view::npos ? std::string{} : std::string{rest.substr(start)};
      started = true;
    }
    else if (started && key == "b")
    {
      _test.bytes = std::string{NextToken(rest)};
    }
    else if (started && key == "i")
    {
      parsed = ParseRegisters(rest, _test.initial);
    }
    else if (started && key == "m")
    {
      parsed = ParseMemory(rest, _test.initialMemory);
    }
    else if (started && key == "f")
    {
      parsed = ParseRegisters(rest, _test.expected);
    }
    else if (started && key == "n")
    {
      parsed = ParseMemory(rest, _test.expectedMemory);
      if (parsed)
      {
        return true;
      }
    }
    else
    {
      parsed = false; // an unknown line, or a test line before any `t`
    }
    if (!parsed)
    {
      m_error = std::format("line {}: cannot read '{}'", m_lineNumber, m_line.substr(0, 60));
      return false;
    }
  }
  if (started)
  {
    m_error = std::format("line {}: the file ends inside a test", m_lineNumber);
  }
  return false;
}

} // namespace CpuConformance
