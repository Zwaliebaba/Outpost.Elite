#include "pch.h"

#include "Flight.h"
#include "FlightSnapshot.h"
#include "GameState.h"
#include "Gauges.h"
#include "SpaceObject.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SEED = 20261012; // a pinned seed, so the states tried are the same each run
constexpr int STATES_TRIED = 20000;

// Marsaglia's xorshift32, pinned here so that the states tried are the same on every standard library.
class Xorshift
{
public:
  explicit Xorshift(std::uint32_t _seed) noexcept
    : m_state(_seed)
  {
  }

  // The next 32 bits.
  [[nodiscard]] std::uint32_t Next() noexcept
  {
    m_state ^= m_state << 13;
    m_state ^= m_state >> 17;
    m_state ^= m_state << 5;
    return m_state;
  }

  // The next _bits bits, the generator's highest.
  [[nodiscard]] std::uint32_t Bits(int _bits) noexcept
  {
    return Next() >> (32 - _bits);
  }

private:
  std::uint32_t m_state;
};

// The condition light's colour a level stands for: green, yellow, red, flashing.
[[nodiscard]] Elite::Condition ConditionOf(Elite::GaugeLevel _level) noexcept
{
  switch (_level)
  {
  case Elite::GaugeLevel::Warning:
    return Elite::Condition::Yellow;
  case Elite::GaugeLevel::Danger:
    return Elite::Condition::Red;
  case Elite::GaugeLevel::Critical:
    return Elite::Condition::Flashing;
  case Elite::GaugeLevel::Normal:
    break;
  }
  return Elite::Condition::Green;
}

} // namespace

