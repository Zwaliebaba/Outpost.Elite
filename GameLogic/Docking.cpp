#include "pch.h"

#include "Docking.h"

#include "DataOverlay.h"
#include "Maths.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t DRAW_LINE = 0x16D1;

constexpr std::uint16_t TUNNEL_EDGES = 4;
constexpr std::uint8_t VIEW_CENTER_X = 0x80;
constexpr std::uint8_t VIEW_CENTER_ROW = 0x40;
constexpr std::uint16_t VIEW_ROW_BYTES = 0x40;
constexpr std::uint16_t VIEW_LAST_WORD = 0x1FFE; // of the buffer at DS:0000, rows 0-127

constexpr std::uint16_t HALF_TURN = 0x400; // in 2048ths
constexpr std::uint16_t ANGLE_MASK = 0x7FF;
constexpr std::uint16_t STATION_SPIN_ANGLE = 0x0E; // in the station's slot
constexpr std::uint8_t SHIP_TYPE_MASK = 0x1F;      // of the slot's first byte, shifted right once

[[nodiscard]] std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word);
}

[[nodiscard]] std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

[[nodiscard]] std::uint16_t WithLow(std::uint16_t _word, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_word & 0xFF00) | _low);
}

[[nodiscard]] bool Carry(Guest& _guest) noexcept
{
  return (_guest.Regs().flags & Machine::FLAG_CARRY) != 0;
}

// A point about the view's centre, as DrawLine takes it: x and row from the top-left.
[[nodiscard]] std::uint16_t FromCenter(std::uint16_t _point) noexcept
{
  const auto x = static_cast<std::uint8_t>(Low(_point) + VIEW_CENTER_X);
  const auto row = static_cast<std::uint8_t>(High(_point) + VIEW_CENTER_ROW);
  return static_cast<std::uint16_t>((row << 8) | x);
}

// REP STOSB or REP STOSW of AX at ES:DI, CX times, backwards when _backward.
void Store(Guest& _guest, std::uint16_t _bytes, bool _backward)
{
  Machine::Registers& regs = _guest.Regs();
  const auto step = static_cast<std::uint16_t>(_backward ? 0u - _bytes : _bytes);
  for (; regs.cx != 0; --regs.cx)
  {
    if (_bytes == 2)
    {
      _guest.SetFarWord(regs.es, regs.di, regs.ax);
    }
    else
    {
      _guest.SetFarByte(regs.es, regs.di, Low(regs.ax));
    }
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
}

} // namespace

void DrawTunnelRectangle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = TUNNEL_EDGES;
  do
  {
    const std::uint16_t edgesLeft = regs.cx;
    const std::uint16_t point = regs.si;
    regs.dx = FromCenter(_guest.Word(point));
    regs.cx = FromCenter(_guest.Word(static_cast<std::uint16_t>(point + 2)));
    _guest.Call(DRAW_LINE);
    regs.si = static_cast<std::uint16_t>(point + 2);
    regs.cx = static_cast<std::uint16_t>(edgesLeft - 1);
  } while (regs.cx != 0);
}

void CheckDockingAlignment(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // Pitch near 0 wants yaw near a half turn, and pitch near a half turn wants yaw near 0.
  regs.ax = _guest.Get(DS.playerPitchAngle);
  regs.cx = 0;
  AngleWithinTolerance(_guest);
  if (Carry(_guest))
  {
    regs.cx = HALF_TURN;
  }
  else
  {
    regs.cx = HALF_TURN;
    AngleWithinTolerance(_guest);
    if (!Carry(_guest))
    {
      return;
    }
    regs.cx = 0;
  }
  regs.ax = _guest.Get(DS.playerYawAngle);
  AngleWithinTolerance(_guest);
  if (!Carry(_guest))
  {
    return;
  }

  // Roll near the station's spin, negated for ship type 0, or half a turn from it.
  regs.ax = _guest.Get(DS.playerRollAngle);
  regs.cx = _guest.Word(static_cast<std::uint16_t>(regs.di + STATION_SPIN_ANGLE));
  regs.dx = WithLow(regs.dx, static_cast<std::uint8_t>((_guest.Byte(regs.di) >> 1) & SHIP_TYPE_MASK));
  if (Low(regs.dx) == 0)
  {
    regs.cx = static_cast<std::uint16_t>(0u - regs.cx);
  }
  regs.cx &= ANGLE_MASK;
  AngleWithinTolerance(_guest);
  if (Carry(_guest))
  {
    return;
  }
  regs.cx = static_cast<std::uint16_t>((regs.cx + HALF_TURN) & ANGLE_MASK);
  AngleWithinTolerance(_guest);
}

void MaskOutsideTunnel(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The margins: x/4 bytes either side, and the rows above and below the rectangle.
  regs.dx = static_cast<std::uint8_t>(static_cast<std::uint8_t>(_guest.Byte(regs.si) + VIEW_CENTER_X) >> 2);
  const std::uint8_t top = _guest.Byte(static_cast<std::uint16_t>(regs.si + 1));
  regs.bx = static_cast<std::uint8_t>(static_cast<std::uint8_t>(0u - top) << 1);
  regs.bp = static_cast<std::uint16_t>(static_cast<std::uint8_t>(top + VIEW_CENTER_ROW) << 5);
  regs.es = regs.ds;
  regs.ax = 0;
  regs.di = 0;
  regs.cx = regs.bp;
  Store(_guest, 2, (regs.flags & Machine::FLAG_DIRECTION) != 0);
  const std::uint16_t rows = regs.bx;
  do
  {
    const std::uint16_t row = regs.di;
    regs.cx = regs.dx;
    Store(_guest, 1, (regs.flags & Machine::FLAG_DIRECTION) != 0);
    regs.di = static_cast<std::uint16_t>(row + VIEW_ROW_BYTES);
    --regs.bx;
  } while (regs.bx != 0);
  regs.bx = rows;

  // The same from the end of the buffer, backwards.
  regs.di = VIEW_LAST_WORD;
  _guest.SetFlag(Machine::FLAG_DIRECTION, true);
  regs.cx = regs.bp;
  Store(_guest, 2, true);
  ++regs.di;
  do
  {
    const std::uint16_t row = regs.di;
    regs.cx = regs.dx;
    Store(_guest, 1, true);
    regs.di = static_cast<std::uint16_t>(row - VIEW_ROW_BYTES);
    --regs.bx;
  } while (regs.bx != 0);
  _guest.SetFlag(Machine::FLAG_DIRECTION, false);
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = Guest::VIDEO_SEGMENT;
}

namespace
{

using Machine::FLAG_CARRY;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract CLOBBERS_ALL{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0};
constexpr Machine::NativeContract CLOBBERS_ALL_BUT_ES{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0};
constexpr Machine::NativeContract ALIGNMENT{REGISTER_AX | REGISTER_CX | REGISTER_DX, FLAG_CARRY};

constexpr std::array ENTRIES = {
  NativeEntry{0x1AA0, "DrawTunnelRectangle", &DrawTunnelRectangle, CLOBBERS_ALL},
  NativeEntry{0x2D0F, "CheckDockingAlignment", &CheckDockingAlignment, ALIGNMENT},
  NativeEntry{0x2E0A, "MaskOutsideTunnel", &MaskOutsideTunnel, CLOBBERS_ALL_BUT_ES},
};

} // namespace

std::span<const NativeEntry> DockingEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
