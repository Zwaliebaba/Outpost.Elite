#pragma once

/*
 * No C++/WinRT here. It is the executable's, and a static library that emits its version
 * directive constrains everything that links it -- see the note in NeuronCore.h.
 */
#define NEURON_NO_CPPWINRT

#include "NeuronCore.h"

#include "CppUnitTest.h"
