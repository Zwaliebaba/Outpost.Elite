#include "pch.h"

#include "AnswerFile.h"
#include "Equipment.h"

#include <array>
#include <format>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// The shipyard's known answers, from the repository's root. The file's header documents its format and states the count.
constexpr std::string_view EQUIPMENT_ANSWERS_FILE = "Tests/GameLogicTests/EquipmentAnswers.txt";
constexpr std::uint8_t ECONOMIES = 8;
constexpr std::uint8_t GOVERNMENTS = 8;
constexpr std::uint8_t TECH_LEVELS = 16;
// The prices of a light year the fuel's answers are at, 0 to 255 in steps of 17, and the tanks they fill.
constexpr std::uint16_t FUEL_PRICE_STEP = 17;
constexpr std::size_t FUEL_PRICES = 16;
constexpr auto FUEL_LEVELS = std::to_array<std::uint8_t>({0, 1, 16, 64, 128, 200, 252, 253, 254, 255});

// The shipyard of _place: the rows on sale, and each one's price and resale price.
[[nodiscard]] std::string Shipyard(const Elite::MarketPlace& _place)
{
  const std::uint8_t rows = Elite::EquipmentRowsOnSale(_place.techLevel);
  std::string line = std::format("shipyard {} {} {} {}", _place.economy, _place.government, _place.techLevel, rows);
  for (std::uint8_t row = 1; row <= rows; ++row)
  {
    const std::uint16_t price = Elite::EquipmentPriceTenths(Elite::EquipmentItemAt(row), _place);
    line += std::format(" {}/{}", price, Elite::ResaleTenths(price));
  }
  return line;
}

// What filling each of FUEL_LEVELS' tanks costs at _priceTenths a light year.
[[nodiscard]] std::string Fuel(std::uint16_t _priceTenths)
{
  std::string line = std::format("fuel {}", _priceTenths);
  for (const std::uint8_t fuel : FUEL_LEVELS)
  {
    line += std::format(" {}:{}", fuel, Elite::FuelPriceTenths(fuel, _priceTenths));
  }
  return line;
}

} // namespace

// Known answers per subsystem (D53): the shipyard, from its rules, which have no screen code.
TEST_CLASS(EquipmentAnswerTests)
{
public:
  // The rows on sale and their prices for every economy and government at tech levels 0 to 15, 1,024 shipyards, and the cost of
  // filling the tank at 16 prices, are the answers recorded in EQUIPMENT_ANSWERS_FILE: 1,040 lines.
  TEST_METHOD(EveryShipyardIsAsRecorded)
  {
    std::vector<std::string> answers;
    for (std::uint8_t economy = 0; economy < ECONOMIES; ++economy)
    {
      for (std::uint8_t government = 0; government < GOVERNMENTS; ++government)
      {
        for (std::uint8_t techLevel = 0; techLevel < TECH_LEVELS; ++techLevel)
        {
          answers.push_back(Shipyard(Elite::MarketPlace{.economy = economy, .government = government, .techLevel = techLevel}));
        }
      }
    }
    for (std::size_t price = 0; price < FUEL_PRICES; ++price)
    {
      answers.push_back(Fuel(static_cast<std::uint16_t>(price * FUEL_PRICE_STEP)));
    }
    CheckSubsystemAnswers(EQUIPMENT_ANSWERS_FILE, answers, std::size_t{ECONOMIES} * GOVERNMENTS * TECH_LEVELS + FUEL_PRICES);
  }
};

} // namespace GameLogicTests
