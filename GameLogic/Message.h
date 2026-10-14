// GameLogic/Message.h
#pragma once

#include <cstdint>

namespace Elite
{

/// A text the message line shows: what messagePointer, messageShown and warningMessage hold, where the original holds the
/// text's address (D33, piece 3). Each is named for the text's row in the symbol table, or for its warning's check, in the
/// order of their addresses. MessageText gives a message's text; MESSAGE_TEXTS holds the constant ones.
enum class Message : std::uint16_t
{
  None,                    ///< 0, as messageShown is when no message is shown
  NavCompError,            ///< navCompErrorMessage
  EquipmentLoss,           ///< equipmentLossMessage
  LeavingStation,          ///< leavingStationMessage
  AutoDock,                ///< autoDockMessage
  FrontView,               ///< frontViewText
  RearView,                ///< rearViewText
  LeftView,                ///< leftViewText
  RightView,               ///< rightViewText
  Bounty,                  ///< bountyText
  ShipIdentity,            ///< shipIdentityText
  MissileWarning,          ///< warningTexts[0]
  AltitudeWarning,         ///< warningTexts[1]
  TemperatureWarning,      ///< warningTexts[2]
  EnergyWarning,           ///< warningTexts[3]
  ScoopRetrievalInactive,  ///< scoopRetrievalInactiveText
  ScoopedCargo,            ///< scoopedCargoText
  FuelScoopsActive,        ///< fuelScoopsActiveText
  CargoBayFull,            ///< cargoBayFullText
  EscapePodRetrieved,      ///< escapePodRetrievedText
  PreciousMetals,          ///< preciousMetalsText
  MetalAlloys,             ///< metalAlloysText
  AlienItems,              ///< alienItemsText
  MaskingDevice,           ///< maskingDeviceText
  ArrivalSystem,           ///< arrivalSystemText
  HyperspaceEngaged,       ///< hyperspaceEngagedText
  ArrivalGalaxy,           ///< arrivalGalaxyText
  NinthGalaxy,             ///< ninthGalaxyText
  WitchSpaceArrival,       ///< witchSpaceArrivalText
  FuelLeak,                ///< fuelLeakText
  IdIndicator,             ///< idIndicatorMessage
  GalacticDriveReady,      ///< galacticDriveReadyMessage
  HyperspaceCountdown,     ///< hyperspaceCountdownMessage
  OutOfRange,              ///< outOfRangeMessage
  NoSystemSelected,        ///< noSystemSelectedMessage
  NotEnoughFuel,           ///< notEnoughFuelMessage
  NavCompRepaired,         ///< navCompRepairedMessage
  HyperspaceJammed,        ///< hyperspaceJammedMessage
  EcmActive,               ///< ecmActiveMessage
  DockingComputerOff,      ///< dockingComputerOffMessage
  DockingComputerOn,       ///< dockingComputerOnMessage
  StationOutOfRange,       ///< stationOutOfRangeMessage
  JumpDriveEngaged,        ///< jumpDriveEngagedMessage
  JumpDriveMassLocked,     ///< jumpDriveMassLockedMessage
  JumpDriveVelocityLocked, ///< jumpDriveVelocityLockedMessage
  GamePaused,              ///< gamePausedMessage
  MissileLaunched,         ///< missileLaunchedMessage
  MissileLocked,           ///< missileLockedMessage
  MissilePrimed,           ///< missilePrimedMessage
  MissileUnarmed,          ///< missileUnarmedMessage
  GameOver,                ///< gameOverMessage
  MissileJammed,           ///< missileJammedMessage
  MissileTargetDestroyed,  ///< missileTargetDestroyedMessage
  DockingDenied,           ///< dockingDeniedMessage
};

} // namespace Elite
