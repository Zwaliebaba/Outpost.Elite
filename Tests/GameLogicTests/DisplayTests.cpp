#include "pch.h"

#include "Display.h"
#include "Timing.h"

#include <array>
#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The platform's clock moved to _time: the display reads it through its reference.
void SetClock(Elite::Milliseconds& _clock, Elite::Milliseconds _time) noexcept
{
  _clock = _time;
}

} // namespace

// What is left of the display once the CGA went (ADR-069): its frame clock and its colour select.
TEST_CLASS(DisplayTests)
{
public:
  // A retrace is the first millisecond of each frame, reported to the first call that sees it and to no later one; the next
  // change is its end, or the next frame's start.
  TEST_METHOD(EachRetraceIsSeenOnce)
  {
    Elite::Milliseconds clock = Elite::FrameStart(3);
    Elite::Display display(clock);
    Assert::IsTrue(display.InRetrace(), L"a frame starts with its retrace");
    Assert::IsTrue(display.TakeRetrace(), L"seen by the first call");
    Assert::IsFalse(display.TakeRetrace(), L"and by no later one");
    Assert::IsTrue(display.InRetrace(), L"though it is still in progress");
    Assert::AreEqual(Elite::FrameStart(3) + Elite::Display::RETRACE_MILLISECONDS, display.NextChangeAt(), L"until it ends");
    SetClock(clock, Elite::FrameStart(3) + Elite::Display::RETRACE_MILLISECONDS);
    Assert::IsFalse(display.InRetrace() || display.TakeRetrace(), L"none after it");
    Assert::AreEqual(Elite::FrameStart(4), display.NextChangeAt(), L"until the next frame's");
    SetClock(clock, Elite::FrameStart(4));
    Assert::IsTrue(display.TakeRetrace(), L"the next one is seen afresh");
  }

  // The colour select names the background and the space view's palette: green, red and brown, or cyan, magenta and light gray,
  // each bright with the intensity bit; only its low six bits are kept.
  TEST_METHOD(TheColorSelectNamesTheFourColors)
  {
    const Elite::Milliseconds clock = 0;
    Elite::Display display(clock);
    display.SetColorSelect(0x10);
    Assert::IsTrue(display.GraphicsColors() == std::array<std::uint8_t, 4>{0, 10, 12, 14}, L"bright green, red and yellow on black");
    display.SetColorSelect(0x34);
    Assert::IsTrue(display.GraphicsColors() == std::array<std::uint8_t, 4>{4, 11, 13, 15}, L"bright palette 1 on red");
    display.SetColorSelect(0xE9);
    Assert::AreEqual(std::uint8_t{0x29}, display.ColorSelect(), L"the low six bits kept");
    Assert::IsTrue(display.GraphicsColors() == std::array<std::uint8_t, 4>{9, 3, 5, 7}, L"palette 1, dim, on light blue");
  }
};

} // namespace GameLogicTests
