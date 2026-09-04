#pragma once

// GameLogic builds on the repository's foundation header, exactly as the other projects do.
//
// Including it is not a licence to use it: GameLogic must not CALL a platform API. No window,
// no GPU, no audio device, no clock, no file system, no randomness, and no float or double
// anywhere (AGENTS.md section 5, ADR-002). Those rules exist so that the replay-equality and
// oracle suites mean what they say, and they are about calls rather than about includes.
/*
 * No C++/WinRT here. It is the executable's, and a static library that emits its version
 * directive constrains everything that links it -- see the note in NeuronCore.h.
 */
#define NEURON_NO_CPPWINRT

#include "NeuronCore.h"
