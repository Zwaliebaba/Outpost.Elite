#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "LineHeap.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The interpreter's cycle counter, which exists for one reason (Risk R3).
 *
 * THE FINDING THAT MADE IT NECESSARY. The C64 main flight loop has no frame cap. A scan of the
 * assembled build for calls to WSCAN -- the routine that waits for vertical sync -- finds exactly
 * three, at DELAY, at TT16+7 and at FREEZE. The BBC and 6502SP builds call it from the flight
 * loop; the C64 build does not, and the version gate in
 * main_flight_loop_part_13_of_16.asm says so. So the C64 game runs its loop as fast as the
 * processor can, which is why it visibly slows down when the screen fills with ships, and why
 * "what step rate should the port use" has no answer in the source. The rate is a CONSEQUENCE of
 * what an iteration costs, and the only way to know that is to add the cycles up.
 *
 * WHAT THESE TESTS PROVE. The base cycle counts are transcribed from the published NMOS timings,
 * and two of the game's own routines are hand-counted from the source and compared against the
 * model instruction for instruction -- TT54 at 66 cycles over 23 instructions and DORND at 36
 * over 13, both exact. That is not the whole table, but it is a real anchor: between them they
 * exercise the zero page, implied, accumulator and immediate groups, ADC's carry chain, and RTS.
 * The rest of the table rests on the transcription. Everything structural is proved outright:
 *
 *   * every opcode the interpreter executes is priced, and nothing else is;
 *   * the page-crossing penalty lands on exactly the indexed READS and on no store or
 *     read-modify-write;
 *   * a branch costs two, three or four, for the right three reasons;
 *   * a hand-counted program totals exactly what it should, so the accumulation neither
 *     double-counts nor drops a term;
 *   * a trapped call costs nothing, which is the documented reason a measurement is a lower
 *     bound.
 *
 * That is the right split of effort. A base count that is uniformly wrong scales a measurement
 * by a few percent; a penalty applied to the wrong opcodes changes its shape, and it is the shape
 * that decides a step rate.
 */
namespace GameLogicTests
{

  namespace
  {
    /// Where the hand-written programs below are assembled. Well clear of the game's own blocks, so
    /// a test that forgets to use a fresh processor fails loudly rather than subtly.
    constexpr std::uint16_t PROGRAM = 0x1000;

    /// CallSubroutine's default stop address, so a hand-written RTS lands somewhere recognisable.
    constexpr std::uint16_t STOP = 0xFFF9;

    bool OracleMissing()
    {
      const OracleImage& oracle = OracleImage::Instance();
      if (oracle.Available())
      {
        return false;
      }
      Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
      return true;
    }

    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    std::string Hex(std::uint8_t _value)
    {
      static constexpr char DIGITS[] = "0123456789ABCDEF";
      return std::string("$") + DIGITS[_value >> 4] + DIGITS[_value & 0x0Fu];
    }

    /// Runs one instruction in isolation and returns what it cost. The operand bytes are $00 $20, so
    /// an absolute mode reads $2000 -- inside memory, away from the program, and not page zero.
    std::uint64_t CostOfOne(std::uint8_t _opcode, std::uint8_t _x, std::uint8_t _y)
    {
      Cpu6502 cpu;
      cpu.memory[PROGRAM] = _opcode;
      cpu.memory[PROGRAM + 1] = 0x00;
      cpu.memory[PROGRAM + 2] = 0x20;
      cpu.pc = PROGRAM;
      cpu.x = _x;
      cpu.y = _y;
      cpu.cycles = 0;
      Assert::IsTrue(cpu.Step(), Widen("opcode " + Hex(_opcode) + " should execute").c_str());
      return cpu.cycles;
    }

    /*
     * The cost of one indexed instruction, run twice: once where the index stays inside the page and
     * once where it carries out of it. Returns the difference, which is the penalty.
     *
     * The base address is chosen per run rather than the index, because the index is what selects
     * absolute,X from absolute,Y and (zero page),Y, and using the same index for both runs keeps the
     * two comparable.
     */
    std::uint64_t CrossingPenaltyOf(std::uint8_t _opcode, bool _indirect)
    {
      const auto run = [&](std::uint16_t _base)
      {
        Cpu6502 cpu;
        cpu.pc = PROGRAM;
        cpu.x = 0x10;
        cpu.y = 0x10;

        cpu.memory[PROGRAM] = _opcode;
        if (_indirect)
        {
          // (zero page),Y: the pointer lives at $80/$81 and the test moves THAT.
          cpu.memory[PROGRAM + 1] = 0x80;
          cpu.memory[0x80] = static_cast<std::uint8_t>(_base & 0xFFu);
          cpu.memory[0x81] = static_cast<std::uint8_t>(_base >> 8);
        }
        else
        {
          cpu.memory[PROGRAM + 1] = static_cast<std::uint8_t>(_base & 0xFFu);
          cpu.memory[PROGRAM + 2] = static_cast<std::uint8_t>(_base >> 8);
        }

        cpu.cycles = 0;
        Assert::IsTrue(cpu.Step(), Widen("opcode " + Hex(_opcode) + " should execute").c_str());
        return cpu.cycles;
      };

      // $2080 + $10 = $2090, same page. $20F8 + $10 = $2108, one page on.
      return run(0x20F8) - run(0x2080);
    }
  } // namespace

