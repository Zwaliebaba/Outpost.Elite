#include "pch.h"

#include "TimeOfDay.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The ticks at noon, the default start moment: 43,200 s at 1,193,181.67 / 65,536 ticks a second.
constexpr std::uint32_t NOON_TICKS = 0x000C0059;

} // namespace

// The time of day the BIOS kept (TimeOfDay, ADR-041): the emulated PC's int 1Ah, and the count its timer handler made.
TEST_CLASS(TimeOfDayTests)
{
public:
  // The tick count starts at the time of day DOS was given, and never reaches a whole day.
  TEST_METHOD(TicksStartAtTheTimeOfDay)
  {
    const Elite::TimeOfDay day(Elite::TimeOfDay::TicksAt(12 * 60 * 60 * 100));
    Assert::AreEqual(NOON_TICKS, day.Ticks(), L"ticks at noon: 786,521");
    Assert::AreEqual(Elite::TimeOfDay::TICKS_PER_DAY - 1, Elite::TimeOfDay::TicksAt(8'639'999), L"never a whole day");
  }

  // RestoreTimerInterrupt (CS:016B) reads the count and sets it back a day when it is over one. The timer handler's count
  // starts again from 0 once a day has passed, and sets the flag that a read reports and clears.
  TEST_METHOD(TimeOfDayReadsAndSetsTheTickCount)
  {
    Elite::TimeOfDay day(NOON_TICKS);
    day.SetClock(0x001800AF);
    Assert::AreEqual(0x001800AFu, day.Ticks());

    const Elite::TimeOfDay::Tick tick = day.CountTick();
    Assert::IsTrue(tick.dayCheck && tick.dayEnded && !tick.carried, L"the day's last tick");
    const Elite::ClockReading clock = day.ReadClock();
    Assert::AreEqual(0u, clock.ticks);
    Assert::AreEqual(1u, std::uint32_t{clock.rolledOver}, L"midnight has passed");
    Assert::AreEqual(0u, std::uint32_t{day.ReadClock().rolledOver}, L"the flag is cleared by reading it");
  }
};

} // namespace GameLogicTests
