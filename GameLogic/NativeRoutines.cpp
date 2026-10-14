#include "pch.h"

#include "NativeRoutines.h"

#include "Input.h"
#include "StartUp.h"
#include "Timer.h"

namespace Elite
{

namespace
{

using EntryList = std::span<const NativeEntry> (*)() noexcept;

// The lists of the subsystems with an entry the program reaches besides its start: the timer's and the keyboard's interrupt
// handlers (int 8 and int 9), which the platform knows by what they are (EntryKind).
constexpr std::array<EntryList, 2> SUBSYSTEMS = {&InputEntries, &TimerEntries};

} // namespace

void InstallNativeRoutines(Platform& _platform, GameState& _state)
{
  GameState* const state = &_state;
  Platform* const platform = &_platform;
  // The program's start, a coroutine whose Guest lives in its frame for the whole run (D77).
  _platform.SetEntry(
    [state, platform]() -> Task<void>
    {
      Guest guest(*platform, *state);
      co_await StartEntry(guest);
    });
  for (const EntryList entries : SUBSYSTEMS)
  {
    for (const NativeEntry& entry : entries())
    {
      const auto body = entry.body;
      const NativeRoutine routine = [body, state, platform]
      {
        Guest guest(*platform, *state);
        body(guest);
      };
      switch (entry.kind)
      {
      case EntryKind::TimerHandler:
        _platform.SetGameHandler(Irq::Timer, routine);
        break;
      case EntryKind::KeyboardHandler:
        _platform.SetGameHandler(Irq::Keyboard, routine);
        break;
      }
    }
  }
}

} // namespace Elite
