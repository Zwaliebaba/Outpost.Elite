#include "pch.h"

#include "OracleImage.h"

#include "Rng.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Rng;
using Elite::RngResult;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Tests that call into the assembled original itself.
 *
 * These are the ones that will carry the port from here on: the hand-assembled trick in
 * RngTests only reaches routines short enough to type out, and almost nothing else is.
 *
 * When the oracle is absent every test here reports what is missing and passes, so that a
 * machine without the assembled game is usable -- but OracleIsPresent fails, so a green run
 * can never quietly mean "none of this executed" (ADR-003 section 1, Risk R9).
 */
namespace GameLogicTests
{

namespace
{
/// Logs why the oracle is unavailable and says whether the caller should stop.
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
} // namespace

TEST_CLASS(OraclePresence)
{
public:
  /// The one test that is allowed to fail for a missing oracle. Without it, a machine that had
  /// never assembled the game would report a fully green suite that proved nothing.
  TEST_METHOD(OracleIsPresent)
  {
    const OracleImage& oracle = OracleImage::Instance();
    Assert::IsTrue(oracle.Available(),
                   (L"the oracle is not loaded: " + Widen(oracle.Reason())
                    + L"\nRun: python tools/labels.py --assemble")
                     .c_str());
  }

  TEST_METHOD(TheWholeGameIsLoadedAndItsLabelsResolve)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();

    // Eleven code blocks plus the data block and the ship blueprints.
    Assert::AreEqual<std::size_t>(13, oracle.BlockCount(), L"every assembled block should load");
    Assert::IsTrue(oracle.LabelCount() > 1900, L"the label table should hold the game and its data");

    // A spread of labels across the address space, so a half-loaded image cannot pass. The last
    // four live in the data build rather than the code blocks, which is the half that was
    // missing until slice 1a.
    for (const char* name :
         { "DORND", "MULTU", "MULT1", "FMLTU", "ADD", "ARCTAN", "TIS1", "LL28", "MVEIT", "TACTICS", "SNE", "ACT",
           "QQ18", "XX21" })
    {
      std::uint16_t address = 0;
      Assert::IsTrue(oracle.TryLabel(name, address), (L"missing label " + Widen(name)).c_str());
      Assert::IsTrue(address != 0, (L"label resolved to zero: " + Widen(name)).c_str());
    }
  }

  TEST_METHOD(CallingALabelExecutesRealCodeAndReturns)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();

    Cpu6502 cpu = oracle.Fresh();
    const auto run = cpu.CallSubroutine(oracle.Label("DORND"));

    Assert::IsTrue(run.completed, L"the call should reach its return");
    Assert::IsFalse(run.illegalOpcode, L"the game uses only documented opcodes");
    Assert::IsTrue(run.instructions > 5, L"real code executed, not an immediate return");
  }
};

TEST_CLASS(RngAgainstTheAssembledGame)
{
public:
  /// The port against the routine as it actually ships, rather than against a copy of it that
  /// this test typed out. If the hand-assembled version in RngTests and this one ever
  /// disagreed, this is the one that would be right.
  TEST_METHOD(MatchesTheShippedRoutineOverALongSequence)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();

    const std::uint16_t routine = oracle.Label("DORND");
    const std::uint16_t randAddress = oracle.Label("RAND");
    Assert::IsTrue(routine != 0 && randAddress != 0, L"DORND and RAND must both resolve");

    const std::array<std::uint8_t, 4> seed = { 0x5A, 0x4A, 0x02, 0x48 };

    Cpu6502 cpu = oracle.Fresh();
    for (std::size_t index = 0; index < seed.size(); ++index)
    {
      cpu.memory[static_cast<std::uint16_t>(randAddress + index)] = seed[index];
    }

    Rng port;
    port.SetState(seed);
    bool carryIn = false;

    for (std::uint32_t iteration = 0; iteration < 20'000; ++iteration)
    {
      cpu.c = carryIn;
      const auto run = cpu.CallSubroutine(routine, 1'000);
      Assert::IsTrue(run.completed, L"the shipped routine should return");

      const RngResult actual = port.Next(carryIn);

      const std::wstring where = L" at iteration " + std::to_wstring(iteration);
      Assert::AreEqual<std::uint32_t>(cpu.a, actual.value, (L"returned byte" + where).c_str());
      Assert::AreEqual<std::uint32_t>(cpu.x, actual.previous, (L"previous byte" + where).c_str());
      Assert::AreEqual<std::uint32_t>(cpu.c ? 1u : 0u, actual.carry ? 1u : 0u, (L"carry" + where).c_str());
      Assert::AreEqual<std::uint32_t>(cpu.v ? 1u : 0u, actual.overflow ? 1u : 0u, (L"overflow" + where).c_str());

      for (std::size_t index = 0; index < seed.size(); ++index)
      {
        const std::uint8_t expected = cpu.memory[static_cast<std::uint16_t>(randAddress + index)];
        Assert::AreEqual<std::uint32_t>(expected, port.State()[index], (L"state byte" + where).c_str());
      }

      carryIn = actual.carry;
    }
  }

  /// The repeatable entry point sits one byte before the main one and clears carry first.
  /// Worth pinning because the port implements it as a call rather than as a second routine.
  TEST_METHOD(TheRepeatableEntryPointMatchesTheShippedOne)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();

    std::uint16_t repeatable = 0;
    if (!oracle.TryLabel("DORND2", repeatable))
    {
      Logger::WriteMessage("SKIPPED -- this build has no DORND2 entry point");
      return;
    }

    const std::uint16_t randAddress = oracle.Label("RAND");
    const std::array<std::uint8_t, 4> seed = { 0x12, 0x34, 0x56, 0x78 };

    for (const bool carryIn : { false, true })
    {
      Cpu6502 cpu = oracle.Fresh();
      for (std::size_t index = 0; index < seed.size(); ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(randAddress + index)] = seed[index];
      }
      cpu.c = carryIn;

      const auto run = cpu.CallSubroutine(repeatable, 1'000);
      Assert::IsTrue(run.completed);

      Rng port;
      port.SetState(seed);
      const RngResult actual = port.NextRepeatable();

      Assert::AreEqual<std::uint32_t>(cpu.a, actual.value, L"the incoming carry must not matter");
      Assert::AreEqual<std::uint32_t>(cpu.x, actual.previous);
    }
  }
};

} // namespace GameLogicTests
