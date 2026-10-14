#include "pch.h"

#include "Equipment.h"

#include <optional>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// Rows of the equipment list, from 1.
constexpr std::uint8_t ECM_ROW = 4;
constexpr std::uint8_t PULSE_LASER_ROW = 5;
constexpr std::uint8_t BEAM_LASER_ROW = 6;
constexpr std::uint8_t MILITARY_LASER_ROW = 14;

// Laser types and mounts.
constexpr std::uint8_t PULSE = 0;
constexpr std::uint8_t BEAM = 1;
constexpr std::uint8_t MINING = 2;
constexpr std::uint8_t MILITARY = 3;
constexpr std::uint8_t FORE = 0;
constexpr std::uint8_t LEFT = 2;

// A commander as Jameson starts: a full tank, three missiles, a pulse laser fore, nothing else, an empty hold, no mission.
[[nodiscard]] Elite::Fitting Jameson()
{
  Elite::Fitting fitting{.counts = {}, .laserMountsFitted = 1, .cargoUsedTonnes = 0, .missionNumber = 0};
  fitting.counts[Elite::FUEL_ROW - 1] = 0x46;
  fitting.counts[Elite::MISSILE_ROW - 1] = 3;
  fitting.counts[PULSE_LASER_ROW - 1] = 1;
  return fitting;
}

// A word, widened as the suite compares words.
[[nodiscard]] std::uint32_t Wide(std::uint16_t _word) noexcept
{
  return _word;
}

} // namespace

