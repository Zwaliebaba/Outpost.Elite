#include "pch.h"

#include "InterruptController.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

// The 8259 as the BIOS leaves it and the game uses it (InterruptController, D33): the emulated PC's controller without its
// ports, so without its initialisation words, its mask register, its specific end of interrupt and its register reads,
// none of which the game writes.
TEST_CLASS(InterruptControllerTests)
{
public:
  // What the BIOS leaves: vector base 8, and only IRQ 0, IRQ 1 and IRQ 6 unmasked.
  TEST_METHOD(PowerOnStateIsWhatTheBiosLeaves)
  {
    const Elite::InterruptController controller;
    Assert::AreEqual(0x08u, std::uint32_t{Elite::InterruptController::VECTOR_BASE});
    Assert::AreEqual(0xBCu, std::uint32_t{Elite::InterruptController::MASK});
    Assert::AreEqual(0x00u, std::uint32_t{controller.RequestRegister()});
    Assert::IsFalse(controller.InterruptPending());

    for (std::uint8_t line = 0; line < 8; ++line)
    {
      Elite::InterruptController fresh;
      fresh.RaiseIrq(line);
      const bool unmasked = line == 0 || line == 1 || line == 6;
      Assert::AreEqual(unmasked, fresh.InterruptPending());
    }
  }

  // IRQ 0 outranks IRQ 1, and while IRQ 0 is in service IRQ 1 waits for its end of interrupt.
  TEST_METHOD(HigherPriorityFirstAndLowerWaitsForTheEnd)
  {
    Elite::InterruptController controller;
    controller.RaiseIrq(1);
    controller.RaiseIrq(0);
    Assert::IsTrue(controller.InterruptPending());
    Assert::AreEqual(0x08u, std::uint32_t{controller.Acknowledge()});
    Assert::AreEqual(0x01u, std::uint32_t{controller.InServiceRegister()});
    Assert::AreEqual(0x02u, std::uint32_t{controller.RequestRegister()});
    Assert::IsFalse(controller.InterruptPending(), L"IRQ 1 is held off while IRQ 0 is in service");

    controller.EndOfInterrupt();
    Assert::AreEqual(0x00u, std::uint32_t{controller.InServiceRegister()});
    Assert::IsTrue(controller.InterruptPending());
    Assert::AreEqual(0x09u, std::uint32_t{controller.Acknowledge()});
  }

  // A higher-priority request interrupts a lower one in service, and an end of interrupt, non-specific as the game's OUT 20h,
  // 20h is, ends the highest in service.
  TEST_METHOD(NestingEndsTheHighestFirst)
  {
    Elite::InterruptController controller;
    controller.RaiseIrq(1);
    Assert::AreEqual(0x09u, std::uint32_t{controller.Acknowledge()});
    controller.RaiseIrq(0);
    Assert::IsTrue(controller.InterruptPending(), L"IRQ 0 outranks IRQ 1 in service");
    Assert::AreEqual(0x08u, std::uint32_t{controller.Acknowledge()});
    Assert::AreEqual(0x03u, std::uint32_t{controller.InServiceRegister()});

    controller.EndOfInterrupt();
    Assert::AreEqual(0x02u, std::uint32_t{controller.InServiceRegister()}, L"the end of interrupt ends IRQ 0, the highest");

    controller.RaiseIrq(6);
    Assert::IsFalse(controller.InterruptPending(), L"IRQ 6 ranks below IRQ 1 in service");
    controller.EndOfInterrupt();
    Assert::AreEqual(0x00u, std::uint32_t{controller.InServiceRegister()});
    Assert::AreEqual(0x0Eu, std::uint32_t{controller.Acknowledge()});
  }

  // A masked request is latched in the request register, and never granted: the game does not change the mask.
  TEST_METHOD(MaskedRequestIsLatchedAndNotGranted)
  {
    Elite::InterruptController controller;
    controller.RaiseIrq(2);
    Assert::IsFalse(controller.InterruptPending());
    Assert::AreEqual(0x04u, std::uint32_t{controller.RequestRegister()});
  }

  // A second edge while the first is still pending is not queued; one after the acknowledge is.
  TEST_METHOD(SecondEdgeWhilePendingIsNotQueued)
  {
    Elite::InterruptController controller;
    controller.RaiseIrq(0);
    controller.RaiseIrq(0);
    Assert::AreEqual(0x08u, std::uint32_t{controller.Acknowledge()});
    Assert::IsFalse(controller.InterruptPending());
    controller.EndOfInterrupt();
    Assert::IsFalse(controller.InterruptPending(), L"nothing was queued behind the first edge");

    controller.RaiseIrq(0);
    Assert::AreEqual(0x08u, std::uint32_t{controller.Acknowledge()});
    controller.RaiseIrq(0);
    Assert::AreEqual(0x01u, std::uint32_t{controller.RequestRegister()}, L"an edge while in service is latched again");
    Assert::IsFalse(controller.InterruptPending(), L"but waits for the end of interrupt");
  }

  TEST_METHOD(AcknowledgeWithNothingPendingIsSpuriousIrq7)
  {
    Elite::InterruptController controller;
    Assert::AreEqual(0x0Fu, std::uint32_t{controller.Acknowledge()});
    Assert::AreEqual(0x00u, std::uint32_t{controller.InServiceRegister()});
  }
};

} // namespace GameLogicTests
