#include "pch.h"

#include "PlanetLook.h"

namespace Elite
{

namespace
{

// A world's colours: four, and its atmosphere's.
struct Palette
{
  std::array<std::uint32_t, 4> colors;
  std::uint32_t atmosphere;
};

// Terrestrial worlds: deep sea, shallows, low land, high land.
constexpr std::array<Palette, 6> TERRESTRIAL = {{
  {.colors = {0x0E2A4A, 0x1F6C8A, 0x4C7A3A, 0xB59A6A}, .atmosphere = 0x6FA8FF}, // blue and green
  {.colors = {0x0F3A40, 0x2B6E6A, 0xA08A5A, 0xD2BE8C}, .atmosphere = 0x7FC8D8}, // teal and sand, as the canvas draws one
  {.colors = {0x2A2A44, 0x4A5A6A, 0x9A6A3A, 0xD8B080}, .atmosphere = 0xE0B890}, // arid
  {.colors = {0x22405A, 0x5A8AAA, 0xC8D8E0, 0xF4F8FF}, .atmosphere = 0xB8D8FF}, // ice
  {.colors = {0x1A2A3A, 0x2A5A5A, 0x3A6A2A, 0x6A8A3A}, .atmosphere = 0x90E0B0}, // jungle
  {.colors = {0x1A0E0E, 0x3A1A12, 0x5A3A2A, 0x8A6A5A}, .atmosphere = 0xFF9060}, // volcanic
}};

// Gas giants: their bands in turn.
constexpr std::array<Palette, 4> GAS_GIANT = {{
  {.colors = {0x8A4A2A, 0xC8784A, 0xE8C8A0, 0xA0583A}, .atmosphere = 0xFFB070}, // rust, as the canvas draws one
  {.colors = {0xC8B080, 0xE8D8B0, 0xA89060, 0xD8C090}, .atmosphere = 0xFFE0A0}, // pale gold
  {.colors = {0x3A6A9A, 0x5A9AC8, 0x8AC0E0, 0x4A80B0}, .atmosphere = 0x90C8FF}, // ice giant
  {.colors = {0x2A6A6A, 0x4A9A8A, 0x8AC8B0, 0x3A7A7A}, .atmosphere = 0x90E8D0}, // teal
}};

// The nebulae of a sky: two colours each.
constexpr std::array<std::array<std::uint32_t, 2>, 6> NEBULAE = {{
  {0x5A2A6A, 0x2A3A7A}, // violet and blue, as the canvas draws one
  {0x6A2A2A, 0x7A4A2A}, // red and amber
  {0x2A4A6A, 0x2A6A6A}, // blue and teal
  {0x4A2A5A, 0x6A2A4A}, // purple and rose
  {0x2A5A4A, 0x3A4A6A}, // green and slate
  {0x5A4A2A, 0x3A2A5A}, // gold and indigo
}};

// Three of eight systems' planets are gas giants.
constexpr std::uint32_t KIND_BITS = 7;
constexpr std::uint32_t GAS_GIANT_BELOW = 3;
// The ranges each share is drawn from, of 255.
constexpr std::uint32_t SEA_LEAST = 90;
constexpr std::uint32_t SEA_SPAN = 100;
constexpr std::uint32_t CLOUD_LEAST = 10;
constexpr std::uint32_t CLOUD_SPAN = 70;
constexpr std::uint32_t BANDS_LEAST = 4;
constexpr std::uint32_t BANDS_SPAN = 8;
constexpr std::uint32_t TURBULENCE_LEAST = 70;
constexpr std::uint32_t TURBULENCE_SPAN = 150;
constexpr std::uint32_t NEBULA_LEAST = 60;
constexpr std::uint32_t NEBULA_SPAN = 180;
constexpr std::uint32_t STARS_LEAST = 25;
constexpr std::uint32_t STARS_SPAN = 40;
// How far each channel of a colour may move from its palette's, either way.
constexpr std::uint32_t CHANNEL_SPREAD = 12;
// What tells the sky's hash from the planet's.
constexpr std::uint32_t SKY_SALT = 0x5EA0C0DEu;

// A well-mixed word of _value (lowbias32, by Chris Wellons).
[[nodiscard]] constexpr std::uint32_t Mix(std::uint32_t _value) noexcept
{
  std::uint32_t value = _value;
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  value ^= value >> 16;
  return value;
}

// The system's word: its galaxy and its number, mixed.
[[nodiscard]] constexpr std::uint32_t SystemWord(std::uint8_t _galaxy, std::uint8_t _system, std::uint32_t _salt) noexcept
{
  return Mix(((std::uint32_t{_galaxy} << 8) | _system) * 0x9E3779B1u + _salt);
}

// _least, and up to _span more, by _random.
[[nodiscard]] constexpr std::uint8_t Within(std::uint32_t _random, std::uint32_t _least, std::uint32_t _span) noexcept
{
  return static_cast<std::uint8_t>(_least + _random % (_span + 1));
}

// _color with each channel moved by up to CHANNEL_SPREAD either way, by _random, and held to a byte.
[[nodiscard]] constexpr std::uint32_t Vary(std::uint32_t _color, std::uint32_t _random) noexcept
{
  std::uint32_t varied = 0;
  for (std::uint32_t shift = 0; shift <= 16; shift += 8)
  {
    const auto channel = static_cast<std::int32_t>((_color >> shift) & 0xFFu);
    const auto moved =
      static_cast<std::int32_t>(((_random >> shift) & 0xFFu) % (2 * CHANNEL_SPREAD + 1)) - static_cast<std::int32_t>(CHANNEL_SPREAD);
    const std::int32_t held = channel + moved < 0 ? 0 : (channel + moved > 0xFF ? 0xFF : channel + moved);
    varied |= static_cast<std::uint32_t>(held) << shift;
  }
  return varied;
}

} // namespace

PlanetLook PlanetLookOf(std::uint8_t _galaxy, std::uint8_t _system) noexcept
{
  const std::uint32_t word = SystemWord(_galaxy, _system, 0);
  PlanetLook look;
  look.kind = (word & KIND_BITS) < GAS_GIANT_BELOW ? PlanetKind::GasGiant : PlanetKind::Terrestrial;
  const Palette& palette =
    look.kind == PlanetKind::GasGiant ? GAS_GIANT.at((word >> 3) % GAS_GIANT.size()) : TERRESTRIAL.at((word >> 3) % TERRESTRIAL.size());
  // A word for each colour, and one for each share, from the system's.
  std::uint32_t random = word;
  for (std::size_t color = 0; color < look.colors.size(); ++color)
  {
    random = Mix(random);
    look.colors.at(color) = Vary(palette.colors.at(color), random);
  }
  random = Mix(random);
  look.atmosphere = Vary(palette.atmosphere, random);
  look.seed = Mix(random);
  look.seaLevel = Within(Mix(look.seed), SEA_LEAST, SEA_SPAN);
  look.cloudCover = Within(Mix(look.seed + 1), CLOUD_LEAST, CLOUD_SPAN);
  look.bands = Within(Mix(look.seed + 2), BANDS_LEAST, BANDS_SPAN);
  look.turbulence = Within(Mix(look.seed + 3), TURBULENCE_LEAST, TURBULENCE_SPAN);
  return look;
}

SkyLook SkyLookOf(std::uint8_t _galaxy, std::uint8_t _system) noexcept
{
  const std::uint32_t word = SystemWord(_galaxy, _system, SKY_SALT);
  SkyLook look;
  look.nebula = NEBULAE.at(word % NEBULAE.size());
  look.nebulaDensity = Within(Mix(word), NEBULA_LEAST, NEBULA_SPAN);
  look.starDensity = Within(Mix(word + 1), STARS_LEAST, STARS_SPAN);
  look.seed = Mix(word + 2);
  return look;
}

} // namespace Elite
