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

// The lists of the subsystems with an entry the program reaches: the program's own, and the timer's and the keyboard's interrupt
// handlers, which the vector table names (int 8 and int 9).
constexpr std::array<EntryList, 3> SUBSYSTEMS = {&InputEntries, &StartUpEntries, &TimerEntries};

} // namespace

void InstallNativeRoutines(Platform& _platform, GameState& _state)
{
  const std::uint16_t code = _state.CodeSegment();
  GameState* const state = &_state;
  Platform* const platform = &_platform;
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
      switch (entry.exit)
      {
      case EntryReturn::Far:
        _platform.SetEntry(std::string(entry.name), routine);
        break;
      case EntryReturn::Interrupt:
        _platform.Hook(FarAddress{code, entry.offset}, std::string(entry.name), routine);
        break;
      }
    }
  }
}

} // namespace Elite
