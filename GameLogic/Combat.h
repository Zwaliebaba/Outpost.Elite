#pragma once

#include <cstdint>

#include "Commander.h"
#include "Dashboard.h"
#include "Rng.h"
#include "ShipSlot.h"
#include "ViewChange.h"

/*
 * What happens after something is hit.
 *
 * Five routines the flight loop reaches on the frames where the shooting lands: two that make a
 * noise proportional to how close it was, one that takes the damage, one that breaks a piece of
 * equipment, and one that puts the energy bomb out. `TACTICS` calls three of them as well, so
 * they live here rather than inside `FlightLoop.cpp` -- but the flight loop is what forced them,
 * and phase 4 inherits them built rather than building them again.
 */
namespace Elite
{

/// 6502: sfxhit and sfxexpl -- the two explosions, and they are not the same sound.
inline constexpr std::uint8_t SOUND_SHIP_EXPLODING = 2; ///< 6502: sfxhit
inline constexpr std::uint8_t SOUND_EXPLOSION = 3;      ///< 6502: sfxexpl
inline constexpr std::uint8_t SOUND_BEEP = 5;           ///< 6502: sfxbeep

/// 6502: the three message tokens this file sends that are not arithmetic on a slot number.
inline constexpr std::uint8_t MESSAGE_RIGHT_ON_COMMANDER = 101; ///< 6502: EXNO2 -- LDA #101
inline constexpr std::uint8_t MESSAGE_ECM_DESTROYED = 108;      ///< 6502: ou2 -- LDA #108
inline constexpr std::uint8_t MESSAGE_SCOOPS_DESTROYED = 111;   ///< 6502: ou3 -- LDA #111

/*
 * 6502: NOISE2's X -- the frequency each explosion is played at, and they differ.
 *
 * `EXNO` passes 208 and `EXNO2` passes 81, so a ship being hit and a ship blowing up are told
 * apart by pitch as well as by effect number. Both are `LDX #n` immediates with no name in the
 * source.
 */
inline constexpr std::uint8_t EXPLOSION_PITCH_HIT = 208;
inline constexpr std::uint8_t EXPLOSION_PITCH_KILL = 81;

/*
 * 6502: EXNO -- the noise a ship makes when our laser lands on it.
 *
 * Returns the sustain byte `NOISE2` is handed, because that is the whole of what the routine
 * computes: five volumes chosen by the target's z high byte, loudest when it is nearest. The
 * caller does not read it -- part 11 discards it -- but the arithmetic is the routine and a
 * seam that swallowed it would leave nothing to compare.
 */
[[nodiscard]] std::uint8_t ExplosionVolume(std::uint8_t _distance) noexcept;

/// 6502: EXNO's tail -- the same, one threshold set apart, for a ship that has actually died.
[[nodiscard]] std::uint8_t KillVolume(std::uint8_t _distance) noexcept;

/// 6502: EXNO -- play it. `_work` is `INWK`, and byte 7 is what picks the volume.
std::uint8_t PlayHitSound(const ShipBlock& _work, DashboardEffects& _effects) noexcept;

/*
 * 6502: EXNO2 -- add a kill to the tally, and make the bigger noise.
 *
 * The tally is TWENTY-FOUR bits and its bottom eight are a fraction: `KWL%` is what a type is
 * worth below one kill and `KWH%` what it is worth above. Both tables live in the ship data
 * region, indexed from one like the blueprint table above them, and this port reads them from
 * there rather than copying them out -- `E%` is between the pointer table and them, so the three
 * addresses are one derivation.
 *
 * Returns the byte `NOISE2` gets, which part 11 stores into the dead ship's energy.
 */
std::uint8_t RecordKill(FlightScreen& _screen, DashboardEffects& _effects,
                        std::uint8_t _type) noexcept;

/*
 * 6502: OOPS -- take `_damage`, on the shield the hit came from, and the banks under it.
 *
 * `LDY #8 / LDA (INF),Y` reads the STORED block rather than `INWK`, which matters only for
 * `TACTICS`: in the flight loop the two are the same copy. Byte 8 is the z sign, so a hit from
 * behind goes to the aft shield.
 *
 * `_carryIn` is the caller's carry, because the `SBC` that takes the damage off the shield reads
 * it and neither entry sets it (§6.87).
 *
 * Returns false when the energy banks have gone -- `JMP DEATH` -- so the caller ends the frame.
 */
[[nodiscard]] bool TakeDamage(FlightScreen& _screen, DashboardEffects& _effects,
                              const ShipBlock& _target, std::uint8_t _damage,
                              bool _carryIn) noexcept;

/*
 * 6502: OUCH -- break something, one time in two, and say what broke.
 *
 * `JSR DORND / BMI out` is the coin toss and `CPX #22 / BCS out` is the range: the generator's X
 * picks which of the twenty-two hold slots is emptied, and the first seventeen are cargo while
 * the last five are equipment. A message already showing (`DLY` non-zero) suppresses it
 * entirely, so the routine is silent as often as it is not.
 *
 * `_carryIn` is `DORND`'s, and it is NOT free: `OOPS` reaches here by `JSR EXNO3 / JMP OUCH`, and
 * `EXNO3` is `LDY #sfxexpl / BNE NOISE`, so the carry is whatever the sound routine returned.
 * WHICH PIECE OF EQUIPMENT BREAKS DEPENDS ON WHETHER THE EXPLOSION GOT A VOICE (§6.88).
 */
void DamageEquipment(FlightScreen& _screen, bool _carryIn) noexcept;

/// 6502: BOMBOFF -- the bomb has burned out: standard bitmap mode again, and stop the flashing.
void StopEnergyBomb(ScreenState& _screen) noexcept;

} // namespace Elite
