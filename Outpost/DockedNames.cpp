#include "pch.h"

#include "DockedNames.h"

#include <array>

namespace Outpost
{

namespace
{

struct Commodity
{
  const wchar_t* name;
  const wchar_t* unit;
};

constexpr std::array<Commodity, Elite::COMMODITY_KINDS> COMMODITIES = {{
  {L"Food", L"t"},
  {L"Textiles", L"t"},
  {L"Radioactives", L"t"},
  {L"Slaves", L"t"},
  {L"Liquor/Wines", L"t"},
  {L"Luxuries", L"t"},
  {L"Narcotics", L"t"},
  {L"Computers", L"t"},
  {L"Machinery", L"t"},
  {L"Alloys", L"t"},
  {L"Firearms", L"t"},
  {L"Furs", L"t"},
  {L"Minerals", L"t"},
  {L"Gold", L"kg"},
  {L"Platinum", L"kg"},
  {L"Gem-Stones", L"g"},
  {L"Alien Items", L"t"},
}};

// The equipment in the status screen's order, FittedKind's (D02's names).
constexpr std::array<const wchar_t*, 13> EQUIPMENT = {
  L"Missiles",       L"Cargo Bay Extension", L"ECM System",        L"Pulse Laser",       L"Beam Laser",          L"Fuel Scoops",
  L"Escape Capsule", L"Energy Bomb",         L"Extra Energy Unit", L"Docking Computers", L"Galactic Hyperdrive", L"Mining Laser",
  L"Military Laser",
};

// The shipyard's rows the names treat apart, from 1.
constexpr std::size_t FUEL_ROW = 1;
constexpr std::size_t MISSILE_ROW = 2;

// The mounts by D03's names, as the game numbers them: fore, aft, right and left.
constexpr std::array<const wchar_t*, 4> MOUNTS = {L"Front", L"Rear", L"Right", L"Left"};

// The laser types.
constexpr std::array<const wchar_t*, 4> LASER_TYPES = {L"Pulse", L"Beam", L"Mining", L"Military"};

} // namespace

const wchar_t* DockedNames::Commodity(std::size_t _commodity) noexcept
{
  return _commodity < COMMODITIES.size() ? COMMODITIES[_commodity].name : L"";
}

const wchar_t* DockedNames::Unit(std::size_t _commodity) noexcept
{
  return _commodity < COMMODITIES.size() ? COMMODITIES[_commodity].unit : L"t";
}

const wchar_t* DockedNames::Equipment(Elite::FittedKind _kind) noexcept
{
  const auto index = static_cast<std::size_t>(_kind);
  return index < EQUIPMENT.size() ? EQUIPMENT[index] : L"";
}

const wchar_t* DockedNames::ListItem(std::size_t _row) noexcept
{
  switch (_row)
  {
  case FUEL_ROW:
    return L"Fuel (per light year)";
  case MISSILE_ROW:
    return L"Missile";
  default:
    // From the cargo bay extension on, the list is the status screen's from its missiles.
    return _row > MISSILE_ROW && _row - 2 < EQUIPMENT.size() ? EQUIPMENT[_row - 2] : L"";
  }
}

const wchar_t* DockedNames::Mount(std::size_t _mount) noexcept
{
  return _mount < MOUNTS.size() ? MOUNTS[_mount] : L"";
}

const wchar_t* DockedNames::LaserType(std::size_t _type) noexcept
{
  return _type < LASER_TYPES.size() ? LASER_TYPES[_type] : L"";
}

} // namespace Outpost
