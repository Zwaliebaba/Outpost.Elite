// GameLogic/DataField.h
#pragma once

#include <cstddef>
#include <cstdint>

namespace Elite
{

/// A byte or a word of the reference's data segment, by its offset (ADR-010). T is std::uint8_t or
/// std::uint16_t. The fields of DataOverlay are these and their kin below; Guest reads and writes them.
template <typename T> struct DataField
{
  std::uint16_t offset;
};

/// A run of Bytes bytes of the data segment.
template <std::size_t Bytes> struct DataBlock
{
  std::uint16_t offset;

  static constexpr std::size_t SIZE_BYTES = Bytes;

  /// The offset of the byte _index bytes in.
  [[nodiscard]] constexpr std::uint16_t At(std::size_t _index) const noexcept
  {
    return static_cast<std::uint16_t>(offset + _index);
  }
};

/// A table of Entries entries of EntryBytes bytes each.
template <std::size_t EntryBytes, std::size_t Entries> struct DataTable
{
  std::uint16_t offset;

  static constexpr std::size_t ENTRY_BYTES = EntryBytes;
  static constexpr std::size_t ENTRY_COUNT = Entries;

  /// The offset of entry _entry.
  [[nodiscard]] constexpr std::uint16_t At(std::size_t _entry) const noexcept
  {
    return static_cast<std::uint16_t>(offset + _entry * EntryBytes);
  }
};

/// A place in the data segment whose layout the symbol table does not give.
struct DataAt
{
  std::uint16_t offset;
};

} // namespace Elite
