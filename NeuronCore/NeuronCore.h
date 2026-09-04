#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Use the C++ standard templated min/max
#define NOMINMAX

// DirectX apps don't need GDI
//#define NODRAWTEXT
//#define NOGDI
//#define NOBITMAP

// Include <mcx.h> if you need this
#define NOMCX

// Include <winsvc.h> if you need this
#define NOSERVICE

// WinHelp is deprecated
#define NOHELP

#if !defined WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif

#include <WinSock2.h>
#include <ws2tcpip.h>

#include <windows.h>

/*
 * C++/WinRT belongs to the EXECUTABLE, and a project that does not want it says so.
 *
 * This was discovered rather than designed, by the first build that ever LINKED `GameLogic.lib`
 * into `Outpost.exe`: thirty-one instances of
 *
 *     LNK2038: mismatch detected for 'C++/WinRT version': value '2.0.250303.5'
 *              doesn't match value '3.0.260818.1' in pch.obj
 *
 * `Outpost` carries the `Microsoft.Windows.CppWinRT` NuGet package and so compiles against 3.0;
 * every other project takes the 2.0 that ships inside the Windows SDK. C++/WinRT emits a
 * `detect_mismatch` pragma precisely so that the linker refuses to mix the two, and it is right
 * to -- they are different headers with different inline definitions.
 *
 * The fix is not to align the versions but to stop the other projects using C++/WinRT at all,
 * which they never did. `GameLogic` is deterministic and platform-free by ADR-004, and
 * AGENTS.md section 5 sanctions C++/WinRT as "a COM-helper sanction only" -- for the presentation
 * layer's `com_ptr` and `check_hresult`. A static library with no COM in it emitting a version
 * directive is a constraint on everything that links it, bought for nothing.
 *
 * So: define NEURON_NO_CPPWINRT before including this header and the library links against
 * anything. `Outpost/pch.h` is the one place that does not.
 */
#if !defined(NEURON_NO_CPPWINRT)

#   include <unknwn.h>
#   include <restrictederrorinfo.h>
#   include <hstring.h>

// Undefine GetCurrentTime macro to prevent
// conflict with Storyboard::GetCurrentTime
#   undef GetCurrentTime

#   include <winrt/Windows.Foundation.h>
#   include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;

#endif // NEURON_NO_CPPWINRT

#include "Debug.h"
