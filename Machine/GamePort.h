// Machine/GamePort.h
#pragma once

#include "PortBus.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Machine
{

/// The IBM game control adapter at port 0x201: four resistance-timed one-shots for two joysticks'
/// axes, and four buttons.
///
/// A write, of any value, fires the one-shots at the current cycle. Bit i (0-3) of a read is 1 from
/// then until the one-shot for axis i times out, start + OneShotCycles(resistance of axis i), and 0
/// after. The 558 timers are not retriggerable: a write while a one-shot is still timing leaves that
/// one alone. Bits 4-7 are the buttons, 0 while pressed. The game's ReadJoystickAxes (0x777E,
/// Reference-Map.md) counts polling loops until a bit drops, so the timing is kept to the cycle.
///
/// The host sets each axis's resistance, 0-100 kOhm, from the stick's position. An axis with no
/// stick on it is AXIS_DISCONNECTED: its one-shot, once fired, never times out. That is every axis
/// at power-on, when the one-shots have not been fired and read 0.
class GamePort final : public PortBus
{
public:
  static constexpr std::uint16_t PORT = 0x201;
  static constexpr std::size_t AXIS_COUNT = 4;
  static constexpr std::size_t BUTTON_COUNT = 4;
  static constexpr std::uint32_t MAXIMUM_OHMS = 100'000;
  static constexpr std::uint32_t AXIS_DISCONNECTED = std::numeric_limits<std::uint32_t>::max();

  /// The one-shot's duration, 24.2 us + 0.011 us per ohm, in CPU cycles, rounded down: 115 cycles at
  /// 0 ohms, 5,365 at 100 kOhm.
  [[nodiscard]] static constexpr Cycles OneShotCycles(std::uint32_t _ohms) noexcept
  {
    const std::uint64_t nanoseconds = BASE_NANOSECONDS + NANOSECONDS_PER_OHM * static_cast<std::uint64_t>(_ohms);
    return nanoseconds * CRYSTAL_HZ / (CPU_CLOCK_DIVISOR * NANOSECONDS_PER_SECOND);
  }

  explicit GamePort(const Cycles& _clock) noexcept;

  /// An axis's resistance in ohms, clamped to MAXIMUM_OHMS, or AXIS_DISCONNECTED. It applies from the
  /// next time the one-shots fire.
  void SetAxisResistance(std::size_t _axis, std::uint32_t _ohms) noexcept;

  void SetButton(std::size_t _button, bool _pressed) noexcept;

  [[nodiscard]] std::uint8_t In8(std::uint16_t _port) noexcept override;
  void Out8(std::uint16_t _port, std::uint8_t _value) noexcept override;

private:
  static constexpr std::uint64_t BASE_NANOSECONDS = 24'200;
  static constexpr std::uint64_t NANOSECONDS_PER_OHM = 11;
  static constexpr std::uint64_t NANOSECONDS_PER_SECOND = 1'000'000'000;
  static constexpr Cycles NEVER = std::numeric_limits<Cycles>::max();

  const Cycles& m_clock;
  std::array<std::uint32_t, AXIS_COUNT> m_ohms{AXIS_DISCONNECTED, AXIS_DISCONNECTED, AXIS_DISCONNECTED, AXIS_DISCONNECTED};
  std::array<Cycles, AXIS_COUNT> m_timeoutAt{}; // each one-shot reads 1 while the clock is below this
  std::uint8_t m_buttonsPressed = 0;            // bit i set while button i is held
};

} // namespace Machine
