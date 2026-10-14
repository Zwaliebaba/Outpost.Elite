// Tests/GameLogicTests/PlatformRig.h
#pragma once

#include "DirectoryFileStore.h"
#include "GameState.h"
#include "Hardware.h"
#include "Platform.h"
#include "ReferenceRig.h"

#include <memory>
#include <string_view>
#include <utility>

namespace GameLogicTests
{

/// The native platform on its own (D33), as the emulated PC's tests ran a Pc: powered on over a GameState with nothing loaded,
/// DOS's files in a scratch directory, and a program whose start is a routine the test gives (Start), which reaches the
/// platform through Hardware as the game's routines do. With no image loaded the GameState's code and data segments are both
/// at segment 0, so data-segment offsets are linear addresses.
class PlatformRig
{
public:
  explicit PlatformRig(std::string_view _name, bool _mousePresent = false, const Elite::DosDateTime& _startMoment = Elite::DosDateTime{})
    : m_directory(_name),
      m_files(m_directory.Path()),
      m_state(std::make_unique<Elite::GameState>()),
      m_platform(std::make_unique<Elite::Platform>(*m_state, m_files, Desc(_mousePresent, _startMoment))),
      m_hardware(*m_platform)
  {
  }

  /// The program's start: _routine, a coroutine (D77), runs from the first RunUntil, and the program ends when it returns.
  void Start(Elite::ProgramRoutine _routine)
  {
    m_platform->SetEntry(std::move(_routine));
  }

  [[nodiscard]] Elite::Platform& Host() noexcept
  {
    return *m_platform;
  }

  /// The platform as native code reaches it.
  [[nodiscard]] Elite::Hardware& Native() noexcept
  {
    return m_hardware;
  }

  [[nodiscard]] Elite::GameState& State() noexcept
  {
    return *m_state;
  }

  /// The platform destroyed, the program's coroutine with it, as a host's Game is at the end.
  void PowerOff() noexcept
  {
    m_platform.reset();
  }

private:
  [[nodiscard]] static Elite::Platform::Desc Desc(bool _mousePresent, const Elite::DosDateTime& _startMoment)
  {
    Elite::Platform::Desc desc;
    desc.startMoment = _startMoment;
    desc.mousePresent = _mousePresent;
    return desc;
  }

  ScratchDirectory m_directory;
  Elite::DirectoryFileStore m_files;
  std::unique_ptr<Elite::GameState> m_state; // over 100 KiB
  std::unique_ptr<Elite::Platform> m_platform;
  Elite::Hardware m_hardware;
};

} // namespace GameLogicTests
