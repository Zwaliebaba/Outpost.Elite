// Engine/Win32.h
#pragma once

// The one header that owns the Windows macro family (AGENTS.md §4). They are set here, before
// <windows.h>, and nowhere else: no other header and no project file defines any of them. Include
// this header instead of <windows.h>.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOMCX
#define NOSERVICE
#define NOHELP
#include <windows.h>
