// GameLogic/InterruptController.h
#pragma once

#include <cstdint>

namespace Elite
{

/// The IBM PC's 8259A interrupt controller as the game uses it (ADR-006 item 2): one controller and eight request lines,
/// IRQ0-IRQ7. It is the emulated PC's model (Machine::Pic until D33) without its ports: the game only ends interrupts, so
/// the initialisation and operation words it never writes went with them, and with them rotation, automatic end of
/// interrupt and the register reads.
///
/// It is as the BIOS leaves it, from power-on: vector base 8, so IRQn is int 8+n; fully nested, IRQ0 the highest priority
/// and IRQ7 the lowest; and the mask BCh, which leaves only IRQ0 (the timer), IRQ1 (the keyboard) and IRQ6 (the floppy
/// controller, which nothing here raises) unmasked.
///
/// Requests. RaiseIrq(n) is an edge on line n: it sets request bit n, masked or not, and the bit stays set until the request
/// is acknowledged; a second edge while it is set is not queued. A request is granted when it is unmasked and its priority is
/// higher than that of every level in service. Acknowledge, the CPU's acknowledge cycle, moves the highest such request into
/// service and returns its vector; with nothing to grant it returns IRQ7's without touching the levels in service, the
/// 8259A's spurious interrupt. EndOfInterrupt, OUT 20h,20h, the non-specific end of interrupt that the game and the BIOS
/// send, ends the highest level in service.
class InterruptController
{
public:
  static constexpr std::uint8_t VECTOR_BASE = 0x08;
  static constexpr std::uint8_t MASK = 0xBC;

  /// An edge on request line _line (0-7); higher bits are ignored.
  void RaiseIrq(std::uint8_t _line) noexcept;

  /// Whether a request would be granted now.
  [[nodiscard]] bool InterruptPending() const noexcept;

  /// The acknowledge cycle: the vector the controller puts on the bus.
  [[nodiscard]] std::uint8_t Acknowledge() noexcept;

  /// The non-specific end of interrupt.
  void EndOfInterrupt() noexcept;

  [[nodiscard]] std::uint8_t RequestRegister() const noexcept
  {
    return m_requests;
  }

  [[nodiscard]] std::uint8_t InServiceRegister() const noexcept
  {
    return m_inService;
  }

private:
  // The line among _lines with the highest priority, or NO_LINE: with IRQ0 the highest, the lowest line set.
  [[nodiscard]] static std::uint8_t HighestPriority(std::uint8_t _lines) noexcept;
  // The request the CPU would be given now, or NO_LINE.
  [[nodiscard]] std::uint8_t GrantableRequest() const noexcept;

  static constexpr std::uint8_t NO_LINE = 0xFF;

  std::uint8_t m_requests = 0;  // IRR
  std::uint8_t m_inService = 0; // ISR
};

} // namespace Elite
