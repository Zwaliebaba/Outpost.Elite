// Tests/GameLogicTests/HostRig.h
#pragma once

#include "DirectoryFileStore.h"
#include "GameRig.h"
#include "GameState.h"
#include "Host.h"

#include <memory>
#include <string_view>
#include <utility>

namespace GameLogicTests
{

/// The Host on its own (D33, D90): powered on over a GameState with nothing loaded, the commander files in a scratch
/// directory, and a program whose start is a routine the test gives (Start), which reaches the Host as the game's routines do.
class HostRig
{
public:
  explicit HostRig(std::string_view _name, bool _mousePresent = false, const Elite::DosDateTime& _startMoment = Elite::DosDateTime{})
    : m_directory(_name),
      m_files(m_directory.Path()),
      m_state(std::make_unique<Elite::GameState>()),
      m_host(std::make_unique<Elite::Host>(*m_state, m_files, Desc(_mousePresent, _startMoment)))
  {
  }

  /// The program's start: _routine, a coroutine (D77), runs from the first RunUntil, and the program ends when it returns.
  void Start(Elite::ProgramRoutine _routine)
  {
    m_host->SetEntry(std::move(_routine));
  }

  [[nodiscard]] Elite::Host& Host() noexcept
  {
    return *m_host;
  }

  [[nodiscard]] Elite::GameState& State() noexcept
  {
    return *m_state;
  }

  /// The platform destroyed, the program's coroutine with it, as a host's Game is at the end.
  void PowerOff() noexcept
  {
    m_host.reset();
  }

private:
  [[nodiscard]] static Elite::Host::Desc Desc(bool _mousePresent, const Elite::DosDateTime& _startMoment)
  {
    Elite::Host::Desc desc;
    desc.startMoment = _startMoment;
    desc.mousePresent = _mousePresent;
    return desc;
  }

  ScratchDirectory m_directory;
  Elite::DirectoryFileStore m_files;
  std::unique_ptr<Elite::GameState> m_state; // over 100 KiB
  std::unique_ptr<Elite::Host> m_host;
};

} // namespace GameLogicTests
