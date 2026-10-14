// GameLogic/Sound.h
#pragma once

#include "GameState.h"
#include "Host.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's sound routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the sound effects and the title tune. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// StartMusic (CS:7401): SilenceSpeakerTimer, the speaker's gate and data on in port 61h but not in speakerPortImage,
/// and the title tune from its start.
void StartMusic(GameState& _state, Host& _host);

/// StopAllSound (CS:7423): StopSoundEffects, the music off and the speaker's gate and data cleared.
void StopAllSound(GameState& _state, Host& _host);

/// SilenceSpeakerTimer (CS:7436): PIT channel 2 in mode 3 with divisor 2, out of hearing.
void SilenceSpeakerTimer(Host& _host);

/// EmitNoiseSample (CS:7A93): the next bit of noiseSource to the speaker. Returns the port 61h value.
std::uint8_t EmitNoiseSample(GameState& _state, Host& _host);

/// ToggleSpeaker (CS:7AB8): flips the speaker's data bit. Returns the port 61h value.
std::uint8_t ToggleSpeaker(GameState& _state, Host& _host);

/// StartBeep (CS:7A57): beepTicks = 70.
void StartBeep(GameState& _state);

/// StartLowBeep (CS:7A5D): lowBeepTicks = 70.
void StartLowBeep(GameState& _state);

/// StopSoundEffects (CS:7A63): every effect's trigger cleared. The original clears them under CLI.
void StopSoundEffects(GameState& _state);

/// StartImpactSound (CS:7AC3): a noise sweep, step 30 shrinking by 4. The original starts it under CLI.
void StartImpactSound(GameState& _state);

/// StartExplosionSound (CS:7AFC): a noise sweep, step 50 shrinking by 8. The original starts it under CLI.
void StartExplosionSound(GameState& _state);

/// StartPlayerDeathSound (CS:7B09): a noise sweep, step 60 shrinking by 7. The original starts it under CLI. Returns the
/// step length the step counter starts at.
std::uint8_t StartPlayerDeathSound(GameState& _state);

/// StartLaserSound (CS:7B71): unless a noise sweep runs, a tone sweep, step 20 shrinking by 2.
void StartLaserSound(GameState& _state);

/// StartPlayerHitSound (CS:7B96): 100 ticks of noise, the tone sweep and the two-tone stopped. The original does it
/// under CLI.
void StartPlayerHitSound(GameState& _state);

} // namespace Elite
