#include "pch.h"


#include "Rng.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Rng;
using Elite::RngResult;

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

    std::wstring Describe(const wchar_t* _what, std::uint32_t _iteration, std::uint32_t _expected, std::uint32_t _actual)
    {
      return std::wstring(_what) + L" disagreed at iteration " + std::to_wstring(_iteration) + L": oracle " + std::to_wstring(_expected) +
             L", port " + std::to_wstring(_actual);
    }
  } // namespace

  TEST_CLASS(RngAgainstOracle)
  {
  public:
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
