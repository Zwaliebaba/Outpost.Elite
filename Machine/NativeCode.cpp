#include "pch.h"

#include "NativeCode.h"

#include <format>
#include <stdexcept>
#include <utility>

namespace Machine
{

namespace
{

struct NamedRegister
{
  std::string_view name;
  std::uint16_t Registers::*field;
  std::uint16_t bit; // 0: never clobbered
};

constexpr std::array<NamedRegister, 13> NAMED_REGISTERS = {{
  {"AX", &Registers::ax, REGISTER_AX},
  {"BX", &Registers::bx, REGISTER_BX},
  {"CX", &Registers::cx, REGISTER_CX},
  {"DX", &Registers::dx, REGISTER_DX},
  {"SI", &Registers::si, REGISTER_SI},
  {"DI", &Registers::di, REGISTER_DI},
  {"BP", &Registers::bp, REGISTER_BP},
  {"DS", &Registers::ds, REGISTER_DS},
  {"ES", &Registers::es, REGISTER_ES},
  {"SP", &Registers::sp, 0},
  {"SS", &Registers::ss, 0},
  {"CS", &Registers::cs, 0},
  {"IP", &Registers::ip, 0},
}};

} // namespace

void NativeCode::Add(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine,
                     const NativeContract& _contract, NativeReturn _exit, bool _waits)
{
  const std::uint32_t linear = Memory::Linear(_segment, _offset);
  // Made in place, never moved: a Hook's books are containers whose moves may allocate.
  const auto [found, added] = m_hooks.try_emplace(linear);
  if (!added)
  {
    throw std::logic_error("NativeCode: two native routines at one entry");
  }
  Hook& hook = found->second;
  hook.name = std::move(_name);
  hook.routine = std::move(_routine);
  hook.contract = _contract;
  hook.exit = _exit;
  hook.waits = _waits;
  hook.segment = _segment;
  hook.offset = _offset;
  m_map.resize(Memory::SIZE_BYTES, 0);
  m_map[linear] = 1;
}

NativeCode::Hook* NativeCode::At(std::uint32_t _linear) noexcept
{
  const auto found = m_hooks.find(_linear);
  return found == m_hooks.end() ? nullptr : &found->second;
}

void NativeCode::AddMismatch(Mismatch _mismatch)
{
  m_mismatches.push_back(std::move(_mismatch));
}

std::string NativeCode::Compare(const Hook& _hook, const Registers& _original, const Registers& _native,
                                const WriteJournal& _originalWrites, std::span<const std::uint8_t> _originalAfter,
                                const WriteJournal& _nativeWrites, const Memory& _memory) const
{
  std::vector<std::string> differences;
  std::size_t total = 0;
  const auto note = [&](std::string _difference)
  {
    if (differences.size() < MOST_DIFFERENCES)
    {
      differences.push_back(std::move(_difference));
    }
    ++total;
  };

  for (const NamedRegister& named : NAMED_REGISTERS)
  {
    const bool compared = (_hook.contract.clobbers & named.bit) == 0;
    if (compared && _original.*named.field != _native.*named.field)
    {
      note(std::format("{} {:04X}, original {:04X}", named.name, _native.*named.field, _original.*named.field));
    }
  }
  if (((_original.flags ^ _native.flags) & _hook.contract.flags) != 0)
  {
    note(std::format("flags {:04X}, original {:04X} (compared {:04X})", _native.flags, _original.flags, _hook.contract.flags));
  }

  // What each byte either run changed must hold: the original's last value, or, for a byte only the
  // native routine changed, what it held before both.
  std::map<std::uint32_t, std::uint8_t> expected;
  const std::span<const WriteJournal::Entry> originalEntries = _originalWrites.Entries();
  for (std::size_t index = 0; index < originalEntries.size(); ++index)
  {
    expected[originalEntries[index].linear] = _originalAfter[index];
  }
  for (const WriteJournal::Entry& entry : _nativeWrites.Entries())
  {
    expected.try_emplace(entry.linear, entry.before);
  }
  // Below SP, the stack is dead once the routine has returned.
  const std::uint32_t deadFrom = m_stackFloor;
  const std::uint32_t deadTo = m_stackFloor == 0 ? 0 : Memory::Linear(_native.ss, _native.sp);
  for (const auto& [linear, value] : expected)
  {
    const bool dead = linear >= deadFrom && linear < deadTo;
    const std::uint8_t actual = _memory.Read8(linear);
    if (!dead && actual != value)
    {
      note(std::format("byte {:05X}h {:02X}, original {:02X}", linear, actual, value));
    }
  }

  std::string text;
  for (const std::string& difference : differences)
  {
    text += text.empty() ? "" : "; ";
    text += difference;
  }
  if (total > differences.size())
  {
    text += std::format("; and {} more", total - differences.size());
  }
  return text;
}

} // namespace Machine