// The shipyard's rules (D53, D74), by known answers: what the 1987 routines decide, with no state and no screen (ADR-043).
// EquipmentAnswerTests checks the prices over every shipyard.
TEST_CLASS(EquipmentRulesTests)
{
public:
  // The list is sorted by tech level: fuel, missiles and the extension from the first, a row more for each level up to 8,
  // and all fourteen from 9.
  TEST_METHOD(TheListGrowsWithTheTechLevel)
  {
    const Elite::EquipmentItem fuel = Elite::EquipmentItemAt(Elite::FUEL_ROW);
    Assert::AreEqual(std::uint8_t{1}, fuel.techLevel);
    Assert::AreEqual(std::uint32_t{20}, Wide(fuel.basePriceTenths), L"2.0 a light year");
    Assert::AreEqual(std::uint32_t{60000}, Wide(Elite::EquipmentItemAt(MILITARY_LASER_ROW).basePriceTenths), L"6000.0");
    Assert::AreEqual(std::uint8_t{3}, Elite::EquipmentRowsOnSale(0));
    Assert::AreEqual(std::uint8_t{4}, Elite::EquipmentRowsOnSale(1));
    Assert::AreEqual(std::uint8_t{11}, Elite::EquipmentRowsOnSale(8));
    Assert::AreEqual(std::uint8_t{14}, Elite::EquipmentRowsOnSale(9));
    Assert::AreEqual(std::uint8_t{14}, Elite::EquipmentRowsOnSale(15));
    Assert::AreEqual(std::uint8_t{0}, Elite::EquipmentRowsOnSale(0xFE), L"the limit, the level + 2, wraps to 0");
  }

  // An item's price is its base price and each factor times its step: missiles cost more under a corporate state.
  TEST_METHOD(PricesMoveWithTheEconomyAndTheGovernment)
  {
    const Elite::EquipmentItem missile = Elite::EquipmentItemAt(Elite::MISSILE_ROW);
    const Elite::MarketPlace anarchy{.economy = 0, .government = 0, .techLevel = 9};
    const Elite::MarketPlace corporate{.economy = 7, .government = 7, .techLevel = 9};
    Assert::AreEqual(std::uint32_t{300}, Wide(Elite::EquipmentPriceTenths(missile, anarchy)), L"rich industrial, anarchy");
    Assert::AreEqual(std::uint32_t{391}, Wide(Elite::EquipmentPriceTenths(missile, corporate)), L"poor agricultural, corporate state");
    const Elite::EquipmentItem bay = Elite::EquipmentItemAt(Elite::CARGO_BAY_ROW);
    Assert::AreEqual(std::uint32_t{3895}, Wide(Elite::EquipmentPriceTenths(bay, corporate)), L"and the extension cheaper");
  }

  // Filling the tank costs the light years missing at the price's low byte, at least a tenth, by a divide whose overflow
  // saturates at 7Fh as the game's trap leaves it.
  TEST_METHOD(FuelCostsWhatTheTankLacks)
  {
    Assert::AreEqual(std::uint32_t{1}, Wide(Elite::FuelPriceTenths(0xFF, 20)), L"a full tank, at least a tenth");
    Assert::AreEqual(std::uint32_t{141}, Wide(Elite::FuelPriceTenths(0, 20)), L"255 * 20 / 36");
    Assert::AreEqual(std::uint32_t{102}, Wide(Elite::FuelPriceTenths(0x46, 20)), L"185 * 20 / 36");
    Assert::AreEqual(std::uint32_t{102}, Wide(Elite::FuelPriceTenths(0x46, 0x114)), L"only the low byte");
    Assert::AreEqual(std::uint32_t{0x7F}, Wide(Elite::FuelPriceTenths(0, 0xFF)), L"65025 / 36 overflows the divide");
  }

  // The laser rows: pulse, beam, mining and military, types 0 to 3.
  TEST_METHOD(FourRowsSellLasers)
  {
    Assert::IsTrue(Elite::LaserTypeOfRow(PULSE_LASER_ROW) == std::optional<std::uint8_t>{PULSE});
    Assert::IsTrue(Elite::LaserTypeOfRow(BEAM_LASER_ROW) == std::optional<std::uint8_t>{BEAM});
    Assert::IsTrue(Elite::LaserTypeOfRow(Elite::MINING_LASER_ROW) == std::optional<std::uint8_t>{MINING});
    Assert::IsTrue(Elite::LaserTypeOfRow(MILITARY_LASER_ROW) == std::optional<std::uint8_t>{MILITARY});
    Assert::IsFalse(Elite::LaserTypeOfRow(ECM_ROW).has_value());
  }

  // What the shipyard will not fit, in the order it asks.
  TEST_METHOD(TheShipyardRefusesToFit)
  {
    using Elite::EquipmentRefusal;
    Elite::Fitting fitting = Jameson();
    Assert::IsTrue(Elite::RefuseToFit(Elite::FUEL_ROW, fitting) == EquipmentRefusal::None);
    fitting.missionNumber = 1;
    Assert::IsTrue(Elite::RefuseToFit(Elite::FUEL_ROW, fitting) == EquipmentRefusal::NoFuelAvailable, L"the supernova's");
    fitting = Jameson();
    fitting.counts[Elite::FUEL_ROW - 1] = 0xFD;
    Assert::IsTrue(Elite::RefuseToFit(Elite::FUEL_ROW, fitting) == EquipmentRefusal::FuelFull);
    fitting.counts[Elite::MISSILE_ROW - 1] = 4;
    Assert::IsTrue(Elite::RefuseToFit(Elite::MISSILE_ROW, fitting) == EquipmentRefusal::FourMissilesOnly);
    fitting.counts[ECM_ROW - 1] = 1;
    Assert::IsTrue(Elite::RefuseToFit(ECM_ROW, fitting) == EquipmentRefusal::AlreadyFitted);
    Assert::IsTrue(Elite::RefuseToFit(PULSE_LASER_ROW, fitting) == EquipmentRefusal::None, L"another pulse laser, aft");
    Assert::IsTrue(Elite::RefuseToFit(Elite::MINING_LASER_ROW, fitting) == EquipmentRefusal::ScoopNeeded);
    fitting.counts[Elite::FUEL_SCOOPS_ROW - 1] = 1;
    Assert::IsTrue(Elite::RefuseToFit(Elite::MINING_LASER_ROW, fitting) == EquipmentRefusal::None);
    fitting.laserMountsFitted = 0x0F;
    Assert::IsTrue(Elite::RefuseToFit(BEAM_LASER_ROW, fitting) == EquipmentRefusal::FourLasers);
  }

  // What the shipyard will not take back, in the order it asks: the extension is refused with 21 tonnes aboard before it
  // asks whether one is fitted.
  TEST_METHOD(TheShipyardRefusesToTakeBack)
  {
    using Elite::EquipmentRefusal;
    Elite::Fitting fitting = Jameson();
    Assert::IsTrue(Elite::RefuseToRemove(Elite::FUEL_ROW, fitting) == EquipmentRefusal::FuelSaleIllegal);
    Assert::IsTrue(Elite::RefuseToRemove(Elite::MISSILE_ROW, fitting) == EquipmentRefusal::None);
    fitting.counts[Elite::MISSILE_ROW - 1] = 0;
    Assert::IsTrue(Elite::RefuseToRemove(Elite::MISSILE_ROW, fitting) == EquipmentRefusal::NoMissileToSell);
    Assert::IsTrue(Elite::RefuseToRemove(Elite::CARGO_BAY_ROW, fitting) == EquipmentRefusal::NoItemToSell);
    fitting.cargoUsedTonnes = 21;
    Assert::IsTrue(Elite::RefuseToRemove(Elite::CARGO_BAY_ROW, fitting) == EquipmentRefusal::TooMuchCargo);
    fitting.counts[Elite::FUEL_SCOOPS_ROW - 1] = 1;
    fitting.counts[Elite::MINING_LASER_ROW - 1] = 1;
    Assert::IsTrue(Elite::RefuseToRemove(Elite::FUEL_SCOOPS_ROW, fitting) == EquipmentRefusal::SellMiningLaser);
    Assert::IsTrue(Elite::RefuseToRemove(PULSE_LASER_ROW, fitting) == EquipmentRefusal::None);
  }

  // Fuel fills the tank; anything else counts one more, in 8 bits.
  TEST_METHOD(BuyingFillsOrCounts)
  {
    Assert::AreEqual(Elite::FUEL_FULL, Elite::CountAfterFitting(Elite::FUEL_ROW, 0x46));
    Assert::AreEqual(std::uint8_t{4}, Elite::CountAfterFitting(Elite::MISSILE_ROW, 3));
    Assert::AreEqual(std::uint8_t{0}, Elite::CountAfterFitting(ECM_ROW, 0xFF));
  }

  // A laser goes on a free mount, its type in the mount's two bits; it comes off only as the type sold, and leaves its bits.
  TEST_METHOD(LasersGoOnFreeMountsAndComeOffByType)
  {
    using Elite::MountRefusal;
    const Elite::LaserMounts jameson{.fitted = 1, .types = 0};
    Assert::IsTrue(Elite::MountHoldsLaser(jameson, FORE));
    Assert::IsFalse(Elite::MountHoldsLaser(jameson, LEFT));
    Assert::IsTrue(Elite::RefuseToFitMount(jameson, FORE) == MountRefusal::Occupied);
    Assert::IsTrue(Elite::RefuseToFitMount(jameson, LEFT) == MountRefusal::None);

    const Elite::LaserMounts beam = Elite::WithLaserFitted(Elite::LaserMounts{.fitted = 1, .types = 0xFF}, LEFT, BEAM);
    Assert::AreEqual(std::uint8_t{0x05}, beam.fitted);
    Assert::AreEqual(std::uint8_t{0xDF}, beam.types, L"left's two bits cleared, then set to beam");
    Assert::AreEqual(BEAM, Elite::LaserTypeOnMount(beam, LEFT));

    Assert::IsTrue(Elite::RefuseToClearMount(beam, LEFT, PULSE) == MountRefusal::WrongType);
    Assert::IsTrue(Elite::RefuseToClearMount(beam, 1, BEAM) == MountRefusal::NoLaser);
    Assert::IsTrue(Elite::RefuseToClearMount(beam, LEFT, BEAM) == MountRefusal::None);
    const Elite::LaserMounts cleared = Elite::WithLaserCleared(beam, LEFT);
    Assert::AreEqual(std::uint8_t{0x01}, cleared.fitted);
    Assert::AreEqual(std::uint8_t{0xDF}, cleared.types, L"the type's bits stay");
  }
};

} // namespace GameLogicTests
