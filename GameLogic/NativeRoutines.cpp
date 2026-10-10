#include "pch.h"

#include "NativeRoutines.h"

#include "Maths.h"
#include "Reference.h"

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DX;

using Body = void (*)(Guest&);

// One hooked entry: where it is, what Symbols.tsv calls it, the native body, and the contract it is
// compared on. Every entry here returns with a plain near RET.
struct Entry
{
  std::uint16_t offset;
  std::string_view name;
  Body body;
  Machine::NativeContract contract;
};

constexpr Machine::NativeContract PRESERVES_ALL{};
constexpr Machine::NativeContract CLOBBERS_DX{REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_BX_CX_DX{REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};

// In call-graph order, leaves first (plan §5 Phase 3).
constexpr std::array ENTRIES = {
  Entry{0x061C, "NextRandom", &NextRandom, PRESERVES_ALL},
  Entry{0x2421, "SetSinCos0", [](Guest& _guest) { SetSinCos(_guest, 0x41A0); }, PRESERVES_ALL},
  Entry{0x2426, "SetSinCos1", [](Guest& _guest) { SetSinCos(_guest, 0x41A4); }, PRESERVES_ALL},
  Entry{0x242B, "SetSinCos2", [](Guest& _guest) { SetSinCos(_guest, 0x41A8); }, PRESERVES_ALL},
  Entry{0x2430, "SetSinCos3", [](Guest& _guest) { SetSinCos(_guest, 0x41AC); }, PRESERVES_ALL},
  Entry{0x2435, "SetSinCos4", [](Guest& _guest) { SetSinCos(_guest, 0x41B0); }, PRESERVES_ALL},
  Entry{0x243B, "SetSinCos5", [](Guest& _guest) { SetSinCos(_guest, 0x41B4); }, PRESERVES_ALL},
  Entry{0x2441, "SetSinCos6", [](Guest& _guest) { SetSinCos(_guest, 0x41B8); }, PRESERVES_ALL},
  Entry{0x2447, "SetSinCos8", [](Guest& _guest) { SetSinCos(_guest, 0x41C0); }, PRESERVES_ALL},
  Entry{0x244D, "SetSinCos7", [](Guest& _guest) { SetSinCos(_guest, 0x41BC); }, PRESERVES_ALL},
  Entry{0x2453, "RotateBySinCos0", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41A0); }, CLOBBERS_DX},
  Entry{0x2465, "RotateBySinCos1", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41A4); }, CLOBBERS_DX},
  Entry{0x246D, "RotateBySinCos2", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41A8); }, CLOBBERS_DX},
  Entry{0x2475, "RotateBySinCos3", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41AC); }, CLOBBERS_DX},
  Entry{0x247D, "RotateBySinCos4", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41B0); }, CLOBBERS_DX},
  Entry{0x2485, "RotateBySinCos5", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41B4); }, CLOBBERS_DX},
  Entry{0x248D, "RotateBySinCos6", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41B8); }, CLOBBERS_DX},
  Entry{0x2495, "RotateBySinCos7", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41BC); }, CLOBBERS_DX},
  Entry{0x249D, "RotateBySinCos8", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41C0); }, CLOBBERS_DX},
  Entry{0x24A5, "ArcTangent2", &ArcTangent2, CLOBBERS_BX_CX_DX},
  Entry{0x24E5, "QuadrantArcTangent", &QuadrantArcTangent, CLOBBERS_BX_CX_DX},
  Entry{0x24F7, "RatioArcTangent", &RatioArcTangent, CLOBBERS_BX_CX_DX},
  Entry{0x2CDB, "AngleWithinTolerance", &AngleWithinTolerance, Machine::NativeContract{0, FLAG_CARRY}},
};

} // namespace

void InstallNativeRoutines(Machine::Pc& _pc, const Machine::LoadedProgram& _program)
{
  const std::uint16_t code = _program.loadSegment;
  const std::uint16_t data = DataSegment(_program);
  for (const Entry& entry : ENTRIES)
  {
    const Body body = entry.body;
    _pc.Hook(
      code, entry.offset, std::string(entry.name),
      [body, data](Machine::Pc& _host)
      {
        Guest guest(_host, data);
        body(guest);
        _host.ReturnNear();
      },
      entry.contract);
  }
  _pc.Native().SetStackFloor(Machine::Memory::Linear(data, STACK_FIRST_OFFSET));
}

std::size_t NativeRoutineCount() noexcept
{
  return ENTRIES.size();
}

} // namespace Elite
