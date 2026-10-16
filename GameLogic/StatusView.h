// GameLogic/StatusView.h
#pragma once

#include "CommanderState.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Elite
{

/// The legal statuses the status screen names, by the commander's legal status: Clean below 1, Offender below 40, Fugitive.
enum class LegalStanding : std::uint8_t
{
  Clean,
  Offender,
  Fugitive,
};

/// The equipment the status screen lists, in its order: the missiles, then each item EQUIPMENT_ITEMS names after them.
enum class FittedKind : std::uint8_t
{
  Missiles,
  CargoBayExtension,
  EcmSystem,
  PulseLaser,
  BeamLaser,
  FuelScoops,
  EscapeCapsule,
  EnergyBomb,
  ExtraEnergyUnit,
  DockingComputers,
  GalacticHyperdrive,
  MiningLaser,
  MilitaryLaser,
};

/// The laser mounts, as the status screen lists a laser's: the original's fore, aft, right and left, a bit each in that order.
inline constexpr std::uint8_t MOUNT_FORE = 0x01;
inline constexpr std::uint8_t MOUNT_AFT = 0x02;
inline constexpr std::uint8_t MOUNT_RIGHT = 0x04;
inline constexpr std::uint8_t MOUNT_LEFT = 0x08;

/// An item fitted, as the status screen lists it.
struct FittedItem
{
  FittedKind kind = FittedKind::Missiles;
  std::uint8_t count = 0;  ///< the missiles': how many; every other item's: its member's value
  std::uint8_t mounts = 0; ///< a laser's: the mounts that carry it (MOUNT_FORE...); 0 for every other item
};

/// What D08, the commander's status (D82, D75, ADR-063), shows: plain values for the shell (R9), which words them.
struct StatusView
{
  std::uint64_t sequence = 0;   ///< one more for each view published
  std::string systemName;       ///< where the ship is docked, for the frame's top bar
  bool dodo = false;            ///< a Dodo station, of tech level 9 and above; otherwise a Coriolis
  std::string title;            ///< COMMANDER, or ARCHANGEL once the third mission is done
  std::string commanderName;    ///< JAMESON
  bool witchSpace = false;      ///< both systems are witch space
  std::string presentSystem;    ///< LAVE
  std::string hyperspaceTarget; ///< DISO
  std::uint8_t fuelTenths = 0;  ///< light years, in tenths: 70 for 7.0
  std::uint32_t creditsTenths = 0;
  LegalStanding legal = LegalStanding::Clean;
  std::uint8_t rating = 0; ///< 0 Harmless to 8 Elite
  std::vector<FittedItem> equipment;
  std::array<std::uint8_t, COMMODITY_KINDS> held{};
  std::uint8_t refugeesTonnes = 0;
  std::uint8_t usedTonnes = 0; ///< the hold's tonnes in use: every commodity but gold, platinum and gem-stones, and the refugees
  std::uint8_t bayTonnes = 0;  ///< 20, or 35 with the extension
};

} // namespace Elite
