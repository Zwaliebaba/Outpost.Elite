// Machine/Memory.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Machine
{

/// Every byte a run of writes changed, with what it held before (ADR-010): enough to undo the run, and to
/// know which bytes to compare. Its capacity is fixed when it is made, so recording never allocates;
/// writes past it are not recorded, and Overflowed() says so.
class WriteJournal
{
public:
  struct Entry
  {
    std::uint32_t linear = 0;
    std::uint8_t before = 0;
  };

  explicit WriteJournal(std::size_t _capacity)
    : m_entries(_capacity)
  {
  }

  void Record(std::uint32_t _linear, std::uint8_t _before) noexcept
  {
    if (m_count < m_entries.size())
    {
      m_entries[m_count++] = Entry{_linear, _before};
    }
    else
    {
      m_overflowed = true;
    }
  }

  void Clear() noexcept
  {
    m_count = 0;
    m_overflowed = false;
  }

  [[nodiscard]] std::span<const Entry> Entries() const noexcept
  {
    return std::span<const Entry>(m_entries).first(m_count);
  }

  [[nodiscard]] bool Overflowed() const noexcept
  {
    return m_overflowed;
  }

private:
  std::vector<Entry> m_entries;
  std::size_t m_count = 0;
  bool m_overflowed = false;
};

/// The 8088's 1 MiB address space, all of it RAM.
///
/// Linear addresses are 20 bits wide and wrap: anything above 0xFFFFF lands at the bottom again, as
/// on the 8088, which has no A20 line to stop it. A word access by linear address takes its second
/// byte from the next linear address, wrapped the same way. A word access by (segment, offset) takes
/// its second byte from offset + 1 of the same segment, so offset 0xFFFF pairs with offset 0x0000.
///
/// Devices that decode memory (the CGA, a ROM) are not modelled here yet; when they are, they hook in
/// below this interface and the CPU does not change.
class Memory
{
public:
  static constexpr std::uint32_t SIZE_BYTES = 0x100000;
  static constexpr std::uint32_t ADDRESS_MASK = SIZE_BYTES - 1;

  Memory();

  [[nodiscard]] static constexpr std::uint32_t Linear(std::uint16_t _segment, std::uint16_t _offset) noexcept
  {
    return ((static_cast<std::uint32_t>(_segment) << 4) + _offset) & ADDRESS_MASK;
  }

  [[nodiscard]] std::uint8_t Read8(std::uint32_t _linear) const noexcept
  {
    return m_bytes[_linear & ADDRESS_MASK];
  }

  void Write8(std::uint32_t _linear, std::uint8_t _value) noexcept
  {
    std::uint8_t& byte = m_bytes[_linear & ADDRESS_MASK];
    if (byte != _value)
    {
      if (m_journal != nullptr)
      {
        m_journal->Record(_linear & ADDRESS_MASK, byte);
      }
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

  /// Sets every byte to zero.
  void Clear() noexcept;

  /// Writes that changed a byte, counted since construction. A write of the value already there is
  /// not counted, so a loop that only stores what it found leaves the count alone: Pc's paced time
  /// uses that to tell a program that is waiting from one that is working.
  [[nodiscard]] std::uint64_t ChangeCount() const noexcept
  {
    return m_changes;
  }

  /// Records every byte a write changes into _journal from now on, or stops recording when it is null.
  void SetJournal(WriteJournal* _journal) noexcept
  {
    m_journal = _journal;
  }

  /// Puts back what _journal recorded, newest first, so memory is as it was when the journal began.
  /// Neither counted as changes nor recorded.
  void Undo(const WriteJournal& _journal) noexcept;

  /// The whole address space, for loaders, snapshots and the conformance runner.
  [[nodiscard]] std::span<std::uint8_t> Bytes() noexcept
  {
    return m_bytes;
  }

  [[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept
  {
    return m_bytes;
  }

private:
  std::vector<std::uint8_t> m_bytes;
  std::uint64_t m_changes = 0;
  WriteJournal* m_journal = nullptr;
};

} // namespace Machine
