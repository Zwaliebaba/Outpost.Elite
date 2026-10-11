// GameLogic/Mouse.h
#pragma once

#include <cstdint>

namespace Elite
{

/// What the mouse driver gives for int 33h AX=0Bh: the motion since the last call.
struct MouseMotion
{
  std::uint16_t acrossMickeys; ///< CX, right positive
  std::uint16_t downMickeys;   ///< DX, down positive
};

/// A two-button Microsoft-compatible mouse driver at the call level, for exactly the int 33h functions the reference calls
/// (plan §1, Reference-Map.md "Input devices"). It is the emulated PC's model (Machine::Mouse until D33), its calls typed. The
/// host sets the mouse's state between runs; the program reads it through the calls. Whether a driver is present is fixed
/// at power-on: the platform points vector 33h at the driver's entry when it is, and at the ROM's IRET when it is not, as on a
/// PC with no driver loaded (Platform).
///
/// Counters are 16 bits and wrap, as the driver's do. Not thread-safe.
class Mouse
{
public:
  static constexpr std::uint8_t BUTTON_LEFT = 0x01;
  static constexpr std::uint8_t BUTTON_RIGHT = 0x02;

  explicit Mouse(bool _present) noexcept;

  [[nodiscard]] bool Present() const noexcept
  {
    return m_present;
  }

  // The host side.

  /// The mouse moved by this many mickeys, right and down positive.
  void Move(std::int32_t _mickeysX, std::int32_t _mickeysY) noexcept;

  /// The buttons now held: BUTTON_LEFT, BUTTON_RIGHT.
  void SetButtons(std::uint8_t _held) noexcept;

  // The program side, int 33h by AX.

  /// 0000h, reset: the motion counters cleared. The game does not read the answer.
  void Reset() noexcept;

  /// 0005h: the buttons held, AX. The game reads nothing else of the answer.
  [[nodiscard]] std::uint16_t Buttons() const noexcept;

  /// 000Bh, the motion: the mickeys moved across and down since the last call, which then start again.
  [[nodiscard]] MouseMotion Motion() noexcept;

private:
  bool m_present;
  std::uint8_t m_held = 0;
  std::uint16_t m_motionX = 0;
  std::uint16_t m_motionY = 0;
};

} // namespace Elite
