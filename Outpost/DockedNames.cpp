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

} // namespace Outpost
