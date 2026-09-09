#pragma once

/*
 * A stand-in for the Win32 surface the test project reaches for, so that the real files compile
 * here rather than being replaced by copies.
 *
 * It was three functions until M6-b-7: two of them told `OracleImage` where its own module lived,
 * so that it could walk up to the repository root and find the assembled game. There is no
 * assembled game. What is left is the one call `Outpost/SaveStore.cpp` makes.
 *
 * This file shadows the real NeuronCore.h and is on the include path ONLY for this runner. It
 * exists because GameLogic's pch.h includes NeuronCore.h (ADR-004 section 2), and NeuronCore.h
 * includes <Windows.h>.
 *
 * The header name deliberately collides with the real one, which is the one place this tree
 * breaks ADR-004's repo-wide-unique rule -- shadowing IS the mechanism, and renaming it would
 * mean editing the includes in the files under test. See ../README.md.
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#define MAX_PATH 4096

using DWORD = unsigned long;
using LPCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;

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
