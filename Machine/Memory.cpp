#include "pch.h"

#include "Memory.h"

#include <algorithm>
#include <stdexcept>

namespace Machine
{

Memory::Memory()
  : m_bytes(SIZE_BYTES, 0),
    m_paragraphs(PARAGRAPH_COUNT)
{
  for (std::size_t paragraph = 0; paragraph < PARAGRAPH_COUNT; ++paragraph)
  {
    m_paragraphs[paragraph] = m_bytes.data() + paragraph * PARAGRAPH_BYTES;
  }
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

void Memory::Map(std::uint32_t _first, std::span<std::uint8_t> _storage)
{
  if (_first % PARAGRAPH_BYTES != 0 || _storage.empty() || _storage.size() % PARAGRAPH_BYTES != 0 || _first >= SIZE_BYTES ||
      _storage.size() > SIZE_BYTES - _first)
  {
    throw std::invalid_argument("Memory::Map: a region is whole paragraphs inside the 1 MiB");
  }
  const std::size_t first = _first / PARAGRAPH_BYTES;
  const std::size_t count = _storage.size() / PARAGRAPH_BYTES;
  for (std::size_t paragraph = first; paragraph < first + count; ++paragraph)
  {
    if (m_paragraphs[paragraph] != m_bytes.data() + paragraph * PARAGRAPH_BYTES)
    {
      throw std::invalid_argument("Memory::Map: a region overlaps one mapped before");
    }
  }
  std::copy_n(m_bytes.data() + _first, _storage.size(), _storage.data());
  for (std::size_t paragraph = 0; paragraph < count; ++paragraph)
  {
    m_paragraphs[first + paragraph] = _storage.data() + paragraph * PARAGRAPH_BYTES;
  }
}

void Memory::Clear() noexcept
{
  for (std::uint8_t* const paragraph : m_paragraphs)
  {
    std::fill_n(paragraph, PARAGRAPH_BYTES, std::uint8_t{0});
  }
}

} // namespace Machine
