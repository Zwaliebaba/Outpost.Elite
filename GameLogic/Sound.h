// GameLogic/Sound.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's sound routines, ported (plan §5 Phase 3, ADR-010): the sound effects and the title tune. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> SoundEntries() noexcept;

/// StartMusic (CS:7401): SilenceSpeakerTimer, the speaker's gate and data on, and the title tune from
/// its start. AX clobbered.
void StartMusic(Guest& _guest);

/// StopAllSound (CS:7423): StopSoundEffects, the music off and the speaker's gate and data cleared. AX
/// clobbered.
void StopAllSound(Guest& _guest);

/// SilenceSpeakerTimer (CS:7436): PIT channel 2 in mode 3 with divisor 2, out of hearing. AL clobbered.
void SilenceSpeakerTimer(Guest& _guest);

/// EmitNoiseSample (CS:7A93): the next bit of noiseSource to the speaker. Out: AL, the port 61h value.
void EmitNoiseSample(Guest& _guest);

/// ToggleSpeaker (CS:7AB8): flips the speaker's data bit. Out: AL, the port 61h value.
void ToggleSpeaker(Guest& _guest);

/// StartImpactSound (CS:7AC3): a noise sweep, step 30 shrinking by 4. AL clobbered; IF set on return.
void StartImpactSound(Guest& _guest);

/// StartExplosionSound (CS:7AFC): a noise sweep, step 50 shrinking by 8. AL clobbered; IF set on return.
void StartExplosionSound(Guest& _guest);

/// StartPlayerDeathSound (CS:7B09): a noise sweep, step 60 shrinking by 7. AL clobbered; IF set on return.
void StartPlayerDeathSound(Guest& _guest);

/// StartLaserSound (CS:7B71): unless a noise sweep runs, a tone sweep, step 20 shrinking by 2. AL
/// clobbered; IF set on return when it starts one.
void StartLaserSound(Guest& _guest);

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// StartBeep (CS:7A57): beepTicks = 70.
void StartBeep(GameState& _state);

/// StartLowBeep (CS:7A5D): lowBeepTicks = 70.
void StartLowBeep(GameState& _state);

/// StopSoundEffects (CS:7A63): every effect's trigger cleared. The original clears them under CLI.
void StopSoundEffects(GameState& _state);

/// StopContinuousNoise (CS:7B6B): continuousNoise = 0.
void StopContinuousNoise(GameState& _state);

/// StartPlayerHitSound (CS:7B96): 100 ticks of noise, the tone sweep and the two-tone stopped. The original does it
/// under CLI.
void StartPlayerHitSound(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void StartBeepEntry(Guest& _guest);           ///< Preserves every register.
void StartLowBeepEntry(Guest& _guest);        ///< Preserves every register.
void StopSoundEffectsEntry(Guest& _guest);    ///< Out: IF=1.
void StopContinuousNoiseEntry(Guest& _guest); ///< Preserves every register.
void StartPlayerHitSoundEntry(Guest& _guest); ///< Out: IF=1.

} // namespace Elite
