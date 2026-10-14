#include "pch.h"

#include "Mouse.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

// The mouse (Mouse, ADR-051): what the game reads of it, the buttons held and the motion since its last read.
TEST_CLASS(MouseTests)
{
public:
  // ReadFireButton looks at the buttons held.
  TEST_METHOD(ButtonsHeldAreReported)
  {
    Elite::Mouse mouse(true);
    Assert::IsTrue(mouse.Present());
    mouse.SetButtons(static_cast<std::uint8_t>(Elite::Mouse::BUTTON_RIGHT | Elite::Mouse::BUTTON_LEFT));
    Assert::AreEqual(3u, std::uint32_t{mouse.Buttons()}, L"both held");
  }

  // ReadMouseSteering reads the motion every frame; ResetMouseIfSelected forgets it.
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

    mouse.Move(9, 9);
    mouse.Reset();
    Assert::AreEqual(0u, std::uint32_t{mouse.Motion().acrossMickeys}, L"forgotten");
  }
};

} // namespace GameLogicTests
