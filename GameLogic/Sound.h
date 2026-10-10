// GameLogic/Sound.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"

#include <cstdint>
#include <optional>
#include <span>

namespace Elite
{

// The reference's sound routines, ported (plan §5 Phase 3, ADR-010): the sound effects and the title tune. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> SoundEntries() noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// StartMusic (CS:7401): SilenceSpeakerTimer, the speaker's gate and data on in port 61h but not in speakerPortImage,
/// and the title tune from its start.
void StartMusic(GameState& _state, Hardware& _hardware);

/// StopAllSound (CS:7423): StopSoundEffects and its STI, the music off and the speaker's gate and data cleared.
void StopAllSound(GameState& _state, Hardware& _hardware);

/// SilenceSpeakerTimer (CS:7436): PIT channel 2 in mode 3 with divisor 2, out of hearing.
void SilenceSpeakerTimer(Hardware& _hardware);

/// EmitNoiseSample (CS:7A93): the next bit of noiseSource to the speaker. Returns the port 61h value.
std::uint8_t EmitNoiseSample(GameState& _state, Hardware& _hardware);

/// ToggleSpeaker (CS:7AB8): flips the speaker's data bit. Returns the port 61h value.
std::uint8_t ToggleSpeaker(GameState& _state, Hardware& _hardware);

/// StartBeep (CS:7A57): beepTicks = 70.
void StartBeep(GameState& _state);

/// StartLowBeep (CS:7A5D): lowBeepTicks = 70.
void StartLowBeep(GameState& _state);

/// StopSoundEffects (CS:7A63): every effect's trigger cleared. The original clears them under CLI.
void StopSoundEffects(GameState& _state);

/// StartImpactSound (CS:7AC3): a noise sweep, step 30 shrinking by 4. The original starts it under CLI. Returns the step
/// length the step counter starts at.
std::uint8_t StartImpactSound(GameState& _state);

/// StartExplosionSound (CS:7AFC): a noise sweep, step 50 shrinking by 8. The original starts it under CLI. Returns the
/// step length the step counter starts at.
std::uint8_t StartExplosionSound(GameState& _state);

/// StartPlayerDeathSound (CS:7B09): a noise sweep, step 60 shrinking by 7. The original starts it under CLI. Returns the
/// step length the step counter starts at.
std::uint8_t StartPlayerDeathSound(GameState& _state);

/// StopContinuousNoise (CS:7B6B): continuousNoise = 0.
void StopContinuousNoise(GameState& _state);

/// StartLaserSound (CS:7B71): unless a noise sweep runs, a tone sweep, step 20 shrinking by 2. The original starts it
/// under CLI. Returns the step length the step counter starts at, or nothing when a noise sweep runs.
std::optional<std::uint8_t> StartLaserSound(GameState& _state);

/// StartPlayerHitSound (CS:7B96): 100 ticks of noise, the tone sweep and the two-tone stopped. The original does it
/// under CLI.
void StartPlayerHitSound(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void StartMusicEntry(Guest& _guest);            ///< AX clobbered.
void StopAllSoundEntry(Guest& _guest);          ///< AX clobbered; IF=1.
void SilenceSpeakerTimerEntry(Guest& _guest);   ///< AX clobbered.
void EmitNoiseSampleEntry(Guest& _guest);       ///< Out: AL, the port 61h value.
void ToggleSpeakerEntry(Guest& _guest);         ///< Out: AL, the port 61h value.
void StartBeepEntry(Guest& _guest);             ///< Preserves every register.
void StartLowBeepEntry(Guest& _guest);          ///< Preserves every register.
void StopSoundEffectsEntry(Guest& _guest);      ///< Out: IF=1.
void StartImpactSoundEntry(Guest& _guest);      ///< AL clobbered; IF set on return.
void StartExplosionSoundEntry(Guest& _guest);   ///< Out: AL the step length, AH as it was; IF=1.
void StartPlayerDeathSoundEntry(Guest& _guest); ///< Out: AL the step length, AH as it was; IF=1.
void StopContinuousNoiseEntry(Guest& _guest);   ///< Preserves every register.
void StartLaserSoundEntry(Guest& _guest);       ///< AL clobbered; IF set on return when it starts a sweep.
void StartPlayerHitSoundEntry(Guest& _guest);   ///< Out: IF=1.

} // namespace Elite
