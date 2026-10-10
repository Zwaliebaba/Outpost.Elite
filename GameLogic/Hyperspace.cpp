#include "pch.h"

#include "Hyperspace.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

// The original routines these call, which other subsystems port.
constexpr std::uint16_t NEXT_RANDOM = 0x061C;
constexpr std::uint16_t DRAW_CIRCLE = 0x1AC1;
constexpr std::uint16_t SET_UP_LOCAL_SPACE = 0x29D0;
constexpr std::uint16_t COMPUTE_ANGLES_TO_OBJECT = 0x4ECF;
constexpr std::uint16_t START_BEEP = 0x7A57;

// ArriveInSystem: the slots it moves (sun, planet, station), 64 bytes apart.
constexpr std::uint8_t ARRIVAL_SLOTS = 3;
constexpr std::uint16_t SLOT_BYTES = 0x40;
constexpr std::uint16_t ARRIVAL_OFFSET_MASK = 0x1FF;
constexpr std::uint16_t ARRIVAL_OFFSET_LEAST = 0x200;
constexpr std::uint16_t ANGLE_MASK = 0x7FF;

constexpr std::uint16_t HYPERSPACE_RING_WORDS = 15;
constexpr std::uint16_t HYPERSPACE_RING_COUNT = 10;
constexpr std::uint16_t HYPERSPACE_RING_BYTES = 3;
constexpr std::uint8_t HYPERSPACE_RING_LARGEST = 0x96;
constexpr std::uint8_t HYPERSPACE_RING_SMALLEST_DRAWN = 0x14;
// The space view's centre, where the rings are drawn.
constexpr std::uint16_t SPACE_VIEW_CENTER_X = 0x80;
constexpr std::uint16_t SPACE_VIEW_CENTER_ROW = 0x40;

// missionJumpCount's milestones, and the mission each starts.
constexpr std::uint8_t FIRST_MISSION_JUMPS = 0x20;
constexpr std::uint8_t SECOND_MISSION_JUMPS = 0x40;
constexpr std::uint8_t THIRD_MISSION_JUMPS = 0x80;

constexpr std::uint8_t COUNTDOWN_TEN = 10;
constexpr std::uint16_t COUNTDOWN_MESSAGE_FRAMES = 10;

[[nodiscard]] constexpr std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word);
}

[[nodiscard]] constexpr std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

[[nodiscard]] constexpr std::uint16_t MakeWord(std::uint8_t _low, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>(_low | (_high << 8));
}

void SetLow(std::uint16_t& _word, std::uint8_t _value) noexcept
{
  _word = MakeWord(_value, High(_word));
}

void SetHigh(std::uint16_t& _word, std::uint8_t _value) noexcept
{
  _word = MakeWord(Low(_word), _value);
}

// The step MOVSW takes, backwards with the direction flag set.
[[nodiscard]] std::uint16_t WordStep(const Machine::Registers& _regs) noexcept
{
  return (_regs.flags & Machine::FLAG_DIRECTION) != 0 ? static_cast<std::uint16_t>(0xFFFE) : static_cast<std::uint16_t>(2);
}

// NextRandom made an offset of 200h-3FFh in AX, negated when the number's top bit is set (mov dh,ah;
// shl dh,1 leaves that bit in CF).
void RandomArrivalOffset(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(NEXT_RANDOM);
  const std::uint8_t top = High(regs.ax);
  SetHigh(regs.dx, static_cast<std::uint8_t>(top << 1));
  regs.ax = static_cast<std::uint16_t>((regs.ax & ARRIVAL_OFFSET_MASK) + ARRIVAL_OFFSET_LEAST);
  if ((top & 0x80) != 0)
  {
    regs.ax = static_cast<std::uint16_t>(0u - regs.ax);
  }
}

// add [_low], low byte of _value; adc [_high], high byte.
void AddCarried(Guest& _guest, std::uint16_t _low, std::uint16_t _high, std::uint16_t _value)
{
  const auto sum = static_cast<unsigned>(_guest.Byte(_low) + Low(_value));
  _guest.SetByte(_low, static_cast<std::uint8_t>(sum));
  _guest.SetByte(_high, static_cast<std::uint8_t>(_guest.Byte(_high) + High(_value) + (sum >> 8)));
}

} // namespace