  TEST_CLASS(CycleCountsAreStructurallySound)
  {
  public:
    /*
     * The set of opcodes the interpreter runs and the set it prices must be the same set.
     *
     * This is the check that matters most, because the failure it catches is silent: an opcode
     * Step executes and BaseCycles does not name would be counted as free, and a routine full of
     * them would report a cost that is simply too low with nothing to indicate it.
     *
     * The two sets were written eight weeks apart by different means -- the switch in Step from
     * Elite's own instruction usage, the table from the published timings -- so their agreeing on
     * all 256 opcodes is worth something.
     */
    TEST_METHOD(EveryOpcodeTheInterpreterRunsIsPricedAndNothingElseIs)
    {
      int priced = 0;
      int executed = 0;

      for (int value = 0; value < 256; ++value)
      {
        const std::uint8_t opcode = static_cast<std::uint8_t>(value);
        const bool isPriced = Cpu6502::BaseCycles(opcode) != 0;

        Cpu6502 cpu;
        cpu.memory[PROGRAM] = opcode;
        cpu.memory[PROGRAM + 1] = 0x00;
        cpu.memory[PROGRAM + 2] = 0x20;
        cpu.pc = PROGRAM;
        const bool runs = cpu.Step();

        Assert::AreEqual(isPriced, runs,
                         Widen("opcode " + Hex(opcode) + (runs ? " executes but is unpriced" : " is priced but does not execute")).c_str());
        priced += isPriced ? 1 : 0;
        executed += runs ? 1 : 0;
      }

      // The documented NMOS 6502 instruction set. Not a number to adjust -- if this fails, either
      // an undocumented opcode has been priced or a documented one has gone missing.
      Assert::AreEqual(151, priced, L"the documented opcode count");
      Assert::AreEqual(151, executed, L"the interpreter's opcode coverage");

      Logger::WriteMessage("cycle table: 151 documented opcodes, priced and executed, sets identical\n");
    }

    /*
     * One representative per addressing-mode group, against the published figure.
     *
     * This is a transcription check and nothing more -- it restates the same knowledge the table
     * holds. It is here because a group-wide slip (every zero page,X priced as 3) is the most
     * likely way the table goes wrong, and a spot check per group catches exactly that.
     */
    TEST_METHOD(TheBaseCountsAreTheDocumentedOnes)
    {
      struct Expectation
      {
        std::uint8_t opcode;
        std::uint8_t cycles;
        const char* what;
      };

      static constexpr Expectation EXPECTED[] = {
        {0xEA, 2, "NOP -- implied"},
        {0x0A, 2, "ASL A -- accumulator"},
        {0xA9, 2, "LDA immediate"},
        {0xD0, 2, "BNE not taken"},
        {0xA5, 3, "LDA zero page"},
        {0x85, 3, "STA zero page"},
        {0x48, 3, "PHA"},
        {0x4C, 3, "JMP absolute"},
        {0xB5, 4, "LDA zero page,X"},
        {0xB6, 4, "LDX zero page,Y"},
        {0xAD, 4, "LDA absolute"},
        {0xBD, 4, "LDA absolute,X -- before the crossing penalty"},
        {0x68, 4, "PLA"},
        {0x9D, 5, "STA absolute,X -- five always"},
        {0xB1, 5, "LDA (zero page),Y -- before the crossing penalty"},
        {0x06, 5, "ASL zero page -- read, modify, write"},
        {0x6C, 5, "JMP (indirect)"},
        {0xA1, 6, "LDA (zero page,X)"},
        {0x91, 6, "STA (zero page),Y -- six always"},
        {0x16, 6, "ASL zero page,X"},
        {0x0E, 6, "ASL absolute"},
        {0x20, 6, "JSR"},
        {0x60, 6, "RTS"},
        {0x1E, 7, "ASL absolute,X -- seven always"},
        {0x00, 7, "BRK"},
      };

      for (const Expectation& item : EXPECTED)
      {
        Assert::AreEqual(item.cycles, Cpu6502::BaseCycles(item.opcode),
                         Widen(std::string(item.what) + " (" + Hex(item.opcode) + ")").c_str());
      }
    }
  };

