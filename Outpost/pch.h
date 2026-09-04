#pragma once

/*
 * The one project that DOES want C++/WinRT, so it is the one place that does not define
 * NEURON_NO_CPPWINRT. `Outpost` carries the Microsoft.Windows.CppWinRT package and uses
 * `winrt::com_ptr` and `check_hresult` for the Direct3D objects, which is the COM-helper
 * sanction AGENTS.md section 5 gives and the only use this repository has for it.
 */
#include "NeuronCore.h"
