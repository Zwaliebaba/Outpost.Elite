#include "pch.h"

#include "StartState.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>

namespace Elite
{

namespace
{

// _element and on, an array of one or more dimensions element by element in their order, from _next up to _end.
template <typename Element, typename Value> void FillElements(Element& _element, const Value*& _next, const Value* _end) noexcept
{
  if constexpr (std::is_array_v<Element>)
  {
    for (auto& inner : _element)
    {
      FillElements(inner, _next, _end);
    }
  }
  else if (_next != _end)
  {
    _element = static_cast<Element>(*_next);
    ++_next;
  }
}

// _array's first elements, in their order, _values; the rest stay 0.
template <typename Array> void Fill(Array& _array, std::initializer_list<std::uint16_t> _values) noexcept
{
  const std::uint16_t* next = _values.begin();
  FillElements(_array, next, _values.end());
}

// _array's first bytes the characters of _text, the NULs inside it too, but not the one that ends it; the rest stay 0.
template <typename Array, std::size_t Length> void FillText(Array& _array, const char (&_text)[Length]) noexcept
{
  const char* next = _text;
  FillElements(_array, next, _text + Length - 1);
}

} // namespace

void SetStartState(GameData& _data) noexcept
{
  _data.combat.laserBeamColorToggle = 0x03;
  _data.combat.laserBeamColor = 0x02;
  _data.docked.fuelUnitsDigit = 0x30;
  _data.docked.fuelTenthsDigit = 0x30;
  FillText(_data.docked.fuelDigits, "00000");
  _data.docked.missileCountDigit = 0x3F;
  FillText(_data.docked.productNames, "Food         t  \0"
                                      "Textiles     t  \0"
                                      "Radioactives t  \0"
                                      "Slaves       t  \0"
                                      "Liquor/Wines t  \0"
                                      "Luxuries     t  \0"
                                      "Narcotics    t  \0"
                                      "Computers    t  \0"
                                      "Machinery    t  \0"
                                      "Alloys       t  \0"
                                      "Firearms     t  \0"
                                      "Furs         t  \0"
                                      "Minerals     t  \0"
                                      "Gold         kg \0"
                                      "Platinum     kg \0"
                                      "Gem-Stones   g  \0"
                                      "Alien Items  t  \0"
                                      "Refugees     t  \0"
                                      "00000");
  _data.docked.refugeeRewardDigit = 0x30;
  _data.docked.titleShipType = 0x1A;
  FillText(_data.equipment.scoopedCargoText, "cargo item...");
  _data.flight.missileCountShown = 0x80;
  _data.flight.missileLockShown = 0x80;
  Fill(_data.flight.energyBanksShown, {0x80, 0x80, 0x80, 0x80});
  Fill(_data.flight.energyBankFill, {0x80, 0x80, 0x80, 0x80});
  _data.flight.rollRateShown = 0x80;
  _data.flight.pitchRateShown = 0x80;
  _data.flight.altitudeShown = 0x80;
  _data.flight.cabinTemperatureShown = 0x80;
  _data.flight.fuelShown = 0x80;
  _data.flight.ecmGlyphShown = 0x80;
  _data.flight.safeZoneGlyphShown = 0x80;
  _data.flight.laserTemperatureShown = 0x80;
  _data.flight.foreShieldShown = 0x80;
  _data.flight.aftShieldShown = 0x80;
  _data.flight.conditionColorShown = 0x80;
  _data.flight.speedShown = 0x80;
  _data.flight.safeZoneFlags = 0x01;
  _data.flight.objectSlotCount = 0x01;
  _data.flight.debrisSlotCount = 0x10;
  Fill(_data.flight.frameRateKeyLabel, {0x38, 0x29, 0x20});
  _data.flight.keyboardRecenter = 0x01;
  _data.flight.keyboardDamping = 0x01;
  _data.flight.soundEnabled = 0x01;
  _data.flight.minimumFrameMs = 0x32;
  FillText(_data.galaxy.distanceText, "Distance: 100.0 Light Years");
  Fill(_data.galaxy.distanceDigits, {0x20, 0x20, 0x20, 0x30, 0x30});
  FillText(_data.galaxy.findInput, "12345678");
  FillText(_data.galaxy.findInputEcho, "12345678");
  _data.galaxy.techLevelTensDigit = 0x2E;
  _data.galaxy.techLevelUnitsDigit = 0x2E;
  FillText(_data.galaxy.selectedDistanceText, "\0"
                                              "01234");
  FillText(_data.galaxy.populationDigits, "000.0");
  FillText(_data.galaxy.productivityDigits, "00000");
  FillText(_data.galaxy.radiusDigits, "00000");
  FillText(_data.galaxy.invasionDebriefText, "Thank you for destroying the\0"
                                             "thargoid base in the last system.\0"
                                             "From your flight log records, we\0"
                                             "have found that you were subject\0"
                                             "to anti ECM broadcasts, which may\0"
                                             "have affected your hyperspace\0"
                                             "drive unit!\0"
                                             "We have fitted a device capable\0"
                                             "of emulating the anti ECM broad-\0"
                                             "casts, which can be activated by\0"
                                             "pressing 'L'.  We have also\0"
                                             "awarded you the title Archangel!\0"
                                             " \0"
                                             "Press any key to continue!\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "\0"
                                             "01234567");
  FillText(_data.galaxy.descriptionNameBuffer, "000000000000");
  FillText(_data.hyperspace.arrivalGalaxyText, "Hyperspaced into galaxy ");
  _data.input.keyboardLastRollKey = 0x7F;
  _data.input.keyboardLastPitchKey = 0x7F;
  _data.input.amstradStickLastRoll = 0x7F;
  _data.input.amstradStickLastPitch = 0x7F;
  Fill(_data.market.marketRandomState, {0x007B, 0x01C8, 0x0315});
  FillText(_data.saveLoad.commanderFileName, "12345678.CDR");
  _data.scene.triangleFillPattern = 0xFFFF;
  _data.sound.musicPlaying = 0x01;
  _data.startUp.amstradPresent = 0x01;
  FillText(_data.text.bountyText, "BOUNTY:000000 Cr");
  FillText(_data.text.shipIdentityText, "CLASS: 1234567 TYPE: 12345678901");
  Fill(_data.text.inputCursorText, {0x20, 0x20});
  FillText(_data.video.screenshotTextFileName, "elite\0\0.lo");
  FillText(_data.video.screenshotGraphicsFileName, "elite\0\0.hi");
  _data.video.screenshotNumber = 0x3030;
  _data.video.fringeRandomA = 0x3039;
  _data.video.fringeRandomB = 0x0045;
  _data.playerShip.playerEnergy = 0x03FF;
  _data.playerShip.playerVelocityZ = 0x0030;
  _data.playerShip.playerSpeed = 0x0030;
  _data.messages.messagePointer = Message{0x05};
  FillText(_data.menus.priceText, "000000");
  FillText(_data.menus.quantityText, "00000");
  _data.drawing.screenLayout = 0xFF;
  _data.world.shipSlotCount = 0x20;
  Fill(_data.commander.fileSignature, {0x43, 0x6F, 0x6D, 0x6D, 0x61, 0x6E, 0x64, 0x65, 0x72, 0x20, 0x66, 0x69, 0x6C, 0x65, 0x1A});
  Fill(_data.commander.commanderTitle, {0x0F, 0x0D});
  FillText(_data.commander.commanderRankText, "COMMANDER");
  Fill(_data.commander.rankTextEnd, {0x20});
  FillText(_data.commander.creditBalanceText, "      100.0 Credits");
  _data.commander.randomState0 = 0x5A4A;
  _data.commander.randomState1 = 0x0248;
  _data.commander.randomState2 = 0xB753;
  _data.commander.currentSystemX = 0x14;
  _data.commander.currentSystemChartY = 0x56;
  _data.commander.chartCursorX = 0x50;
  _data.commander.chartCursorY = 0x40;
  _data.commander.shortRangeCursorX = 0x50;
  _data.commander.shortRangeCursorY = 0x40;
  _data.commander.galacticCursorX = 0x14;
  _data.commander.galacticCursorY = 0x56;
  _data.commander.chartIsShortRange = 0x01;
  FillText(_data.commander.currentSystem.name, "LAVE");
  _data.commander.currentSystem.nameLength = 0x04;
  _data.commander.currentSystem.index = 0x07;
  _data.commander.currentSystem.government = 0x03;
  _data.commander.currentSystem.economy = 0x02;
  _data.commander.currentSystem.techLevel = 0x05;
  _data.commander.currentSystem.populationTenthsBillion = 0x1C;
  _data.commander.currentSystem.speciesAdjective1 = 0xFF;
  _data.commander.currentSystem.productivityMillionCredits = 0x34F0;
  _data.commander.currentSystem.radiusKm = 0x1884;
  FillText(_data.commander.selectedSystem.name, "LAVE");
  _data.commander.selectedSystem.nameLength = 0x04;
  _data.commander.selectedSystem.index = 0x07;
  _data.commander.selectedSystem.government = 0x03;
  _data.commander.selectedSystem.economy = 0x02;
  _data.commander.selectedSystem.techLevel = 0x05;
  _data.commander.selectedSystem.populationTenthsBillion = 0x1C;
  _data.commander.selectedSystem.speciesAdjective1 = 0xFF;
  _data.commander.selectedSystem.productivityMillionCredits = 0x34F0;
  _data.commander.selectedSystem.radiusKm = 0x1884;
  _data.commander.descriptionSeed0 = 0xB9A4;
  _data.commander.descriptionSeed1 = 0xACB9;
  _data.commander.fuel = 0xFF;
  _data.commander.missileCount = 0x03;
  _data.commander.pulseLaserCount = 0x01;
  _data.commander.laserMountsFitted = 0x01;
  _data.commander.creditsTenths = 0x000003E8;
  FillText(_data.commander.defaultCommanderName, "JAMESON ");
}

} // namespace Elite
