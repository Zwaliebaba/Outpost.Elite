// GameLogic/PlanetLook.h
#pragma once

#include <array>
#include <cstdint>

namespace Elite
{

/// What kind of world a system's planet is (D45, D64): the canvas's two, picked by the system's seed.
enum class PlanetKind : std::uint8_t
{
  Terrestrial, ///< seas, lands and cloud, as `cockpit/01-planet-approach` draws one
  GasGiant     ///< bands, as `cockpit/02-station-approach` draws one
};

/// How a system's planet looks (ADR-035, D45, D64), from its galaxy and its number there, in integer arithmetic: the same system
/// always looks the same. Colours are 0x00RRGGBB, display-encoded; shares are of 255. An interim look, which the owner may redraw.
struct PlanetLook
{
  PlanetKind kind = PlanetKind::Terrestrial;
  std::array<std::uint32_t, 4> colors{}; ///< terrestrial: deep sea, shallows, low land, high land; a gas giant's bands in turn
  std::uint32_t atmosphere = 0;          ///< the haze at its rim
  std::uint32_t seed = 0;                ///< which surface of its kind
  std::uint8_t seaLevel = 0;             ///< terrestrial: the share of it under the sea
  std::uint8_t cloudCover = 0;           ///< terrestrial: the share of it under cloud
  std::uint8_t bands = 0;                ///< a gas giant: the pairs of bands from pole to pole
  std::uint8_t turbulence = 0;           ///< the share by which the noise stirs its pattern

  friend bool operator==(const PlanetLook&, const PlanetLook&) = default;
};

/// The sky a system's sky looks like from it (ADR-035, D45), likewise: its nebula's two colours and its share of the sky, and the
/// share of the sky's cells that hold a star.
struct SkyLook
{
  std::array<std::uint32_t, 2> nebula{};
  std::uint8_t nebulaDensity = 0;
  std::uint8_t starDensity = 0;
  std::uint32_t seed = 0;

  friend bool operator==(const SkyLook&, const SkyLook&) = default;
};

/// The planet of system _system of galaxy _galaxy, and its sky.
[[nodiscard]] PlanetLook PlanetLookOf(std::uint8_t _galaxy, std::uint8_t _system) noexcept;
[[nodiscard]] SkyLook SkyLookOf(std::uint8_t _galaxy, std::uint8_t _system) noexcept;

} // namespace Elite
