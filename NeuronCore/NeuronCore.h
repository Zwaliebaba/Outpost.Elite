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
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

// Undefine GetCurrentTime macro to prevent
// conflict with Storyboard::GetCurrentTime
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;

#include "Debug.h"
