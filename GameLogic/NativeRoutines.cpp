#include "pch.h"

#include "NativeRoutines.h"

#include "Ai.h"
#include "Combat.h"
#include "Docked.h"
#include "Docking.h"
#include "Equipment.h"
#include "Flight.h"
#include "Galaxy.h"
#include "Hyperspace.h"
#include "Input.h"
#include "Market.h"
#include "Maths.h"
#include "SaveLoad.h"
#include "Scene.h"
#include "Ships.h"
#include "Sound.h"
#include "StartUp.h"
#include "Text.h"
#include "Timer.h"
#include "Video.h"
#include "Firmware.h"
#include "Reference.h"

#include <format>
#include <ostream>

namespace Elite
{

namespace
{

using EntryList = std::span<const NativeEntry> (*)() noexcept;

// Every subsystem's list of ported entries (plan §5 Phase 3).
constexpr std::array<EntryList, 19> SUBSYSTEMS = {
  &AiEntries,         &CombatEntries,  &DockedEntries, &DockingEntries, &EquipmentEntries, &FlightEntries, &GalaxyEntries,
  &HyperspaceEntries, &InputEntries,   &MarketEntries, &MathsEntries,   &SaveLoadEntries,  &SceneEntries,  &ShipsEntries,
  &SoundEntries,      &StartUpEntries, &TextEntries,   &TimerEntries,   &VideoEntries,
};

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
  _pc.Native().SetStackFloor(Machine::Memory::Linear(data, STACK_FIRST_OFFSET));
}

std::size_t NativeRoutineCount() noexcept
{
  std::size_t count = 0;
  for (const EntryList entries : SUBSYSTEMS)
  {
    count += entries().size();
  }
  return count;
}

void WriteNativeReport(const Machine::NativeCode& _native, std::ostream& _out)
{
  _out << "entry\troutine\twait\tcalls\tverified\tunverifiable\tmismatches\texecuted\n";
  for (const auto& [linear, hook] : _native.Hooks())
  {
    if (hook.segment == Machine::Firmware::ROM_SEGMENT)
    {
      continue; // the machine's own firmware (NativeFirmware.h), not a routine of the program
    }
    std::string executed;
    for (const std::uint16_t offset : hook.executed.Offsets())
      executed += std::format("{}{:04X}", executed.empty() ? "" : " ", offset);
    const std::string_view wait = hook.wait == Machine::NativeWait::Never       ? "never"
                                  : hook.wait == Machine::NativeWait::Sometimes ? "sometimes"
                                                                                : "always";
    _out << std::format("{:04X}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", hook.offset, hook.name, wait, hook.calls, hook.verified, hook.unverifiable,
                        hook.mismatches, executed);
  }
}

void WriteExecutedOffsets(std::span<const std::uint8_t> _executionMap, const Machine::LoadedProgram& _program, std::ostream& _out)
{
  const std::uint32_t base = Machine::Memory::Linear(_program.loadSegment, 0);
  for (std::uint32_t offset = 0; offset < CODE_SEGMENT_BYTES && base + offset < _executionMap.size(); ++offset)
  {
    if (_executionMap[base + offset] != 0)
      _out << std::format("{:04X}\n", offset);
  }
}

} // namespace Elite
