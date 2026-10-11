#include "pch.h"

#include "Mouse.h"

#include <optional>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

// The mouse driver (Mouse, D33): the three int 33h calls the reference makes, typed. Without a driver the platform points
// vector 33h at the ROM's IRET (PlatformTests).
TEST_CLASS(MouseTests)
{
public:
  // ResetMouseIfSelected (CS:7F86) resets the driver when the mouse is the input device.
  TEST_METHOD(ResetReportsTwoButtons)
  {
    Elite::Mouse mouse(true);
    Assert::IsTrue(mouse.Present());
    const Elite::MouseReset reset = mouse.Reset();
    Assert::AreEqual(0xFFFFu, std::uint32_t{reset.status}, L"installed");
    Assert::AreEqual(2u, std::uint32_t{reset.buttonCount});
  }

  // ReadFireButton (CS:7543) asks for button 1's presses and looks at the buttons held.
  TEST_METHOD(ButtonPressInformationCountsAndClears)
  {
    Elite::Mouse mouse(true);
    mouse.Move(10, 20); // to (330, 110)
    mouse.SetButtons(Elite::Mouse::BUTTON_RIGHT);
    mouse.SetButtons(0);
    mouse.SetButtons(static_cast<std::uint8_t>(Elite::Mouse::BUTTON_RIGHT | Elite::Mouse::BUTTON_LEFT));

    const Elite::MousePresses right = mouse.Presses(1).value_or(Elite::MousePresses{});
    Assert::AreEqual(3u, std::uint32_t{right.buttons}, L"both held");
    Assert::AreEqual(2u, std::uint32_t{right.presses}, L"two presses of the right button");
    Assert::AreEqual(330u, std::uint32_t{right.xPixels});
    Assert::AreEqual(110u, std::uint32_t{right.yPixels});

    Assert::AreEqual(0u, std::uint32_t{mouse.Presses(1).value_or(Elite::MousePresses{}).presses}, L"counted since the last call");
    Assert::AreEqual(1u, std::uint32_t{mouse.Presses(0).value_or(Elite::MousePresses{}).presses}, L"one press of the left");
    Assert::IsFalse(mouse.Presses(2).has_value(), L"a two-button driver refuses a third: Hardware stops the run");
  }

  // ReadMouseSteering (CS:79A1) reads the motion counters every frame.
  TEST_METHOD(MotionCountersAreSinceTheLastCall)
  {
    Elite::Mouse mouse(true);
    mouse.Move(5, -3);
    mouse.Move(2, -4);

    const Elite::MouseMotion motion = mouse.Motion();
    Assert::AreEqual(7u, std::uint32_t{motion.acrossMickeys});
    Assert::AreEqual(0xFFF9u, std::uint32_t{motion.downMickeys}, L"-7, as a 16-bit count");

    const Elite::MouseMotion still = mouse.Motion();
    Assert::AreEqual(0u, std::uint32_t{still.acrossMickeys});
    Assert::AreEqual(0u, std::uint32_t{still.downMickeys});
  }
};

} // namespace GameLogicTests
