#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Guest.h"

#include <array>
#include <initializer_list>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t ARRIVE_IN_SYSTEM = 0x2B5A;
constexpr std::uint16_t RESET_HYPERSPACE_RINGS = 0x48AB;
constexpr std::uint16_t DRAW_HYPERSPACE_RINGS = 0x48C0;
constexpr std::uint16_t UPDATE_MISSION_SCHEDULE = 0x4953;
constexpr std::uint16_t LATCH_HYPERSPACE_TARGET = 0x49F6;
constexpr std::uint16_t SHOW_HYPERSPACE_COUNTDOWN = 0x8C62;

constexpr std::uint16_t HYPERSPACE_RING_COUNT = 10;
constexpr std::uint16_t HYPERSPACE_RING_BYTES = 3;
constexpr std::uint8_t HYPERSPACE_RING_LARGEST = 0x96;

/// The state an arrival starts from.
struct Arrival
{
  std::uint8_t witchspaceCountdown;
  std::uint8_t thargoidInvasionActive;
  std::uint8_t techLevel;
  std::uint16_t messageFrames;
  std::uint8_t screenLayout;
};

[[nodiscard]] Elite::Guest GuestOf(ComparisonRig& _rig)
{
  return Elite::Guest(_rig.Host(), _rig.Program().loadSegment, Elite::DataSegment(_rig.Program()));
}

[[nodiscard]] std::wstring Hex(std::uint16_t _value)
{
  constexpr std::wstring_view DIGITS = L"0123456789ABCDEF";
  std::wstring text = L"CS:";
  for (int shift = 12; shift >= 0; shift -= 4)
    text += DIGITS[static_cast<std::size_t>((_value >> shift) & 0xF)];
  return text;
}

// Every offset in _offsets ran in the original while a call of _entry was being compared.
void AssertExecuted(ComparisonRig& _rig, std::uint16_t _entry, std::initializer_list<std::uint16_t> _offsets)
{
  const auto& hooks = _rig.Host().Native().Hooks();
  const auto found = hooks.find(Machine::Memory::Linear(_rig.Program().loadSegment, _entry));
  Assert::IsTrue(found != hooks.end(), L"the routine is ported");
  for (const std::uint16_t offset : _offsets)
  {
    const std::wstring message = Hex(offset) + L" ran in a comparison";
    Assert::IsTrue(found->second.executed.Contains(offset), message.c_str());
  }
}

// A fixed sequence of words, for inputs the replays do not give.
class Words
{
public:
  [[nodiscard]] std::uint16_t Next() noexcept
  {
    m_state = m_state * 1103515245u + 12345u;
    return static_cast<std::uint16_t>(m_state >> 16);
  }

private:
  std::uint32_t m_state = 1;
};

} // namespace

