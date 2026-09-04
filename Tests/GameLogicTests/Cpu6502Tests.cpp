#include "pch.h"

#include "Cpu6502.h"

#include <array>
#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::RunResult;

/*
 * The interpreter has to be trusted before it can be an oracle, so it gets its own suite
 * (ADR-003 section 4). Every test here is a short program whose result is decided by the
 * published behaviour of the processor rather than by anything in Elite.
 *
 * The cases were chosen for the things a hand-written interpreter actually gets wrong: the
 * direction of the carry flag in subtraction, signed overflow, zero-page index wrapping, and
 * the indirect-jump page bug.
 */
namespace GameLogicTests
{

  namespace
  {
    constexpr std::uint16_t PROGRAM = 0x0600;

    /// Loads a program at 0x0600 and runs it as a subroutine call.
    RunResult RunProgram(Cpu6502& _cpu, std::initializer_list<std::uint8_t> _bytes)
    {
      std::size_t offset = 0;
      for (const std::uint8_t byte : _bytes)
      {
        _cpu.memory[static_cast<std::uint16_t>(PROGRAM + offset)] = byte;
        ++offset;
      }
      return _cpu.CallSubroutine(PROGRAM, 10'000);
    }
  } // namespace

  TEST_CLASS(Cpu6502Basics)
  {
  public:
    TEST_METHOD(LoadAndStoreMovesAByteAndSetsTheZeroFlag)
    {
      Cpu6502 cpu;
      // LDA #$00 ; STA $10 ; RTS
      const RunResult result = RunProgram(cpu, {0xA9, 0x00, 0x85, 0x10, 0x60});

      Assert::IsTrue(result.completed, L"the program should have returned");
      Assert::IsFalse(result.illegalOpcode, L"every opcode used here is implemented");
      Assert::AreEqual<std::uint8_t>(0x00, cpu.memory[0x10]);
      Assert::IsTrue(cpu.z, L"loading zero sets Z");
      Assert::IsFalse(cpu.n, L"loading zero clears N");
    }

    TEST_METHOD(AdditionCarriesOutOfTheTopBit)
    {
      Cpu6502 cpu;
      // CLC ; LDA #$FF ; ADC #$01 ; RTS
      RunProgram(cpu, {0x18, 0xA9, 0xFF, 0x69, 0x01, 0x60});

      Assert::AreEqual<std::uint8_t>(0x00, cpu.a);
      Assert::IsTrue(cpu.c, L"0xFF + 1 carries");
      Assert::IsTrue(cpu.z, L"and wraps to zero");
      Assert::IsFalse(cpu.v, L"unsigned wrap is not signed overflow");
    }

    TEST_METHOD(AdditionFlagsSignedOverflowSeparatelyFromCarry)
    {
      Cpu6502 cpu;
      // CLC ; LDA #$7F ; ADC #$01 ; RTS  -- 127 + 1 is -128 in signed terms.
      RunProgram(cpu, {0x18, 0xA9, 0x7F, 0x69, 0x01, 0x60});

      Assert::AreEqual<std::uint8_t>(0x80, cpu.a);
      Assert::IsTrue(cpu.v, L"two positives producing a negative is overflow");
      Assert::IsFalse(cpu.c, L"no carry out of bit 7 here");
      Assert::IsTrue(cpu.n, L"the result is negative");
    }

    TEST_METHOD(SubtractionTreatsCarryAsNoBorrow)
    {
      Cpu6502 cpu;
      // SEC ; LDA #$05 ; SBC #$03 ; RTS
      RunProgram(cpu, {0x38, 0xA9, 0x05, 0xE9, 0x03, 0x60});

      Assert::AreEqual<std::uint8_t>(0x02, cpu.a);
      Assert::IsTrue(cpu.c, L"no borrow was needed, so C stays set");
    }

    TEST_METHOD(SubtractionClearsCarryWhenItBorrows)
    {
      Cpu6502 cpu;
      // SEC ; LDA #$03 ; SBC #$05 ; RTS
      RunProgram(cpu, {0x38, 0xA9, 0x03, 0xE9, 0x05, 0x60});

      Assert::AreEqual<std::uint8_t>(0xFE, cpu.a);
      Assert::IsFalse(cpu.c, L"a borrow clears C");
      Assert::IsTrue(cpu.n, L"and the result reads as negative");
    }

    TEST_METHOD(ZeroPageIndexingWrapsInsideThePage)
    {
      Cpu6502 cpu;
      cpu.memory[0x007F] = 0xAB;
      // LDX #$80 ; LDA $FF,X ; RTS  -- 0xFF + 0x80 wraps to 0x7F, it does not reach 0x017F.
      RunProgram(cpu, {0xA2, 0x80, 0xB5, 0xFF, 0x60});

      Assert::AreEqual<std::uint8_t>(0xAB, cpu.a);
    }

    TEST_METHOD(IndirectJumpReproducesThePageCrossingBug)
    {
      Cpu6502 cpu;
      // The pointer sits at 0x02FF, so the high byte comes from 0x0200, not 0x0300.
      cpu.memory[0x02FF] = 0x34;
      cpu.memory[0x0200] = 0x12;
      cpu.memory[0x0300] = 0xFF;

      cpu.memory[PROGRAM + 0] = 0x6C; // JMP ($02FF)
      cpu.memory[PROGRAM + 1] = 0xFF;
      cpu.memory[PROGRAM + 2] = 0x02;
      cpu.pc = PROGRAM;

      Assert::IsTrue(cpu.Step());
      Assert::AreEqual<std::uint16_t>(0x1234, cpu.pc, L"the high byte wraps within the page");
    }

    TEST_METHOD(CallAndReturnBalanceTheStack)
    {
      Cpu6502 cpu;
      // The subroutine at 0x0700 loads 0x42; the caller calls it and returns.
      cpu.memory[0x0700] = 0xA9;
      cpu.memory[0x0701] = 0x42;
      cpu.memory[0x0702] = 0x60;

      const std::uint8_t entrySp = cpu.sp;
      // JSR $0700 ; RTS
      const RunResult result = RunProgram(cpu, {0x20, 0x00, 0x07, 0x60});

      Assert::IsTrue(result.completed);
      Assert::AreEqual<std::uint8_t>(0x42, cpu.a);
      Assert::AreEqual<std::uint8_t>(entrySp, cpu.sp, L"the stack came back to where it started");
    }

    TEST_METHOD(BranchesTakeSignedBackwardOffsets)
    {
      Cpu6502 cpu;
      // LDX #$03 ; DEX ; BNE -1 ; RTS  -- counts down to zero and falls through.
      const RunResult result = RunProgram(cpu, {0xA2, 0x03, 0xCA, 0xD0, 0xFD, 0x60});

      Assert::IsTrue(result.completed);
      Assert::AreEqual<std::uint8_t>(0x00, cpu.x);
      Assert::IsTrue(cpu.z);
    }

    TEST_METHOD(AnUnimplementedOpcodeStopsTheRunOnTheOffendingByte)
    {
      Cpu6502 cpu;
      // 0xFF is not a documented opcode.
      const RunResult result = RunProgram(cpu, {0xFF});

      Assert::IsFalse(result.completed, L"an illegal opcode is not a completed call");
      Assert::IsTrue(result.illegalOpcode);
      Assert::AreEqual<std::uint16_t>(PROGRAM, result.stoppedAt, L"pc stays on the byte it could not run");
    }

    TEST_METHOD(DecimalModeAdditionCarriesInTens)
    {
      Cpu6502 cpu;
      // SED ; CLC ; LDA #$28 ; ADC #$14 ; RTS  -- 28 + 14 is 42 in packed decimal.
      RunProgram(cpu, {0xF8, 0x18, 0xA9, 0x28, 0x69, 0x14, 0x60});

      Assert::AreEqual<std::uint8_t>(0x42, cpu.a);
      Assert::IsFalse(cpu.c);
    }
  };

} // namespace GameLogicTests
