// Machine/PortRouter.h
#pragma once

#include "PortBus.h"

#include <cstdint>
#include <map>
#include <vector>

namespace Machine
{

/// The machine's whole I/O space: the PortBus the CPU is given. Each device's PortBus is mapped to
/// the port range it decodes, and every IN and OUT goes to the device that owns its port.
///
/// A port nobody owns reads 0xFF, the floating bus, and ignores writes; both are counted per port
/// so that the host can report what the program reached for that this machine does not have.
///
/// A 16-bit access is two byte accesses, to the port and the next one (PortBus), unless both ports
/// belong to one device, which then gets the 16-bit access itself and can decode it as it likes.
///
/// Ports decode all 16 address bits. The PC's own devices decode fewer and so answer at aliases
/// (the PIC at 0x22, for instance); nothing here does, and the game uses none.
class PortRouter final : public PortBus
{
public:
  struct UnmappedCount
  {
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
  };

  /// Not noexcept: the port table is allocated here.
  PortRouter();

  /// Gives ports _first to _last, inclusive, to _device, which must outlive the router. Returns
  /// false and maps nothing if the range is empty or overlaps one already mapped, or if 255
  /// devices are mapped already.
  [[nodiscard]] bool Map(std::uint16_t _first, std::uint16_t _last, PortBus& _device);

  [[nodiscard]] std::uint8_t In8(std::uint16_t _port) override;
  void Out8(std::uint16_t _port, std::uint8_t _value) override;
  [[nodiscard]] std::uint16_t In16(std::uint16_t _port) override;
  void Out16(std::uint16_t _port, std::uint16_t _value) override;

  /// Accesses to unmapped ports since construction or ClearUnmapped(), by port.
  [[nodiscard]] const std::map<std::uint16_t, UnmappedCount>& Unmapped() const noexcept
  {
    return m_unmapped;
  }

  /// Byte writes to any port, mapped or not, since construction. A word write counts as the device
  /// takes it: once when one device decodes both bytes, twice when it is split.
  [[nodiscard]] std::uint64_t WriteCount() const noexcept
  {
    return m_writes;
  }

  /// One read or write that reached the bus.
  struct Access
  {
    std::uint16_t port = 0;
    std::uint16_t value = 0;
    bool write = false;
    bool word = false;

    [[nodiscard]] bool operator==(const Access&) const noexcept = default;
  };

  /// Appends every access to _log from now on, in order, or stops when it is null: how a test sees the
  /// sequence of accesses native code makes. A word access the device does not take as a word is logged
  /// as the two byte accesses it becomes.
  void SetLog(std::vector<Access>* _log) noexcept
  {
    m_log = _log;
  }

  void ClearUnmapped() noexcept
  {
    m_unmapped.clear();
  }

private:
  [[nodiscard]] PortBus* DeviceAt(std::uint16_t _port) const noexcept
  {
    return m_devices[m_owner[_port]];
  }

  std::vector<std::uint8_t> m_owner; // per port: an index into m_devices, 0 for none
  std::vector<PortBus*> m_devices;   // m_devices[0] is null
  std::map<std::uint16_t, UnmappedCount> m_unmapped;
  std::uint64_t m_writes = 0;
  std::vector<Access>* m_log = nullptr;
};

} // namespace Machine
