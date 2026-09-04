#include "pch.h"

#include "Cpu6502.h"

#include "Rng.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Rng;
using Elite::RngResult;
using Elite::Testing::Cpu6502;

/*
 * The first oracle suite: the ported generator against the same routine executing on the
 * interpreter (ADR-003 section 1).
 *
 * This one does not need the assembled game. The routine is twenty bytes long, so the test
 * assembles it directly and the comparison runs on any machine -- which is the whole reason
 * slice 0c could close before BeebAsm was available. The suites that follow it will load the
 * real binaries through the oracle fixture instead.
 */
namespace GameLogicTests
{

  namespace
  {
    constexpr std::uint16_t ROUTINE = 0x0600;
    constexpr std::uint8_t RAND = 0x00; // the four state bytes live at 0x00..0x03 for this test

    /// The generator as machine code: twelve instructions in twenty bytes, two chained additions
    /// that each stash the previous byte into its partner. Assembled here so the comparison has
    /// something to be an oracle for.
    constexpr std::array<std::uint8_t, 20> ROUTINE_BYTES = {
      0xA5, RAND + 0, // LDA RAND
      0x2A,           // ROL A
      0xAA,           // TAX
      0x65, RAND + 2, // ADC RAND+2
      0x85, RAND + 0, // STA RAND
      0x86, RAND + 2, // STX RAND+2
      0xA5, RAND + 1, // LDA RAND+1
      0xAA,           // TAX
      0x65, RAND + 3, // ADC RAND+3
      0x85, RAND + 1, // STA RAND+1
      0x86, RAND + 3, // STX RAND+3
      0x60            // RTS
    };

    void InstallRoutine(Cpu6502& _cpu)
    {
      _cpu.Load(ROUTINE, ROUTINE_BYTES.data(), ROUTINE_BYTES.size());
    }

    std::wstring Describe(const wchar_t* _what, std::uint32_t _iteration, std::uint32_t _expected, std::uint32_t _actual)
    {
      return std::wstring(_what) + L" disagreed at iteration " + std::to_wstring(_iteration) + L": oracle " + std::to_wstring(_expected) +
             L", port " + std::to_wstring(_actual);
    }
  } // namespace

  TEST_CLASS(RngAgainstOracle)
  {
  public:
    /// Runs both for many iterations from one starting state and requires that every byte of
    /// state, both returned registers and both returned flags agree every single time.
    static void CompareSequence(const std::array<std::uint8_t, 4>& _seed, bool _carryIn, std::uint32_t _iterations)
    {
      Cpu6502 cpu;
      InstallRoutine(cpu);
      for (std::size_t index = 0; index < _seed.size(); ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(RAND + index)] = _seed[index];
      }
      cpu.c = _carryIn;

      Rng port;
      port.SetState(_seed);
      bool carryIn = _carryIn;

      for (std::uint32_t iteration = 0; iteration < _iterations; ++iteration)
      {
        cpu.c = carryIn;
        const auto run = cpu.CallSubroutine(ROUTINE, 1'000);
        Assert::IsTrue(run.completed, L"the oracle routine should return");

        const RngResult actual = port.Next(carryIn);

        Assert::AreEqual<std::uint32_t>(cpu.a, actual.value, Describe(L"A", iteration, cpu.a, actual.value).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.x, actual.previous, Describe(L"X", iteration, cpu.x, actual.previous).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.c ? 1u : 0u, actual.carry ? 1u : 0u, Describe(L"C", iteration, cpu.c, actual.carry).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.v ? 1u : 0u, actual.overflow ? 1u : 0u,
                                        Describe(L"V", iteration, cpu.v, actual.overflow).c_str());

        for (std::size_t index = 0; index < 4; ++index)
        {
          const std::uint8_t expected = cpu.memory[static_cast<std::uint16_t>(RAND + index)];
          Assert::AreEqual<std::uint32_t>(expected, port.State()[index],
                                          Describe(L"state byte", iteration, expected, port.State()[index]).c_str());
        }

        // The next call inherits the carry this one produced, as it does in the game.
        carryIn = actual.carry;
      }
    }

    TEST_METHOD(MatchesTheOracleFromAZeroedState)
    {
      CompareSequence({0x00, 0x00, 0x00, 0x00}, false, 20'000);
    }

    TEST_METHOD(MatchesTheOracleFromTheStateTheGameStartsWith)
    {
      // Elite seeds these bytes from the loader; any non-trivial pattern exercises the same code.
      CompareSequence({0x5A, 0x4A, 0x02, 0x48}, false, 20'000);
    }

    TEST_METHOD(MatchesTheOracleWithCarrySetOnEntry)
    {
      CompareSequence({0xFF, 0xFF, 0xFF, 0xFF}, true, 20'000);
    }

    TEST_METHOD(EveryStartingStateAgreesForTheFirstCall)
    {
      // A broad sweep rather than a deep one: the interesting bugs in this routine are in the
      // carry chain between the two halves, and they show up on the first call or not at all.
      Cpu6502 cpu;
      InstallRoutine(cpu);

      for (std::uint32_t seed = 0; seed < 4096; ++seed)
      {
        const std::array<std::uint8_t, 4> state = {static_cast<std::uint8_t>(seed & 0xFFu), static_cast<std::uint8_t>((seed >> 4) & 0xFFu),
                                                   static_cast<std::uint8_t>((seed >> 8) & 0xFFu),
                                                   static_cast<std::uint8_t>(seed * 7u & 0xFFu)};

        for (const bool carryIn : {false, true})
        {
          for (std::size_t index = 0; index < state.size(); ++index)
          {
            cpu.memory[static_cast<std::uint16_t>(RAND + index)] = state[index];
          }
          cpu.c = carryIn;
          const auto run = cpu.CallSubroutine(ROUTINE, 1'000);
          Assert::IsTrue(run.completed);

          Rng port;
          port.SetState(state);
          const RngResult actual = port.Next(carryIn);

          Assert::AreEqual<std::uint32_t>(cpu.a, actual.value, Describe(L"A", seed, cpu.a, actual.value).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.c ? 1u : 0u, actual.carry ? 1u : 0u, Describe(L"C", seed, cpu.c, actual.carry).c_str());
        }
      }
    }

    TEST_METHOD(TheRepeatableEntryPointIgnoresTheIncomingCarry)
    {
      // What DORND2 buys the game: the same sequence regardless of what the caller left in C.
      Rng afterClear;
      afterClear.SetState({0x12, 0x34, 0x56, 0x78});
      const RngResult fromClear = afterClear.NextRepeatable();

      Rng afterSet;
      afterSet.SetState({0x12, 0x34, 0x56, 0x78});
      const RngResult fromSet = afterSet.NextRepeatable();

      Assert::AreEqual<std::uint32_t>(fromClear.value, fromSet.value);
      Assert::IsTrue(afterClear.State() == afterSet.State());
    }
  };

} // namespace GameLogicTests
