// Machine/Mouse.h
#pragma once

#include "Registers.h"
#include "ServiceFault.h"

#include <array>
#include <cstdint>
#include <optional>

namespace Machine
{

/// A two-button Microsoft-compatible mouse driver at the call level, for exactly the int 33h functions the reference
/// calls (plan §1, Reference-Map.md "Input devices"). The host sets the mouse's state between steps; the program reads
/// it through int 33h. Whether a driver is present is fixed at power-on: Firmware points vector 33h at a driver entry
/// when it is, and PcServices routes int 33h here only then. Without one, int 33h vectors to an IRET, as on a PC with
/// no driver loaded.
///
/// Positions are in the driver's virtual screen, 640x200, at the default ratios of 8 mickeys per 8 pixels across and
/// 16 per 8 down. Counters are 16 bits and wrap, as the driver's do. Not thread-safe.
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

  // The program side.

  /// Int 33h, by AX:
  ///  * 0000h  Reset: AX = FFFFh (installed), BX = 2 buttons. The pointer goes to the centre (320, 100) and the
  ///           motion and press counters are cleared.
  ///  * 0005h  Press information for button BX (0 left, 1 right): AX = the buttons held, BX = that button's presses
  ///           since the last call, which then start again from 0, CX and DX = where the last one happened. Any other
  ///           BX: UnsupportedRequest.
  ///  * 000Bh  Motion: CX and DX = the mickeys moved across and down since the last call, which then start again.
  ///  * Anything else: UnknownFunction.
  [[nodiscard]] std::optional<FaultKind> Call(Registers& _regs) noexcept;

private:
  void Reset() noexcept;

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

} // namespace Machine
