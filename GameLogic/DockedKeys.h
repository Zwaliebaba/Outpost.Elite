// GameLogic/DockedKeys.h
#pragma once

#include <cstdint>
#include <optional>

namespace Elite
{

/// Where a screen is shown, which says which keys close it: docked, under the docked host's keys (D96), or in flight, under the
/// original's.
enum class ScreenPlace : std::uint8_t
{
  Docked,
  Flight
};

/// The screens the docked host shows (D96): D01's market, and the original's screens until theirs land. The disc menu is Esc's.
enum class DockedScreen : std::uint8_t
{
  Disc,
  Market,
  Equip,
  Galaxy,
  Local,
  Status,
  Data,
  Inventory
};

/// What a key does to the docked host: launches, shows a screen, or nothing.
struct DockedTarget
{
  bool launches = false;
  std::optional<DockedScreen> screen; ///< the screen it shows, when it does not launch
};

/// The docked host's keys (D96): F1 launches, F2 the market, F3 the equipment, F4 the galactic chart, F5 the short-range chart, F6
/// the status screen, and Esc the disc menu, as the key bar shows them. F7's Data on System and F10's inventory keep their keys
/// until the screens that take them in land. F8 and F9 do nothing docked: their screens are D01 and F6's.
[[nodiscard]] DockedTarget DockedTargetOf(std::uint8_t _scanCode) noexcept;

/// The key that shows _screen docked.
[[nodiscard]] std::uint8_t DockedKeyOf(DockedScreen _screen) noexcept;

/// Whether _scanCode closes a screen shown at _place: Esc, or a key that shows another screen there or launches. Docked, those are
/// the docked host's (DockedTargetOf); in flight, every F-key, as in the original.
[[nodiscard]] bool ClosesScreen(std::uint8_t _scanCode, ScreenPlace _place) noexcept;

} // namespace Elite
