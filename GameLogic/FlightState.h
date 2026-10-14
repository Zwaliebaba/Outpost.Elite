// GameLogic/FlightState.h
#pragma once

#include "Field.h"
#include "Message.h"
#include "SpaceObject.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Flight.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct FlightState
{
  FlightState() noexcept = default;
  explicit FlightState(const FlightState&) noexcept = default;

  /// 6-byte particle; +0 x, +2 y (signed words, on screen while the high bytes are -0x20..0x1F and -0x10..0x0F), +4 byte 0x28-0x47 (the...
  Field<std::uint8_t> stardust[30][6];
  /// 6-byte particle; stardust+0xB4: start-of-frame copy for streaks; +5 is 1 for a particle respawned this frame
  Field<std::uint8_t> stardustPrevious[30][6];
  /// written by ComputeStardustShift; 4 at full playerSpeed
  Field<std::uint8_t> stardustShift;
  /// HandleFlightFunctionKeys: a screen replaced the cockpit, so it must be restored
  Field<std::uint8_t> flightScreenShown;
  /// +5 per shot, saturating at FFh; the fire button does nothing at F0h or above
  Field<std::uint8_t> laserTemperature;
  /// 1 green, 3 yellow, 2 red, 0 flashing red; written by UpdateConditionColor
  Field<std::uint8_t> conditionColor;
  /// first of the 22 dashboard cache bytes DS:4222-4237 that InvalidateDashboard sets to 0x80
  Field<std::uint8_t> missileCountShown;
  /// dashboard cache of missileState
  Field<std::uint8_t> missileLockShown;
  /// byte; dashboard cache of energyBankFill
  Field<std::uint8_t> energyBanksShown[4];
  /// byte; per-bank fill computed by DrawEnergyBanks from playerEnergy, bottom bank last
  Field<std::uint8_t> energyBankFill[4];
  /// dashboard cache
  Field<std::uint8_t> rollRateShown;
  /// dashboard cache
  Field<std::uint8_t> pitchRateShown;
  /// dashboard cache
  Field<std::uint8_t> altitudeShown;
  /// dashboard cache
  Field<std::uint8_t> cabinTemperatureShown;
  /// dashboard cache
  Field<std::uint8_t> fuelShown;
  /// dashboard cache: 0x20 or 0x81
  Field<std::uint8_t> ecmGlyphShown;
  /// dashboard cache: 0x20 or 0x82
  Field<std::uint8_t> safeZoneGlyphShown;
  /// dashboard cache
  Field<std::uint8_t> laserTemperatureShown;
  /// compared but never written by UpdateDashboard, so the fore shield bar is redrawn every frame unless the shield is 0x80
  Field<std::uint8_t> foreShieldShown;
  /// compared but never written, as foreShieldShown
  Field<std::uint8_t> aftShieldShown;
  /// dashboard cache
  Field<std::uint8_t> conditionColorShown;
  /// dashboard cache
  Field<std::uint8_t> speedShown;
  /// 1 after a successful dock (0x2C4F), 0 at 0x2A11; ShowCommanderStatusScreen debriefs only when it is 1
  Field<std::uint8_t> playerDocked;
  /// bit 0: inside the station's safe zone (UpdateSafeZone)
  Field<std::uint8_t> safeZoneFlags;
  /// currentGovernment on arrival (0x2B56), 0 in witch space (0x2A51); spawn limits and the station handler use it
  Field<std::uint8_t> spawnGovernment;
  /// text of the active warning
  Field<Message> warningMessage;
  /// last warning raised; the next search starts after it
  Field<std::uint8_t> warningIndex;
  /// slots before the debris: 20 (SetUpLocalSpace), 1 in the file
  Field<std::uint8_t> objectSlotCount;
  /// 16
  Field<std::uint8_t> debrisSlotCount;
  /// UpdateCompass; TransformAndDrawObjects classifies the station only when 1
  Field<std::uint8_t> compassTargetIsStation;
  /// selectedDistanceTenthsLy*36/10 (at least 1), set when H starts the countdown; taken from fuel on arrival (4720)
  Field<std::uint8_t> hyperspaceFuelCost;
  /// negated new pitch angle while UpdatePlayerMotion re-derives the orientation
  Field<std::uint16_t> pitchAngleScratch;
  /// negated new yaw angle, as pitchAngleScratch
  Field<std::uint16_t> yawAngleScratch;
  /// 1 for one frame after J: UpdatePlayerVelocity multiplies the playerSpeed by 32
  Field<std::uint8_t> jumpDriveEngaged;
  /// slot the missile is locked on
  Field<SlotIndex> missileTarget;
  /// laser type (0-3) for this view, from GetViewLaser; damage is type + 1; set at 0x8297 when fire is pressed; DrawLaserBeams picks its...
  Field<std::uint8_t> firingLaserType;
  /// toggled on each frame a type-0 laser is fired; that type fires only on alternate frames
  Field<std::uint8_t> laserAlternateFrame;
  /// 1 after I until the ship in the sights is identified
  Field<std::uint8_t> shipIdRequested;
  /// 40 at death; RunFlight keeps running frames until it reaches 0
  Field<std::uint8_t> gameOverFrames;
  /// frameRateKeyLabel's fields, DS:ABFF-AC01 in 1987, at their places there.
  struct FrameRateKeyLabel
  {
    FrameRateKeyLabel() noexcept = default;
    explicit FrameRateKeyLabel(const FrameRateKeyLabel&) noexcept = default;

    Field<std::uint8_t> gapABFF[1];
    Field<std::uint8_t> dataAC00;
    Field<std::uint8_t> dataAC01;

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("gapABFF", _self.gapABFF);
      _visitor("dataAC00", _self.dataAC00);
      _visitor("dataAC01", _self.dataAC01);
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
  /// the '8) ' of '(F8) ' at the end of the pause menu's frame-rate line; rewritten by F1-F10
  FrameRateKeyLabel frameRateKeyLabel;
  /// pause option R, 'Keyboard Recentre', 1 in the file: a key against the current rate zeroes it first. First of five (value, letter) r...
  Field<std::uint8_t> keyboardRecenter;
  /// pause option D, 'Keyboard Damping', 1 in the file: an idle axis decays 3 a frame toward 0
  Field<std::uint8_t> keyboardDamping;
  /// pause option Y, 'Reverse Y control', 0 in the file: negates pitch (ApplyReverseControls)
  Field<std::uint8_t> reverseYControl;
  /// pause option B, 'Reverse X and Y', 0 in the file: negates roll and pitch
  Field<std::uint8_t> reverseXAndY;
  /// pause option S, 'Sound Toggle', 1 in the file; TimerTick does nothing more when it is 0
  Field<std::uint8_t> soundEnabled;
  /// toggled by 'L' (0x8379, 0x838C) when antiEcmEmulatorFitted; at 1 the station starts no ECM
  Field<std::uint8_t> antiEcmActive;
  /// TimerTick plays nothing while it is 1
  Field<std::uint8_t> gamePaused;
  /// 50 in the file, which is F7's choice, though the label in the file reads (F8) (40 ms); the shortest a frame may take
  Field<std::uint8_t> minimumFrameMs;
  /// set on a 500/65536 chance at launch (Missile Jammed... Can't Launch!); M then fails until reset at 2A33
  Field<std::uint8_t> missileJammed;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("stardust", _self.stardust);
    _visitor("stardustPrevious", _self.stardustPrevious);
    _visitor("stardustShift", _self.stardustShift);
    _visitor("flightScreenShown", _self.flightScreenShown);
    _visitor("laserTemperature", _self.laserTemperature);
    _visitor("conditionColor", _self.conditionColor);
    _visitor("missileCountShown", _self.missileCountShown);
    _visitor("missileLockShown", _self.missileLockShown);
    _visitor("energyBanksShown", _self.energyBanksShown);
    _visitor("energyBankFill", _self.energyBankFill);
    _visitor("rollRateShown", _self.rollRateShown);
    _visitor("pitchRateShown", _self.pitchRateShown);
    _visitor("altitudeShown", _self.altitudeShown);
    _visitor("cabinTemperatureShown", _self.cabinTemperatureShown);
    _visitor("fuelShown", _self.fuelShown);
    _visitor("ecmGlyphShown", _self.ecmGlyphShown);
    _visitor("safeZoneGlyphShown", _self.safeZoneGlyphShown);
    _visitor("laserTemperatureShown", _self.laserTemperatureShown);
    _visitor("foreShieldShown", _self.foreShieldShown);
    _visitor("aftShieldShown", _self.aftShieldShown);
    _visitor("conditionColorShown", _self.conditionColorShown);
    _visitor("speedShown", _self.speedShown);
    _visitor("playerDocked", _self.playerDocked);
    _visitor("safeZoneFlags", _self.safeZoneFlags);
    _visitor("spawnGovernment", _self.spawnGovernment);
    _visitor("warningMessage", _self.warningMessage);
    _visitor("warningIndex", _self.warningIndex);
    _visitor("objectSlotCount", _self.objectSlotCount);
    _visitor("debrisSlotCount", _self.debrisSlotCount);
    _visitor("compassTargetIsStation", _self.compassTargetIsStation);
    _visitor("hyperspaceFuelCost", _self.hyperspaceFuelCost);
    _visitor("pitchAngleScratch", _self.pitchAngleScratch);
    _visitor("yawAngleScratch", _self.yawAngleScratch);
    _visitor("jumpDriveEngaged", _self.jumpDriveEngaged);
    _visitor("missileTarget", _self.missileTarget);
    _visitor("firingLaserType", _self.firingLaserType);
    _visitor("laserAlternateFrame", _self.laserAlternateFrame);
    _visitor("shipIdRequested", _self.shipIdRequested);
    _visitor("gameOverFrames", _self.gameOverFrames);
    _visitor("frameRateKeyLabel", _self.frameRateKeyLabel);
    _visitor("keyboardRecenter", _self.keyboardRecenter);
    _visitor("keyboardDamping", _self.keyboardDamping);
    _visitor("reverseYControl", _self.reverseYControl);
    _visitor("reverseXAndY", _self.reverseXAndY);
    _visitor("soundEnabled", _self.soundEnabled);
    _visitor("antiEcmActive", _self.antiEcmActive);
    _visitor("gamePaused", _self.gamePaused);
    _visitor("minimumFrameMs", _self.minimumFrameMs);
    _visitor("missileJammed", _self.missileJammed);
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

static_assert(std::is_trivially_copyable_v<FlightState> && std::is_standard_layout_v<FlightState>);

} // namespace Elite
