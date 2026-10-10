#include "pch.h"

#include "NativeRoutines.h"

#include "Input.h"
#include "Maths.h"
#include "SaveLoad.h"
#include "StartUp.h"
#include "Timer.h"
#include "Reference.h"

namespace Elite
{

namespace
{

using EntryList = std::span<const NativeEntry> (*)() noexcept;

// The lists of the subsystems with an entry the program reaches: the program's own, and the interrupt handlers the vector table
// names.
constexpr std::array<EntryList, 5> SUBSYSTEMS = {&InputEntries, &MathsEntries, &SaveLoadEntries, &StartUpEntries, &TimerEntries};

} // namespace

void InstallNativeRoutines(Machine::Pc& _pc, const Machine::LoadedProgram& _program)
{
  const std::uint16_t code = _program.loadSegment;
  const std::uint16_t data = DataSegment(_program);
  for (const EntryList entries : SUBSYSTEMS)
  {
    for (const NativeEntry& entry : entries())
    {
      const auto body = entry.body;
      const Machine::NativeReturn exit = entry.exit;
      const std::uint16_t popBytes = entry.popBytes;
      _pc.Hook(
        code, entry.offset, std::string(entry.name),
        [body, exit, popBytes, code, data](Machine::Pc& _host)
        {
          Guest guest(_host, code, data);
          body(guest);
          switch (exit)
          {
          case Machine::NativeReturn::Near:
            _host.ReturnNear(popBytes);
            break;
          case Machine::NativeReturn::Far:
            _host.ReturnFar(popBytes);
            break;
          case Machine::NativeReturn::Interrupt:
            _host.ReturnInterrupt();
            break;
          }
        },
        entry.contract, exit, entry.wait);
    }
  }
}

} // namespace Elite
