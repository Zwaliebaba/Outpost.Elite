// GameLogic/Hardware.h
#pragma once

#include <cstdint>

namespace Machine
{
class Pc;
} // namespace Machine

namespace Elite
{

/// The IBM PC's devices as the game drives them (ADR-014): what a de-assembled routine does to the speaker, the timer and
/// the interrupt controller, named for what it does rather than by port. Each operation is the original's own sequence of
/// port accesses, in its order and at its width, so the emulated devices and paced time's wait detection see what they
/// saw. When the interpreter goes (D7), the implementation becomes native devices and the routines do not change.
class Hardware
{
public:
  explicit Hardware(Machine::Pc& _pc) noexcept
    : m_pc(_pc)
  {
  }

  /// IN AL,61h: the system control port, the speaker's gate and data in bits 0 and 1.
  [[nodiscard]] std::uint8_t SystemControl();

  /// OUT 61h,_value.
  void SetSystemControl(std::uint8_t _value);

  /// The speaker's tone: OUT 43h,B6h, then _divisor's low and high bytes to the PIT's channel 2 at 42h.
  void SetToneDivisor(std::uint16_t _divisor);

  /// The timer's tick: OUT 43h,_mode, then _divisor's low and high bytes to the PIT's channel 0 at 40h.
  void SetTickDivisor(std::uint8_t _mode, std::uint16_t _divisor);

  /// OUT 20h,20h: the end of an interrupt, to the interrupt controller.
  void EndOfInterrupt();

  /// STI: interrupts are taken again when they fall due.
  void EnableInterrupts() noexcept;

  /// CLI.
  void DisableInterrupts() noexcept;

private:
  Machine::Pc& m_pc;
};

} // namespace Elite
