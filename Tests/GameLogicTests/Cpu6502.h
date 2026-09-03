#pragma once

#include <array>
#include <cstdint>
#include <vector>

/*
 * An NMOS 6502 interpreter, for tests only (ADR-003 section 1).
 *
 * This is the oracle. It loads the assembled original, calls one routine with chosen inputs,
 * and hands back the memory and registers the real game would have produced -- which turns
 * "does the port feel right" into "does the port match", one routine at a time.
 *
 * It is deliberately not an emulator: no cycle counting, no interrupts beyond BRK/RTI, no
 * undocumented opcodes, no hardware. Elite's own code is well-behaved 6502; where it pokes a
 * VIC-II or SID register the fixture reads the write log rather than pretending to be a chip.
 *
 * Nothing in GameLogic or Outpost may include this header.
 */

namespace Elite::Testing
{

/// Where a CallSubroutine run stopped, and why.
struct RunResult
{
  bool completed = false;        ///< the routine returned (or reached the stop address)
  bool illegalOpcode = false;    ///< an opcode this interpreter does not implement
  std::uint32_t instructions = 0;
  std::uint16_t stoppedAt = 0;
};

class Cpu6502
{
public:
  Cpu6502() { Reset(); }

  void Reset() noexcept;

  // ---- state -------------------------------------------------------------------------

  std::array<std::uint8_t, 65536> memory{};

  std::uint8_t a = 0;
  std::uint8_t x = 0;
  std::uint8_t y = 0;
  std::uint8_t sp = 0xFD;
  std::uint16_t pc = 0;

  bool c = false;
  bool z = false;
  bool i = false;
  bool d = false;
  bool v = false;
  bool n = false;

  [[nodiscard]] std::uint8_t StatusByte() const noexcept;
  void SetStatusByte(std::uint8_t _status) noexcept;

  // ---- execution ---------------------------------------------------------------------

  /// Executes one instruction. False means the opcode is not implemented, and pc is left on it.
  bool Step() noexcept;

  /*
   * Calls a subroutine the way a test wants to: push a return address that cannot be reached
   * by the code under test, jump to _address, and run until the routine's RTS lands on it.
   *
   * The stack is also watched, because Elite does discard its own return address in places to
   * jump somewhere else entirely -- a run that unwinds past where it started has finished just
   * as surely as one that returned, and reporting that is better than spinning to the budget.
   */
  RunResult CallSubroutine(std::uint16_t _address, std::uint32_t _maxInstructions = 2'000'000, std::uint16_t _stopAddress = 0xFFF9) noexcept;

  // ---- call traps --------------------------------------------------------------------

  /*
   * Addresses that are recorded and returned from instead of being executed.
   *
   * This is how the oracle observes a routine's OUTPUT rather than only its arithmetic. The
   * text code prints by calling a character routine, and the drawing code plots by calling a
   * pixel routine; trapping those turns "what did the game put on screen" into a list, without
   * running any of the hardware-facing code underneath.
   *
   * A trapped call is recorded with the registers as they arrived, then returns immediately.
   */
  struct TrapHit
  {
    std::uint16_t address = 0;
    std::uint8_t a = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
  };

  /*
   * What the trapped routine's own RTS would have left behind.
   *
   * A trap returns without running anything, so a routine whose callers depend on the flags it
   * exits with is not faithfully stood in for by the default. CHPR is the case that matters:
   * every path through it ends CLC, and the justification code four instructions later does an
   * SBC that borrows because of it. Trapping CHPR without this makes the game's own text come
   * out a character wider than the game produces.
   */
  enum class TrapExit
  {
    Unchanged,  ///< leave the flags as the caller had them
    ClearCarry, ///< the routine ends CLC
  };

  struct Trap
  {
    std::uint16_t address = 0;
    TrapExit exit = TrapExit::Unchanged;
  };

  std::vector<Trap> traps;
  std::vector<TrapHit> trapHits;

  void AddTrap(std::uint16_t _address, TrapExit _exit = TrapExit::Unchanged);
  void ClearTrapHits() noexcept { trapHits.clear(); }

  // ---- helpers for fixtures ----------------------------------------------------------

  void Load(std::uint16_t _address, const std::uint8_t* _bytes, std::size_t _count) noexcept;
  [[nodiscard]] std::uint16_t ReadWord(std::uint16_t _address) const noexcept;

private:
  [[nodiscard]] std::uint8_t Fetch() noexcept { return memory[pc++]; }
  [[nodiscard]] std::uint16_t FetchWord() noexcept;

  void Push(std::uint8_t _value) noexcept;
  [[nodiscard]] std::uint8_t Pop() noexcept;

  void SetNz(std::uint8_t _value) noexcept
  {
    n = (_value & 0x80u) != 0u;
    z = _value == 0u;
  }

  // Addressing modes, each returning the effective address.
  [[nodiscard]] std::uint16_t AddrZeroPage() noexcept { return Fetch(); }
  [[nodiscard]] std::uint16_t AddrZeroPageX() noexcept { return static_cast<std::uint8_t>(Fetch() + x); }
  [[nodiscard]] std::uint16_t AddrZeroPageY() noexcept { return static_cast<std::uint8_t>(Fetch() + y); }
  [[nodiscard]] std::uint16_t AddrAbsolute() noexcept { return FetchWord(); }
  [[nodiscard]] std::uint16_t AddrAbsoluteX() noexcept { return static_cast<std::uint16_t>(FetchWord() + x); }
  [[nodiscard]] std::uint16_t AddrAbsoluteY() noexcept { return static_cast<std::uint16_t>(FetchWord() + y); }
  [[nodiscard]] std::uint16_t AddrIndirectX() noexcept;
  [[nodiscard]] std::uint16_t AddrIndirectY() noexcept;

  void Adc(std::uint8_t _operand) noexcept;
  void Sbc(std::uint8_t _operand) noexcept;
  void Compare(std::uint8_t _register, std::uint8_t _operand) noexcept;
  void Branch(bool _condition) noexcept;

  [[nodiscard]] std::uint8_t ShiftLeft(std::uint8_t _value) noexcept;
  [[nodiscard]] std::uint8_t ShiftRight(std::uint8_t _value) noexcept;
  [[nodiscard]] std::uint8_t RollLeft(std::uint8_t _value) noexcept;
  [[nodiscard]] std::uint8_t RollRight(std::uint8_t _value) noexcept;
};

} // namespace Elite::Testing
