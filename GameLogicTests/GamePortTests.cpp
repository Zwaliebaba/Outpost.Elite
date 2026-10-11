#include "pch.h"

#include "GamePort.h"
#include "Timing.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

// The game port (GamePort, D33): the emulated PC's adapter without its port, timed on the instruction clock.
TEST_CLASS(GamePortTests)
{
public:
  // 24.2 us + 0.011 us per ohm, in cycles of the 4.77 MHz clock, rounded down.
  TEST_METHOD(OneShotDurationFollowsTheResistance)
  {
    Assert::AreEqual(Elite::Cycles{115}, Elite::GamePort::OneShotCycles(0));        // 24.2 us
    Assert::AreEqual(Elite::Cycles{2740}, Elite::GamePort::OneShotCycles(50'000));  // 574.2 us
    Assert::AreEqual(Elite::Cycles{5365}, Elite::GamePort::OneShotCycles(100'000)); // 1124.2 us
  }

  // Each axis bit reads 1 from the firing until its own duration has passed, to the cycle.
  TEST_METHOD(AxisBitsDropWhenTheirOneShotsTimeOut)
  {
    Elite::Cycles clock = 0;
    Elite::GamePort port(clock);
    port.SetAxisResistance(0, 50'000);
    port.SetAxisResistance(1, 0);
    Assert::AreEqual(0xF0u, std::uint32_t{port.Read()}, L"not fired yet; no button down");

    // Axes 2 and 3 have no stick on them, so once fired they read 1 throughout.
    clock = 1000;
    port.Fire();
    Assert::AreEqual(0xFFu, std::uint32_t{port.Read()});
    clock = 1000 + 115 - 1;
    Assert::AreEqual(0xFFu, std::uint32_t{port.Read()});
    clock = 1000 + 115;
    Assert::AreEqual(0xFDu, std::uint32_t{port.Read()}, L"Y drops after 115 cycles");
    clock = 1000 + 2740 - 1;
    Assert::AreEqual(0xFDu, std::uint32_t{port.Read()});
    clock = 1000 + 2740;
    Assert::AreEqual(0xFCu, std::uint32_t{port.Read()}, L"X drops after 2740 cycles");
  }

  // The 558's one-shots ignore a trigger while they are timing.
  TEST_METHOD(FiringWhileTimingDoesNotRetrigger)
  {
    Elite::Cycles clock = 1000;
    Elite::GamePort port(clock);
    port.SetAxisResistance(1, 0);
    port.Fire();
    clock = 1050;
    port.Fire();
    clock = 1115;
    Assert::AreEqual(0x00u, std::uint32_t{port.Read()} & 0x02u, L"still times out 115 cycles after the first firing");
    port.Fire();
    Assert::AreEqual(0x02u, std::uint32_t{port.Read()} & 0x02u, L"and fires again once it has");
  }

  // With no stick on an axis its one-shot never times out; resistance is clamped to 100 kOhm.
  TEST_METHOD(DisconnectedAxisStaysHighAndResistanceIsClamped)
  {
    Elite::Cycles clock = 0;
    Elite::GamePort port(clock);
    port.SetAxisResistance(0, 250'000);
    port.Fire();
    clock = 5365 - 1;
    Assert::AreEqual(0xFFu, std::uint32_t{port.Read()});
    clock = 5365;
    Assert::AreEqual(0xFEu, std::uint32_t{port.Read()}, L"axis 0 at 100 kOhm; axes 1-3 disconnected");
    clock = 100'000'000;
    Assert::AreEqual(0xFEu, std::uint32_t{port.Read()});
  }

  TEST_METHOD(ButtonsAreActiveLow)
  {
    const Elite::Cycles clock = 0;
    Elite::GamePort port(clock);
    port.SetButton(0, true);
    port.SetButton(3, true);
    Assert::AreEqual(0x60u, std::uint32_t{port.Read()});
    port.SetButton(0, false);
    Assert::AreEqual(0x70u, std::uint32_t{port.Read()});
  }
};

} // namespace GameLogicTests