  TEST_CLASS(ThePageCrossingPenaltyLandsWhereItShould)
  {
  public:
    /*
     * Every indexed read costs exactly one more cycle when the index carries into a new page.
     *
     * All twenty-three of them, measured rather than asserted from the table: the run is set up
     * twice with the same index and a base address chosen so that one crosses and one does not, and
     * the difference is the penalty. So this tests the mechanism -- that Indexed notices, that Step
     * asks, and that the answer is applied once -- not the list.
     */
    TEST_METHOD(EveryIndexedReadPaysExactlyOneForCrossing)
    {
      static constexpr std::uint8_t ABSOLUTE_INDEXED[] = {
        0x1D, 0x3D, 0x5D, 0x7D, 0xBD, 0xDD, 0xFD, 0xBC, // absolute,X
        0x19, 0x39, 0x59, 0x79, 0xB9, 0xD9, 0xF9, 0xBE, // absolute,Y
      };
      static constexpr std::uint8_t INDIRECT_INDEXED[] = {
        0x11, 0x31, 0x51, 0x71, 0xB1, 0xD1, 0xF1, // (zero page),Y
      };

      for (const std::uint8_t opcode : ABSOLUTE_INDEXED)
      {
        Assert::IsTrue(Cpu6502::PaysPageCrossPenalty(opcode), Widen(Hex(opcode) + " should pay").c_str());
        Assert::AreEqual<std::uint64_t>(1, CrossingPenaltyOf(opcode, false), Widen(Hex(opcode) + " crossing a page").c_str());
      }

      for (const std::uint8_t opcode : INDIRECT_INDEXED)
      {
        Assert::IsTrue(Cpu6502::PaysPageCrossPenalty(opcode), Widen(Hex(opcode) + " should pay").c_str());
        Assert::AreEqual<std::uint64_t>(1, CrossingPenaltyOf(opcode, true), Widen(Hex(opcode) + " crossing a page").c_str());
      }

      Logger::WriteMessage("page crossing: 23 indexed reads, each one cycle dearer across a page\n");
    }

    /*
     * A store or a read-modify-write pays nothing for crossing, because it already paid.
     *
     * This is the asymmetry a port gets wrong. The processor cannot know whether the index will
     * carry until it has done the addition, and by then a write is already committed -- so a store
     * through an indexed mode spends the extra cycle unconditionally, and a read-modify-write
     * spends two. Pricing them like a read would make every indexed store in the game a cycle
     * cheap on some addresses and right on others, which is worse than being uniformly wrong.
     */
    TEST_METHOD(AnIndexedStoreOrReadModifyWriteNeverPaysForCrossing)
    {
      static constexpr std::uint8_t ABSOLUTE_INDEXED[] = {
        0x9D, 0x99,                         // STA absolute,X / absolute,Y
        0x1E, 0x3E, 0x5E, 0x7E, 0xFE, 0xDE, // ASL ROL LSR ROR INC DEC absolute,X
      };

      for (const std::uint8_t opcode : ABSOLUTE_INDEXED)
      {
        Assert::IsFalse(Cpu6502::PaysPageCrossPenalty(opcode), Widen(Hex(opcode) + " should not pay").c_str());
        Assert::AreEqual<std::uint64_t>(0, CrossingPenaltyOf(opcode, false),
                                        Widen(Hex(opcode) + " must cost the same either side").c_str());
      }

      Assert::IsFalse(Cpu6502::PaysPageCrossPenalty(0x91), L"STA (zero page),Y should not pay");
      Assert::AreEqual<std::uint64_t>(0, CrossingPenaltyOf(0x91, true), L"STA (zero page),Y either side");

      // Zero page indexing wraps inside the page, so there is no crossing to price at all -- the
      // wasted cycle is already in the base figure.
      Assert::AreEqual<std::uint64_t>(4, CostOfOne(0xB5, 0xFF, 0), L"LDA zero page,X wrapping");
      Assert::AreEqual<std::uint64_t>(4, CostOfOne(0xB6, 0, 0xFF), L"LDX zero page,Y wrapping");
    }
  };