void ArriveInSystem(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(SET_UP_LOCAL_SPACE);
  if (_guest.Get(DS.witchspaceCountdown) != 0)
  {
    return;
  }
  _guest.Call(NEXT_RANDOM);
  regs.cx = regs.ax;
  RandomArrivalOffset(_guest);
  regs.bx = regs.ax;
  RandomArrivalOffset(_guest);
  // xchg cx,ax; cwd; xchg cx,ax: DL is the first number's sign, which carries into the z offset's top.
  regs.dx = (regs.cx & 0x8000) != 0 ? static_cast<std::uint16_t>(0xFFFF) : static_cast<std::uint16_t>(0);
  SetHigh(regs.dx, ARRIVAL_SLOTS);
  regs.di = DS.shipSlots.offset;
  do
  {
    AddCarried(_guest, static_cast<std::uint16_t>(regs.di + 5), static_cast<std::uint16_t>(regs.di + 1), regs.ax);
    AddCarried(_guest, static_cast<std::uint16_t>(regs.di + 7), static_cast<std::uint16_t>(regs.di + 2), regs.bx);
    const auto z = static_cast<std::uint16_t>(regs.di + 8);
    const std::uint32_t sum = std::uint32_t{_guest.Word(z)} + regs.cx;
    _guest.SetWord(z, static_cast<std::uint16_t>(sum));
    const auto top = static_cast<std::uint16_t>(regs.di + 3);
    _guest.SetByte(top, static_cast<std::uint8_t>(_guest.Byte(top) + Low(regs.dx) + (sum >> 16)));
    regs.di = static_cast<std::uint16_t>(regs.di + SLOT_BYTES);
    SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) - 1));
  } while (High(regs.dx) != 0);
  regs.di = DS.stationSlot.offset;
  _guest.Call(COMPUTE_ANGLES_TO_OBJECT);
  _guest.Set(DS.playerPitchAngle, regs.ax);
  _guest.Set(DS.playerYawAngle, regs.bx);
  _guest.Call(NEXT_RANDOM);
  regs.ax = static_cast<std::uint16_t>(regs.ax & ANGLE_MASK);
  _guest.Set(DS.playerRollAngle, regs.ax);
}

void ResetHyperspaceRings(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.hyperspaceRingStart.offset;
  regs.ax = regs.ds;
  regs.es = regs.ax;
  regs.di = DS.hyperspaceRings.offset;
  for (regs.cx = HYPERSPACE_RING_WORDS; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarWord(regs.es, regs.di, _guest.FarWord(regs.ds, regs.si));
    regs.si = static_cast<std::uint16_t>(regs.si + WordStep(regs));
    regs.di = static_cast<std::uint16_t>(regs.di + WordStep(regs));
  }
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
}

void DrawHyperspaceRings(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.hyperspaceRings.offset;
  for (regs.cx = HYPERSPACE_RING_COUNT; regs.cx != 0; --regs.cx)
  {
    const std::uint8_t delay = _guest.Byte(regs.si);
    const auto radius = static_cast<std::uint16_t>(regs.si + 1);
    if (delay != 0)
    {
      _guest.SetByte(regs.si, static_cast<std::uint8_t>(delay - 1));
    }
    else
    {
      SetLow(regs.bx, _guest.Byte(radius));
      if (Low(regs.bx) < HYPERSPACE_RING_LARGEST)
      {
        // Grows by an eighth, at least 1.
        SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.bx) >> 3));
        if (Low(regs.ax) == 0)
        {
          SetLow(regs.ax, 1);
        }
        SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + Low(regs.ax)));
        _guest.SetByte(radius, Low(regs.bx));
        if (Low(regs.bx) >= HYPERSPACE_RING_SMALLEST_DRAWN)
        {
          const std::uint16_t count = regs.cx;
          const std::uint16_t ring = regs.si;
          regs.cx = SPACE_VIEW_CENTER_X;
          regs.dx = SPACE_VIEW_CENTER_ROW;
          SetLow(regs.ax, _guest.Byte(static_cast<std::uint16_t>(ring + 2)));
          _guest.Set(DS.drawColor, Low(regs.ax));
          _guest.Call(DRAW_CIRCLE);
          regs.si = ring;
          regs.cx = count;
        }
      }
    }
    regs.si = static_cast<std::uint16_t>(regs.si + HYPERSPACE_RING_BYTES);
  }
}

