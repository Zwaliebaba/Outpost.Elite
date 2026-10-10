// Machine/Memory.h
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Machine
{

/// The 8088's 1 MiB address space, all of it RAM.
///
/// Linear addresses are 20 bits wide and wrap: anything above 0xFFFFF lands at the bottom again, as
/// on the 8088, which has no A20 line to stop it. A word access by linear address takes its second
/// byte from the next linear address, wrapped the same way. A word access by (segment, offset) takes
/// its second byte from offset + 1 of the same segment, so offset 0xFFFF pairs with offset 0x0000.
///
/// Memory holds its own bytes, but a region of whole paragraphs can be mapped to storage it does not
/// own (Map): every read and write there, by any path, then reaches that storage instead. Each
/// paragraph is looked up in a table, so a mapped byte costs what an own one does.
///
/// Devices that decode memory (the CGA, a ROM) are not modelled here yet; when they are, they hook in
/// below this interface and the code that reads and writes it does not change.
class Memory
{
public:
  static constexpr std::uint32_t SIZE_BYTES = 0x100000;
  static constexpr std::uint32_t ADDRESS_MASK = SIZE_BYTES - 1;
  static constexpr std::uint32_t PARAGRAPH_BYTES = 16;

  Memory();
  Memory(const Memory&) = delete;
  Memory& operator=(const Memory&) = delete;

  [[nodiscard]] static constexpr std::uint32_t Linear(std::uint16_t _segment, std::uint16_t _offset) noexcept
  {
    return ((static_cast<std::uint32_t>(_segment) << 4) + _offset) & ADDRESS_MASK;
  }

  [[nodiscard]] std::uint8_t Read8(std::uint32_t _linear) const noexcept
  {
    return *At(_linear);
  }

  void Write8(std::uint32_t _linear, std::uint8_t _value) noexcept
  {
    std::uint8_t& byte = *At(_linear);
    if (byte != _value)
    {
      byte = _value;
      ++m_changes;
    }
  }

  [[nodiscard]] std::uint16_t Read16(std::uint32_t _linear) const noexcept
  {
    return static_cast<std::uint16_t>(Read8(_linear) | (Read8(_linear + 1) << 8));
  }

  void Write16(std::uint32_t _linear, std::uint16_t _value) noexcept
  {
    Write8(_linear, static_cast<std::uint8_t>(_value & 0xFF));
    Write8(_linear + 1, static_cast<std::uint8_t>(_value >> 8));
  }

  [[nodiscard]] std::uint8_t Read8(std::uint16_t _segment, std::uint16_t _offset) const noexcept
  {
    return Read8(Linear(_segment, _offset));
  }

  void Write8(std::uint16_t _segment, std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    Write8(Linear(_segment, _offset), _value);
  }

  [[nodiscard]] std::uint16_t Read16(std::uint16_t _segment, std::uint16_t _offset) const noexcept
  {
    const auto next = static_cast<std::uint16_t>(_offset + 1);
    return static_cast<std::uint16_t>(Read8(_segment, _offset) | (Read8(_segment, next) << 8));
  }

  void Write16(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    const auto next = static_cast<std::uint16_t>(_offset + 1);
    Write8(_segment, _offset, static_cast<std::uint8_t>(_value & 0xFF));
    Write8(_segment, next, static_cast<std::uint8_t>(_value >> 8));
  }

  /// Copies bytes in at a linear address, wrapping at 1 MiB.
  void Load(std::uint32_t _linear, std::span<const std::uint8_t> _bytes) noexcept;

  /// From now on the bytes from linear address _first are _storage's: every read and write of them, by linear
  /// address or by segment and offset, reaches _storage, and a write that changes a byte is counted as any other
  /// (ChangeCount). _storage is not Memory's, and must outlive it. Mapping takes over the bytes already there: they
  /// are copied into _storage, uncounted, so a program sees no change. A region is whole paragraphs from a
  /// paragraph boundary, inside the 1 MiB and clear of every region mapped before; otherwise Map throws
  /// std::invalid_argument and maps nothing.
  void Map(std::uint32_t _first, std::span<std::uint8_t> _storage);

  /// Sets every byte to zero, mapped regions' included.
  void Clear() noexcept;

  /// Writes that changed a byte, counted since construction. A write of the value already there is
  /// not counted, so a loop that only stores what it found leaves the count alone: Pc's paced time
  /// uses that to tell a program that is waiting from one that is working.
  [[nodiscard]] std::uint64_t ChangeCount() const noexcept
  {
    return m_changes;
  }

  /// Memory's own bytes, the whole address space, for snapshots. In a mapped region they are what was there when
  /// it was mapped (Map): the region's bytes are its storage's.
  [[nodiscard]] std::span<std::uint8_t> Bytes() noexcept
  {
    return m_bytes;
  }

  [[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept
  {
    return m_bytes;
  }

private:
  static constexpr std::uint32_t PARAGRAPH_COUNT = SIZE_BYTES / PARAGRAPH_BYTES;

  /// Where the byte at _linear is: in Memory's own bytes, or in a mapped region's storage.
  [[nodiscard]] std::uint8_t* At(std::uint32_t _linear) const noexcept
  {
    const std::uint32_t linear = _linear & ADDRESS_MASK;
    return m_paragraphs[linear / PARAGRAPH_BYTES] + linear % PARAGRAPH_BYTES;
  }

  std::vector<std::uint8_t> m_bytes;
  // Each paragraph's 16 bytes: Memory's own, or a mapped region's.
  std::vector<std::uint8_t*> m_paragraphs;
  std::uint64_t m_changes = 0;
};

} // namespace Machine
