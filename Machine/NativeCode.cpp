#include "pch.h"

#include "NativeCode.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace Machine
{

namespace
{

constexpr std::uint16_t POISON_FIRST = 0xA5A1;

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

std::vector<std::uint16_t> OffsetSet::Offsets() const
{
  std::vector<std::uint16_t> offsets;
  for (std::size_t word = 0; word < m_words.size(); ++word)
  {
    for (std::size_t bit = 0; bit < WORD_BITS; ++bit)
    {
      if ((m_words[word] & (std::uint64_t{1} << bit)) != 0)
        offsets.push_back(static_cast<std::uint16_t>(word * WORD_BITS + bit));
    }
  }
  return offsets;
}

void NativeCode::Add(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine,
                     const NativeContract& _contract, NativeReturn _exit, NativeWait _wait)
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
  hook.wait = _wait;
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

void NativeCode::Poison(Registers& _registers, std::uint16_t _registersToPoison) const noexcept
{
  if (!m_poisoning)
  {
    return;
  }
  // A marked value per register, A5h in the high byte: nothing the game computes is likely to land on it.
  std::uint16_t mark = POISON_FIRST;
  for (const NamedRegister& named : NAMED_REGISTERS)
  {
    if (named.bit != 0 && (_registersToPoison & named.bit) != 0)
    {
      _registers.*named.field = mark;
    }
    mark = static_cast<std::uint16_t>(mark + 1);
  }
}

void NativeCode::AddMismatch(Mismatch _mismatch)
{
  m_mismatches.push_back(std::move(_mismatch));
}

std::string NativeCode::Compare(const Hook& _hook, const Registers& _original, const Registers& _native,
                                const WriteJournal& _originalWrites, std::span<const std::uint8_t> _originalAfter,
                                const WriteJournal& _nativeWrites, const Memory& _memory)
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
  // native routine changed, what it held before both, which its first journal entry records. Below SP,
  // the stack is dead once the routine has returned. Each byte is marked as it is checked, so that a
  // byte the native routine also changed, or changed twice, is checked once; the marks are cleared after.
  const std::uint32_t deadFrom = m_stackFloor;
  const std::uint32_t deadTo = m_stackFloor == 0 ? 0 : Memory::Linear(_native.ss, _native.sp);
  m_marks.resize(Memory::SIZE_BYTES, 0);
  std::vector<std::pair<std::uint32_t, std::uint8_t>> wrong; // address, and what it should hold
  const auto check = [&](std::uint32_t _linear, std::uint8_t _expected)
  {
    if (m_marks[_linear] != 0)
    {
      return;
    }
    m_marks[_linear] = 1;
    const bool dead = _linear >= deadFrom && _linear < deadTo;
    if (!dead && _memory.Read8(_linear) != _expected)
    {
      wrong.emplace_back(_linear, _expected);
    }
  };
  const std::span<const WriteJournal::Entry> originalEntries = _originalWrites.Entries();
  for (std::size_t index = 0; index < originalEntries.size(); ++index)
  {
    check(originalEntries[index].linear, _originalAfter[index]);
  }
  const std::span<const WriteJournal::Entry> nativeEntries = _nativeWrites.Entries();
  for (const WriteJournal::Entry& entry : nativeEntries)
  {
    check(entry.linear, entry.before);
  }
  for (const WriteJournal::Entry& entry : originalEntries)
  {
    m_marks[entry.linear] = 0;
  }
  for (const WriteJournal::Entry& entry : nativeEntries)
  {
    m_marks[entry.linear] = 0;
  }
  std::sort(wrong.begin(), wrong.end());
  for (const auto& [linear, value] : wrong)
  {
    note(std::format("byte {:05X}h {:02X}, original {:02X}", linear, _memory.Read8(linear), value));
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
