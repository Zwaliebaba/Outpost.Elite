#include "pch.h"

#include "DockedKeys.h"

#include <array>
#include <utility>

namespace Elite
{

namespace
{

constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F10 = 0x44;

constexpr std::array<std::pair<std::uint8_t, DockedScreen>, 8> DOCKED_KEYS = {{
  {SCAN_ESCAPE, DockedScreen::Disc},
  {0x3C, DockedScreen::Market},
  {0x3D, DockedScreen::Equip},
  {0x3E, DockedScreen::Galaxy},
  {0x3F, DockedScreen::Local},
  {0x40, DockedScreen::Status},
  {0x41, DockedScreen::Data},
  {SCAN_F10, DockedScreen::Inventory},
}};

} // namespace

DockedTarget DockedTargetOf(std::uint8_t _scanCode) noexcept
{
  if (_scanCode == SCAN_F1)
  {
    return DockedTarget{.launches = true, .screen = std::nullopt};
  }
  for (const auto& [key, screen] : DOCKED_KEYS)
  {
    if (key == _scanCode)
    {
      return DockedTarget{.launches = false, .screen = screen};
    }
  }
  return DockedTarget{};
}

std::uint8_t DockedKeyOf(DockedScreen _screen) noexcept
{
  for (const auto& [key, screen] : DOCKED_KEYS)
  {
    if (screen == _screen)
    {
      return key;
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
