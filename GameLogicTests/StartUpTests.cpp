#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"

#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t CHECK_CHEAT_ARGUMENT = 0x02A5;
constexpr std::uint16_t COMMAND_TAIL = 0x80; // in the PSP: its length, then the text

} // namespace

// Constructed inputs for start-up (plan §6.3): the command tails no replay gives.
TEST_CLASS(StartUpTests)
{
public:
  // ' cheat' itself, and a tail of the same length that differs only in its last letter.
  TEST_METHOD(CheckCheatArgumentAgreesOnTheCheatAndANearMiss)
  {
    ComparisonRig rig("CheckCheatArgument");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::uint16_t psp = ram.Read16(data, Elite::DS.pspSegment.offset);
    for (const std::string_view tail : {std::string_view(" cheat"), std::string_view(" cheaT")})
    {
      ram.Write8(psp, COMMAND_TAIL, static_cast<std::uint8_t>(tail.size()));
      for (std::size_t index = 0; index < tail.size(); ++index)
        ram.Write8(psp, static_cast<std::uint16_t>(COMMAND_TAIL + 1 + index), static_cast<std::uint8_t>(tail[index]));
      rig.Call(CHECK_CHEAT_ARGUMENT, {});
      Assert::IsTrue((ram.Read8(data, Elite::DS.cheatEnabled.offset) == 1) == (tail == " cheat"), L"only ' cheat' enables it");
    }
    rig.AssertAllAgreed(CHECK_CHEAT_ARGUMENT, 2);
  }
};

} // namespace GameLogicTests
