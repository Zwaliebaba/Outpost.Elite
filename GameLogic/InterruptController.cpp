#include "pch.h"

#include "InterruptController.h"

namespace Elite
{

namespace
{

constexpr std::uint8_t LINE_COUNT = 8;
constexpr std::uint8_t LEVEL_MASK = 0x07;

[[nodiscard]] constexpr std::uint8_t Bit(std::uint8_t _line) noexcept
{
  return static_cast<std::uint8_t>(1u << (_line & LEVEL_MASK));
}

} // namespace

void InterruptController::RaiseIrq(std::uint8_t _line) noexcept
{
  m_requests = static_cast<std::uint8_t>(m_requests | Bit(_line));
}

std::uint8_t InterruptController::HighestPriority(std::uint8_t _lines) noexcept
{
  for (std::uint8_t line = 0; line < LINE_COUNT; ++line)
  {
    if ((_lines & Bit(line)) != 0)
    {
      return line;
    }
  }
  return NO_LINE;
}

std::uint8_t InterruptController::GrantableRequest() const noexcept
{
  const auto unmasked = static_cast<std::uint8_t>(m_requests & ~MASK);
  if (unmasked == 0)
  {
    return NO_LINE;
  }
  const std::uint8_t request = HighestPriority(unmasked);
  const std::uint8_t serving = HighestPriority(m_inService);
  // Fully nested: only a request of strictly higher priority than every level in service gets in.
  if (serving != NO_LINE && request >= serving)
  {
    return NO_LINE;
  }
  return request;
}

bool InterruptController::InterruptPending() const noexcept
{
  // The common case, nothing requested, costs one test.
  return m_requests != 0 && GrantableRequest() != NO_LINE;
}

std::uint8_t InterruptController::Acknowledge() noexcept
{
  const std::uint8_t line = GrantableRequest();
  m_requests = static_cast<std::uint8_t>(m_requests & ~Bit(line));
  m_inService = static_cast<std::uint8_t>(m_inService | Bit(line));
  return static_cast<std::uint8_t>(VECTOR_BASE | line);
}

void InterruptController::EndOfInterrupt() noexcept
{
  const std::uint8_t line = HighestPriority(m_inService);
  if (line != NO_LINE)
  {
    m_inService = static_cast<std::uint8_t>(m_inService & ~Bit(line));
  }
}

} // namespace Elite
