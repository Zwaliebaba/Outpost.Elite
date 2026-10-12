#include "pch.h"

#include "Gauges.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Elite
{

namespace
{

// The ranges UpdateDashboard draws each value over (Flight.cpp): a byte for the bars, 30h pixels of speed, four banks of FFh of
// energy, and -23 to 23 for the roll and the pitch (DrawSignedIndicator).
constexpr float BYTE_RANGE = 255.0f;
constexpr float SPEED_RANGE = 48.0f;
constexpr int BANK_ENERGY = 0x100;
constexpr float BANK_RANGE = 255.0f;
constexpr int INDICATOR_TRAVEL = 23;
constexpr int FUEL_PER_LIGHT_YEAR = 36; // the status screen's: 255 shows 7.0
constexpr int TENTHS = 10;

// UpdateConditionColor's thresholds (Flight.cpp), and the laser's: it will not fire from F0h (LASER_TOO_HOT).
constexpr std::uint16_t ENERGY_CRITICAL = 0x100;
constexpr std::uint16_t ENERGY_DANGER = 0x200;
constexpr std::uint16_t ENERGY_WARNING = 0x300;
constexpr std::uint8_t CABIN_CRITICAL = 0xE0;
constexpr std::uint8_t CABIN_DANGER = 0xC0;
constexpr std::uint8_t CABIN_WARNING = 0x80;
constexpr std::uint8_t ALTITUDE_CRITICAL = 0x20;
constexpr std::uint8_t ALTITUDE_DANGER = 0x28;
constexpr std::uint8_t ALTITUDE_WARNING = 0x80;
constexpr std::uint8_t SHIELD_WARNING = 0x80;
constexpr std::uint8_t LASER_TOO_HOT = 0xF0;

// A byte's gauge: its share of FFh, as a bar and in percent.
[[nodiscard]] Gauge ByteGauge(std::uint8_t _value, GaugeLevel _level)
{
  const float fill = static_cast<float>(_value) / BYTE_RANGE;
  return Gauge{.fill = fill, .number = std::format("{}", std::lround(fill * 100.0f)), .level = _level};
}

[[nodiscard]] GaugeLevel ShieldLevel(std::uint8_t _shield) noexcept
{
  return _shield == 0 ? GaugeLevel::Danger : _shield < SHIELD_WARNING ? GaugeLevel::Warning : GaugeLevel::Normal;
}

[[nodiscard]] GaugeLevel EnergyLevel(std::uint16_t _energy) noexcept
{
  return _energy < ENERGY_CRITICAL  ? GaugeLevel::Critical
         : _energy < ENERGY_DANGER  ? GaugeLevel::Danger
         : _energy < ENERGY_WARNING ? GaugeLevel::Warning
                                    : GaugeLevel::Normal;
}

[[nodiscard]] GaugeLevel CabinLevel(std::uint8_t _temperature) noexcept
{
  return _temperature >= CABIN_CRITICAL  ? GaugeLevel::Critical
         : _temperature >= CABIN_DANGER  ? GaugeLevel::Danger
         : _temperature >= CABIN_WARNING ? GaugeLevel::Warning
                                         : GaugeLevel::Normal;
}

[[nodiscard]] GaugeLevel AltitudeLevel(std::uint8_t _altitude) noexcept
{
  return _altitude < ALTITUDE_CRITICAL  ? GaugeLevel::Critical
         : _altitude < ALTITUDE_DANGER  ? GaugeLevel::Danger
         : _altitude < ALTITUDE_WARNING ? GaugeLevel::Warning
                                        : GaugeLevel::Normal;
}

// The roll or the pitch, held to the indicator's travel as DrawSignedIndicator holds it, as -1 to 1.
[[nodiscard]] float Travel(std::int8_t _value) noexcept
{
  return static_cast<float>(std::clamp<int>(_value, -INDICATOR_TRAVEL, INDICATOR_TRAVEL)) / INDICATOR_TRAVEL;
}

} // namespace

Gauges GaugesOf(const SnapshotDashboard& _dashboard)
{
  Gauges gauges;
  if (!_dashboard.live)
  {
    gauges.live = false;
    return gauges;
  }
  gauges.foreShield = ByteGauge(_dashboard.foreShield, ShieldLevel(_dashboard.foreShield));
  gauges.aftShield = ByteGauge(_dashboard.aftShield, ShieldLevel(_dashboard.aftShield));
  // Fuel: its share of a full tank as the bar, and light years, to the tenth below, as the number.
  const int tenths = _dashboard.fuel * TENTHS / FUEL_PER_LIGHT_YEAR;
  gauges.fuel = Gauge{.fill = static_cast<float>(_dashboard.fuel) / BYTE_RANGE,
                      .number = std::format("{}.{}", tenths / TENTHS, tenths % TENTHS),
                      .level = GaugeLevel::Normal};
  gauges.cabinTemperature = ByteGauge(_dashboard.cabinTemperature, CabinLevel(_dashboard.cabinTemperature));
  gauges.laserTemperature =
    ByteGauge(_dashboard.laserTemperature, _dashboard.laserTemperature >= LASER_TOO_HOT ? GaugeLevel::Danger : GaugeLevel::Normal);
  gauges.altitude = ByteGauge(_dashboard.altitude, AltitudeLevel(_dashboard.altitude));
  gauges.speed =
    Gauge{.fill = std::min(static_cast<float>(_dashboard.speed) / SPEED_RANGE, 1.0f), .number = {}, .level = GaugeLevel::Normal};
  // The banks fill in turn, each FFh of the energy, as DrawEnergyBanks splits it; each is as low as the energy is.
  const GaugeLevel energyLevel = EnergyLevel(_dashboard.energy);
  for (std::size_t bank = 0; bank < gauges.energyBanks.size(); ++bank)
  {
    const int inBank = std::clamp(static_cast<int>(_dashboard.energy) - static_cast<int>(bank) * BANK_ENERGY, 0, BANK_ENERGY - 1);
    gauges.energyBanks[bank] = Gauge{.fill = static_cast<float>(inBank) / BANK_RANGE, .number = {}, .level = energyLevel};
  }
  gauges.roll = Travel(_dashboard.roll);
  gauges.pitch = Travel(_dashboard.pitch);
  gauges.missiles = _dashboard.missiles;
  gauges.missileState = _dashboard.missileState == 1   ? MissileState::Armed
                        : _dashboard.missileState == 2 ? MissileState::Locked
                                                       : MissileState::Idle;
  constexpr std::array<Condition, 4> CONDITIONS = {Condition::Flashing, Condition::Green, Condition::Red, Condition::Yellow};
  gauges.condition = CONDITIONS[_dashboard.condition & 3u];
  return gauges;
}

} // namespace Elite
