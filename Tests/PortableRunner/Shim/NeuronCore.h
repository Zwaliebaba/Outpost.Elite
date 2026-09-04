#pragma once

/*
 * A stand-in for the Win32 surface the test project reaches for, so that the REAL
 * Tests/GameLogicTests/OracleImage.cpp compiles here rather than being replaced by a copy.
 *
 * OracleImage's job is to find the assembled game. It does that by asking Windows where its own
 * module lives and walking up the directory tree looking for the repository root -- which is a
 * good design, because it works for a test run from Visual Studio, from vstest.console.exe and
 * from a bare command line without any of them agreeing on a working directory. The two calls it
 * needs are the two implemented below, and /proc/self/exe answers the same question Windows
 * answers with GetModuleFileNameW.
 *
 * This file shadows the real NeuronCore.h and is on the include path ONLY for this runner. It
 * exists because GameLogic's pch.h includes NeuronCore.h (ADR-004 section 2), and NeuronCore.h
 * includes <Windows.h>.
 *
 * The header name deliberately collides with the real one, which is the one place this tree
 * breaks ADR-004's repo-wide-unique rule -- shadowing IS the mechanism, and renaming it would
 * mean editing the includes in the files under test. See ../README.md.
 */

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#define MAX_PATH 4096

using DWORD = unsigned long;
using HMODULE = void*;
using LPCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;

#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 1
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2

/*
 * There are no modules here, so there is no handle to hand back and no refcount to leave alone.
 * A null handle is what GetModuleFileNameW below expects anyway -- on Windows that means "the
 * running executable", and here it is the only thing it can mean.
 */
inline int GetModuleHandleExW(int, LPCWSTR, HMODULE* _module)
{
  *_module = nullptr;
  return 1;
}

/*
 * Where a commander file goes -- the one Win32 call Outpost/SaveStore.cpp makes.
 *
 * Answered from the process environment, which is the same question Windows answers, so the
 * default SaveStore constructor works here too when LOCALAPPDATA is set. The contract is Win32's
 * and is easy to get wrong in a stand-in: with a null buffer it returns the length INCLUDING the
 * terminator, and with a real one the length EXCLUDING it. SaveStore's `written >= needed` check
 * depends on both halves.
 */
inline DWORD GetEnvironmentVariableW(LPCWSTR _name, LPWSTR _buffer, DWORD _size)
{
  std::string narrow;
  for (const wchar_t* at = _name; *at != 0; ++at)
  {
    narrow += static_cast<char>(*at);
  }

  const char* value = std::getenv(narrow.c_str());
  if (value == nullptr)
  {
    return 0;
  }

  const std::size_t length = std::strlen(value);
  if (_buffer == nullptr || _size <= length)
  {
    return static_cast<DWORD>(length + 1);
  }

  for (std::size_t index = 0; index < length; ++index)
  {
    _buffer[index] = static_cast<wchar_t>(static_cast<unsigned char>(value[index]));
  }
  _buffer[length] = 0;
  return static_cast<DWORD>(length);
}

/// The running executable's path, widened a byte at a time. Non-ASCII path components would come
/// out wrong, and would have to for a shim that does not link a locale library; a repository
/// checked out under such a path fails here and works under MSVC, which the README records.
inline int GetModuleFileNameW(HMODULE, wchar_t* _buffer, int _size)
{
  char path[PATH_MAX] = {};
  const ssize_t length = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (length <= 0)
  {
    return 0;
  }

  int index = 0;
  for (; index < length && index < _size - 1; ++index)
  {
    _buffer[index] = static_cast<wchar_t>(static_cast<unsigned char>(path[index]));
  }
  _buffer[index] = 0;
  return index;
}
