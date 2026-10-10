// Outpost/Application.h
#pragma once

#include "Win32.h"

#include <filesystem>
#include <string>

namespace Outpost
{

/// The game, as the plan's Phase 2 shell runs it (ADR-009): the reference on the PC host in paced time
/// (ADR-008), kept in step with the wall clock, drawn by the Engine's presenter, heard through its audio
/// stream, played from the keyboard, and recorded as a replay.
class Application
{
public:
  /// Runs until the window is closed or the game ends. Returns the process's exit code.
  [[nodiscard]] int Run(HINSTANCE _instance);

private:
  void Fail(const std::wstring& _message) const;

  HWND m_window = nullptr;
};

} // namespace Outpost
