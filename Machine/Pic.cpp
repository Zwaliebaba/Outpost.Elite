#include "pch.h"

#include "Pic.h"

namespace Machine
{

namespace
{

constexpr std::uint8_t ICW1 = 0x10;
constexpr std::uint8_t ICW1_NEEDS_ICW4 = 0x01;
constexpr std::uint8_t ICW1_SINGLE = 0x02;
constexpr std::uint8_t ICW4_AUTO_EOI = 0x02;
constexpr std::uint8_t OCW3 = 0x08;
constexpr std::uint8_t OCW3_READ_REGISTER = 0x02;
constexpr std::uint8_t OCW3_READ_IN_SERVICE = 0x01;
constexpr std::uint8_t VECTOR_BASE_MASK = 0xF8;
constexpr std::uint8_t LEVEL_MASK = 0x07;
constexpr std::uint8_t SPURIOUS_LINE = 7;

// OCW2's R, SL and EOI bits, bits 7-5.
constexpr std::uint8_t OCW2_COMMAND_SHIFT = 5;
constexpr std::uint8_t OCW2_ROTATE_AUTO_EOI_CLEAR = 0;
constexpr std::uint8_t OCW2_NON_SPECIFIC_EOI = 1;
constexpr std::uint8_t OCW2_SPECIFIC_EOI = 3;
constexpr std::uint8_t OCW2_ROTATE_AUTO_EOI_SET = 4;
constexpr std::uint8_t OCW2_ROTATE_NON_SPECIFIC_EOI = 5;
constexpr std::uint8_t OCW2_SET_PRIORITY = 6;
constexpr std::uint8_t OCW2_ROTATE_SPECIFIC_EOI = 7;

[[nodiscard]] constexpr std::uint8_t Bit(std::uint8_t _line) noexcept
{
  return static_cast<std::uint8_t>(1u << (_line & LEVEL_MASK));
}

} // namespace

Pic::Pic() noexcept
{
  Reset();
}

void Pic::Reset() noexcept
{
  m_requests = 0;
  m_inService = 0;
  m_mask = POWER_ON_MASK;
  m_vectorBase = POWER_ON_VECTOR_BASE;
  m_lowestPriority = 7;
  m_expect = Expect::Mask;
  m_single = true;
  m_needsMode = true;
  m_autoEoi = false;
  m_rotateOnAutoEoi = false;
  m_readInService = false;
}

void Pic::RaiseIrq(std::uint8_t _line) noexcept
{
  m_requests = static_cast<std::uint8_t>(m_requests | Bit(_line));
}

std::uint8_t Pic::Rank(std::uint8_t _line) const noexcept
{
  return static_cast<std::uint8_t>((_line - m_lowestPriority - 1u) & LEVEL_MASK);
}

std::uint8_t Pic::HighestPriority(std::uint8_t _lines) const noexcept
{
  // Walk the lines from the highest priority down: the one after the lowest-priority line first.
  for (std::uint8_t step = 1; step <= 8; ++step)
  {
    const auto line = static_cast<std::uint8_t>((m_lowestPriority + step) & LEVEL_MASK);
    if ((_lines & Bit(line)) != 0)
    {
      return line;
    }
  }
  return NO_LINE;
}

std::uint8_t Pic::GrantableRequest() const noexcept
{
  const auto unmasked = static_cast<std::uint8_t>(m_requests & ~m_mask);
  if (unmasked == 0)
  {
    return NO_LINE;
  }
  const std::uint8_t request = HighestPriority(unmasked);
  const std::uint8_t serving = HighestPriority(m_inService);
  // Fully nested: only a request of strictly higher priority than every level in service gets in.
  if (serving != NO_LINE && Rank(request) >= Rank(serving))
  {
    return NO_LINE;
  }
  return request;
}

bool Pic::InterruptPending() const noexcept
{
  // The common case, nothing requested, costs one test.
  return m_requests != 0 && GrantableRequest() != NO_LINE;
}

std::uint8_t Pic::AcknowledgeInterrupt() noexcept
{
  const std::uint8_t line = GrantableRequest();
  if (line == NO_LINE)
  {
    return static_cast<std::uint8_t>(m_vectorBase | SPURIOUS_LINE);
  }
  m_requests = static_cast<std::uint8_t>(m_requests & ~Bit(line));
  if (!m_autoEoi)
  {
    m_inService = static_cast<std::uint8_t>(m_inService | Bit(line));
  }
  else if (m_rotateOnAutoEoi)
  {
    m_lowestPriority = line;
  }
  return static_cast<std::uint8_t>(m_vectorBase | line);
}

std::uint8_t Pic::In8(std::uint16_t _port) noexcept
{
  if ((_port & 1u) == 0)
  {
    return m_readInService ? m_inService : m_requests;
  }
  return m_mask;
}

void Pic::Out8(std::uint16_t _port, std::uint8_t _value) noexcept
{
  if ((_port & 1u) == 0)
  {
    WriteCommand(_value);
  }
  else
  {
    WriteData(_value);
  }
}

void Pic::WriteCommand(std::uint8_t _value) noexcept
{
  if ((_value & ICW1) != 0)
  {
    m_single = (_value & ICW1_SINGLE) != 0;
    m_needsMode = (_value & ICW1_NEEDS_ICW4) != 0;
    m_mask = 0;
    m_requests = 0; // the edge-sense latches are reset: a request has to edge again
    m_inService = 0;
    m_lowestPriority = 7;
    m_readInService = false;
    m_autoEoi = false; // ICW4's functions are zero unless ICW4 sets them
    m_rotateOnAutoEoi = false;
    m_expect = Expect::VectorBase;
    return;
  }
  if ((_value & OCW3) != 0)
  {
    if ((_value & OCW3_READ_REGISTER) != 0)
    {
      m_readInService = (_value & OCW3_READ_IN_SERVICE) != 0;
    }
    return;
  }

  const auto level = static_cast<std::uint8_t>(_value & LEVEL_MASK);
  switch (_value >> OCW2_COMMAND_SHIFT)
  {
  case OCW2_NON_SPECIFIC_EOI:
    EndOfInterrupt(HighestPriority(m_inService), false);
    break;
  case OCW2_SPECIFIC_EOI:
    EndOfInterrupt(level, false);
    break;
  case OCW2_ROTATE_NON_SPECIFIC_EOI:
    EndOfInterrupt(HighestPriority(m_inService), true);
    break;
  case OCW2_ROTATE_SPECIFIC_EOI:
    EndOfInterrupt(level, true);
    break;
  case OCW2_SET_PRIORITY:
    m_lowestPriority = level;
    break;
  case OCW2_ROTATE_AUTO_EOI_SET:
    m_rotateOnAutoEoi = true;
    break;
  case OCW2_ROTATE_AUTO_EOI_CLEAR:
    m_rotateOnAutoEoi = false;
    break;
  default: // 2: no operation
    break;
  }
}

void Pic::WriteData(std::uint8_t _value) noexcept
{
  switch (m_expect)
  {
  case Expect::VectorBase:
    m_vectorBase = static_cast<std::uint8_t>(_value & VECTOR_BASE_MASK);
    if (!m_single)
    {
      m_expect = Expect::Cascade;
    }
    else
    {
      m_expect = m_needsMode ? Expect::Mode : Expect::Mask;
    }
    break;
  case Expect::Cascade:
    m_expect = m_needsMode ? Expect::Mode : Expect::Mask;
    break;
  case Expect::Mode:
    m_autoEoi = (_value & ICW4_AUTO_EOI) != 0;
    m_expect = Expect::Mask;
    break;
  case Expect::Mask:
    m_mask = _value;
    break;
  }
}

void Pic::EndOfInterrupt(std::uint8_t _line, bool _rotate) noexcept
{
  if (_line == NO_LINE)
  {
    return;
  }
  m_inService = static_cast<std::uint8_t>(m_inService & ~Bit(_line));
  if (_rotate)
  {
    m_lowestPriority = static_cast<std::uint8_t>(_line & LEVEL_MASK);
  }
}

} // namespace Machine
