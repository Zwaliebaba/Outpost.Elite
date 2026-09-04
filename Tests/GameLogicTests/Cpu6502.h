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
 * It is deliberately not an emulator: no interrupts beyond BRK/RTI, no undocumented opcodes, no
 * hardware. Elite's own code is well-behaved 6502; where it pokes a VIC-II or SID register the
 * fixture reads the write log rather than pretending to be a chip.
 *
 * It DOES count cycles, which it did not until 2026-09-03. That is not a step towards being an
 * emulator -- nothing here is driven by the count -- it is Risk R3's oracle: the C64 main loop
 * has no frame cap, so how fast the game runs is a consequence of what an iteration costs, and
 * the only honest way to answer that is to add the cycles up. See `cycles` below.
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

  /*
   * Cycles executed since the last Reset, on an NMOS 6502.
   *
   * The C64 runs at 985,248 Hz on PAL and 1,022,727 Hz on NTSC, so this divided by one of those
   * is a duration -- which is the whole point, and Risk R3's only oracle. Set it to zero to
   * start a fresh measurement; Reset does too.
   *
   * TWO THINGS IT DOES NOT COUNT, and both matter when reading a measurement:
   *
   *   * A TRAPPED CALL COSTS NOTHING. A trap stands in for a routine the fixture did not want to
   *     run -- the character printer, the pixel plotter -- and that routine's real cost is
   *     unknown here. So the cycle count of anything that trips a trap is a LOWER BOUND, and a
   *     measurement that matters should say which traps were armed.
   *   * NOTHING THE HARDWARE STEALS. On a real C64 the VIC-II halts the processor for 40-ish
   *     cycles on every eighth scanline to fetch character data, and for more when sprites are
   *     on. That is a further 5-10% the game does not get, and it is not modelled.
   *
   * So this is what the instruction stream costs, not what the machine takes. For deciding a
   * step rate that is the right number to start from and the wrong number to stop at.
   */
  std::uint64_t cycles = 0;

  [[nodiscard]] std::uint8_t StatusByte() const noexcept;
  void SetStatusByte(std::uint8_t _status) noexcept;

  /*
   * The documented NMOS cycle count for an opcode, before the two variable penalties below.
   *
   * Zero for anything this table does not name, which is every undocumented opcode. Public
   * because the tests check it against the interpreter's own opcode coverage: an opcode Step
   * executes and this does not price would be counted as free.
   *
   * These numbers are TRANSCRIBED from the published NMOS timings. CycleTests hand-counts two of
   * the game's own routines from the source and finds the model exact on both, which anchors the
   * zero page, implied, accumulator and immediate groups but not the whole table. What the tests
   * prove outright is the structure -- that everything executed is priced, and that the two
   * penalties land on exactly the right opcodes under exactly the right conditions -- which is
   * where a hand-written table actually goes wrong. A base count that is uniformly wrong scales a
   * measurement; a penalty that is wrong changes its shape.
   */
  [[nodiscard]] static std::uint8_t BaseCycles(std::uint8_t _opcode) noexcept;

  /*
   * True for the indexed READS that cost one more cycle when the index carries into a new page.
   *
   * Reads only. A store through the same addressing mode always pays the higher figure whether
   * it crosses or not (the processor cannot know in time, so it always spends the extra cycle),
   * and a read-modify-write always pays the maximum. That asymmetry is the part of 6502 timing
   * a port gets wrong, so it is a function with a test rather than a comment.
   */
  [[nodiscard]] static bool PaysPageCrossPenalty(std::uint8_t _opcode) noexcept;

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
  /// How many memory addresses a trap hit carries with it. See `watch` below.
  static constexpr std::size_t WATCH_SLOTS = 4;

  struct TrapHit
  {
    std::uint16_t address = 0;
    std::uint8_t a = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;

    /// The bytes at `watch`, as they were when the trap fired. Routines that take arguments in
    /// memory rather than in registers -- SUN takes its centre in K3 and K4 -- cannot be
    /// compared without this, because by the time the run ends the caller has moved on.
    std::array<std::uint8_t, WATCH_SLOTS> watched{};
  };

  /*
   * Addresses recorded alongside every trap hit.
   *
   * Four is enough for every routine the suite traps and keeps the hit small; a slot left at
   * zero records the byte at address zero, which no caller reads.
   */
  std::array<std::uint16_t, WATCH_SLOTS> watch{};

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
    SetCarry,   ///< the routine ends SEC -- `NOISE` does, on the path that takes a voice
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
  [[nodiscard]] std::uint16_t AddrAbsoluteX() noexcept { return Indexed(FetchWord(), x); }
  [[nodiscard]] std::uint16_t AddrAbsoluteY() noexcept { return Indexed(FetchWord(), y); }
  [[nodiscard]] std::uint16_t AddrIndirectX() noexcept;
  [[nodiscard]] std::uint16_t AddrIndirectY() noexcept;

  /*
   * Add an index to a base address, and remember whether that carried into a new page.
   *
   * The flag is recorded here and priced in Step, rather than the cycle being added here,
   * because only Step knows the opcode -- and whether an indexed access pays for the crossing
   * depends entirely on whether the opcode is reading or writing.
   */
  [[nodiscard]] std::uint16_t Indexed(std::uint16_t _base, std::uint8_t _index) noexcept
  {
    const std::uint16_t effective = static_cast<std::uint16_t>(_base + _index);
    m_crossedPage = (_base & 0xFF00u) != (effective & 0xFF00u);
    return effective;
  }

  /// Set by Indexed, cleared at the top of every Step, read once the opcode is known. The clear
  /// is unreachable in practice -- see Step, which says why it is there anyway.
  bool m_crossedPage = false;

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
