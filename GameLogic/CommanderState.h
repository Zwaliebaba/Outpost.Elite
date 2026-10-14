// GameLogic/CommanderState.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Elite
{

/// A system's record: the selected (hyperspace target) system's, which SelectSystemAtCursor fills and every WaitForScreenExitKey
/// rebuilds; the current system's, which an arrival copies from the target's; and the hyperspace countdown's copy of its target
/// (HyperspaceState).
struct SystemRecord
{
  /// the name, up to eight letters and its NUL; GenerateSystemName blanks characters 5 to 8 before it writes it
  std::uint8_t name[9];
  /// written by GenerateSystemName; TerminateSelectedSystemName puts the NUL here
  std::uint8_t nameLength;
  /// 0-255 in the galaxy; 7 (Lave) in the file's current system
  std::uint8_t index;
  /// printed as nnn.n Light Years; 0 = none selected (No System Selected), 47h or more = out of range; compared with fuel*10/36 by H
  std::uint16_t distanceTenthsLy;
  /// 0-7, index into DS:7CBD names (Anarchy ... Corporate State); the current system's prices equipment and the market, and picks
  /// the ship spawn tables (0x4A86)
  std::uint8_t government;
  /// 0-7, index into DS:7C0C names (Poor Agricultural ... Rich Industrial); the current system's prices equipment and the market
  std::uint8_t economy;
  /// 0-based, shown +1; the current system's equipment for sale has tech byte < this+2, market prices use min(this,9), and 9+ makes
  /// the station a Dodo (0x2ADE)
  std::uint8_t techLevel;
  /// tenths of a billion
  std::uint8_t populationTenthsBillion;
  /// 0xFF = Human Colonials; otherwise size adjective index (DS:7D9E)
  std::uint8_t speciesAdjective1;
  /// index into DS:7DC5
  std::uint8_t speciesAdjective2;
  /// index into DS:7E01
  std::uint8_t speciesAdjective3;
  /// index into DS:7E3D (Rodents ... Insects)
  std::uint8_t speciesType;
  /// millions of credits
  std::uint16_t productivityMillionCredits;
  /// shown in km
  std::uint16_t radiusKm;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("name", _self.name);
    _visitor("nameLength", _self.nameLength);
    _visitor("index", _self.index);
    _visitor("distanceTenthsLy", _self.distanceTenthsLy);
    _visitor("government", _self.government);
    _visitor("economy", _self.economy);
    _visitor("techLevel", _self.techLevel);
    _visitor("populationTenthsBillion", _self.populationTenthsBillion);
    _visitor("speciesAdjective1", _self.speciesAdjective1);
    _visitor("speciesAdjective2", _self.speciesAdjective2);
    _visitor("speciesAdjective3", _self.speciesAdjective3);
    _visitor("speciesType", _self.speciesType);
    _visitor("productivityMillionCredits", _self.productivityMillionCredits);
    _visitor("radiusKm", _self.radiusKm);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }
};

/// The commodities, in the market's order: food first, alien items last.
inline constexpr std::uint8_t COMMODITY_KINDS = 17;

/// The commodities the game reaches by name, by their place in the market's order.
inline constexpr std::uint8_t COMMODITY_SLAVES = 3;
inline constexpr std::uint8_t COMMODITY_ALLOYS = 9;
inline constexpr std::uint8_t COMMODITY_MINERALS = 12;
inline constexpr std::uint8_t COMMODITY_GOLD = 13;
inline constexpr std::uint8_t COMMODITY_PLATINUM = 14;
inline constexpr std::uint8_t COMMODITY_GEM_STONES = 15;
inline constexpr std::uint8_t COMMODITY_ALIEN_ITEMS = 16;

