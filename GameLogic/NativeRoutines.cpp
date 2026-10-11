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

void InstallNativeRoutines(Machine::Pc& _pc, GameState& _state, Platform& _platform)
{
  const std::uint16_t code = _state.CodeSegment();
  GameState* const state = &_state;
  Platform* const platform = &_platform;
  for (const EntryList entries : SUBSYSTEMS)
  {
    for (const NativeEntry& entry : entries())
    {
      const auto body = entry.body;
      const EntryReturn exit = entry.exit;
      _pc.Hook(
        code, entry.offset, std::string(entry.name),
        [body, exit, state, platform](Machine::Pc& _host)
        {
          Guest guest(_host, *state, *platform);
          body(guest);
          switch (exit)
          {
          case EntryReturn::Far:
            _host.ReturnFar();
            break;
          case EntryReturn::Interrupt:
            _host.ReturnInterrupt();
            break;
          }
        },
        entry.wait);
    }
  }
}

} // namespace Elite
