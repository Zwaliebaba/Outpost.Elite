#include "pch.h"

#include "Memory.h"

#include <algorithm>

namespace Machine
{

Memory::Memory()
  : m_bytes(SIZE_BYTES, 0)
{
}

void Memory::Load(std::uint32_t _linear, std::span<const std::uint8_t> _bytes) noexcept
{
  std::uint32_t address = _linear;
  for (const std::uint8_t value : _bytes)
  {
    Write8(address, value);
    address = (address + 1) & ADDRESS_MASK;
  }
}

void Memory::Clear() noexcept
{
  std::fill(m_bytes.begin(), m_bytes.end(), std::uint8_t{0});
}

void Memory::Undo(const WriteJournal& _journal) noexcept
{
  const std::span<const WriteJournal::Entry> entries = _journal.Entries();
  for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
  {
    m_bytes[entry->linear] = entry->before;
  }
}

} // namespace Machine
