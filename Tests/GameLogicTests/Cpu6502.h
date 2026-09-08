#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
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
    bool completed = false;     ///< the routine returned (or reached the stop address)
    bool illegalOpcode = false; ///< an opcode this interpreter does not implement
    std::uint32_t instructions = 0;
    std::uint16_t stoppedAt = 0;
  };

  class Cpu6502
  {
  public:
    Cpu6502()
    {
      Reset();
    }

    void Reset() noexcept;

    // ---- state -------------------------------------------------------------------------

    std::array<std::uint8_t, 65536> memory{};

    /*
     * 6502: the I/O page -- what a load or store at &D000-&DFFF reaches when the 6510's port
     * register maps it in (M6-0-a, §6.108).
     *
     * On a C64 the VIC-II, the SID, the colour RAM and the two CIAs sit at &D000-&DFFF ON TOP OF
     * 4K of RAM, and bits 0 to 2 of address &0001 decide which of the two a bus cycle reaches.
     * Elite keeps the ship blueprints in that RAM (`XX21` is &D000) and banks the chips in around
     * every register write with `SETL1`. Until this slice the interpreter had one flat array, so
     * `NOSPRITES`' `STA VIC+&15` zeroed a blueprint pointer and `DOEXP` corrupted the ships drawn
     * after it -- which is why `NOSPRITES` and `DOEXP` were trapped in every composition test, why
     * `ShipDrawEffects` outlived every other seam, and why no whole frame with an explosion in it
     * had ever been compared. `memory` is the RAM, all 64K of it; this is the page the chips are.
     * A fixture that seeds or reads a register addresses it through `Io`.
     */
    std::array<std::uint8_t, 0x1000> io{};

    static constexpr std::uint16_t IO_BASE = 0xD000;
    static constexpr std::uint16_t IO_TOP = 0xDFFF;

    /*
     * Coverage (M6-0-f): where every instruction this processor executes is marked, and where
     * every trap it hits is marked, when the image that made it is recording. Null otherwise, and
     * `Step` costs one branch for it. `OracleImage::TakeCoverage` turns the marks into labels --
     * a routine a test REACHED and a routine a test RAN are different answers, and the ledger
     * review needs the second.
     */
    std::bitset<65536>* executed = nullptr;
    std::bitset<65536>* trapped = nullptr;

    /*
     * The read census (M6-b-1), which is what §1 R-g asks to be sized before anything is built on
     * it: how much of what the oracle answers depends on bytes of the assembled original that
     * NOTHING WROTE.
     *
     * M6-b keys a fixture record on what the test wrote, not on the whole machine. That is sound
     * exactly as far as a routine's answer is a function of the write set — and a routine that
     * reads a table, a blueprint or a constant out of the image is reading something the key does
     * not name. Those reads do not make replay wrong (the recorded answer already holds what they
     * produced) but they decide how much of the original the fixture's ANSWERS carry, which is the
     * ADR-001 §5 question, and how much a fixture would silently rot if the image ever moved.
     *
     * A read is counted against the image when the byte is still equal to the base image's. A test
     * that wrote a byte the value it already held is therefore counted as an image read, which
     * errs towards reporting MORE dependence than there is — the safe direction for a measurement
     * a ruling will be made on.
     *
     * Only DATA reads are counted: the `Read` path and the two indirect pointer fetches. An
     * instruction fetch is the original's code by definition and would drown the number; the stack
     * is the call's own.
     */
    const std::array<std::uint8_t, 65536>* baseImage = nullptr;

    struct ReadCensus
    {
      bool on = false;             ///< off in every normal run; `--measure` turns it on
      std::uint64_t reads = 0;     ///< data reads that reached RAM
      std::uint64_t fromImage = 0; ///< of those, the ones still holding the base image's byte
      /*
       * And of THOSE, the ones where the image's byte is not zero.
       *
       * The distinction decides how the number should be read. A drawing routine reads the screen
       * byte before it EORs into it, and in a fresh machine that byte is zero because the bitmap
       * is zero -- so the read is "off the image" by the test above while carrying nothing the
       * original put there. A non-zero byte is the other case: a table entry, a blueprint, a piece
       * of text. The first is noise in this measurement and the second is what §1 R-g is about.
       */
      std::uint64_t fromImageContent = 0;
      std::bitset<65536> addresses{};        ///< which image bytes the whole suite read that way
      std::bitset<65536> contentAddresses{}; ///< and which of those held something

      /// One per process. The distinct-address set is 8 KB and a machine is copied per call, so it
      /// cannot live on the processor.
      [[nodiscard]] static ReadCensus& Instance() noexcept;
    };

    /*
     * 6502: CIA1's two ports, &DC00 and &DC01 -- the keyboard matrix as the chip presents it
     * (M6-0-a-4).
     *
     * The game selects columns by storing a byte with one bit CLEAR to port A and reads the rows
     * of the selected columns from port B, where a held key reads as a clear bit; `RDKEY` walks
     * the eight columns that way and `DEC`s a logger entry for every clear bit it finds. Port A
     * reads back what was stored (its lines are outputs on a C64, and joystick 2 shares them:
     * nothing is plugged in here, so they read high). Port B is computed from `keysDown` on every
     * read, which is what lets `RDKEY` run on the oracle against the same held keys a fixture's
     * `Keyboard` answers on the port side, instead of being trapped around.
     */
    static constexpr std::uint16_t CIA1_PORT_A = 0xDC00;
    static constexpr std::uint16_t CIA1_PORT_B = 0xDC01;

    /// One byte per matrix column; a SET bit is a key held in that row. `HoldKey` is how a fixture
    /// presses one, and the ninth "column" `RDKEY`'s walk ends on selects nothing and reads &FF.
    std::array<std::uint8_t, 8> keysDown{};

    void HoldKey(std::uint8_t _column, std::uint8_t _row) noexcept
    {
      keysDown[static_cast<std::size_t>(_column & 0x07u)] |= static_cast<std::uint8_t>(1u << (_row & 0x07u));
    }

    /// 6502: bits 0 to 2 of &0001. The I/O page is mapped in when CHAREN (bit 2) is set and LORAM
    /// or HIRAM is -- %101 is what `SETL1` maps in with, %100 what it maps out with, and %x00 is
    /// RAM everywhere whatever CHAREN says.
    [[nodiscard]] bool IoMappedIn() const noexcept
    {
      const std::uint8_t bits = static_cast<std::uint8_t>(memory[1] & 0x07u);
      return (bits & 0x04u) != 0u && (bits & 0x03u) != 0u;
    }

    /// A data read as the 6510 makes it: the I/O page when it is mapped in and the address is on
    /// it, RAM otherwise. Instruction fetches, the stack and zero-page pointers never reach the
    /// page and read `memory` directly.
    [[nodiscard]] std::uint8_t Read(std::uint16_t _address) const noexcept
    {
      if (_address >= IO_BASE && _address <= IO_TOP && IoMappedIn())
      {
        if (_address == CIA1_PORT_B)
        {
          // The rows of every selected column, pulled low where a key is held.
          const std::uint8_t columns = io[static_cast<std::size_t>(CIA1_PORT_A - IO_BASE)];
          std::uint8_t rows = 0xFFu;
          for (std::size_t column = 0; column < keysDown.size(); ++column)
          {
            if ((columns & (1u << column)) == 0u)
            {
              rows = static_cast<std::uint8_t>(rows & ~keysDown[column]);
            }
          }
          return rows;
        }
        return io[static_cast<std::size_t>(_address - IO_BASE)];
      }
      NoteRead(_address);
      return memory[_address];
    }

    /// A register on the I/O page, for a fixture: `Io(0xD015)` is VIC+&15 whatever the port
    /// register says, which is how a test seeds a chip or reads what the game left in it.
    [[nodiscard]] std::uint8_t& Io(std::uint16_t _address) noexcept
    {
      return io[static_cast<std::size_t>(_address - IO_BASE) & 0x0FFFu];
    }
    [[nodiscard]] std::uint8_t Io(std::uint16_t _address) const noexcept
    {
      return io[static_cast<std::size_t>(_address - IO_BASE) & 0x0FFFu];
    }

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
    /*
     * WHAT ANSWERS IT IS A SEAM (Design/Modernize.md §4.10, built M6-a-2).
     *
     * With no oracle installed this IS `Interpret` below, which is what every test has always got.
     * `Oracle::Install` puts a recorder or a fixture in its place instead, and no test changes
     * shape: the seam is the call, not the state the test builds around it.
     */
    RunResult CallSubroutine(std::uint16_t _address, std::uint32_t _maxInstructions = 2'000'000,
                             std::uint16_t _stopAddress = 0xFFF9) noexcept;

    /// The interpreter itself, which `LiveOracle` and `RecordingOracle` run and `RecordedOracle`
    /// does not. Public so the oracles can reach it; nothing else should call it directly.
    RunResult Interpret(std::uint16_t _address, std::uint32_t _maxInstructions, std::uint16_t _stopAddress) noexcept;

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

      /*
       * The carry ON ENTRY, which is an ARGUMENT to more of this game than anyone would guess.
       *
       * `NOISE` passes it through when sound is off, `OUCH` opens its `DORND` on it, `LASLI`'s
       * `ROL A` reads it, and the pitch reads what the roll left across a `JSR` -- §6.85, §6.86,
       * §6.88 and §6.99 are all the same shape. Every one of those was established by reading the
       * assembly; this makes the flag something the oracle REPORTS, so a port's claim about the
       * carry at a seam can be compared instead of argued.
       */
      bool carry = false;

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

    // ---- probes ------------------------------------------------------------------------

    /*
     * An address that runs a fixture's code and is then EXECUTED, unlike a trap.
     *
     * For a routine whose input changes while it runs: `TT217` waits for the matrix to empty and
     * then to fill, and a fixture that can only set the matrix once before the call cannot reach
     * its second wait. A probe on `RDKEY` lets the fixture change what the CIA holds on every scan,
     * which is what a person's hand does (InputTimer.md I-1).
     */
    struct Probe
    {
      std::uint16_t address = 0;
      std::function<void(Cpu6502&)> act;

      /*
       * What tells one probe from another, and it is REQUIRED (M6-b-1).
       *
       * A probe changes the machine WHILE the call runs, so the call's answer is not a function of
       * the machine the call started on -- and a fixture keyed on that machine cannot tell two
       * probes apart. `TT217` is the case: five scripts, five identical starting machines, five
       * different answers. The measuring pass found it as four key collisions, which is exactly
       * what the recorder's collision counter is for.
       *
       * The identity is the caller's promise that two probes with the same number behave the same
       * way. There is no default: a new probe site cannot forget to make it.
       */
      std::uint64_t identity = 0;
    };

    std::vector<Probe> probes;

    void AddProbe(std::uint16_t _address, std::function<void(Cpu6502&)> _act, std::uint64_t _identity);
    void ClearTrapHits() noexcept
    {
      trapHits.clear();
    }

    // ---- store log ------------------------------------------------------------------------

    /*
     * The stores made to a range of addresses, in the order they were made.
     *
     * The traps above observe CALLS; this observes WRITES, and it exists for the SID. The sound
     * interrupt handler writes the chip's registers in a particular order -- seven zeros and then
     * the values, which a real chip hears as a gate going down and up -- and reading the registers
     * back after the run would show the values and not the zeros. So a test that wants to know
     * what the handler DID rather than what it LEFT sets the range to the chip's registers and
     * compares the log against the port's own list of writes.
     *
     * Only STA, STX and STY are logged. Read-modify-write instructions on the range would be
     * missed, and none of the routines under test make one to a hardware register.
     */
    struct StoreHit
    {
      std::uint16_t address = 0;
      std::uint8_t value = 0;
    };

    std::uint16_t storeLogLow = 1;  ///< the range is empty until a test sets it
    std::uint16_t storeLogHigh = 0;
    std::vector<StoreHit> stores;

    void LogStores(std::uint16_t _low, std::uint16_t _high)
    {
      storeLogLow = _low;
      storeLogHigh = _high;
      stores.clear();
    }

    // ---- helpers for fixtures ----------------------------------------------------------

    void Load(std::uint16_t _address, const std::uint8_t* _bytes, std::size_t _count) noexcept;
    [[nodiscard]] std::uint16_t ReadWord(std::uint16_t _address) const noexcept;

    /// Public because a fixture that enters an INTERRUPT handler has to push what the 6510 pushes --
    /// the return address and the status byte -- before jumping in; `CallSubroutine` pushes a return
    /// address only, and an RTI would pop the wrong thing.
    void Push(std::uint8_t _value) noexcept;

    /// The one place a store lands, so that the store log sees every STA, STX and STY -- logged by
    /// address, then routed to the I/O page or to RAM as the port register says.
    void Store(std::uint16_t _address, std::uint8_t _value) noexcept;

  private:
    /// One branch when the census is off, which is every run but `--measure`.
    void NoteRead(std::uint16_t _address) const noexcept
    {
      if (ReadCensus::Instance().on)
      {
        CountRead(_address);
      }
    }
    void CountRead(std::uint16_t _address) const noexcept;

    [[nodiscard]] std::uint8_t Fetch() noexcept
    {
      return memory[pc++];
    }
    [[nodiscard]] std::uint16_t FetchWord() noexcept;

    [[nodiscard]] std::uint8_t Pop() noexcept;

    void SetNz(std::uint8_t _value) noexcept
    {
      n = (_value & 0x80u) != 0u;
      z = _value == 0u;
    }

    // Addressing modes, each returning the effective address.
    [[nodiscard]] std::uint16_t AddrZeroPage() noexcept
    {
      return Fetch();
    }
    [[nodiscard]] std::uint16_t AddrZeroPageX() noexcept
    {
      return static_cast<std::uint8_t>(Fetch() + x);
    }
    [[nodiscard]] std::uint16_t AddrZeroPageY() noexcept
    {
      return static_cast<std::uint8_t>(Fetch() + y);
    }
    [[nodiscard]] std::uint16_t AddrAbsolute() noexcept
    {
      return FetchWord();
    }
    [[nodiscard]] std::uint16_t AddrAbsoluteX() noexcept
    {
      return Indexed(FetchWord(), x);
    }
    [[nodiscard]] std::uint16_t AddrAbsoluteY() noexcept
    {
      return Indexed(FetchWord(), y);
    }
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