// Constructed inputs for the ported hyperspace routines (plan §6.3): arrivals in witch space, during an
// invasion and from another screen, every mission milestone, and rings of every size.
TEST_CLASS(HyperspaceTests)
{
public:
  // The mask ship's count running out (CS:4962-4968), galaxy 1 and DS:7629, each mission's jump count,
  // and an arrival in witch space (CS:495A).
  TEST_METHOD(MissionScheduleAgreesInEveryState)
  {
    ComparisonRig rig("UpdateMissionSchedule");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<std::uint8_t, 2> WITCHSPACE = {0, 1};
    constexpr std::array<std::uint8_t, 3> MASK_JUMPS = {0, 1, 2};
    constexpr std::array<std::uint8_t, 2> GALAXIES = {0, 1};
    constexpr std::array<std::uint8_t, 2> FLAGS_7629 = {0, 1};
    constexpr std::array<std::uint8_t, 5> JUMPS = {0x1F, 0x3F, 0x7F, 0x05, 0xFF};
    std::uint64_t calls = 0;
    for (const std::uint8_t witchspace : WITCHSPACE)
      for (const std::uint8_t maskJumps : MASK_JUMPS)
        for (const std::uint8_t galaxy : GALAXIES)
          for (const std::uint8_t flag : FLAGS_7629)
            for (const std::uint8_t jumps : JUMPS)
            {
              guest.Set(DS.witchspaceCountdown, witchspace);
              guest.Set(DS.maskSystemJumps, maskJumps);
              guest.Set(DS.fledMaskShip, 0);
              guest.Set(DS.galaxyNumber, galaxy);
              guest.Set(DS.data7629, flag);
              guest.Set(DS.missionJumpCount, jumps);
              guest.Set(DS.missionNumber, 0);
              rig.Call(UPDATE_MISSION_SCHEDULE, {.ax = 0xA55A});
              ++calls;
            }
    rig.AssertAllAgreed(UPDATE_MISSION_SCHEDULE, calls);
    AssertExecuted(
      rig, UPDATE_MISSION_SCHEDULE,
      {0x495A, 0x4962, 0x4966, 0x4968, 0x497C, 0x4980, 0x4985, 0x4987, 0x4989, 0x498B, 0x4990, 0x4992, 0x4997, 0x4999, 0x499B, 0x499E});
  }

  // Arrivals in witch space (SetUpLocalSpace stops at CS:2A51), during a Thargoid invasion (CS:2B3B), at a
  // high-tech system (CS:2AE5), with no message showing (CS:0A4A), from a graphics screen other than the
  // cockpit (ShowCockpitScreen clears it, CS:7BC7-7C11), and from random states that leave the station
  // behind the player (GetPositionScaleShift, CS:436F).
  TEST_METHOD(ArrivalAgreesInWitchSpaceAndOutside)
  {
    ComparisonRig rig("ArriveInSystem");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<Arrival, 8> ARRIVALS = {{{0, 0, 3, 5, 0},
                                                  {1, 0, 3, 5, 0},
                                                  {0, 1, 12, 0x0100, 1},
                                                  {0, 0, 9, 0, 0},
                                                  {0, 1, 8, 0x0200, 0},
                                                  {0, 0, 2, 0, 1},
                                                  {2, 1, 14, 0, 1},
                                                  {0, 0, 7, 0x10, 0}}};
    Words words;
    for (const Arrival& arrival : ARRIVALS)
    {
      guest.Set(DS.witchspaceCountdown, arrival.witchspaceCountdown);
      guest.Set(DS.thargoidInvasionActive, arrival.thargoidInvasionActive);
      guest.Set(DS.currentTechLevel, arrival.techLevel);
      guest.Set(DS.messageFrames, arrival.messageFrames);
      guest.Set(DS.screenLayout, arrival.screenLayout);
      guest.Set(DS.randomState0, words.Next());
      guest.Set(DS.randomState1, words.Next());
      guest.Set(DS.randomState2, words.Next());
      rig.Call(ARRIVE_IN_SYSTEM, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
    }
    rig.AssertAllAgreed(ARRIVE_IN_SYSTEM, ARRIVALS.size());
    AssertExecuted(rig, ARRIVE_IN_SYSTEM,
                   {0x0A4A, 0x0A4C, 0x2A51, 0x2A56, 0x2AE5, 0x2B3B, 0x2B3F, 0x2B43, 0x2B47, 0x2B4B, 0x2B4F, 0x436F, 0x4371, 0x4373, 0x4376,
                    0x7BC7, 0x7BCC, 0x7BD4, 0x7BD7, 0x7BDC, 0x7BDD, 0x7BE0, 0x7BE2, 0x7BE5, 0x7BE7, 0x7BE9, 0x7BEB, 0x7BEE, 0x7BEF, 0x7BF1,
                    0x7BF4, 0x7BF7, 0x7BF9, 0x7BFB, 0x7BFE, 0x7C00, 0x7C03, 0x7C05, 0x7C07, 0x7C09, 0x7C0C, 0x7C0F, 0x7C11});
  }

  // From a text screen, ShowCockpitScreen sets the graphics mode through the BIOS (SetGraphicsMode,
  // CS:7BCE-7BD1 and CS:7CFE-7D10), which a comparison cannot undo: the call stands, uncompared.
  TEST_METHOD(ArrivalFromATextScreenCannotBeCompared)
  {
    ComparisonRig rig("ArriveInSystemFromText");
    Elite::Guest guest = GuestOf(rig);
    guest.Set(DS.witchspaceCountdown, 0);
    guest.Set(DS.screenLayout, 2);
    rig.Call(ARRIVE_IN_SYSTEM, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
    const auto& hooks = rig.Host().Native().Hooks();
    const auto found = hooks.find(Machine::Memory::Linear(rig.Program().loadSegment, ARRIVE_IN_SYSTEM));
    Assert::IsTrue(found != hooks.end(), L"the routine is ported");
    Assert::AreEqual(std::uint64_t{1}, found->second.calls);
    Assert::AreEqual(std::uint64_t{1}, found->second.unverifiable, L"the BIOS call cannot be undone");
    Assert::IsTrue(rig.Host().Native().Mismatches().empty(), L"nothing compared, nothing differs");
  }

  // Rings of every radius the tunnel draws, waiting, growing past the largest and never drawn; the
  // reset, forwards and with the direction flag set.
  TEST_METHOD(HyperspaceRingsAgreeAtEverySize)
  {
    ComparisonRig rig("DrawHyperspaceRings");
    Elite::Guest guest = GuestOf(rig);
    std::uint64_t frames = 0;
    for (std::uint8_t first = 0x0E; first < HYPERSPACE_RING_LARGEST + 4; first = static_cast<std::uint8_t>(first + HYPERSPACE_RING_COUNT))
    {
      for (std::uint16_t ring = 0; ring < HYPERSPACE_RING_COUNT; ++ring)
      {
        const auto entry = static_cast<std::uint16_t>(DS.hyperspaceRings.offset + ring * HYPERSPACE_RING_BYTES);
        guest.SetByte(entry, static_cast<std::uint8_t>(ring % 3 == 2 ? 1 : 0));
        guest.SetByte(static_cast<std::uint16_t>(entry + 1), static_cast<std::uint8_t>(first + ring));
        guest.SetByte(static_cast<std::uint16_t>(entry + 2), static_cast<std::uint8_t>(1 + ring % 3));
      }
      rig.Call(DRAW_HYPERSPACE_RINGS, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
      rig.Call(DRAW_HYPERSPACE_RINGS, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
      frames += 2;
    }
    rig.AssertAllAgreed(DRAW_HYPERSPACE_RINGS, frames);
    AssertExecuted(rig, DRAW_HYPERSPACE_RINGS, {0x1654, 0x1658, 0x165C, 0x17DA, 0x17DC, 0x17E2});

    rig.Call(RESET_HYPERSPACE_RINGS, {.ax = 0x1111, .cx = 0x3333, .si = 0x5555, .di = 0x6666});
    Machine::Registers& regs = rig.Host().Processor().Regs();
    regs.flags = static_cast<std::uint16_t>(regs.flags | Machine::FLAG_DIRECTION);
    rig.Call(RESET_HYPERSPACE_RINGS, {.ax = 0x1111, .cx = 0x3333, .si = 0x5555, .di = 0x6666});
    regs.flags = static_cast<std::uint16_t>(regs.flags & ~Machine::FLAG_DIRECTION);
    rig.AssertAllAgreed(RESET_HYPERSPACE_RINGS, 2);
  }

  // Records of several lengths latched, and every countdown digit with the 10 that takes two.
  TEST_METHOD(TargetAndCountdownAgree)
  {
    ComparisonRig rig("LatchHyperspaceTarget");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<std::uint16_t, 4> RECORD_BYTES = {1, 2, 25, 0x100};
    for (const std::uint16_t bytes : RECORD_BYTES)
    {
      guest.Set(DS.systemRecordBytes, bytes);
      guest.Set(DS.selectedSystemIndex, static_cast<std::uint8_t>(bytes * 7));
      rig.Call(LATCH_HYPERSPACE_TARGET, {.ax = 0xA55A, .cx = 0x3333, .si = 0x5555, .di = 0x6666});
    }
    rig.AssertAllAgreed(LATCH_HYPERSPACE_TARGET, RECORD_BYTES.size());
    constexpr std::uint8_t COUNTDOWNS = 16;
    for (std::uint8_t countdown = 0; countdown < COUNTDOWNS; ++countdown)
    {
      guest.Set(DS.hyperspaceCountdown, countdown);
      rig.Call(SHOW_HYPERSPACE_COUNTDOWN, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444});
    }
    rig.AssertAllAgreed(SHOW_HYPERSPACE_COUNTDOWN, COUNTDOWNS);
  }
};

} // namespace GameLogicTests
