// GameLogic/Platform.h
#pragma once

#include "Dos.h"
#include "DosFiles.h"
#include "FileStore.h"

#include <cstdint>

namespace Elite
{

class GameState;

/// The native platform the game runs on (D23, D27): the devices that take over from the emulated PC's, one Phase 5 step at a
/// time, each held here for as long as the Game that owns the platform. A de-assembled routine reaches them through Hardware,
/// which takes the platform beside the Pc (ADR-014).
///
/// Since the first step it holds DOS's file services (DosFiles). The speaker, video, input and the scheduler follow, in that
/// order (plan §5, Phase 5), each joining the platform as it replaces its emulated device.
class Platform
{
public:
  /// _state is the game's memory, which the devices read and write, and _files where DOS's files are; both must outlive the
  /// platform. DOS's clock starts at _startMoment.
  Platform(GameState& _state, Machine::FileStore& _files, const Machine::Dos::DateTime& _startMoment) noexcept;
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  /// The program with its PSP at _pspSegment starts, as DOS starts it (DosFiles::StartProgram).
  void StartProgram(std::uint16_t _pspSegment) noexcept;

  /// DOS's file services.
  [[nodiscard]] DosFiles& Files() noexcept
  {
    return m_files;
  }

  [[nodiscard]] const DosFiles& Files() const noexcept
  {
    return m_files;
  }

private:
  DosFiles m_files;
};

} // namespace Elite
