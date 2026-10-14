// GameLogic/PlayerShipState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// The player's ship, which several subsystems write: its energy, shields, temperature, altitude, angles and rates, velocity, missiles,
/// laser, ECM, escape pod, view and docking computer, and whether it is dead. Its members are the original data segment's, by their names,
/// in their order there; the GameState holds it in GameData.
struct PlayerShipState
{
  /// set to 1 when an ECM goes off: NPC or the player's E key with ecmFitted; read and cleared, which picks...
  std::uint8_t ecmFired;
  /// the sun's apparent radius from DrawSunOrPlanet; CheckTemperatureWarning at >= 0xE1
  std::uint8_t cabinTemperature;
  /// 2*min(127, 255 - planet radius) from DrawSunOrPlanet; 0xFF at launch; CheckAltitudeWarning below 0x32
  std::uint8_t altitude;
  /// drawn on the top dashboard bar (cell 368Ch, the FORE-SHIELD row); ApplyEnemyLaserHit takes 15 off it
  std::uint8_t foreShield;
  /// drawn on the AFT-SHIELD bar (cell 37CCh)
  std::uint8_t aftShield;
  /// 0-3FFh; DrainEnergy, UseMaskingDevice and ApplyEnemyLaserHit take from it, below 0 sets playerDead; CheckEnergyWarning below 0x100;...
  std::uint16_t playerEnergy;
  /// 0 idle, 1 primed and looking for a target (T or I), 2 locked on missileTarget; also the colour index DrawMissileLockIndicator draws
  std::uint8_t missileState;
  /// set when playerEnergy goes below 0 (DrainEnergy, ApplyEnemyLaserHit) or by KillPlayer; GameLoop then reruns the title
  std::uint8_t playerDead;
  /// orientation angle, 800h per turn; with playerYawAngle sets the velocity direction (UpdatePlayerVelocity); re-derived after each pit...
  std::uint16_t playerPitchAngle;
  /// orientation angle, 800h per turn; see playerPitchAngle
  std::uint16_t playerYawAngle;
  /// 800h per turn; the roll input is added to it directly; the docking computer matches it to the station's spin
  std::uint16_t playerRollAngle;
  /// set by UpdateMissileAi; CheckMissileWarning clears it
  std::uint8_t incomingMissileAlert;
  /// signed -23..+23: the input code adds to it with clamping and the steering code stores it; drawn...
  std::uint16_t rollRate;
  /// set by ReadScanCode when D (20h) is released while inFlight; ProcessFlightKeys then toggles the docking computer if fitted; cleared...
  std::uint8_t dockingKeyReleased;
  /// 80 when the capsule launches; TickEscapePod counts it down and ends RunFlight; also suppresses enemy fire; while non-zero Ki...
  std::uint8_t escapePodFrames;
  /// 1 while the docking computer flies the ship (UpdatePlayerMotion calls RunDockingComputer)
  std::uint8_t dockingComputerOn;
  /// per-frame displacement subtracted from every object by MoveObjectsByVelocity
  std::uint16_t playerVelocityX;
  /// see playerVelocityX
  std::uint16_t playerVelocityY;
  /// see playerVelocityX
  std::uint16_t playerVelocityZ;
  /// 4-48 in steps of 4 (. and ,), 0 while the docking computer stops the ship; the jump drive needs 48
  std::uint16_t playerSpeed;
  /// 1 makes UpdatePlayerVelocity recompute the velocity
  std::uint8_t velocityDirty;
  /// F1 0, F2 400h, F3 200h, F4 600h (800h per turn); 0 front, 0x200 left, 0x400 rear, 0x600 right; RotateToViewDirection
  std::uint16_t viewAngle;
  /// set by the docking computer's final approach; F1-F4 are ignored while 1
  std::uint8_t viewLocked;
  /// 1 for the frame the laser fires; ResolveLaserFire clears it
  std::uint8_t laserFiring;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("ecmFired", _self.ecmFired);
    _visitor("cabinTemperature", _self.cabinTemperature);
    _visitor("altitude", _self.altitude);
    _visitor("foreShield", _self.foreShield);
    _visitor("aftShield", _self.aftShield);
    _visitor("playerEnergy", _self.playerEnergy);
    _visitor("missileState", _self.missileState);
    _visitor("playerDead", _self.playerDead);
    _visitor("playerPitchAngle", _self.playerPitchAngle);
    _visitor("playerYawAngle", _self.playerYawAngle);
    _visitor("playerRollAngle", _self.playerRollAngle);
    _visitor("incomingMissileAlert", _self.incomingMissileAlert);
    _visitor("rollRate", _self.rollRate);
    _visitor("dockingKeyReleased", _self.dockingKeyReleased);
    _visitor("escapePodFrames", _self.escapePodFrames);
    _visitor("dockingComputerOn", _self.dockingComputerOn);
    _visitor("playerVelocityX", _self.playerVelocityX);
    _visitor("playerVelocityY", _self.playerVelocityY);
    _visitor("playerVelocityZ", _self.playerVelocityZ);
    _visitor("playerSpeed", _self.playerSpeed);
    _visitor("velocityDirty", _self.velocityDirty);
    _visitor("viewAngle", _self.viewAngle);
    _visitor("viewLocked", _self.viewLocked);
    _visitor("laserFiring", _self.laserFiring);
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

static_assert(std::is_trivially_copyable_v<PlayerShipState> && std::is_standard_layout_v<PlayerShipState>);

} // namespace Elite