/// The commander (D38): commanderBlock, DS:756B-763A in 1987, its members in their order there. Written by its members, it is the
/// .cdr file's format, 208 bytes (D78), which SaveCommanderFile writes from it and LoadCommanderFile reads over it, and Replays/*.cdr are
/// files of it (ADR-008 item 8). Several subsystems write it.
struct CommanderState
{
  /// "Commander file" and the end-of-file byte 1Ah, the .cdr file's first bytes, which only a file writes
  std::uint8_t fileSignature[15];
  /// frame bytes 0x0F 0x0D then "COMMANDER " (ShowCommanderStatusScreen)
  std::uint8_t commanderTitle[2];
  /// 'COMMANDER', the rank inside commanderTitle (DS:757A, frame bytes first); AwardArchangelTitle overwrites it
  std::uint8_t commanderRankText[9];
  /// the space and the NUL after the rank, the end of the status screen's title
  std::uint8_t rankTextEnd[2];
  /// cash as text, " 100.0 Credits", and its NUL; FormatCredits writes the first 19 characters
  std::uint8_t creditBalanceText[20];
  /// with randomState1-2 three words stepped by NextRandom (a,b,c -> b,c,c+a+b); ShowBuyCargoScreen copies all three to marketRandomState
  std::uint16_t randomState0;
  std::uint16_t randomState1;
  std::uint16_t randomState2;
  /// 0-7, index into galaxySeeds
  std::uint8_t galaxyNumber;
  /// 0x14 (Lave) in the file
  std::uint8_t currentSystemX;
  /// the current system's y/2; 0x56 (Lave) in the file
  std::uint8_t currentSystemChartY;
  std::uint8_t chartCursorX;
  /// 0-0x7F
  std::uint8_t chartCursorY;
  /// the short-range chart's cursor kept between visits
  std::uint8_t shortRangeCursorX;
  std::uint8_t shortRangeCursorY;
  /// the galactic chart's cursor kept between visits
  std::uint8_t galacticCursorX;
  std::uint8_t galacticCursorY;
  /// 0 galactic chart, 1 short-range
  std::uint8_t chartIsShortRange;
  /// the system the commander is at
  SystemRecord currentSystem;
  /// the system selected on a chart, the hyperspace target
  SystemRecord selectedSystem;
  /// with descriptionSeed1 (DS:75DF) stepped by NextDescriptionRandom; set by SelectSystemAtCursor (0x1284)
  std::uint16_t descriptionSeed0;
  /// descriptionSeed0 XOR systemSeed2
  std::uint16_t descriptionSeed1;
  /// a byte nothing reads or writes but the commander file
  std::uint8_t spare;
  /// 255 = full; light years = value/36 (255 shows 7.0); equipment item 1
  std::uint8_t fuel;
  /// equipment item 2, at most 4
  std::uint8_t missileCount;
  /// item 3; hold 35 t instead of 20
  std::uint8_t largeCargoBayFitted;
  /// item 4 (by the order of DS:7EC4 and DS:84D2)
  std::uint8_t ecmFitted;
  /// item 5; one per mount fitted
  std::uint8_t pulseLaserCount;
  /// item 6
  std::uint8_t beamLaserCount;
  /// item 7; needed before mining lasers
  std::uint8_t fuelScoopsFitted;
  /// item 8
  std::uint8_t escapePodFitted;
  /// item 9
  std::uint8_t energyBombFitted;
  /// item 10
  std::uint8_t energyUnitFitted;
  /// item 11
  std::uint8_t dockingComputerFitted;
  /// item 12; cleared at 0x4802
  std::uint8_t galacticHyperdriveFitted;
  /// item 13; one more rock may spawn while it is 1 (0x4AB2)
  std::uint8_t miningLaserCount;
  /// item 14
  std::uint8_t militaryLaserCount;
  /// mission 2 reward, 'N' in flight (0x836F)
  std::uint8_t maskingDeviceFitted;
  /// bits 0-3: fore, aft, right, left hold a laser
  std::uint8_t laserMountsFitted;
  /// 2 bits per mount, mount 0 lowest: 0 pulse, 1 beam, 2 mining, 3 military
  std::uint8_t laserMountTypes;
  /// cash in tenths of a credit
  std::uint32_t creditsTenths;
  /// 0 clean, 1-39 offender, 40+ fugitive (DS:842A); saturates at 255; -5 per jump (0x4727)
  std::uint8_t legalStatus;
  /// incremented by CreditKill for kills with a bounty, saturates at 254; rating = first DS:8457 threshold above it; enemy missiles need...
  std::uint8_t killCount;
  /// JAMESON
  std::uint8_t defaultCommanderName[9];
  /// What the hold carries and what the market has: a lot per commodity, then the refugees and the tonnes in use.
  struct CargoHold
  {
    /// A commodity's units in the hold and on sale here.
    struct Lot
    {
      /// in the commodity's unit: tonnes, or kilograms for gold and platinum, grams for gem stones; gold is capped at 0xFA when
      /// scooped
      std::uint8_t held;
      /// the market's quantity, which ShowBuyCargoScreen rolls after an arrival
      std::uint8_t onSale;

      /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
      template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
      {
        _visitor("held", _self.held);
        _visitor("onSale", _self.onSale);
      }

      template <typename Visitor> void Visit(Visitor& _visitor)
      {
        VisitMembers(*this, _visitor);
      }

      template <typename Visitor> void Visit(Visitor& _visitor) const
      {
        VisitMembers(*this, _visitor);
      }
    };

