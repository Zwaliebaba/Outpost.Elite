// GameLogic/Guest.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "Pc.h"

#include <cstdint>

namespace Elite
{

/// The machine as a hooked entry sees it (ADR-010): the program's registers and stack, beside the game's memory and devices that
/// the routine behind the entry takes. One is made for each call of an entry, around the Pc that runs the program and the
/// GameState the host made for the whole run (Game). Only the entries the Dispatcher reaches have one (NativeRoutines.h): Start,
/// which sets up the program's registers and stack, and the timer's and keyboard's interrupt handlers, which keep every register.
class Guest
{
public:
  Guest(Machine::Pc& _pc, GameState& _state) noexcept
    : m_pc(_pc),
      m_state(_state),
      m_hardware(_pc)
  {
  }

  /// The game's memory without the registers: what a de-assembled routine is given (ADR-012).
  [[nodiscard]] GameState& State() noexcept
  {
    return m_state;
  }

  /// The devices as the game drives them: what a de-assembled routine that sounds, times or reads a device is given
  /// besides the GameState (ADR-014).
  [[nodiscard]] Hardware& Devices() noexcept
  {
    return m_hardware;
  }

  [[nodiscard]] Machine::Registers& Regs() noexcept
  {
    return m_pc.Processor().Regs();
  }

  /// Pushes _value on the program's stack, as PUSH does.
  void Push(std::uint16_t _value) noexcept
  {
    Machine::Registers& regs = Regs();
    regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
    m_state.SetFarWord(regs.ss, regs.sp, _value);
  }

  /// Whether _flag (FLAG_* in Registers.h) is set: the direction flag the code an entry interrupts or is called from left.
  [[nodiscard]] bool Flag(std::uint16_t _flag) const noexcept
  {
    return (m_pc.Processor().Regs().flags & _flag) != 0;
  }

private:
  Machine::Pc& m_pc;
  GameState& m_state;
  Hardware m_hardware;
};

} // namespace Elite
