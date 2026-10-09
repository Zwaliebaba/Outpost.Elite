// Machine/Pic.h
#pragma once

#include "InterruptSource.h"
#include "PortBus.h"

#include <cstdint>

namespace Machine
{

/// The 8259A programmable interrupt controller, as the IBM PC wires it: one controller, eight
/// request lines IRQ0-IRQ7, at ports 0x20 (A0 = 0) and 0x21 (A0 = 1).
///
/// Power-on state. What the PC BIOS leaves behind, since there is no BIOS ROM here to program it
/// (ADR-005): edge-triggered, single controller, 8086 mode, vector base 8 (so IRQn is INT 8+n),
/// fully nested with IRQ0 the highest priority, normal EOI, and the mask register at 0xBC, which
/// leaves only IRQ0 (the timer), IRQ1 (the keyboard) and IRQ6 (the floppy controller) unmasked.
///
/// Requests. RaiseIrq(n) is an edge on line n: it sets bit n of the request register (IRR), masked
/// or not, and the bit stays set until the request is acknowledged. A second edge while the bit is
/// still set is not queued. A request is granted when it is unmasked and its priority is higher
/// than that of every level in service (ISR). The CPU's acknowledge, AcknowledgeInterrupt(), moves
/// the highest such request from IRR to ISR and returns its vector; with nothing to grant it returns
/// IRQ7's vector without touching ISR, the 8259A's spurious interrupt.
///
/// Programming. Port 0x20 takes ICW1 (bit 4 set), OCW3 (bits 4-3 = 01) or OCW2 (bits 4-3 = 00).
/// ICW1 restarts initialisation: it clears IMR, IRR and ISR, makes IRQ7 the lowest priority and
/// selects IRR for reading, and the following writes to 0x21 are ICW2 (the vector base, low three
/// bits ignored), ICW3 when ICW1 did not say single, and ICW4 when ICW1 asked for it. Outside that
/// sequence a write to 0x21 is OCW1, the mask register, and a read of 0x21 returns it. OCW2 does
/// non-specific and specific EOI, both with and without rotation, set-priority, and the rotate-in-
/// automatic-EOI switch. OCW3 selects whether reads of 0x20 return IRR or ISR, and the choice holds
/// until the next OCW3 that changes it.
///
/// Not modelled, because nothing on this machine uses them: level-triggered mode (ICW1 bit 3; edges
/// are all RaiseIrq can express), cascading, buffered mode, 8080 mode, the poll command and special
/// mask mode (OCW3 bits 2, 5 and 6), and special fully nested mode. Ports decode A0 only, so any
/// even port is 0x20 and any odd one 0x21, as on the PC's motherboard.
class Pic final : public InterruptSource, public PortBus
{
public:
  static constexpr std::uint16_t COMMAND_PORT = 0x20;
  static constexpr std::uint16_t DATA_PORT = 0x21;

  static constexpr std::uint8_t POWER_ON_VECTOR_BASE = 0x08;
  static constexpr std::uint8_t POWER_ON_MASK = 0xBC;

  Pic() noexcept;

  /// The power-on state described above.
  void Reset() noexcept;

  /// An edge on request line _line (0-7); higher bits are ignored.
  void RaiseIrq(std::uint8_t _line) noexcept;

  [[nodiscard]] bool InterruptPending() const noexcept override;
  [[nodiscard]] std::uint8_t AcknowledgeInterrupt() noexcept override;

  [[nodiscard]] std::uint8_t In8(std::uint16_t _port) noexcept override;
  void Out8(std::uint16_t _port, std::uint8_t _value) noexcept override;

  [[nodiscard]] std::uint8_t RequestRegister() const noexcept
  {
    return m_requests;
  }

  [[nodiscard]] std::uint8_t InServiceRegister() const noexcept
  {
    return m_inService;
  }

  [[nodiscard]] std::uint8_t MaskRegister() const noexcept
  {
    return m_mask;
  }

  [[nodiscard]] std::uint8_t VectorBase() const noexcept
  {
    return m_vectorBase;
  }

private:
  // What the next write to the data port is: an initialisation word, or the mask (OCW1).
  enum class Expect : std::uint8_t
  {
    Mask,
    VectorBase, // ICW2
    Cascade,    // ICW3
    Mode        // ICW4
  };

  // The line among _lines with the highest priority, or NO_LINE.
  [[nodiscard]] std::uint8_t HighestPriority(std::uint8_t _lines) const noexcept;
  // 0 for the highest priority, 7 for the lowest.
  [[nodiscard]] std::uint8_t Rank(std::uint8_t _line) const noexcept;
  // The request the CPU would be given now, or NO_LINE.
  [[nodiscard]] std::uint8_t GrantableRequest() const noexcept;

  void WriteCommand(std::uint8_t _value) noexcept;
  void WriteData(std::uint8_t _value) noexcept;
  void EndOfInterrupt(std::uint8_t _line, bool _rotate) noexcept;

  static constexpr std::uint8_t NO_LINE = 0xFF;

  std::uint8_t m_requests = 0;  // IRR
  std::uint8_t m_inService = 0; // ISR
  std::uint8_t m_mask = POWER_ON_MASK;
  std::uint8_t m_vectorBase = POWER_ON_VECTOR_BASE;
  std::uint8_t m_lowestPriority = 7;
  Expect m_expect = Expect::Mask;
  bool m_single = true;
  bool m_needsMode = true;
  bool m_autoEoi = false;
  bool m_rotateOnAutoEoi = false;
  bool m_readInService = false;
};

} // namespace Machine