    /// per commodity, in the market's order; slaves are escape pods scooped, alien items Thargons
    Lot lots[COMMODITY_KINDS];
    /// mission 1 refugees aboard (20 or 35), shown as "Refugees" by ShowInventoryScreen
    std::uint8_t refugeesTonnes;
    /// tonnes in the hold (gold, platinum and gems excluded); capacity 20, 35 with the extension
    std::uint8_t cargoUsedTonnes;

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("lots", _self.lots);
      _visitor("refugeesTonnes", _self.refugeesTonnes);
      _visitor("cargoUsedTonnes", _self.cargoUsedTonnes);
    }

    template <typename Visitor> void Visit(Visitor& _visitor)
    {
      VisitMembers(*this, _visitor);
    }

    template <typename Visitor> void Visit(Visitor& _visitor) const
    {
      VisitMembers(*this, _visitor);
    }
  };
  CargoHold cargoHold;
  /// 0 after an arrival (0x4754); ShowBuyCargoScreen then rolls the quantities on sale and sets 1
  std::uint8_t marketQuantitiesSet;
  /// jumps counted by UpdateMissionSchedule
  std::uint8_t missionJumpCount;
  /// 0 none, 1 supernova refugees, 2 masking device, 3 Thargoid invasion; set at 0x499B, cleared by ShowMissionDebriefing
  std::uint8_t missionNumber;
  /// 1 lets UpdateMissionSchedule count the missions' jumps in the first galaxy too; a new game clears it, only a file sets it
  std::uint8_t missionsInFirstGalaxy;
  /// nonzero in witch space: 100 on entry (0x4917), lowered by Thargoid kills (0x8C03); the status screen shows WITCH SPACE
  std::uint8_t witchspaceCountdown;
  /// frames before a fuel leak starts; 50 set on arrival in missions 1 and 3
  std::uint8_t fuelLeakDelayFrames;
  /// 51 when a leak starts; frames of leak left
  std::uint8_t fuelLeakFrames;
  /// set when the ship carrying the masking device is destroyed
  std::uint8_t maskShipDestroyed;
  /// set by TryScoopObject (0x447A); mission 2 debrief fits the cloaking device
  std::uint8_t maskingDeviceRecovered;
  /// 5 at the mission 2 briefing; decremented by TallyMaskMissionKill; mask spawning runs while nonzero
  std::uint8_t maskMissionShipsLeft;
  /// mission 3: set by ShowMissionBriefing; ships may fire in the safe zone, docking is refused (0x2C32), missiles damage the station; c...
  std::uint8_t thargoidInvasionActive;
  /// set at 0x5589 and 0x8B46
  std::uint8_t invadedStationDestroyed;
  /// mission 3 reward, 'L' in flight (0x8385) toggles DS:AC0E
  std::uint8_t antiEcmEmulatorFitted;
  /// 0 until supernovaFrames runs out; then starts from cabinTemperature and grows by max(1, h/4) a frame, killing on overflow (0x3F68-0...
  std::uint8_t supernovaHeat;
  /// 400 after the supernova briefing (0x6E77); counts down at 0x3F5E and stays 1; cleared on arrival
  std::uint16_t supernovaFrames;
  /// 0 not briefed, 1 briefed, 2 debriefing on screen
  std::uint8_t missionStage;
  /// set on every hyperspace arrival (0x47E1), cleared at a briefing; mission 1 completes only after a jump
  std::uint8_t jumpedSinceBriefing;
  /// set at 0x4968; mission 2 debrief "Running away from the mask ship..."
  std::uint8_t fledMaskShip;
  /// 2 at the mission 2 briefing; -1 per arrival; 1 means the mask ship's system
  std::uint8_t maskSystemJumps;
  /// selectedSystemIndex latched when the countdown starts
  std::uint8_t hyperspaceTargetIndex;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("fileSignature", _self.fileSignature);
    _visitor("commanderTitle", _self.commanderTitle);
    _visitor("commanderRankText", _self.commanderRankText);
    _visitor("rankTextEnd", _self.rankTextEnd);
    _visitor("creditBalanceText", _self.creditBalanceText);
    _visitor("randomState0", _self.randomState0);
    _visitor("randomState1", _self.randomState1);
    _visitor("randomState2", _self.randomState2);
    _visitor("galaxyNumber", _self.galaxyNumber);
    _visitor("currentSystemX", _self.currentSystemX);
    _visitor("currentSystemChartY", _self.currentSystemChartY);
    _visitor("chartCursorX", _self.chartCursorX);
    _visitor("chartCursorY", _self.chartCursorY);
    _visitor("shortRangeCursorX", _self.shortRangeCursorX);
    _visitor("shortRangeCursorY", _self.shortRangeCursorY);
    _visitor("galacticCursorX", _self.galacticCursorX);
    _visitor("galacticCursorY", _self.galacticCursorY);
    _visitor("chartIsShortRange", _self.chartIsShortRange);
    _visitor("currentSystem", _self.currentSystem);
    _visitor("selectedSystem", _self.selectedSystem);
    _visitor("descriptionSeed0", _self.descriptionSeed0);
    _visitor("descriptionSeed1", _self.descriptionSeed1);
    _visitor("spare", _self.spare);
    _visitor("fuel", _self.fuel);
    _visitor("missileCount", _self.missileCount);
    _visitor("largeCargoBayFitted", _self.largeCargoBayFitted);
    _visitor("ecmFitted", _self.ecmFitted);
    _visitor("pulseLaserCount", _self.pulseLaserCount);
    _visitor("beamLaserCount", _self.beamLaserCount);
    _visitor("fuelScoopsFitted", _self.fuelScoopsFitted);
    _visitor("escapePodFitted", _self.escapePodFitted);
    _visitor("energyBombFitted", _self.energyBombFitted);
    _visitor("energyUnitFitted", _self.energyUnitFitted);
    _visitor("dockingComputerFitted", _self.dockingComputerFitted);
    _visitor("galacticHyperdriveFitted", _self.galacticHyperdriveFitted);
    _visitor("miningLaserCount", _self.miningLaserCount);
    _visitor("militaryLaserCount", _self.militaryLaserCount);
    _visitor("maskingDeviceFitted", _self.maskingDeviceFitted);
    _visitor("laserMountsFitted", _self.laserMountsFitted);
    _visitor("laserMountTypes", _self.laserMountTypes);
    _visitor("creditsTenths", _self.creditsTenths);
    _visitor("legalStatus", _self.legalStatus);
    _visitor("killCount", _self.killCount);
    _visitor("defaultCommanderName", _self.defaultCommanderName);
    _visitor("cargoHold", _self.cargoHold);
    _visitor("marketQuantitiesSet", _self.marketQuantitiesSet);
    _visitor("missionJumpCount", _self.missionJumpCount);
    _visitor("missionNumber", _self.missionNumber);
    _visitor("missionsInFirstGalaxy", _self.missionsInFirstGalaxy);
    _visitor("witchspaceCountdown", _self.witchspaceCountdown);
    _visitor("fuelLeakDelayFrames", _self.fuelLeakDelayFrames);
    _visitor("fuelLeakFrames", _self.fuelLeakFrames);
    _visitor("maskShipDestroyed", _self.maskShipDestroyed);
    _visitor("maskingDeviceRecovered", _self.maskingDeviceRecovered);
    _visitor("maskMissionShipsLeft", _self.maskMissionShipsLeft);
    _visitor("thargoidInvasionActive", _self.thargoidInvasionActive);
    _visitor("invadedStationDestroyed", _self.invadedStationDestroyed);
    _visitor("antiEcmEmulatorFitted", _self.antiEcmEmulatorFitted);
    _visitor("supernovaHeat", _self.supernovaHeat);
    _visitor("supernovaFrames", _self.supernovaFrames);
    _visitor("missionStage", _self.missionStage);
    _visitor("jumpedSinceBriefing", _self.jumpedSinceBriefing);
    _visitor("fledMaskShip", _self.fledMaskShip);
    _visitor("maskSystemJumps", _self.maskSystemJumps);
    _visitor("hyperspaceTargetIndex", _self.hyperspaceTargetIndex);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }
};

static_assert(std::is_trivially_copyable_v<CommanderState> && std::is_standard_layout_v<CommanderState>);

/// The commander's equipment, a member to an item, in the equipment list's order (DS:7EC4 and DS:84D2): the fuel first and the
/// military laser last, as the shipyard's rows count them from 1 and the status screen's list from the missiles.
inline constexpr std::uint8_t CommanderState::*const EQUIPMENT_ITEMS[] = {
  &CommanderState::fuel,
  &CommanderState::missileCount,
  &CommanderState::largeCargoBayFitted,
  &CommanderState::ecmFitted,
  &CommanderState::pulseLaserCount,
  &CommanderState::beamLaserCount,
  &CommanderState::fuelScoopsFitted,
  &CommanderState::escapePodFitted,
  &CommanderState::energyBombFitted,
  &CommanderState::energyUnitFitted,
  &CommanderState::dockingComputerFitted,
  &CommanderState::galacticHyperdriveFitted,
  &CommanderState::miningLaserCount,
  &CommanderState::militaryLaserCount,
};

} // namespace Elite