  TEST_CLASS(BranchesAndAccumulation)
  {
  public:
    /*
     * A branch costs two, three or four, and the page compared is the one AFTER the offset byte.
     *
     * That last part is the subtle one. A branch at the end of a page has its opcode in one page
     * and its target reached from the next, and comparing the wrong pair mis-times exactly the
     * tight loops that a compiler-less assembly program puts there.
     */
    TEST_METHOD(ABranchCostsTwoThreeOrFour)
    {
      // Opcode, and the flag state that makes it branch.
      struct Case
      {
        std::uint8_t opcode;
        const char* name;
        bool n, v, c, z;
      };

      static constexpr Case CASES[] = {
        {0x10, "BPL", false, false, false, false}, {0x30, "BMI", true, false, false, false},  {0x50, "BVC", false, false, false, false},
        {0x70, "BVS", false, true, false, false},  {0x90, "BCC", false, false, false, false}, {0xB0, "BCS", false, false, true, false},
        {0xD0, "BNE", false, false, false, false}, {0xF0, "BEQ", false, false, false, true},
      };

      for (const Case& item : CASES)
      {
        // Not taken: the flags are inverted, so nothing happens and it costs two.
        {
          Cpu6502 cpu;
          cpu.pc = PROGRAM;
          cpu.memory[PROGRAM] = item.opcode;
          cpu.memory[PROGRAM + 1] = 0x10;
          cpu.n = !item.n;
          cpu.v = !item.v;
          cpu.c = !item.c;
          cpu.z = !item.z;
          cpu.cycles = 0;
          Assert::IsTrue(cpu.Step(), Widen(std::string(item.name) + " should execute").c_str());
          Assert::AreEqual<std::uint64_t>(2, cpu.cycles, Widen(std::string(item.name) + " not taken").c_str());
          Assert::AreEqual<std::uint16_t>(PROGRAM + 2, cpu.pc, Widen(std::string(item.name) + " should not move").c_str());
        }

        // Taken, staying in the page: three.
        {
          Cpu6502 cpu;
          cpu.pc = PROGRAM;
          cpu.memory[PROGRAM] = item.opcode;
          cpu.memory[PROGRAM + 1] = 0x10;
          cpu.n = item.n;
          cpu.v = item.v;
          cpu.c = item.c;
          cpu.z = item.z;
          cpu.cycles = 0;
          Assert::IsTrue(cpu.Step(), Widen(std::string(item.name) + " should execute").c_str());
          Assert::AreEqual<std::uint64_t>(3, cpu.cycles, Widen(std::string(item.name) + " taken, same page").c_str());
          Assert::AreEqual<std::uint16_t>(PROGRAM + 0x12, cpu.pc, Widen(std::string(item.name) + " target").c_str());
        }

        /*
         * Taken across a page: four. The opcode sits at $10FD, so the pc after the offset byte is
         * $10FF and a +2 offset lands at $1101 -- one page on from where the branch resumed, which
         * is the comparison the processor makes.
         */
        {
          Cpu6502 cpu;
          cpu.pc = 0x10FD;
          cpu.memory[0x10FD] = item.opcode;
          cpu.memory[0x10FE] = 0x02;
          cpu.n = item.n;
          cpu.v = item.v;
          cpu.c = item.c;
          cpu.z = item.z;
          cpu.cycles = 0;
          Assert::IsTrue(cpu.Step(), Widen(std::string(item.name) + " should execute").c_str());
          Assert::AreEqual<std::uint64_t>(4, cpu.cycles, Widen(std::string(item.name) + " taken across a page").c_str());
          Assert::AreEqual<std::uint16_t>(0x1101, cpu.pc, Widen(std::string(item.name) + " target across a page").c_str());
        }
      }

      Logger::WriteMessage("branches: 8 opcodes at 2 not taken, 3 taken, 4 across a page\n");
    }

    /*
     * A branch whose OPCODE straddles a page boundary, which is where the comparison is decided.
     *
     * The processor computes the target by adding the offset to the low byte of the address after
     * the operand and spending one more cycle if the high byte needs fixing. So the pair compared
     * is (address after the operand, target) -- NOT (address of the opcode, target). Those two
     * readings agree everywhere except here, on a branch that itself spans the boundary, which is
     * why a test that only put the TARGET across a page could not tell them apart. A mutation that
     * compared the opcode's page passed everything else in this file.
     *
     * Both directions are checked, because a wrong implementation gets one of them too dear and
     * the other too cheap:
     *
     *   BNE at $10FF, operand at $1100, resumes at $1101, target $1111. Same page: 3 cycles.
     *     Reading the opcode's page instead ($10 against $11) would charge 4.
     *   BNE at $10FE, operand at $10FF, resumes at $1100, target $10F0. Different page: 4 cycles.
     *     Reading the opcode's page instead ($10 against $10) would charge 3.
     */
    TEST_METHOD(ABranchWhoseOpcodeStraddlesAPageIsTimedFromWhereItResumes)
    {
      // Forwards, resuming in the new page and landing in it too. No fixup, so three.
      {
        Cpu6502 cpu;
        cpu.pc = 0x10FF;
        cpu.memory[0x10FF] = 0xD0; // BNE
        cpu.memory[0x1100] = 0x10; // +16
        cpu.z = false;
        cpu.cycles = 0;
        Assert::IsTrue(cpu.Step(), L"BNE should execute");
        Assert::AreEqual<std::uint16_t>(0x1111, cpu.pc, L"target");
        Assert::AreEqual<std::uint64_t>(3, cpu.cycles, L"resumes and lands in the same page");
      }

      // Backwards, resuming in the new page and landing back in the old one. A fixup, so four.
      {
        Cpu6502 cpu;
        cpu.pc = 0x10FE;
        cpu.memory[0x10FE] = 0xD0; // BNE
        cpu.memory[0x10FF] = 0xF0; // -16
        cpu.z = false;
        cpu.cycles = 0;
        Assert::IsTrue(cpu.Step(), L"BNE should execute");
        Assert::AreEqual<std::uint16_t>(0x10F0, cpu.pc, L"target");
        Assert::AreEqual<std::uint64_t>(4, cpu.cycles, L"resumes in one page and lands in another");
      }
    }

    /*
     * A hand-counted program, to prove the accumulation itself.
     *
     * Everything above measures one instruction at a time, which cannot catch a term added twice
     * across a call or a penalty that leaks from one instruction into the next. This runs a real
     * loop and compares the total against a figure worked out on paper:
     *
     *     $1000  LDA #$00      A9 00       2
     *     $1002  LDX #$10      A2 10       2
     *     $1004  STA $2000,X   9D 00 20    5   x16   -- a store, so five whether it crosses or not
     *     $1007  DEX           CA          2   x16
     *     $1008  BNE $1004     D0 FA       3   x15, and 2 on the last pass
     *     $100A  RTS           60          6
     *
     *     4 + 16*(5 + 2) + 15*3 + 2 + 6 = 4 + 112 + 45 + 2 + 6 = 169
     *
     * The BNE stays inside page $10 and the STA inside page $20, so neither pays a penalty -- which
     * is deliberate: this test is about the sum, and the penalties have their own.
     */
    TEST_METHOD(AHandCountedLoopTotalsExactlyWhatItShould)
    {
      static constexpr std::uint8_t CODE[] = {
        0xA9, 0x00,       // LDA #$00
        0xA2, 0x10,       // LDX #$10
        0x9D, 0x00, 0x20, // STA $2000,X
        0xCA,             // DEX
        0xD0, 0xFA,       // BNE $1004
        0x60,             // RTS
      };

      Cpu6502 cpu;
      cpu.Load(PROGRAM, CODE, sizeof(CODE));
      cpu.cycles = 0;

      const auto run = cpu.CallSubroutine(PROGRAM, 1'000, STOP);
      Assert::IsTrue(run.completed, L"the hand-written loop should return");
      Assert::AreEqual<std::uint32_t>(51, run.instructions, L"2 + 16*3 + 1 instructions");
      Assert::AreEqual<std::uint64_t>(169, cpu.cycles, L"the hand-counted total");

      // And the loop really did what the count assumes.
      Assert::AreEqual<std::uint8_t>(0, cpu.x, L"X should have counted down to zero");
      Assert::AreEqual<std::uint8_t>(0, cpu.memory[0x2001], L"the first store");
      Assert::AreEqual<std::uint8_t>(0, cpu.memory[0x2010], L"the last store");
    }

    /*
     * A trapped call costs nothing, which is the documented reason a measurement is a lower bound.
     *
     * The trap stands in for a routine the fixture did not want to run, so its real cost is not
     * knowable here. Counting zero is the honest choice -- inventing a figure would make a
     * measurement look complete when it is not -- but it has to be TESTED, because a trap that
     * quietly charged the JSR's six cycles would put a plausible-looking error into every
     * measurement of every routine that prints or plots.
     */
    TEST_METHOD(ATrappedCallCostsNothing)
    {
      static constexpr std::uint8_t CODE[] = {
        0x20,
        0x00,
        0x30, // JSR $3000  -- trapped
        0x60, // RTS
      };

      Cpu6502 cpu;
      cpu.Load(PROGRAM, CODE, sizeof(CODE));
      cpu.AddTrap(0x3000);
      cpu.cycles = 0;

      const auto run = cpu.CallSubroutine(PROGRAM, 1'000, STOP);
      Assert::IsTrue(run.completed, L"the trapped program should return");
      Assert::AreEqual<std::size_t>(1, cpu.trapHits.size(), L"the trap should have fired once");

      // JSR 6 + RTS 6. The trapped address itself contributes nothing.
      Assert::AreEqual<std::uint64_t>(12, cpu.cycles, L"JSR and RTS only");
    }
  };

  TEST_CLASS(CycleCountsAgainstTheShippedGame)
  {
  public:
    /*
     * The counter against real code, hand-counted from the source to check the model.
     *
     * Both routines run constantly in the real game, both are pure arithmetic with no traps armed,
     * and both are measured on the assembled original. The figures are exact rather than bounded,
     * because the binary is fixed -- so a change to either the cycle model or the oracle moves
     * them, which is exactly when someone should be told.
     *
     * TT54, the seed twist, hand-counted from tt54.asm. QQ15 is at $7F, so every access is zero
     * page:
     *
     *     LDA CLC ADC TAX          3 + 2 + 3 + 2   = 10
     *     LDA ADC TAY              3 + 3 + 2       =  8
     *     LDA STA  x4              (3 + 3) * 4     = 24
     *     CLC TXA ADC STA          2 + 2 + 3 + 3   = 10
     *     TYA ADC STA              2 + 3 + 3       =  8
     *     RTS                                        6
     *                                              ----
     *                              23 instructions   66
     *
     * DORND, the randomiser, hand-counted from dornd.asm. RAND is at $02:
     *
     *     LDA ROL TAX ADC STA STX  3 + 2 + 2 + 3 + 3 + 3 = 16
     *     LDA TAX ADC STA STX      3 + 2 + 3 + 3 + 3     = 14
     *     RTS                                               6
     *                                                    ----
     *                              13 instructions          36
     *
     * The thirteen is the point of that second one. DORND2 sits ONE BYTE EARLIER, at the CLC that
     * makes the entry carry-independent, and DORND itself starts after it -- so entering at DORND
     * costs 36 and entering at DORND2 costs 38. A cycle model that had those the same way round
     * would be wrong by two cycles on every random number the game draws.
     *
     * At 985,248 Hz (PAL) a cycle is about 1.015 microseconds, which is the conversion Risk R3
     * needs and the reason these numbers are worth having at all.
     */
    TEST_METHOD(TheSeedTwistAndTheRandomiserCostWhatTheyCost)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();

      // 6502: TT54 -- one twist of the six seed bytes, which is how the galaxy is walked.
      {
        Cpu6502 cpu = oracle.Fresh();
        const std::uint16_t qq15 = oracle.Label("QQ15");
        static constexpr std::uint8_t LAVE[] = {0x5A, 0x4A, 0x02, 0x53, 0xB7, 0x00};
        cpu.Load(qq15, LAVE, sizeof(LAVE));
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.cycles = 0;

        const auto run = cpu.CallSubroutine(oracle.Label("TT54"), 5'000);
        Assert::IsTrue(run.completed, L"TT54 should return");
        Assert::AreEqual<std::uint64_t>(66, cpu.cycles, L"TT54's measured cost");

        Logger::WriteMessage(("TT54  (twist the seeds): " + std::to_string(cpu.cycles) + " cycles over " +
                              std::to_string(run.instructions) + " instructions\n")
                               .c_str());
      }

      // 6502: DORND -- the randomiser, called several times per ship per iteration.
      {
        Cpu6502 cpu = oracle.Fresh();
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.cycles = 0;

        const auto run = cpu.CallSubroutine(oracle.Label("DORND"), 5'000);
        Assert::IsTrue(run.completed, L"DORND should return");
        Assert::AreEqual<std::uint64_t>(36, cpu.cycles, L"DORND's measured cost");

        Logger::WriteMessage(("DORND (one random byte): " + std::to_string(cpu.cycles) + " cycles over " +
                              std::to_string(run.instructions) + " instructions\n")
                               .c_str());
      }

      /*
       * 6502: DORND2 -- the same routine entered one byte earlier, at the CLC.
       *
       * Two cycles dearer than DORND, and asserting the DIFFERENCE rather than the total is what
       * makes this worth a test: it pins the model to a two-cycle instruction at a known address,
       * which no amount of comparing whole-routine totals would.
       */
      {
        Cpu6502 cpu = oracle.Fresh();
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.cycles = 0;

        const auto run = cpu.CallSubroutine(oracle.Label("DORND2"), 5'000);
        Assert::IsTrue(run.completed, L"DORND2 should return");
        Assert::AreEqual<std::uint64_t>(38, cpu.cycles, L"DORND2 is DORND plus a CLC");
        Assert::AreEqual<std::uint16_t>(1, static_cast<std::uint16_t>(oracle.Label("DORND") - oracle.Label("DORND2")),
                                        L"DORND2 should be exactly one byte before DORND");
      }
    }

    /*
     * What one turn of the title screen's ship COSTS, which is what decides how fast it spins.
     *
     * `TITLE` has no frame cap -- §6.17's scan found `WSCAN` called from `DELAY`, `TT16+7` and
     * `FREEZE` and from nowhere else -- so `TLL2` runs as fast as a 6510 can get round it, and the
     * rotation rate is a CONSEQUENCE of `MVEIT` and `LL9` rather than a number anyone chose. This
     * is that consequence, measured: the loop body is run against the shipped routines with the
     * ship state `TITLE` sets up, and the cycles are added.
     *
     * IT IS THE FIRST TIME THE COUNTER HAS BEEN USED FOR ITS PURPOSE. Everything else in this file
     * proves the model; this uses it, and the numbers it produces are `TITLE_TURN_COSTS` in
     * the shell. Without it the title screen ran one turn per PRESENT, so the ship span at the
     * monitor's refresh rate -- 60 turns a second on one machine and 165 on another, against the
     * twenty-odd the original manages (§6.110).
     *
     * TWO KNOWN BIASES, BOTH DOWNWARD, both from the `cycles` field's own documentation: a trapped
     * call costs nothing (nothing is trapped here, so this one does not apply) and the VIC-II's
     * stolen cycles are not modelled, which on a real C64 is a further 5-10%. So the true machine
     * is slightly slower than this says and the port runs slightly fast.
     */
    TEST_METHOD(TheTitleScreensLoopCostsWhatItCosts)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      Cpu6502 cpu = oracle.Fresh();

      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t frin = oracle.Label("FRIN");
      const std::uint16_t many = oracle.Label("MANY");
      const std::uint16_t slsp = oracle.Label("SLSP");
      const std::uint16_t type = oracle.Label("TYPE");

      // 6502: RES2's `LDA #LO(LS%) / STA SLSP` and the loops that clear `FRIN` and `MANY` -- the
      // state `NWSHP` needs to be able to create anything at all (§6.95: a default-constructed
      // world is a state the machine cannot be in).
      for (std::uint16_t offset = 0; offset < 13u; ++offset)
      {
        cpu.memory[static_cast<std::uint16_t>(frin + offset)] = 0;
      }
      for (std::uint16_t offset = 0; offset < 40u; ++offset)
      {
        cpu.memory[static_cast<std::uint16_t>(many + offset)] = 0;
      }
      // 6502: LDA #LO(LS%) / STA SLSP -- and `LS%` is &FFC0, the TOP of memory, because the ship
      // line heap descends from there towards `K%`. `LSO` is the sun's and is a different region.
      cpu.memory[slsp] = static_cast<std::uint8_t>(Elite::LineHeap::TOP & 0xFFu);
      cpu.memory[static_cast<std::uint16_t>(slsp + 1)] = static_cast<std::uint8_t>(Elite::LineHeap::TOP >> 8);

      // 6502: JSR ZINF -- the ship workspace, as `TITLE` gets it from `RES2`.
      Assert::IsTrue(cpu.CallSubroutine(oracle.Label("ZINF")).completed, L"ZINF returned");

      /*
       * 6502: LDA #96 / STA INWK+14 / STA INWK+7 / LDX #127 / STX INWK+29 / STX INWK+30, and then
       * `LDA TYPE / JSR NWSHP`.
       *
       * The Cobra Mk III at the distance `BR1` passes for it, which is the pair the player actually
       * looks at -- the Adder is the second screen and is smaller and nearer, so it is cheaper.
       */
      constexpr std::uint8_t COBRA = 11;     // 6502: CYL
      constexpr std::uint8_t DISTANCE = 210; // 6502: distaway, from BR1
      cpu.memory[static_cast<std::uint16_t>(inwk + 14)] = 96;
      cpu.memory[static_cast<std::uint16_t>(inwk + 7)] = 96;
      cpu.memory[static_cast<std::uint16_t>(inwk + 29)] = 127;
      cpu.memory[static_cast<std::uint16_t>(inwk + 30)] = 127;
      cpu.memory[type] = COBRA;

      cpu.a = COBRA;
      Assert::IsTrue(cpu.CallSubroutine(oracle.Label("NWSHP")).completed, L"NWSHP returned");
      Assert::AreEqual<std::uint32_t>(COBRA, cpu.memory[frin], L"the Cobra is in the bubble");

      /*
       * 6502: .TLL2 -- the loop body, and nothing else. The ship closes from 96 to 1 over the first
       * ninety-five turns and holds there afterwards, and it is the HELD state that is measured:
       * that is where a player watching the title screen spends every second after the first.
       */
      const std::uint16_t mveit = oracle.Label("MVEIT");
      const std::uint16_t ll9 = oracle.Label("LL9");

      std::uint64_t settled = 0;
      std::uint64_t settledLines = 0;
      std::uint32_t turns = 0;

      for (int turn = 0; turn < 160; ++turn)
      {
        if (cpu.memory[static_cast<std::uint16_t>(inwk + 7)] != 1u)
        {
          --cpu.memory[static_cast<std::uint16_t>(inwk + 7)];
        }

        const std::uint64_t before = cpu.cycles;

        Assert::IsTrue(cpu.CallSubroutine(mveit, 4'000'000).completed, L"MVEIT returned");

        // 6502: LDX distaway / STX INWK+6 / LDA #0 / STA INWK / STA INWK+3 -- the three stores that
        // undo the movement, so the ship turns on the spot instead of flying past.
        cpu.memory[static_cast<std::uint16_t>(inwk + 6)] = DISTANCE;
        cpu.memory[inwk] = 0;
        cpu.memory[static_cast<std::uint16_t>(inwk + 3)] = 0;

        Assert::IsTrue(cpu.CallSubroutine(ll9, 4'000'000).completed, L"LL9 returned");

        const std::uint64_t cost = cpu.cycles - before;

        // How many lines `LL9` left on this ship's heap: the first byte of the heap is the number
        // of bytes used, and a line is four of them after it.
        const std::uint16_t heapAt = static_cast<std::uint16_t>(cpu.memory[static_cast<std::uint16_t>(inwk + 33)] |
                                                                (cpu.memory[static_cast<std::uint16_t>(inwk + 34)] << 8));
        const std::uint32_t lines = (cpu.memory[heapAt] > 1u) ? ((cpu.memory[heapAt] - 1u) / 4u) : 0u;
        const std::uint8_t remaining = cpu.memory[static_cast<std::uint16_t>(inwk + 7)];

        if (remaining == 1u)
        {
          settled += cost;
          settledLines += lines;
          ++turns;
        }
        else if ((remaining % 8u) == 0u)
        {
          // The approach, logged rather than asserted: `LL9` draws a distant ship as a DOT, so the
          // first turns are a fraction of the cost of the last and the original closes the distance
          // far quicker than it then turns. That spread is what a single fixed rate cannot express.
          Logger::WriteMessage(("  approach at z_hi " + std::to_string(remaining) + ": " + std::to_string(cost) + " cycles, " +
                                std::to_string(lines) + " lines, " + std::to_string(1022727.0 / static_cast<double>(cost)) +
                                " turns a second")
                                 .c_str());
        }
      }

      Assert::IsTrue(turns >= 60u, L"the ship reached its settled distance and stayed there");

      const std::uint64_t perTurn = settled / turns;
      Logger::WriteMessage(("title screen: " + std::to_string(settledLines / turns) + " lines a turn").c_str());
      Logger::WriteMessage(("title screen: " + std::to_string(perTurn) + " cycles a turn over " + std::to_string(turns) +
                            " turns, which is " + std::to_string(1022727.0 / static_cast<double>(perTurn)) + " turns a second")
                             .c_str());

      /*
       * 121,276 cycles when this was written, which is 8.4 turns a second on an NTSC 6510. The
       * bound is wide because the number is a MEASUREMENT and the port's constant is derived from
       * it -- narrow it and any upstream change to `LL9` becomes a failing test with nothing wrong.
       * What it really asserts is the ORDER: a hundred thousand cycles a turn is single-figure
       * turns a second, not hundreds, so a shell that spins the ship once per PRESENT is wrong by
       * a factor nobody has to argue about -- seven times over on a 60 Hz display and twenty on a
       * 165 Hz one.
       */
      Assert::IsTrue(perTurn > 80'000u && perTurn < 180'000u, (L"a turn of the title ship costs " + std::to_wstring(perTurn) +
                                                               L" cycles, which is outside the "
                                                               L"range Outpost::TITLE_TURN_COSTS was derived from")
                                                                .c_str());
    }
  };

} // namespace GameLogicTests