void UpdateMissionSchedule(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.witchspaceCountdown) != 0)
  {
    return;
  }
  if (_guest.Get(DS.maskSystemJumps) != 0)
  {
    const auto maskJumps = static_cast<std::uint8_t>(_guest.Get(DS.maskSystemJumps) - 1);
    _guest.Set(DS.maskSystemJumps, maskJumps);
    if (maskJumps == 0)
    {
      _guest.Set(DS.fledMaskShip, 1);
    }
  }
  if (_guest.Get(DS.galaxyNumber) == 0 && _guest.Get(DS.data7629) != 1)
  {
    return;
  }
  const auto jumps = static_cast<std::uint8_t>(_guest.Get(DS.missionJumpCount) + 1);
  _guest.Set(DS.missionJumpCount, jumps);
  SetLow(regs.ax, 1);
  if (jumps != FIRST_MISSION_JUMPS)
  {
    SetLow(regs.ax, 2);
    if (jumps != SECOND_MISSION_JUMPS)
    {
      SetLow(regs.ax, 3);
      if (jumps != THIRD_MISSION_JUMPS)
      {
        return;
      }
    }
  }
  _guest.Set(DS.missionNumber, Low(regs.ax));
}

void LatchHyperspaceTarget(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, _guest.Get(DS.selectedSystemIndex));
  _guest.Set(DS.hyperspaceTargetIndex, Low(regs.ax));
  regs.si = DS.selectedSystemName.offset;
  regs.di = DS.hyperspaceTargetRecord.offset;
  regs.cx = _guest.Get(DS.systemRecordBytes);
  // loop: a count of 0 copies 65536 bytes.
  do
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.si;
    ++regs.di;
  } while (--regs.cx != 0);
}

void ShowHyperspaceCountdown(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(START_BEEP);
  const std::uint8_t countdown = _guest.Get(DS.hyperspaceCountdown);
  // Two characters: "10", or a space and the digit.
  regs.ax = countdown == COUNTDOWN_TEN ? MakeWord('1', '0') : MakeWord(' ', static_cast<std::uint8_t>(countdown + '0'));
  _guest.Set(DS.hyperspaceCountdownDigits, regs.ax);
  regs.ax = DS.hyperspaceCountdownMessage.offset;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, COUNTDOWN_MESSAGE_FRAMES);
  _guest.Set(DS.messageShown, 0);
}

namespace
{

using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_SI;

// UpdateMissionSchedule and LatchHyperspaceTarget clobber AL but keep AH: AX is compared whole, and the
// ports leave AL as the original does.
constexpr std::array ENTRIES = {
  NativeEntry{0x2B5A, "ArriveInSystem", &ArriveInSystem, Machine::NativeContract{REGISTER_ALL, 0}},
  NativeEntry{0x48AB, "ResetHyperspaceRings", &ResetHyperspaceRings,
              Machine::NativeContract{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0}},
  NativeEntry{0x48C0, "DrawHyperspaceRings", &DrawHyperspaceRings, Machine::NativeContract{REGISTER_ALL, 0}},
  NativeEntry{0x4953, "UpdateMissionSchedule", &UpdateMissionSchedule, PRESERVES_ALL},
  NativeEntry{0x49F6, "LatchHyperspaceTarget", &LatchHyperspaceTarget, Machine::NativeContract{REGISTER_CX | REGISTER_SI | REGISTER_DI, 0}},
  NativeEntry{0x8C62, "ShowHyperspaceCountdown", &ShowHyperspaceCountdown, Machine::NativeContract{REGISTER_AX, 0}},
};

} // namespace

std::span<const NativeEntry> HyperspaceEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