// The HUD's gauges (ADR-034, D51): each value's share of its range, its number, and its level as the condition light rates it.
TEST_CLASS(GaugesTests)
{
public:
  // Full and empty: the bars and the numbers at the ends of their ranges, fuel in light years.
  TEST_METHOD(BarsAndNumbersSpanTheirRanges)
  {
    Elite::SnapshotDashboard full;
    full.foreShield = 0xFF;
    full.aftShield = 0xFF;
    full.fuel = 0xFF;
    full.altitude = 0xFF;
    full.speed = 0x30;
    full.energy = 0x3FF;
    full.roll = 40;
    full.pitch = -40;
    full.condition = 1;
    const Elite::Gauges gauges = Elite::GaugesOf(full);
    Assert::AreEqual(1.0f, gauges.foreShield.fill);
    Assert::AreEqual(std::string("100"), gauges.foreShield.number);
    Assert::AreEqual(std::string("7.0"), gauges.fuel.number, L"255 is 7.0 light years, as the status screen shows it");
    Assert::AreEqual(1.0f, gauges.speed.fill, L"30h is full speed");
    for (const Elite::Gauge& bank : gauges.energyBanks)
    {
      Assert::AreEqual(1.0f, bank.fill, L"every bank full");
    }
    Assert::IsTrue(gauges.roll == 1.0f && gauges.pitch == -1.0f, L"held to the indicator's travel");
    Assert::IsTrue(gauges.condition == Elite::Condition::Green);

    Elite::SnapshotDashboard low;
    low.energy = 0x150;
    low.fuel = 36;
    const Elite::Gauges lowGauges = Elite::GaugesOf(low);
    Assert::AreEqual(1.0f, lowGauges.energyBanks[0].fill, L"the first bank fills first");
    Assert::AreEqual(0x50 / 255.0f, lowGauges.energyBanks[1].fill, L"then the second");
    Assert::AreEqual(0.0f, lowGauges.energyBanks[2].fill);
    Assert::AreEqual(std::string("1.0"), lowGauges.fuel.number, L"36 is a light year");
    Assert::AreEqual(std::string("0"), lowGauges.foreShield.number);
    Assert::IsTrue(lowGauges.foreShield.level == Elite::GaugeLevel::Danger, L"a shield at 0 is red");
    Assert::IsTrue(lowGauges.energyBanks[0].level == Elite::GaugeLevel::Danger, L"energy below 200h is red");
  }

  // The levels are UpdateConditionColor's: for states of every value, the worst level of the shields, the energy, the cabin's
  // temperature and the altitude is the colour the game's own routine gives the condition light.
  TEST_METHOD(LevelsAreTheConditionLights)
  {
    Xorshift random(SEED);
    const auto state = std::make_unique<Elite::GameState>();
    for (int tried = 0; tried < STATES_TRIED; ++tried)
    {
      Elite::SnapshotDashboard dashboard;
      // Half the values near the thresholds' ends, so that each is crossed often.
      dashboard.foreShield = static_cast<std::uint8_t>(tried % 7 == 0 ? 0 : random.Bits(8));
      dashboard.aftShield = static_cast<std::uint8_t>(random.Bits(8));
      dashboard.energy = static_cast<std::uint16_t>(random.Bits(10));
      dashboard.cabinTemperature = static_cast<std::uint8_t>(random.Bits(8));
      dashboard.altitude = static_cast<std::uint8_t>(random.Bits(8));
      state->Data().playerShip.foreShield = dashboard.foreShield;
      state->Data().playerShip.aftShield = dashboard.aftShield;
      state->Data().playerShip.playerEnergy = dashboard.energy;
      state->Data().playerShip.cabinTemperature = dashboard.cabinTemperature;
      state->Data().playerShip.altitude = dashboard.altitude;
      Elite::UpdateConditionColor(*state);
      dashboard.condition = state->Data().flight.conditionColor;

      const Elite::Gauges gauges = Elite::GaugesOf(dashboard);
      const Elite::GaugeLevel worst = std::max({gauges.foreShield.level, gauges.aftShield.level, gauges.energyBanks[0].level,
                                                gauges.cabinTemperature.level, gauges.altitude.level});
      Assert::IsTrue(ConditionOf(worst) == gauges.condition,
                     (L"fore " + std::to_wstring(dashboard.foreShield) + L", aft " + std::to_wstring(dashboard.aftShield) + L", energy " +
                      std::to_wstring(dashboard.energy) + L", cabin " + std::to_wstring(dashboard.cabinTemperature) + L", altitude " +
                      std::to_wstring(dashboard.altitude))
                       .c_str());
    }
  }

  // The snapshot takes the dashboard as the frame left it: the values, the glyphs shown, the compass's dot and the blips.
  TEST_METHOD(TheSnapshotTakesTheDashboard)
  {
    const auto state = std::make_unique<Elite::GameState>();
    Elite::GameData& data = state->Data();
    data.playerShip.foreShield = 0x90;
    data.playerShip.aftShield = 0x10;
    data.commander.fuel = 0x46;
    data.playerShip.cabinTemperature = 0x22;
    data.flight.laserTemperature = 0x33;
    data.playerShip.altitude = 0x44;
    data.playerShip.playerSpeed = 0x18;
    data.playerShip.rollRate = static_cast<std::uint16_t>(0xFB05); // pitch -5, roll 5
    data.playerShip.playerEnergy = 0x2AB;
    data.commander.missileCount = 3;
    data.playerShip.missileState = 2;
    data.flight.conditionColor = 3;
    data.flight.safeZoneGlyphShown = 0x82;
    data.flight.ecmGlyphShown = 0x20;
    Elite::SpaceObject& station = state->Slot(Elite::SlotIndex::Station);
    station.flags = 0x02;
    station.blipX = static_cast<std::uint8_t>(0xCF - 3);
    station.blipY = static_cast<std::uint8_t>(0x27 + 6);
    station.blipZ = 0x20;
    Elite::SpaceObject& ship = state->Slot(Elite::SlotIndex::FirstShip);
    ship.type = static_cast<std::uint8_t>((9 << 1) | Elite::SpaceObject::ACTIVE);
    ship.flags = 0x03;
    ship.blipX = static_cast<std::uint8_t>(-20);
    ship.blipY = 12;
    ship.blipZ = 40;

    const Elite::SnapshotDashboard dashboard = Elite::TakeFlightSnapshot(*state).dashboard;
    Assert::IsTrue(dashboard.foreShield == 0x90 && dashboard.aftShield == 0x10 && dashboard.fuel == 0x46 &&
                     dashboard.cabinTemperature == 0x22 && dashboard.laserTemperature == 0x33 && dashboard.altitude == 0x44,
                   L"the bars' values");
    Assert::IsTrue(dashboard.speed == 0x18 && dashboard.roll == 5 && dashboard.pitch == -5 && dashboard.energy == 0x2AB, L"the rest");
    Assert::IsTrue(dashboard.missiles == 3 && dashboard.missileState == 2 && dashboard.condition == 3, L"missiles and condition");
    Assert::IsTrue(dashboard.safeZone && !dashboard.ecm, L"the S shown, the E not");
    Assert::IsTrue(dashboard.compass && dashboard.compassX == -3 && dashboard.compassY == 6 && dashboard.compassInFront, L"the compass");
    Assert::AreEqual(std::size_t{1}, dashboard.blipCount, L"one blip");
    Assert::IsTrue(dashboard.blips[0] == Elite::SnapshotBlip{.slot = 3, .x = -20, .y = 12, .z = 40, .hostile = true}, L"the blip");
  }
};

} // namespace GameLogicTests
