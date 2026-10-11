// GameLogic/Mouse.h
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Elite
{

/// What the mouse driver answers to a reset (int 33h AX=0).
struct MouseReset
{
  std::uint16_t status;      ///< AX: FFFFh when a driver is installed
  std::uint16_t buttonCount; ///< BX
};

/// What the mouse driver gives for int 33h AX=5: the buttons held, and the presses of the one button asked about.
struct MousePresses
{
  std::uint16_t buttons; ///< AX: the buttons held, bit 0 the left and bit 1 the right
  std::uint16_t presses; ///< BX: the presses of the button asked about since the last call
  std::uint16_t xPixels; ///< CX: where the last of them was, in the driver's virtual screen
  std::uint16_t yPixels; ///< DX
};

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
/// Positions are in the driver's virtual screen, 640x200, at the default ratios of 8 mickeys per 8 pixels across and 16 per 8
/// down. Counters are 16 bits and wrap, as the driver's do. Not thread-safe.
class Mouse
{
public:
  static constexpr std::uint16_t BUTTON_COUNT = 2;
  static constexpr std::uint8_t BUTTON_LEFT = 0x01;
  static constexpr std::uint8_t BUTTON_RIGHT = 0x02;
  static constexpr std::int32_t SCREEN_WIDTH_PIXELS = 640;
  static constexpr std::int32_t SCREEN_HEIGHT_PIXELS = 200;

  explicit Mouse(bool _present) noexcept;

  [[nodiscard]] bool Present() const noexcept
  {
    return m_present;
  }

  // The host side.

  /// The mouse moved by this many mickeys, right and down positive.
  void Move(std::int32_t _mickeysX, std::int32_t _mickeysY) noexcept;

  /// The buttons now held: BUTTON_LEFT, BUTTON_RIGHT. A button that goes down counts a press at the current position.
  void SetButtons(std::uint8_t _held) noexcept;

  // The program side, int 33h by AX.

  /// 0000h, reset: FFFFh (installed) and 2 buttons. The pointer goes to the centre (320, 100) and the motion and press
  /// counters are cleared.
  MouseReset Reset() noexcept;

  /// 0005h, the presses of button _button (0 left, 1 right): the buttons held, that button's presses since the last call,
  /// which then start again from 0, and where the last one happened. Nothing for any other button, which the driver refuses.
  [[nodiscard]] std::optional<MousePresses> Presses(std::uint16_t _button) noexcept;

  /// 000Bh, the motion: the mickeys moved across and down since the last call, which then start again.
  [[nodiscard]] MouseMotion Motion() noexcept;

private:
  void Clear() noexcept;

  bool m_present;
  std::uint8_t m_held = 0;
  std::uint16_t m_motionX = 0;
  std::uint16_t m_motionY = 0;
  std::int32_t m_positionMickeysX = 0;
  std::int32_t m_positionMickeysY = 0;
  std::array<std::uint16_t, BUTTON_COUNT> m_presses{};
  std::array<std::uint16_t, BUTTON_COUNT> m_pressX{};
  std::array<std::uint16_t, BUTTON_COUNT> m_pressY{};
};

} // namespace Elite
