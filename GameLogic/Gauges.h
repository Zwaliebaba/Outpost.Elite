// GameLogic/Gauges.h
#pragma once

#include "FlightSnapshot.h"

#include <array>
#include <cstdint>
#include <string>

namespace Elite
{

/// How a gauge's value stands (ADR-034): as UpdateConditionColor rates it into the condition light's colours, or, for the laser,
/// where it will not fire.
enum class GaugeLevel : std::uint8_t
{
  Normal,
  Warning, ///< the condition light's yellow
  Danger,  ///< its red
  Critical ///< its flashing
};

/// One gauge as the HUD draws it (D51): the bar's fill, 0 to 1 of the value's range; the number beside it; and its level.
struct Gauge
{
  float fill = 0.0f;
  std::string number;
  GaugeLevel level = GaugeLevel::Normal;
};

/// The condition light (UpdateConditionColor): green, yellow, red, or flashing.
enum class Condition : std::uint8_t
{
  Green,
  Yellow,
  Red,
  Flashing
};

/// What the missiles show: none armed, one armed and looking for a target, or one locked on.
enum class MissileState : std::uint8_t
{
  Idle,
  Armed,
  Locked
};

/// The HUD's gauges of a frame's dashboard (ADR-034's table, D51): bars and numbers are percent of each value's range, fuel's
/// number is in light years, and the missiles are a count. The roll and the dive or climb are -1 to 1 of the indicator's
/// travel, -23 to 23 as the game holds it. Each energy bank is a quarter of the energy, filled in turn.
struct Gauges
{
  Gauge foreShield;
  Gauge aftShield;
  Gauge fuel;
  Gauge cabinTemperature;
  Gauge laserTemperature;
  Gauge altitude;
  Gauge speed;
  std::array<Gauge, 4> energyBanks; ///< bank 1, the first to fill, to bank 4
  float roll = 0.0f;
  float pitch = 0.0f;
  std::uint8_t missiles = 0;
  MissileState missileState = MissileState::Idle;
  Condition condition = Condition::Green;
  bool live = true; ///< false for a dashboard with nothing live (the title's, D64): every bar empty, no number, the light dark
};

/// The gauges of _dashboard; none live, every bar empty and every number blank, for a dashboard that is not.
[[nodiscard]] Gauges GaugesOf(const SnapshotDashboard& _dashboard);

} // namespace Elite
