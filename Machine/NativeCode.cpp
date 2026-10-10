#include "pch.h"

#include "NativeCode.h"

#include <stdexcept>
#include <utility>

namespace Machine
{

void NativeCode::Add(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine,
                     const NativeContract& _contract, NativeReturn _exit, NativeWait _wait)
{
  const std::uint32_t linear = Memory::Linear(_segment, _offset);
  // Made in place, in a node the map never moves: Pc holds on to a Hook while its routine waits.
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

} // namespace Machine
