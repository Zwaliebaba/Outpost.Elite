#include "pch.h"

#include "DockedKeys.h"

#include <array>

namespace Elite
{

namespace
{

constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F2 = 0x3C;
constexpr std::uint8_t SCAN_F3 = 0x3D;
constexpr std::uint8_t SCAN_F4 = 0x3E;
constexpr std::uint8_t SCAN_F5 = 0x3F;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F7 = 0x41;
constexpr std::uint8_t SCAN_F10 = 0x44;

// A key and the screen it shows docked.
struct KeyedScreen
{
  std::uint8_t scanCode;
  DockedScreen screen;
};

constexpr std::array<KeyedScreen, 8> DOCKED_KEYS = {{
  {SCAN_ESCAPE, DockedScreen::Disc},
  {SCAN_F2, DockedScreen::Market},
  {SCAN_F3, DockedScreen::Equip},
  {SCAN_F4, DockedScreen::Galaxy},
  {SCAN_F5, DockedScreen::Local},
  {SCAN_F6, DockedScreen::Status},
  {SCAN_F7, DockedScreen::Data},
  {SCAN_F10, DockedScreen::Inventory},
}};

} // namespace

DockedTarget DockedTargetOf(std::uint8_t _scanCode) noexcept
{
  if (_scanCode == SCAN_F1)
  {
    return DockedTarget{.launches = true, .screen = std::nullopt};
  }
  for (const KeyedScreen& keyed : DOCKED_KEYS)
  {
    if (keyed.scanCode == _scanCode)
    {
      return DockedTarget{.launches = false, .screen = keyed.screen};
    }
  }
  return DockedTarget{};
}

std::uint8_t DockedKeyOf(DockedScreen _screen) noexcept
{
  for (const KeyedScreen& keyed : DOCKED_KEYS)
  {
    if (keyed.screen == _screen)
    {
      return keyed.scanCode;
    }
  }
  return SCAN_ESCAPE;
}

bool ClosesScreen(std::uint8_t _scanCode, ScreenPlace _place) noexcept
{
  if (_place == ScreenPlace::Docked)
  {
    const DockedTarget target = DockedTargetOf(_scanCode);
    return target.launches || target.screen.has_value();
  }
  return _scanCode == SCAN_ESCAPE || (_scanCode >= SCAN_F1 && _scanCode <= SCAN_F10);
}

} // namespace Elite
