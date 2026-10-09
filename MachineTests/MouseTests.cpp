#include "pch.h"

#include "ServiceRig.h"

#include "Firmware.h"
#include "Mouse.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

TEST_CLASS(MouseTests)
{
public:
  // Without a driver, int 33h goes through the table to an IRET, as on a PC with none loaded.
  TEST_METHOD(WithoutADriverInt33hVectorsThroughTheTable)
  {
    ServiceRig rig("MouseAbsent", false);
    rig.Regs().ax = 0x0000;

    rig.Interrupt(0x33);

    Assert::AreEqual(std::uint32_t{Machine::Firmware::ROM_SEGMENT}, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(Machine::Firmware::IRET_STUBS_OFFSET + 0x33u, std::uint32_t{rig.Regs().ip});
    Assert::AreEqual(0x0000u, std::uint32_t{rig.Regs().ax}, L"AX = 0: no driver");
    Assert::IsFalse(rig.Services().Fault().has_value());
  }

  // ResetMouseIfSelected (CS:7F86) resets the driver when the mouse is the input device.
  TEST_METHOD(ResetReportsTwoButtons)
  {
    ServiceRig rig("MouseReset", true);
    rig.Regs().ax = 0x0000;
    rig.Interrupt(0x33);
    Assert::AreEqual(0xFFFFu, std::uint32_t{rig.Regs().ax}, L"installed");
    Assert::AreEqual(2u, std::uint32_t{rig.Regs().bx});
    Assert::AreEqual(2u, std::uint32_t{rig.Regs().ip}, L"serviced, not vectored");
  }

  // ReadFireButton (CS:7543) asks AX=5 BX=1 and looks at the buttons held in AX.
  TEST_METHOD(ButtonPressInformationCountsAndClears)
  {
    ServiceRig rig("MouseButtons", true);
    Machine::Mouse& mouse = rig.Services().MouseDriver();
    mouse.Move(10, 20); // to (330, 110)
    mouse.SetButtons(Machine::Mouse::BUTTON_RIGHT);
    mouse.SetButtons(0);
    mouse.SetButtons(static_cast<std::uint8_t>(Machine::Mouse::BUTTON_RIGHT | Machine::Mouse::BUTTON_LEFT));

    rig.Regs().ax = 0x0005;
    rig.Regs().bx = 0x0001;
    rig.Interrupt(0x33);
    Assert::AreEqual(3u, std::uint32_t{rig.Regs().ax}, L"both held");
    Assert::AreEqual(2u, std::uint32_t{rig.Regs().bx}, L"two presses of the right button");
    Assert::AreEqual(330u, std::uint32_t{rig.Regs().cx});
    Assert::AreEqual(110u, std::uint32_t{rig.Regs().dx});

    rig.Regs().ax = 0x0005;
    rig.Regs().bx = 0x0001;
    rig.Interrupt(0x33);
    Assert::AreEqual(0u, std::uint32_t{rig.Regs().bx}, L"counted since the last call");

    rig.Regs().ax = 0x0005;
    rig.Regs().bx = 0x0000;
    rig.Interrupt(0x33);
    Assert::AreEqual(1u, std::uint32_t{rig.Regs().bx}, L"one press of the left");
    Assert::IsFalse(rig.Services().Fault().has_value());

    rig.Regs().ax = 0x0005;
    rig.Regs().bx = 0x0002;
    rig.Interrupt(0x33);
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnsupportedRequest), L"a two-button driver");
  }

  // ReadMouseSteering (CS:79A1) reads the motion counters every frame.
  TEST_METHOD(MotionCountersAreSinceTheLastCall)
  {
    ServiceRig rig("MouseMotion", true);
    Machine::Mouse& mouse = rig.Services().MouseDriver();
    mouse.Move(5, -3);
    mouse.Move(2, -4);

    rig.Regs().ax = 0x000B;
    rig.Interrupt(0x33);
    Assert::AreEqual(7u, std::uint32_t{rig.Regs().cx});
    Assert::AreEqual(0xFFF9u, std::uint32_t{rig.Regs().dx}, L"-7, as a 16-bit count");

    rig.Regs().ax = 0x000B;
    rig.Interrupt(0x33);
    Assert::AreEqual(0u, std::uint32_t{rig.Regs().cx});
    Assert::AreEqual(0u, std::uint32_t{rig.Regs().dx});
  }

  TEST_METHOD(UnknownMouseFunctionFaults)
  {
    ServiceRig rig("MouseUnknown", true);
    rig.Regs().ax = 0x0003;
    rig.Interrupt(0x33);
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnknownFunction));
    Assert::AreEqual(0x0003u, std::uint32_t{rig.Regs().ax});
  }
};

} // namespace MachineTests
