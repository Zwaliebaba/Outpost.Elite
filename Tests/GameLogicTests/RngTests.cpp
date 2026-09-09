#include "pch.h"


#include "Rng.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Rng;
using Elite::RngResult;

/*
 * What `DORND2` buys the game.
 *
 * The generator was the first oracle suite: twenty bytes of machine code assembled by the test
 * itself and stepped beside the port for twenty thousand iterations from three starting states,
 * which is why slice 0c could close before BeebAsm was available. It went with the oracle
 * (M6-b-5). What is left is the property the comparison never asserted -- that the repeatable
 * entry point ignores the carry it was called with, which is the whole reason the game has two.
 */
namespace GameLogicTests
{

  TEST_CLASS(TheRepeatableGenerator)
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
