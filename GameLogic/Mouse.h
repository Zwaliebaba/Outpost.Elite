// GameLogic/Mouse.h
#pragma once

#include <cstdint>

namespace Elite
{

/// The mouse's motion since the game last read it.
struct MouseMotion
{
  std::uint16_t acrossMickeys; ///< right positive, as a 16-bit count
  std::uint16_t downMickeys;   ///< down positive
};

/// A two-button mouse as the game reads it (ADR-051): the host moves it and sets its buttons between runs, and the game reads
/// the motion since its last read and the buttons held. Whether one is attached is the host's; the disc menu offers the mouse
/// only when one is. A mouse that is not attached is never moved and holds no button.
///
/// Counters are 16 bits and wrap, as the driver's did. Not thread-safe.
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

  /// A mouse attached, or taken away.
  void SetPresent(bool _present) noexcept
  {
    m_present = _present;
  }

  /// The mouse moved by this many mickeys, right and down positive.
  void Move(std::int32_t _mickeysX, std::int32_t _mickeysY) noexcept;

  /// The buttons now held: BUTTON_LEFT, BUTTON_RIGHT.
  void SetButtons(std::uint8_t _held) noexcept;

  // The game's side.

  /// The motion so far forgotten, as the game does when flight starts with the mouse.
  void Reset() noexcept;

  /// The buttons held: BUTTON_LEFT, BUTTON_RIGHT.
  [[nodiscard]] std::uint8_t Buttons() const noexcept;

  /// The mickeys moved across and down since the last read, which then start again.
  [[nodiscard]] MouseMotion Motion() noexcept;

private:
  bool m_present;
  std::uint8_t m_held = 0;
  std::uint16_t m_motionX = 0;
  std::uint16_t m_motionY = 0;
};

} // namespace Elite
