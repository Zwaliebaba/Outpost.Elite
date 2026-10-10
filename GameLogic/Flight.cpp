#include "pch.h"

#include "Flight.h"

#include "DataOverlay.h"
#include "Maths.h"

#include <utility>

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_DIRECTION;
using Machine::FLAG_ZERO;
using Machine::Registers;

// The original routines these call, which are not ported here.
constexpr std::uint16_t GET_VIEW_LASER = 0x066C;
constexpr std::uint16_t GET_PREVIOUS_DUST_SCREEN_POSITION = 0x08A1;
constexpr std::uint16_t SHIFT_STARDUST_SIDEWAYS = 0x08CB;
constexpr std::uint16_t IS_DUST_NEAR_CENTER = 0x09EE;
constexpr std::uint16_t SAVE_STARDUST_POSITIONS = 0x0A88;
constexpr std::uint16_t SHOW_GALACTIC_CHART = 0x0CAE;
constexpr std::uint16_t SHOW_SHORT_RANGE_CHART = 0x0E52;
constexpr std::uint16_t LOAD_SYSTEM_SEEDS = 0x139C;
constexpr std::uint16_t RESTORE_FLIGHT_SCREEN = 0x15CF;
constexpr std::uint16_t PLOT_PIXEL = 0x15E0;
constexpr std::uint16_t DRAW_LINE = 0x16D1;
constexpr std::uint16_t TAKE_DAMAGE = 0x2C9B;
constexpr std::uint16_t CHECK_DOCKING_ALIGNMENT = 0x2D0F;
constexpr std::uint16_t VECTOR_LENGTH = 0x2E96;
constexpr std::uint16_t DETONATE_ENERGY_BOMB = 0x2ED6;
constexpr std::uint16_t LAUNCH_ESCAPE_POD = 0x2F0F;
constexpr std::uint16_t OBJECT_WITHIN_BOX = 0x2F65;
constexpr std::uint16_t VECTOR_WITHIN_BOX = 0x2F6E;
constexpr std::uint16_t DRAW_SCREEN_CHAR = 0x31F9;
constexpr std::uint16_t UPDATE_MESSAGE_LINE = 0x35A3;
constexpr std::uint16_t SHOW_SHIP_IDENTITY = 0x364D;
constexpr std::uint16_t IS_OBJECT_NEAR = 0x3B9A;
constexpr std::uint16_t ROTATE_PITCH_YAW_ROLL = 0x3EAC;
constexpr std::uint16_t TRANSFORM_TO_VIEW = 0x3EE3;
constexpr std::uint16_t ROTATE_BY_SIN_COS_7210 = 0x3F02;
constexpr std::uint16_t IS_SUN_OR_PLANET = 0x3F2A;
constexpr std::uint16_t IS_STATION = 0x3F40;
constexpr std::uint16_t GET_POSITION_SCALE_SHIFT = 0x4333;
constexpr std::uint16_t SCALE_POSITION_DOWN = 0x439E;
constexpr std::uint16_t IS_OBJECT_NEAR_KEEP_BLIP = 0x460E;
constexpr std::uint16_t RESET_HYPERSPACE_RINGS = 0x48AB;
constexpr std::uint16_t LATCH_HYPERSPACE_TARGET = 0x49F6;
constexpr std::uint16_t REMOVE_OBJECT = 0x4F98;
constexpr std::uint16_t REMOVE_ALL_MISSILES = 0x4F9F;
constexpr std::uint16_t LAUNCH_PLAYER_MISSILE = 0x5242;
constexpr std::uint16_t CLEAR_ALL_OBJECTS = 0x52B2;
constexpr std::uint16_t SHOW_SYSTEM_DATA_SCREEN = 0x5CDE;
constexpr std::uint16_t SHOW_MARKET_PRICES_SCREEN = 0x5E2C;
constexpr std::uint16_t SHOW_COMMANDER_STATUS_SCREEN = 0x5EA9;
constexpr std::uint16_t SHOW_INVENTORY_SCREEN = 0x6020;
constexpr std::uint16_t READ_FIRE_BUTTON = 0x74E0;
constexpr std::uint16_t READ_STEERING = 0x7536;
constexpr std::uint16_t RESET_KEYBOARD = 0x7668;
constexpr std::uint16_t START_BEEP = 0x7A57;
constexpr std::uint16_t START_LOW_BEEP = 0x7A5D;
constexpr std::uint16_t START_IMPACT_SOUND = 0x7AC3;
constexpr std::uint16_t SHOW_COCKPIT_SCREEN = 0x7BC0;
constexpr std::uint16_t TOGGLE_DOCKING_COMPUTER = 0x83B2;
constexpr std::uint16_t DRAIN_ENERGY = 0x839F;
constexpr std::uint16_t ENGAGE_JUMP_DRIVE = 0x8430;
constexpr std::uint16_t RUN_DOCKING_COMPUTER = 0x8622;
constexpr std::uint16_t CHECK_MISSILE_TARGET_DESTROYED = 0x8B8B;
constexpr std::uint16_t CREDIT_KILL = 0x8BC6;
constexpr std::uint16_t SHOW_HYPERSPACE_COUNTDOWN = 0x8C62;
constexpr std::uint16_t RUN_PAUSE_SCREEN = 0x8D6A;
constexpr std::uint16_t USE_MASKING_DEVICE = 0x8ECF;
constexpr std::uint16_t APPLY_REVERSE_CONTROLS = 0x8EA5;
constexpr std::uint16_t APPLY_REVERSE_CONTROLS_TO_DX = 0x8EBA;
constexpr std::uint16_t FIND_SHIP_IN_CROSSHAIRS = 0x8A46;

// Where two of these routines wait for a key with GetKey. Native code cannot wait yet (ADR-010 item
// 8), so from there the original runs the rest of the routine, to its return.
constexpr std::uint16_t FLIGHT_SCREEN_KEY_WAIT = 0x0BF3;
constexpr std::uint16_t FROZEN_KEY_WAIT = 0x8153;

// DivideOverflowInterrupt's scratch words in the code segment, and what it leaves in AL or AX.
constexpr std::uint16_t DIVIDE_SAVED_BX_OFFSET = 0x02A1;
constexpr std::uint16_t DIVIDE_SAVED_DS_OFFSET = 0x02A3;
constexpr std::uint8_t DIVIDE_OVERFLOW_BYTE = 0x7F;
constexpr std::uint16_t DIVIDE_OVERFLOW_WORD = 0x7FFF;

// The stardust: 30 particles of 6 bytes, x and y words, a lifetime byte and a spare; stardustPrevious
// follows at +0xB4 with the same layout, its lifetime copy at +0xB8 and its new-particle flag at +0xB9.
constexpr std::uint16_t STARDUST_COUNT = 30;
constexpr std::uint16_t PARTICLE_BYTES = 6;
constexpr std::uint16_t PARTICLE_LIFETIME = 4;
constexpr std::uint16_t PREVIOUS_X = 0xB4;
constexpr std::uint16_t PREVIOUS_Y = 0xB6;
constexpr std::uint16_t PREVIOUS_LIFETIME = 0xB8;
constexpr std::uint16_t PREVIOUS_NEW = 0xB9;

// viewAngle for each view, in 2048ths of a turn; the front view is 0.
constexpr std::uint16_t LEFT_VIEW = 0x200;
constexpr std::uint16_t REAR_VIEW = 0x400;
constexpr std::uint16_t RIGHT_VIEW = 0x600;

// An object slot's fields.
constexpr std::uint16_t SLOT_BYTES = 0x40;
constexpr std::uint16_t SLOT_X = 4;
constexpr std::uint16_t SLOT_Y = 6;
constexpr std::uint16_t SLOT_Z = 8;
constexpr std::uint16_t SLOT_COLLIDED = 0x0C;
constexpr std::uint16_t SLOT_FLAGS = 0x1E;
constexpr std::uint16_t SLOT_VIEW_X = 0x20;
constexpr std::uint16_t SLOT_VIEW_Y = 0x22;
constexpr std::uint16_t SLOT_VIEW_Z = 0x24;
constexpr std::uint16_t SLOT_BLIP = 0x26; // the scanner blip, or the compass dot: x, y, z bytes
constexpr std::uint16_t SLOT_CLASS = 0x33;
constexpr std::uint16_t SLOT_SCANNED = 0x34;
constexpr std::uint16_t SLOT_SCALE_SHIFT = 0x3D;
constexpr std::uint8_t FLAG_BLIP_DRAWN = 0x02;
constexpr std::uint16_t DEBRIS_SLOTS = 0x6E30;

// The dashboard in CGA memory: the line DI names, then the next odd-bank lines and the even-bank lines
// between them.
constexpr std::array<std::uint16_t, 5> FIVE_LINES = {0x0000, 0x0050, 0x00A0, 0xE050, 0xE0A0};
constexpr std::array<std::uint16_t, 3> THREE_LINES = {0x0000, 0x0050, 0xE050};
constexpr std::uint16_t EVEN_BANK = 0x2000;
constexpr std::uint8_t BAR_VALUE_BYTE = 0x55;
constexpr std::uint8_t BAR_REST_BYTE = 0xAA;
constexpr std::uint8_t BAR_PIXELS = 0x30;
constexpr std::uint16_t BACKGROUND_WORD = 0xAAAA;
constexpr std::uint16_t DASHBOARD_ORIGIN = 0x1698;

constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F4 = 0x3E;
constexpr std::uint8_t SCAN_F5 = 0x3F;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F7 = 0x41;
constexpr std::uint8_t SCAN_F8 = 0x42;
constexpr std::uint8_t SCAN_F9 = 0x43;
constexpr std::uint8_t SCAN_F10 = 0x44;

[[nodiscard]] std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word);
}

[[nodiscard]] std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

[[nodiscard]] std::uint16_t Pair(std::uint8_t _high, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_high << 8) | _low);
}

void SetLow(std::uint16_t& _word, int _byte) noexcept
{
  _word = Pair(High(_word), static_cast<std::uint8_t>(_byte));
}

void SetHigh(std::uint16_t& _word, int _byte) noexcept
{
  _word = Pair(static_cast<std::uint8_t>(_byte), Low(_word));
}

[[nodiscard]] std::uint16_t Plus(std::uint16_t _word, int _value) noexcept
{
  return static_cast<std::uint16_t>(_word + _value);
}

[[nodiscard]] std::uint16_t Negate(std::uint16_t _value) noexcept
{
  return static_cast<std::uint16_t>(0u - _value);
}

[[nodiscard]] bool Negative(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0;
}

[[nodiscard]] std::uint16_t SignExtend(std::uint8_t _byte) noexcept // CBW
{
  return static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int8_t>(_byte)));
}

[[nodiscard]] std::uint16_t SignOf(std::uint16_t _value) noexcept // the DX of CWD
{
  return Negative(_value) ? std::uint16_t{0xFFFF} : std::uint16_t{0};
}

// SAR by a count the 8088 does not mask: every bit the sign once the count reaches the width.
[[nodiscard]] std::uint16_t Sar(std::uint16_t _value, unsigned _count) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::int16_t>(_value) >> (_count < 16 ? _count : 15));
}

[[nodiscard]] std::uint8_t SarByte(std::uint8_t _value, unsigned _count) noexcept
{
  return static_cast<std::uint8_t>(static_cast<std::int8_t>(_value) >> (_count < 8 ? _count : 7));
}

[[nodiscard]] bool Carry(Guest& _guest) noexcept
{
  return (_guest.Regs().flags & FLAG_CARRY) != 0;
}

[[nodiscard]] bool Zero(Guest& _guest) noexcept
{
  return (_guest.Regs().flags & FLAG_ZERO) != 0;
}

// What DivideOverflowInterrupt does before it saturates the quotient.
void TrapDivide(Guest& _guest)
{
  _guest.SetCodeWord(DIVIDE_SAVED_BX_OFFSET, _guest.Regs().bx);
  _guest.SetCodeWord(DIVIDE_SAVED_DS_OFFSET, _guest.Regs().ds);
}

// DIV r8: AL = AX / _divisor, AH = the remainder; on overflow the trap leaves AL = 7Fh and AH alone.
void DivideByte(Guest& _guest, std::uint8_t _divisor)
{
  Registers& regs = _guest.Regs();
  if (_divisor == 0 || regs.ax / _divisor > 0xFF)
  {
    TrapDivide(_guest);
    SetLow(regs.ax, DIVIDE_OVERFLOW_BYTE);
    return;
  }
  regs.ax = Pair(static_cast<std::uint8_t>(regs.ax % _divisor), static_cast<std::uint8_t>(regs.ax / _divisor));
}

// DIV r16: AX = DX:AX / _divisor, DX = the remainder; on overflow the trap leaves AX = 7FFFh and DX alone.
void DivideWord(Guest& _guest, std::uint16_t _divisor)
{
  Registers& regs = _guest.Regs();
  const std::uint32_t dividend = (std::uint32_t{regs.dx} << 16) | regs.ax;
  if (_divisor == 0 || dividend / _divisor > 0xFFFF)
  {
    TrapDivide(_guest);
    regs.ax = DIVIDE_OVERFLOW_WORD;
    return;
  }
  regs.ax = static_cast<std::uint16_t>(dividend / _divisor);
  regs.dx = static_cast<std::uint16_t>(dividend % _divisor);
}

// MUL r8: AX = AL * _factor.
void MultiplyByte(Registers& _regs, std::uint8_t _factor) noexcept
{
  _regs.ax = static_cast<std::uint16_t>(Low(_regs.ax) * _factor);
}

[[nodiscard]] std::uint8_t SaturatingAdd(std::uint8_t _value, std::uint8_t _add) noexcept
{
  return _value + _add > 0xFF ? std::uint8_t{0xFF} : static_cast<std::uint8_t>(_value + _add);
}

[[nodiscard]] std::uint8_t SaturatingSubtract(std::uint8_t _value, std::uint8_t _subtract) noexcept
{
  return _value < _subtract ? std::uint8_t{0} : static_cast<std::uint8_t>(_value - _subtract);
}

// mov ax, _text; mov [messagePointer], ax; mov [messageFrames], _frames.
void SetMessage(Guest& _guest, DataAt _text, std::uint16_t _frames)
{
  _guest.Regs().ax = _text.offset;
  _guest.Set(DS.messagePointer, _text.offset);
  _guest.Set(DS.messageFrames, _frames);
}

// SetSinCos0-8 and RotateBySinCos0-8: the pairs of rotationSinCos, by number.
void SetSinCosPair(Guest& _guest, std::size_t _pair)
{
  SetSinCos(_guest, DS.rotationSinCos.At(_pair));
}

void RotateByPair(Guest& _guest, std::size_t _pair)
{
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(_pair));
}

// ---- Stardust -------------------------------------------------------------------------------------

// The lifetime byte, its copy in stardustPrevious, and the copy marked new, for a respawned particle.
void MarkDustRespawned(Guest& _guest, std::uint8_t _lifetime)
{
  const std::uint16_t particle = _guest.Regs().si;
  _guest.SetByte(Plus(particle, PARTICLE_LIFETIME), _lifetime);
  _guest.SetByte(Plus(particle, PREVIOUS_LIFETIME), _lifetime);
  _guest.SetByte(Plus(particle, PREVIOUS_NEW), 1);
}

[[nodiscard]] std::uint8_t RandomLifetime(std::uint16_t _random) noexcept
{
  return static_cast<std::uint8_t>((Low(_random) & 0x1F) + 0x28);
}

// sar 2, keeping AH off 20h (which a 16-bit value shifted twice cannot reach).
[[nodiscard]] std::uint16_t RandomDustX(std::uint16_t _random) noexcept
{
  std::uint16_t x = Sar(_random, 2);
  if (High(x) == 0x20)
  {
    SetHigh(x, 0x1F);
  }
  return x;
}

// The particle as a point, or while the jump drive is engaged a streak from where it was last frame,
// unless it has just respawned.
void DrawDust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  DustToScreen(_guest);
  SetLow(regs.dx, Low(regs.ax));
  SetHigh(regs.dx, Low(regs.bx));
  if (_guest.Get(DS.jumpDriveEngaged) == 0)
  {
    _guest.Call(PLOT_PIXEL);
    return;
  }
  if (_guest.Byte(Plus(regs.si, PREVIOUS_NEW)) != 0)
  {
    return;
  }
  _guest.Call(GET_PREVIOUS_DUST_SCREEN_POSITION);
  if (!Carry(_guest))
  {
    _guest.Call(DRAW_LINE);
  }
}

// UpdateFrontStardust (0x06C0): the dust streams outwards from the centre.
void UpdateFrontStardust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.dx = _guest.Get(DS.rollRate);
  _guest.Call(APPLY_REVERSE_CONTROLS_TO_DX);
  if (High(regs.dx) != 0)
  {
    SetLow(regs.dx, 0);
    regs.dx = Sar(Negate(regs.dx), 1);
    ShiftStardustVertically(_guest);
  }
  regs.ax = _guest.Get(DS.rollRate);
  _guest.Call(APPLY_REVERSE_CONTROLS);
  regs.ax = static_cast<std::uint16_t>(Negate(SignExtend(Low(regs.ax))) << 1);
  if (regs.ax != 0)
  {
    SetSinCosPair(_guest, 7);
    RollStardust(_guest);
  }
  regs.cx = STARDUST_COUNT;
  regs.si = DS.stardust.offset;
  ComputeStardustShift(_guest);
  do
  {
    const std::uint16_t count = regs.cx;
    LoadDustPosition(_guest);
    IsDustOnScreen(_guest);
    if (Carry(_guest))
    {
      if (_guest.Get(DS.playerSpeed) != 0)
      {
        ScaleDustStep(_guest);
        regs.ax = Plus(regs.ax, regs.bp);
        regs.bx = Plus(regs.bx, regs.dx);
      }
      for (;;)
      {
        StoreDustPosition(_guest);
        IsDustOnScreen(_guest);
        if (Carry(_guest))
        {
          break;
        }
        RespawnDustAnywhere(_guest);
        StorePreviousDustPosition(_guest);
      }
      DrawDust(_guest);
    }
    regs.si = Plus(regs.si, PARTICLE_BYTES);
    regs.cx = count;
  } while (--regs.cx != 0);
}

// UpdateRearStardust (0x0742): the dust streams in towards the centre, and respawns when it gets there
// or its lifetime runs out.
void UpdateRearStardust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.dx = _guest.Get(DS.rollRate);
  _guest.Call(APPLY_REVERSE_CONTROLS_TO_DX);
  if (High(regs.dx) != 0)
  {
    SetLow(regs.dx, 0);
    regs.dx = Sar(regs.dx, 1);
    ShiftStardustVertically(_guest);
  }
  regs.ax = _guest.Get(DS.rollRate);
  _guest.Call(APPLY_REVERSE_CONTROLS);
  regs.ax = static_cast<std::uint16_t>(SignExtend(Low(regs.ax)) << 1);
  if (regs.ax != 0)
  {
    SetSinCosPair(_guest, 7);
    RollStardust(_guest);
  }
  regs.cx = STARDUST_COUNT;
  regs.si = DS.stardust.offset;
  ComputeStardustShift(_guest);
  do
  {
    const std::uint16_t count = regs.cx;
    LoadDustPosition(_guest);
    IsDustOnScreen(_guest);
    if (Carry(_guest))
    {
      if (_guest.Get(DS.playerSpeed) != 0)
      {
        ScaleDustStep(_guest);
        regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bp);
        regs.bx = static_cast<std::uint16_t>(regs.bx - regs.dx);
      }
      for (;;)
      {
        StoreDustPosition(_guest);
        _guest.Call(IS_DUST_NEAR_CENTER);
        if (!Carry(_guest))
        {
          const std::uint16_t lifetime = Plus(regs.si, PARTICLE_LIFETIME);
          _guest.SetByte(lifetime, static_cast<std::uint8_t>(_guest.Byte(lifetime) - 1));
          if (_guest.Byte(lifetime) != 0)
          {
            break;
          }
        }
        RespawnDustAnywhere(_guest);
        StorePreviousDustPosition(_guest);
      }
      DrawDust(_guest);
    }
    regs.si = Plus(regs.si, PARTICLE_BYTES);
    regs.cx = count;
  } while (--regs.cx != 0);
}

// 0x084D: the side views only draw; ShiftStardustSideways has moved the dust.
void DrawSideStardust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.cx = STARDUST_COUNT;
  regs.si = DS.stardust.offset;
  do
  {
    const std::uint16_t count = regs.cx;
    const std::uint16_t particle = regs.si;
    LoadDustPosition(_guest);
    IsDustOnScreen(_guest);
    if (Carry(_guest))
    {
      DrawDust(_guest);
    }
    regs.si = Plus(particle, PARTICLE_BYTES);
    regs.cx = count;
  } while (--regs.cx != 0);
}

// UpdateLeftStardust (0x07C5) and UpdateRightStardust (0x0809): pitch rolls the dust and speed moves it
// sideways; _left mirrors both.
void UpdateSideStardust(Guest& _guest, bool _left)
{
  Registers& regs = _guest.Regs();
  regs.dx = _guest.Get(DS.rollRate);
  _guest.Call(APPLY_REVERSE_CONTROLS_TO_DX);
  SetHigh(regs.dx, Low(regs.dx));
  if (High(regs.dx) != 0)
  {
    SetLow(regs.dx, 0);
    regs.dx = Sar(_left ? regs.dx : Negate(regs.dx), 1);
    ShiftStardustVertically(_guest);
  }
  regs.dx = _guest.Get(DS.playerSpeed);
  if (regs.dx != 0)
  {
    if (_left)
    {
      regs.dx = Negate(regs.dx);
    }
    SetHigh(regs.dx, Low(regs.dx));
    SetLow(regs.dx, 0);
    regs.dx = Sar(regs.dx, 3);
    _guest.Call(SHIFT_STARDUST_SIDEWAYS);
  }
  regs.ax = _guest.Get(DS.rollRate);
  _guest.Call(APPLY_REVERSE_CONTROLS);
  SetLow(regs.ax, High(regs.ax));
  regs.ax = static_cast<std::uint16_t>(SignExtend(Low(regs.ax)) << 1);
  if (regs.ax != 0)
  {
    if (_left)
    {
      regs.ax = Negate(regs.ax);
    }
    SetSinCosPair(_guest, 7);
    RollStardust(_guest);
  }
  DrawSideStardust(_guest);
}

// ---- The dashboard --------------------------------------------------------------------------------

template <std::size_t Lines> void FillColumn(Guest& _guest, const std::array<std::uint16_t, Lines>& _lines, std::uint8_t _byte)
{
  Registers& regs = _guest.Regs();
  for (const std::uint16_t line : _lines)
  {
    _guest.SetFarByte(regs.es, Plus(regs.di, line), _byte);
  }
  ++regs.di;
}

void FillFiveLineWord(Guest& _guest, std::uint16_t _word)
{
  Registers& regs = _guest.Regs();
  for (const std::uint16_t line : FIVE_LINES)
  {
    _guest.SetFarWord(regs.es, Plus(regs.di, line), _word);
  }
}

// DrawFiveLineBarPixels (0x264D) and its copy in DrawThreeLineBar: AL of 0-48 pixels in colour 1, the
// partial byte, then colour 2 to the end. When the first run is empty its loop leaves CH alone, and the
// second runs CH:CL times.
template <std::size_t Lines> void DrawBarPixels(Guest& _guest, const std::array<std::uint16_t, Lines>& _lines)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.cx, Low(regs.ax));
  SetLow(regs.dx, BAR_PIXELS - Low(regs.ax));
  SetLow(regs.cx, Low(regs.cx) >> 2);
  if (Low(regs.cx) != 0)
  {
    SetHigh(regs.cx, 0);
    SetLow(regs.bx, BAR_VALUE_BYTE);
    do
    {
      FillColumn(_guest, _lines, Low(regs.bx));
    } while (--regs.cx != 0);
  }
  regs.ax = static_cast<std::uint16_t>(regs.ax & 3);
  if (regs.ax != 0)
  {
    regs.bx = Plus(DS.barPartialBytes.offset, regs.ax);
    SetLow(regs.bx, _guest.Byte(regs.bx));
    FillColumn(_guest, _lines, Low(regs.bx));
  }
  SetLow(regs.cx, Low(regs.dx));
  SetLow(regs.bx, BAR_REST_BYTE);
  SetLow(regs.cx, Low(regs.cx) >> 2);
  if (Low(regs.cx) == 0)
  {
    return;
  }
  do
  {
    FillColumn(_guest, _lines, Low(regs.bx));
  } while (--regs.cx != 0);
}

// mul bl / div bl: AL * 12 / 63, 0-48.
void ScaleBarValue(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.bx, 12);
  MultiplyByte(regs, 12);
  SetLow(regs.bx, 63);
  DivideByte(_guest, 63);
}

// mov al, [_value]; cmp al, [_shown]; je; (mov [_shown], al); mov di, _line; call DrawThreeLineBar.
void RedrawThreeLineBar(Guest& _guest, DataField<std::uint8_t> _value, DataField<std::uint8_t> _shown, std::uint16_t _line, bool _remember)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.ax, _guest.Get(_value));
  if (Low(regs.ax) == _guest.Get(_shown))
  {
    return;
  }
  if (_remember)
  {
    _guest.Set(_shown, Low(regs.ax));
  }
  regs.di = _line;
  DrawThreeLineBar(_guest);
}

// cmp [_shown], al; mov [_shown], al; je: whether the glyph changed.
[[nodiscard]] bool ExchangeShown(Guest& _guest, DataField<std::uint8_t> _shown)
{
  const std::uint8_t glyph = Low(_guest.Regs().ax);
  const bool changed = _guest.Get(_shown) != glyph;
  _guest.Set(_shown, glyph);
  return changed;
}

// ---- Collisions -----------------------------------------------------------------------------------

// 0x2BD9-0x2C8D: the slot at DI, whose byte 0 is in BL, against the player.
void CheckCollision(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint16_t collided = Plus(regs.di, SLOT_COLLIDED);
  regs.bx = static_cast<std::uint16_t>(regs.bx & 0x3E);
  regs.dx = _guest.Word(Plus(DS.collisionRanges.offset, regs.bx));
  _guest.Call(OBJECT_WITHIN_BOX);
  if (!Carry(_guest))
  {
    _guest.Call(IS_STATION);
    if (Zero(_guest))
    {
      _guest.SetByte(collided, static_cast<std::uint8_t>(_guest.Byte(collided) & 0xFE));
    }
    return;
  }
  std::uint16_t damage = 0x5DC;
  _guest.Call(IS_STATION);
  if (!Zero(_guest))
  {
    damage = 0x1C2;
  }
  else if ((_guest.Byte(collided) & 1) == 0)
  {
    _guest.SetByte(collided, static_cast<std::uint8_t>(_guest.Byte(collided) | 1));
    if ((_guest.Byte(regs.di) & 0x80) != 0)
    {
      regs.bx = 0x64;
      _guest.Call(CHECK_DOCKING_ALIGNMENT);
      if (Carry(_guest))
      {
        // Aligned: docked, if inside the slot and the station is not the Thargoids'.
        if (_guest.Get(DS.thargoidInvasionActive) != 1)
        {
          regs.ax = _guest.Word(Plus(regs.di, SLOT_X));
          regs.bx = _guest.Word(Plus(regs.di, SLOT_Y));
          regs.cx = 0;
          regs.dx = 0x5A;
          _guest.Call(VECTOR_WITHIN_BOX);
          if (Carry(_guest) && (_guest.Byte(Plus(regs.di, SLOT_FLAGS)) & 1) == 0)
          {
            _guest.Set(DS.playerDocked, 1);
            _guest.Set(DS.dockingComputerOn, 0);
            _guest.Set(DS.rollRate, 0);
            _guest.Set(DS.viewLocked, 0);
            return;
          }
        }
      }
      else
      {
        regs.bx = 0xFA;
        _guest.Call(CHECK_DOCKING_ALIGNMENT);
        if (Carry(_guest))
        {
          // Nearly aligned: a scrape if inside the slot, a crash if not.
          regs.ax = _guest.Word(Plus(regs.di, SLOT_X));
          regs.bx = _guest.Word(Plus(regs.di, SLOT_Y));
          regs.cx = 0;
          regs.dx = 0x6E;
          _guest.Call(VECTOR_WITHIN_BOX);
          damage = 0x190;
          if (Carry(_guest))
          {
            _guest.SetByte(collided, static_cast<std::uint8_t>(_guest.Byte(collided) & 0xFE));
            damage = 0x1E;
          }
        }
      }
    }
  }
  regs.ax = damage;
  _guest.Call(CREDIT_KILL);
  _guest.Call(CHECK_MISSILE_TARGET_DESTROYED);
  regs.ax = damage;
  bool remove = true;
  if (damage != 0x5DC)
  {
    _guest.Call(IS_STATION);
    remove = !Zero(_guest);
  }
  if (remove)
  {
    _guest.Call(REMOVE_OBJECT);
  }
  regs.ax = damage;
  _guest.Call(TAKE_DAMAGE);
  _guest.Call(START_IMPACT_SOUND);
}

// ---- The warnings ---------------------------------------------------------------------------------

// SetWarning (0x36E7): posts warning AL for 20 frames.
void SetWarning(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.warningFrames, 0x14);
  _guest.Set(DS.warningIndex, Low(regs.ax));
  regs.bx = static_cast<std::uint16_t>(Low(regs.ax) << 1);
  regs.ax = _guest.Word(Plus(DS.warningTexts.offset, regs.bx));
  _guest.Set(DS.warningMessage, regs.ax);
}

// CheckMissileWarning, CheckAltitudeWarning, CheckTemperatureWarning and CheckEnergyWarning (0x36FD,
// 0x370E, 0x371A, 0x3726): warning _check, with AL = its number. The missile check clears its alert.
[[nodiscard]] bool WarningApplies(Guest& _guest, std::uint16_t _check)
{
  SetLow(_guest.Regs().ax, _check);
  switch (_check)
  {
  case 0:
  {
    const bool alert = _guest.Get(DS.incomingMissileAlert) == 1;
    _guest.Set(DS.incomingMissileAlert, 0);
    return alert;
  }
  case 1:
    return _guest.Get(DS.altitude) < 0x32;
  case 2:
    return _guest.Get(DS.cabinTemperature) >= 0xE1;
  default:
    return _guest.Get(DS.playerEnergy) < 0x100;
  }
}

// ---- The scanner and the compass ------------------------------------------------------------------

// 0x41FF-0x4217: AX = 8 * AX / CX, at most 7, where CX is at least AX.
void ScaleCompassAxis(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (regs.cx >= regs.ax)
  {
    regs.dx = SignOf(regs.ax);
    const std::uint32_t dividend = ((std::uint32_t{regs.dx} << 16) | regs.ax) << 3;
    regs.dx = static_cast<std::uint16_t>(dividend >> 16);
    regs.ax = static_cast<std::uint16_t>(dividend);
    DivideWord(_guest, regs.cx);
  }
  if (regs.ax >= 8)
  {
    regs.ax = 7;
  }
}

// The eight neighbours XorCompassDot visits, in its order, as steps of DL and DH.
struct DotStep
{
  std::int8_t x;
  std::int8_t y;
};

constexpr std::array<DotStep, 8> COMPASS_RING = {
  DotStep{1, 0}, DotStep{0, 1}, DotStep{-1, 0}, DotStep{-1, 0}, DotStep{0, -1}, DotStep{0, -1}, DotStep{1, 0}, DotStep{1, 0},
};

// ---- The flight keys ------------------------------------------------------------------------------

// FlightScreenDispatch (0x0BDE): leaving the function keys, the cockpit back if a screen was shown.
void LeaveFlightScreens(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.flightScreenShown) != 0)
  {
    const std::uint16_t key = regs.ax;
    _guest.Call(RESTORE_FLIGHT_SCREEN);
    _guest.Call(RESET_KEYBOARD);
    regs.ax = key;
  }
  _guest.Set(DS.inFlight, 1);
}

// FlightScreenDispatch (0x0BF8): the screen for the key in AH, then the key that screen returns, until
// F1-F4.
void DispatchFlightScreens(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  for (;;)
  {
    std::uint8_t key = High(regs.ax);
    if (key >= SCAN_F1 && key <= SCAN_F4)
    {
      LeaveFlightScreens(_guest);
      return;
    }
    if (_guest.Get(DS.witchspaceCountdown) == 0)
    {
      _guest.Set(DS.flightScreenShown, 1);
      const std::uint16_t saved = regs.ax;
      EraseCompassAndBlips(_guest);
      regs.ax = saved;
      if (key == SCAN_F5)
      {
        _guest.Call(SHOW_GALACTIC_CHART);
        continue;
      }
      if (key == SCAN_F6)
      {
        _guest.Call(SHOW_SHORT_RANGE_CHART);
        continue;
      }
      if (key == SCAN_F7)
      {
        _guest.Call(SHOW_SYSTEM_DATA_SCREEN);
        continue;
      }
      if (key == SCAN_F8)
      {
        _guest.Call(SHOW_MARKET_PRICES_SCREEN);
        continue;
      }
    }
    else if (key == SCAN_F6 && _guest.Get(DS.witchspaceCountdown) == 1)
    {
      // In witch space the short-range chart still shows, once the countdown is down to 1.
      _guest.Set(DS.flightScreenShown, 1);
      EraseCompassAndBlips(_guest);
      _guest.Call(SHOW_SHORT_RANGE_CHART);
      continue;
    }
    else if (key >= SCAN_F9)
    {
      _guest.Set(DS.flightScreenShown, 1);
      const std::uint16_t saved = regs.ax;
      EraseCompassAndBlips(_guest);
      regs.ax = saved;
    }
    else
    {
      // The charts and the market need the navigation computer, which witch space jams.
      if (_guest.Get(DS.flightScreenShown) != 1)
      {
        _guest.Call(START_BEEP);
      }
      SetMessage(_guest, DS.navCompErrorMessage, 0x19);
      SetHigh(regs.ax, 0);
      LeaveFlightScreens(_guest);
      return;
    }
    key = High(regs.ax);
    if (key == SCAN_F9)
    {
      _guest.Call(RESET_KEYBOARD);
      _guest.Set(DS.inFlight, 1);
      _guest.Call(SHOW_COMMANDER_STATUS_SCREEN);
      _guest.Set(DS.inFlight, 0);
      continue;
    }
    if (key == SCAN_F10)
    {
      _guest.Call(SHOW_INVENTORY_SCREEN);
      continue;
    }
    _guest.Call(FLIGHT_SCREEN_KEY_WAIT);
    return;
  }
}

// 0x7FC5-0x802B: F1-F4 pick the view.
void ChangeView(Guest& _guest)
{
  const std::uint8_t key = High(_guest.Regs().ax);
  if (key < SCAN_F1 || key > SCAN_F4)
  {
    return;
  }
  constexpr std::array<std::uint16_t, 4> VIEWS = {0, REAR_VIEW, LEFT_VIEW, RIGHT_VIEW};
  const std::uint16_t view = VIEWS[static_cast<std::size_t>(key - SCAN_F1)];
  if (_guest.Get(DS.viewAngle) == view)
  {
    return;
  }
  _guest.Set(DS.viewAngle, view);
  _guest.Call(START_BEEP);
  ResetStardust(_guest);
}

// 0x8076-0x80FF: H. True when the countdown started, which ends ProcessFlightKeys.
[[nodiscard]] bool PressHyperspace(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.ax,
         (_guest.Get(DS.invadedStationDestroyed) ^ 1) & _guest.Get(DS.thargoidInvasionActive) & _guest.Get(DS.jumpedSinceBriefing));
  if (Low(regs.ax) != 0)
  {
    SetMessage(_guest, DS.hyperspaceJammedMessage, 0x19);
    return false;
  }
  if (_guest.Get(DS.galacticDriveReadyFrames) != 0)
  {
    _guest.Set(DS.galacticJumpPending, 1);
  }
  else
  {
    regs.ax = _guest.Get(DS.selectedDistanceTenthsLy);
    if (regs.ax == 0)
    {
      SetMessage(_guest, DS.noSystemSelectedMessage, 0x19);
      return false;
    }
    if (regs.ax >= 0x47)
    {
      SetMessage(_guest, DS.outOfRangeMessage, 0x19);
      return false;
    }
    // The fuel in tenths of a light year against the distance, and the cost.
    SetLow(regs.bx, Low(regs.ax));
    SetHigh(regs.bx, 0x0A);
    SetLow(regs.ax, _guest.Get(DS.fuel));
    MultiplyByte(regs, High(regs.bx));
    SetHigh(regs.bx, 0x24);
    DivideByte(_guest, High(regs.bx));
    if (Low(regs.ax) < Low(regs.bx))
    {
      SetMessage(_guest, DS.notEnoughFuelMessage, 0x19);
      return false;
    }
    regs.ax = _guest.Get(DS.selectedDistanceTenthsLy);
    MultiplyByte(regs, High(regs.bx));
    SetHigh(regs.bx, 0x0A);
    DivideByte(_guest, High(regs.bx));
    if (Low(regs.ax) == 0)
    {
      SetLow(regs.ax, 1);
    }
    _guest.Set(DS.hyperspaceFuelCost, Low(regs.ax));
  }
  _guest.Set(DS.hyperspaceCountdown, 0x0A);
  _guest.Set(DS.hyperspaceCountdownFrames, 0x0A);
  _guest.Call(SHOW_HYPERSPACE_COUNTDOWN);
  _guest.Call(RESET_HYPERSPACE_RINGS);
  _guest.Set(DS.missileState, 0);
  _guest.Set(DS.shipIdRequested, 0);
  _guest.Call(LATCH_HYPERSPACE_TARGET);
  return true;
}

// 0x8171-0x826C: T primes a missile, which locks on what is in the sights; U unarms it; M launches it.
void HandleMissileKeys(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.keyDownT) == 1 && _guest.Get(DS.missileCount) != 0 && _guest.Get(DS.missileState) == 0 &&
      _guest.Get(DS.hyperspaceCountdown) == 0 && _guest.Get(DS.shipIdRequested) != 1)
  {
    _guest.Set(DS.missileState, 1);
    _guest.Set(DS.shipIdRequested, 0);
    _guest.Call(START_BEEP);
    SetMessage(_guest, DS.missilePrimedMessage, 0x0F);
  }
  if (_guest.Get(DS.missileState) == 1)
  {
    _guest.Call(FIND_SHIP_IN_CROSSHAIRS);
    if (Carry(_guest) && (_guest.Byte(Plus(regs.di, SLOT_FLAGS)) & 0x20) == 0)
    {
      _guest.Set(DS.missileTarget, regs.di);
      _guest.Set(DS.missileState, 2);
      _guest.Call(START_BEEP);
      SetMessage(_guest, DS.missileLockedMessage, 0x14);
    }
  }
  if (_guest.Get(DS.missileState) == 2)
  {
    regs.bx = _guest.Get(DS.missileTarget);
    if ((_guest.Byte(regs.bx) & 1) == 0)
    {
      _guest.Set(DS.missileState, 0);
      _guest.Call(START_LOW_BEEP);
      SetMessage(_guest, DS.missileTargetDestroyedMessage, 0x14);
    }
  }
  if (_guest.Get(DS.keyDownU) == 1 && _guest.Get(DS.missileState) != 0)
  {
    _guest.Set(DS.missileState, 0);
    _guest.Set(DS.shipIdRequested, 0);
    _guest.Call(START_LOW_BEEP);
    SetMessage(_guest, DS.missileUnarmedMessage, 0x0F);
  }
  if (_guest.Get(DS.keyDownM) == 1 && _guest.Get(DS.missileState) == 2 && _guest.Get(DS.missileJammed) != 1)
  {
    NextRandom(_guest);
    if (regs.ax < 0x1F4)
    {
      _guest.Set(DS.missileJammed, 1);
      SetMessage(_guest, DS.missileJammedMessage, 0x19);
      return;
    }
    _guest.Set(DS.missileState, 0);
    _guest.Set(DS.shipIdRequested, 0);
    SetMessage(_guest, DS.missileLaunchedMessage, 0x14);
    _guest.Set(DS.missileCount, static_cast<std::uint8_t>(_guest.Get(DS.missileCount) - 1));
    _guest.Call(LAUNCH_PLAYER_MISSILE);
  }
}

// 0x826F-0x829A: fire heats the laser by 5; a pulse laser (type 0) fires every other frame.
void HandleFireButton(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(READ_FIRE_BUTTON);
  if (!Carry(_guest))
  {
    return;
  }
  _guest.Call(GET_VIEW_LASER);
  if (!Carry(_guest) || _guest.Get(DS.laserTemperature) >= 0xF0)
  {
    return;
  }
  _guest.Set(DS.laserTemperature, SaturatingAdd(_guest.Get(DS.laserTemperature), 5));
  if (Low(regs.ax) == 0)
  {
    _guest.Set(DS.laserAlternateFrame, static_cast<std::uint8_t>(_guest.Get(DS.laserAlternateFrame) ^ 1));
    if (_guest.Get(DS.laserAlternateFrame) != 0)
    {
      return;
    }
  }
  _guest.Set(DS.firingLaserType, Low(regs.ax));
  _guest.Set(DS.laserFiring, 1);
}

// 0x82F7-0x8363: I identifies the ship in the sights, and locks a missile on it if there is one.
void HandleIdentifyKey(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.shipIdRequested) != 1)
  {
    if (_guest.Get(DS.keyDownI) != 1 || _guest.Get(DS.missileState) != 0 || _guest.Get(DS.hyperspaceCountdown) != 0)
    {
      return;
    }
    _guest.Call(START_BEEP);
    if (_guest.Get(DS.missileCount) != 0)
    {
      _guest.Set(DS.missileState, 1);
    }
    _guest.Set(DS.shipIdRequested, 1);
    SetMessage(_guest, DS.idIndicatorMessage, 0x19);
  }
  _guest.Call(FIND_SHIP_IN_CROSSHAIRS);
  if (!Carry(_guest))
  {
    return;
  }
  _guest.Call(IS_STATION);
  if (Zero(_guest))
  {
    return;
  }
  _guest.Set(DS.missileState, 0);
  _guest.Set(DS.shipIdRequested, 0);
  SetLow(regs.ax, (_guest.Byte(regs.di) >> 1) & 0x1F);
  SetHigh(regs.ax, _guest.Byte(Plus(regs.di, SLOT_CLASS)));
  const std::uint16_t slot = regs.di;
  _guest.Call(SHOW_SHIP_IDENTITY);
  _guest.Call(START_BEEP);
  regs.di = slot;
  if (_guest.Get(DS.missileCount) != 0)
  {
    _guest.Set(DS.missileTarget, regs.di);
    _guest.Set(DS.missileState, 2);
  }
}

// 0x8379-0x8398: L runs the anti-ECM emulator, at one unit of energy a frame.
void HandleAntiEcmKey(Guest& _guest)
{
  _guest.Set(DS.antiEcmActive, 0);
  if (_guest.Get(DS.keyDownL) != 1 || _guest.Get(DS.antiEcmEmulatorFitted) != 1)
  {
    return;
  }
  _guest.Set(DS.antiEcmActive, 1);
  const std::uint16_t energy = _guest.Get(DS.playerEnergy);
  _guest.Set(DS.playerEnergy, energy == 0 ? std::uint16_t{0} : static_cast<std::uint16_t>(energy - 1));
}

// 0x84F2-0x8586: pitch rotates the camera frame, from which the three angles are derived again.
void ApplyPitch(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = Negate(static_cast<std::uint16_t>(SignExtend(Low(regs.ax)) << 1));
  SetSinCosPair(_guest, 7);
  regs.ax = Negate(_guest.Get(DS.playerRollAngle));
  SetSinCosPair(_guest, 2);
  regs.ax = Negate(_guest.Get(DS.playerYawAngle));
  SetSinCosPair(_guest, 1);
  regs.ax = Negate(_guest.Get(DS.playerPitchAngle));
  SetSinCosPair(_guest, 0);

  // The forward axis, rotated: pitch from its y and z, then yaw from its x and the rotated z.
  regs.ax = 0;
  regs.bx = 0;
  regs.cx = 0x2710;
  _guest.Call(ROTATE_BY_SIN_COS_7210);
  const std::uint16_t forwardX = regs.ax;
  const std::uint16_t forwardY = regs.bx;
  const std::uint16_t forwardZ = regs.cx;
  regs.ax = regs.bx;
  regs.bx = regs.cx;
  ArcTangent2(_guest);
  regs.ax = Negate(regs.ax);
  _guest.Set(DS.pitchAngleScratch, regs.ax);
  regs.ax = Negate(regs.ax);
  SetSinCosPair(_guest, 6);
  regs.bx = forwardZ;
  regs.ax = forwardY;
  RotateByPair(_guest, 6);
  regs.ax = forwardX;
  ArcTangent2(_guest);
  regs.ax = Negate(regs.ax);
  _guest.Set(DS.yawAngleScratch, regs.ax);

  // The up axis, unrotated by pitch and yaw: roll from what is left.
  regs.ax = Negate(Plus(_guest.Get(DS.pitchAngleScratch), 0x400));
  SetSinCosPair(_guest, 6);
  regs.ax = 0;
  regs.bx = 0xD8F0;
  regs.cx = 0;
  _guest.Call(ROTATE_BY_SIN_COS_7210);
  const std::uint16_t upX = regs.ax;
  regs.ax = regs.bx;
  regs.bx = regs.cx;
  RotateByPair(_guest, 6);
  const std::uint16_t upY = regs.ax;
  const std::uint16_t upZ = regs.bx;
  regs.ax = Plus(_guest.Get(DS.yawAngleScratch), 0x400);
  SetSinCosPair(_guest, 7);
  regs.bx = upZ;
  regs.cx = upY;
  regs.ax = upX;
  RotateByPair(_guest, 7);
  regs.bx = upY;
  ArcTangent2(_guest);
  _guest.Set(DS.playerRollAngle, regs.ax);
  regs.ax = Negate(_guest.Get(DS.yawAngleScratch));
  _guest.Set(DS.playerYawAngle, regs.ax);
  regs.ax = Negate(_guest.Get(DS.pitchAngleScratch));
  _guest.Set(DS.playerPitchAngle, regs.ax);
  _guest.Set(DS.velocityDirty, 1);
}

// 0x2A57-0x2B53: the sun in slot 0, the planet in slot 1 and the station in slot 2.
void PlaceSunPlanetAndStation(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const auto setByte = [&](std::uint16_t _field, int _value) { _guest.SetByte(Plus(regs.di, _field), static_cast<std::uint8_t>(_value)); };
  const auto setWord = [&](std::uint16_t _field, std::uint16_t _value) { _guest.SetWord(Plus(regs.di, _field), _value); };

  // The sun, at random far behind: x and y within 2 of 0, z 10 to 12 behind, in 24-bit coordinates.
  regs.di = DS.shipSlots.offset;
  setByte(0, 0x3D);
  NextRandom(_guest);
  regs.ax = static_cast<std::uint16_t>((regs.ax & 0x3FF) - 0x200);
  setByte(5, Low(regs.ax));
  setByte(1, High(regs.ax));
  NextRandom(_guest);
  regs.ax = static_cast<std::uint16_t>((regs.ax & 0x3FF) - 0x200);
  setByte(7, Low(regs.ax));
  setByte(2, High(regs.ax));
  NextRandom(_guest);
  regs.ax = static_cast<std::uint16_t>((regs.ax & 0x3FF) - 0xC00);
  setByte(3, High(regs.ax));
  setByte(9, Low(regs.ax));
  regs.ax = _guest.Get(DS.systemSeed0);
  SetLow(regs.ax, High(regs.ax));
  regs.ax = static_cast<std::uint16_t>(regs.ax & 1);
  SetLow(regs.ax, Low(regs.ax) + 1);
  setWord(0x0B, regs.ax);
  setByte(SLOT_CLASS, 0);
  setByte(SLOT_FLAGS, 6);
  setByte(0x3F, 0xFF);

  // The planet, straight ahead.
  regs.di = Plus(regs.di, SLOT_BYTES);
  setByte(0, 0x3F);
  regs.ax = 0;
  regs.dx = 0;
  setWord(SLOT_X, regs.ax);
  setByte(1, Low(regs.dx));
  setWord(SLOT_Y, regs.ax);
  setByte(2, Low(regs.dx));
  regs.ax = 0x6E;
  setByte(9, Low(regs.ax));
  setByte(3, High(regs.ax));
  setByte(8, 0);
  regs.ax = static_cast<std::uint16_t>(_guest.Get(DS.systemSeed0) & 1);
  SetLow(regs.ax, Low(regs.ax) + 1);
  setWord(0x0B, regs.ax);
  setByte(SLOT_CLASS, 0);
  setByte(SLOT_FLAGS, 6);
  setByte(0x3F, 0xFF);

  // The station, just behind: a Dodo (type 0) at tech level 9 and up, else a Coriolis (type 1).
  SetLow(regs.ax, _guest.Get(DS.currentTechLevel) >= 9 ? 0 : 1);
  SetLow(regs.ax, (Low(regs.ax) << 1) | 1);
  regs.di = Plus(regs.di, SLOT_BYTES);
  setByte(0, Low(regs.ax));
  regs.ax = 0x400;
  setWord(0x0C, regs.ax);
  regs.ax = 0;
  setWord(0x0E, regs.ax);
  setWord(0x0A, regs.ax);
  regs.dx = 0;
  setWord(SLOT_X, regs.ax);
  setByte(1, Low(regs.dx));
  setWord(SLOT_Y, regs.ax);
  setByte(2, Low(regs.dx));
  regs.ax = 0xFED4;
  setWord(SLOT_Z, regs.ax);
  regs.dx = SignOf(regs.ax);
  setByte(3, Low(regs.dx));
  setByte(SLOT_FLAGS, 4);
  setByte(SLOT_CLASS, 1);
  NextRandom(_guest);
  SetHigh(regs.ax, (High(regs.ax) & 7) + 0x0A);
  setByte(0x1F, High(regs.ax));
  setByte(0x3F, 0xFF);
  setByte(0x2B, 0x96);
  setByte(0x31, 0);
  if (_guest.Get(DS.thargoidInvasionActive) == 1)
  {
    setByte(0x32, 0);
    setByte(0x30, 0);
    setByte(0x2B, 0x96);
    setByte(0x1F, 0);
    setByte(0x2D, 0x0A);
    setByte(SLOT_FLAGS, _guest.Byte(Plus(regs.di, SLOT_FLAGS)) & 0xFE);
  }
  SetLow(regs.ax, _guest.Get(DS.currentGovernment));
  _guest.Set(DS.spawnGovernment, Low(regs.ax));
}

// sub word [si+_low], ax; sbb byte [si+_high], dl; sub word [si+_view], ax: a 24-bit coordinate and its
// view-frame word, less the velocity in AX.
void SubtractVelocity(Guest& _guest, std::uint16_t _low, std::uint16_t _high, std::uint16_t _view)
{
  const Registers& regs = _guest.Regs();
  const std::uint16_t low = Plus(regs.si, _low);
  const std::uint16_t high = Plus(regs.si, _high);
  const std::uint16_t view = Plus(regs.si, _view);
  const std::uint16_t before = _guest.Word(low);
  const int borrow = before < regs.ax ? 1 : 0;
  _guest.SetWord(low, static_cast<std::uint16_t>(before - regs.ax));
  _guest.SetByte(high, static_cast<std::uint8_t>(_guest.Byte(high) - Low(regs.dx) - borrow));
  _guest.SetWord(view, static_cast<std::uint16_t>(_guest.Word(view) - regs.ax));
}

} // namespace

void UpdateStardust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.jumpDriveEngaged) != 0)
  {
    _guest.Call(SAVE_STARDUST_POSITIONS);
  }
  SetLow(regs.ax, _guest.Get(DS.stardustColor));
  _guest.Set(DS.drawColor, Low(regs.ax));
  switch (_guest.Get(DS.viewAngle))
  {
  case RIGHT_VIEW:
    UpdateSideStardust(_guest, false);
    break;
  case LEFT_VIEW:
    UpdateSideStardust(_guest, true);
    break;
  case REAR_VIEW:
    UpdateRearStardust(_guest);
    break;
  default:
    UpdateFrontStardust(_guest);
    break;
  }
}

void ComputeStardustShift(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = static_cast<std::uint16_t>(0x34 - _guest.Get(DS.playerSpeed));
  SetLow(regs.bx, 12);
  DivideByte(_guest, 12);
  SetLow(regs.ax, Low(regs.ax) + 4);
  if (_guest.Get(DS.jumpDriveEngaged) != 0)
  {
    SetLow(regs.ax, Low(regs.ax) - 1);
  }
  _guest.Set(DS.stardustShift, Low(regs.ax));
}

void ComputeDustStripMask(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = regs.dx;
  regs.bp = 0;
  for (;;)
  {
    regs.ax = Sar(regs.ax, 1);
    if (regs.ax == 0xFFFF || regs.ax == 0)
    {
      break;
    }
    regs.bp = static_cast<std::uint16_t>((regs.bp << 1) | 1);
  }
  regs.ax = 0; // inc ax, or inc ax and dec ax, on the way out
}

void RollStardust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.cx = STARDUST_COUNT;
  regs.si = DS.stardust.offset;
  do
  {
    LoadDustPosition(_guest);
    RotateByPair(_guest, 7);
    StoreDustPosition(_guest);
    regs.si = Plus(regs.si, PARTICLE_BYTES);
  } while (--regs.cx != 0);
}

void ShiftStardustVertically(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  ComputeDustStripMask(_guest);
  regs.si = DS.stardust.offset;
  regs.cx = STARDUST_COUNT;
  regs.ax = 0;
  do
  {
    LoadDustPosition(_guest);
    IsDustOnScreen(_guest);
    if (Carry(_guest))
    {
      regs.bx = Plus(regs.bx, regs.dx);
      IsDustOnScreen(_guest);
      if (!Carry(_guest))
      {
        RespawnDustAtVerticalEdge(_guest);
        StorePreviousDustPosition(_guest);
      }
      StoreDustPosition(_guest);
    }
    regs.si = Plus(regs.si, PARTICLE_BYTES);
  } while (--regs.cx != 0);
}

void RespawnDustAtVerticalEdge(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  NextRandom(_guest);
  SetLow(regs.bx, RandomLifetime(regs.ax));
  MarkDustRespawned(_guest, Low(regs.bx));
  const std::uint16_t x = RandomDustX(regs.ax);
  NextRandom(_guest);
  regs.ax = static_cast<std::uint16_t>(regs.ax & regs.bp);
  regs.bx = Negative(regs.dx) ? static_cast<std::uint16_t>(0x0F00 - regs.ax) : Plus(0xF000, regs.ax);
  regs.ax = x;
}

void RespawnDustAnywhere(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  NextRandom(_guest);
  SetLow(regs.ax, RandomLifetime(regs.ax));
  MarkDustRespawned(_guest, Low(regs.ax));
  NextRandom(_guest);
  regs.bx = regs.ax;
  regs.ax = RandomDustX(regs.ax);
  regs.bx = Sar(Pair(Low(regs.bx), High(regs.bx)), 3);
  if (High(regs.bx) == 0x10)
  {
    SetHigh(regs.bx, 0x0F);
  }
}

void IsDustOnScreen(Guest& _guest)
{
  const Registers& regs = _guest.Regs();
  const auto x = static_cast<std::int8_t>(High(regs.ax));
  const auto y = static_cast<std::int8_t>(High(regs.bx));
  _guest.SetFlag(FLAG_CARRY, x >= -0x20 && x <= 0x1F && y >= -0x10 && y <= 0x0F);
}

void ScaleDustStep(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.cx, _guest.Get(DS.stardustShift));
  regs.dx = Sar(regs.bx, Low(regs.cx));
  regs.bp = Sar(regs.ax, Low(regs.cx));
}

void LoadDustPosition(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = _guest.Word(regs.si);
  regs.bx = _guest.Word(Plus(regs.si, 2));
}

void StoreDustPosition(Guest& _guest)
{
  const Registers& regs = _guest.Regs();
  _guest.SetWord(regs.si, regs.ax);
  _guest.SetWord(Plus(regs.si, 2), regs.bx);
}

void StorePreviousDustPosition(Guest& _guest)
{
  const Registers& regs = _guest.Regs();
  _guest.SetWord(Plus(regs.si, PREVIOUS_X), regs.ax);
  _guest.SetWord(Plus(regs.si, PREVIOUS_Y), regs.bx);
}

void DustToScreen(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  // shl ax,1 / xchg ah,al / shl ah,1 / cbw / rcl al,1: bits 13-6 in AL, AH the sign of bit 14.
  const auto toScreen = [](std::uint16_t _value, std::uint16_t _center)
  {
    const auto doubled = static_cast<std::uint16_t>(_value << 1);
    const std::uint8_t high = High(doubled);
    std::uint16_t result = SignExtend(high);
    SetLow(result, (high << 1) | ((doubled & 0x80) != 0 ? 1 : 0));
    return Plus(result, _center);
  };
  const std::uint16_t x = toScreen(regs.ax, 0x80);
  regs.bx = toScreen(regs.bx, 0x40);
  regs.ax = x;
}

void ResetStardust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = _guest.Get(DS.messageFrames);
  if (Low(regs.ax) == 0)
  {
    SetHigh(regs.ax, 0);
    _guest.Set(DS.messageFrames, regs.ax);
  }
  // Coordinates within 23h of the centre in the high byte, retried until one is.
  const auto randomCoordinate = [&]()
  {
    for (;;)
    {
      NextRandom(_guest);
      regs.ax = Sar(regs.ax, 1);
      const auto high = static_cast<std::int8_t>(High(regs.ax));
      if (high >= -0x23 && high <= 0x23)
      {
        return;
      }
    }
  };
  regs.cx = STARDUST_COUNT;
  regs.di = DS.stardust.offset;
  do
  {
    randomCoordinate();
    _guest.SetWord(regs.di, regs.ax);
    randomCoordinate();
    _guest.SetWord(Plus(regs.di, 2), regs.ax);
    NextRandom(_guest);
    SetLow(regs.ax, RandomLifetime(regs.ax));
    _guest.SetByte(Plus(regs.di, PARTICLE_LIFETIME), Low(regs.ax));
    regs.di = Plus(regs.di, PARTICLE_BYTES);
  } while (--regs.cx != 0);
}

void HandleFlightFunctionKeys(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.inFlight, 0);
  regs.cx = 10;
  regs.bx = SCAN_F1;
  do
  {
    if (_guest.Byte(Plus(DS.keyDown.offset, regs.bx)) == 1)
    {
      SetHigh(regs.ax, Low(regs.bx));
      const std::uint16_t key = regs.ax;
      _guest.Call(RESET_KEYBOARD);
      regs.ax = key;
      _guest.Set(DS.flightScreenShown, 0);
      DispatchFlightScreens(_guest);
      return;
    }
    ++regs.bx;
  } while (--regs.cx != 0);
  SetHigh(regs.ax, 0);
  _guest.Set(DS.inFlight, 1);
}

void InvalidateDashboard(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = regs.ds;
  regs.es = regs.ax;
  regs.di = DS.missileCountShown.offset;
  regs.cx = 0x16;
  SetLow(regs.ax, 0x80);
  // rep stosb
  const bool down = (regs.flags & FLAG_DIRECTION) != 0;
  for (; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarByte(regs.es, regs.di, Low(regs.ax));
    regs.di = Plus(regs.di, down ? -1 : 1);
  }
}

void UpdateDashboard(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  UpdateConditionColor(_guest);
  DrawConditionLight(_guest);
  UpdateEnergyAndLaserHeat(_guest);
  UpdateSafeZone(_guest);
  InSafeZone(_guest);

  // The S glyph inside the station's safe zone, and E for a frame when the ECM fires.
  SetLow(regs.ax, Carry(_guest) ? 0x82 : 0x20);
  regs.di = 0x1658;
  regs.bx = 0xFFFF;
  if (ExchangeShown(_guest, DS.safeZoneGlyphShown))
  {
    _guest.Call(DRAW_SCREEN_CHAR);
  }
  SetLow(regs.ax, 0x20);
  if (_guest.Get(DS.ecmFired) == 1)
  {
    _guest.Set(DS.ecmFired, 0);
    SetLow(regs.ax, 0x81);
  }
  regs.bx = 0xFFFF;
  regs.di = 0x1656;
  if (ExchangeShown(_guest, DS.ecmGlyphShown))
  {
    _guest.Call(DRAW_SCREEN_CHAR);
  }

  DrawEnergyBanks(_guest);
  DrawMissileLockIndicator(_guest);
  DrawMissileIcons(_guest);

  regs.ax = _guest.Get(DS.rollRate);
  SetLow(regs.ax, High(regs.ax));
  if (Low(regs.ax) != _guest.Get(DS.pitchRateShown))
  {
    _guest.Set(DS.pitchRateShown, Low(regs.ax));
    regs.di = 0x3938;
    DrawSignedIndicator(_guest);
  }
  regs.ax = _guest.Get(DS.rollRate);
  if (Low(regs.ax) != _guest.Get(DS.rollRateShown))
  {
    _guest.Set(DS.rollRateShown, Low(regs.ax));
    regs.di = 0x37F8;
    DrawSignedIndicator(_guest);
  }

  RedrawThreeLineBar(_guest, DS.laserTemperature, DS.laserTemperatureShown, 0x3B8C, true);
  RedrawThreeLineBar(_guest, DS.altitude, DS.altitudeShown, 0x3CCC, true);
  RedrawThreeLineBar(_guest, DS.cabinTemperature, DS.cabinTemperatureShown, 0x3A4C, true);
  RedrawThreeLineBar(_guest, DS.fuel, DS.fuelShown, 0x390C, true);
  // The original never stores the shields' shown values, so a shield bar is drawn every frame.
  RedrawThreeLineBar(_guest, DS.foreShield, DS.foreShieldShown, 0x368C, false);
  RedrawThreeLineBar(_guest, DS.aftShield, DS.aftShieldShown, 0x37CC, false);

  regs.ax = _guest.Get(DS.playerSpeed);
  if (Low(regs.ax) == _guest.Get(DS.speedShown))
  {
    return;
  }
  _guest.Set(DS.speedShown, Low(regs.ax));
  regs.di = 0x36B8;
  DrawBarPixels(_guest, FIVE_LINES);
}

void DrawFiveLineBar(Guest& _guest)
{
  ScaleBarValue(_guest);
  DrawBarPixels(_guest, FIVE_LINES);
}

void DrawSignedIndicator(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  auto value = static_cast<std::int8_t>(Low(regs.ax));
  if (value <= -24)
  {
    value = -23;
  }
  if (value >= 24)
  {
    value = 23;
  }
  SetLow(regs.ax, static_cast<std::uint8_t>(value));
  SetLow(regs.bx, 0x17 + Low(regs.ax));
  regs.cx = 6;
  regs.ax = BACKGROUND_WORD;
  do
  {
    FillFiveLineWord(_guest, regs.ax);
    regs.di = Plus(regs.di, 2);
  } while (--regs.cx != 0);
  regs.di = static_cast<std::uint16_t>(regs.di - 0x0C);

  // The marker: a word of indicatorMarkers at pixel BL, or its first byte alone at the right end.
  regs.ax = static_cast<std::uint16_t>((Low(regs.bx) >> 2) & 0x0F);
  regs.di = Plus(regs.di, regs.ax);
  regs.bx = Plus(static_cast<std::uint16_t>((regs.bx & 3) << 1), DS.indicatorMarkers.offset);
  const bool lastByte = regs.ax == 0x0B;
  regs.ax = _guest.Word(regs.bx);
  if (lastByte)
  {
    for (const std::uint16_t line : FIVE_LINES)
    {
      _guest.SetFarByte(regs.es, Plus(regs.di, line), Low(regs.ax));
    }
    return;
  }
  FillFiveLineWord(_guest, regs.ax);
}

void DrawThreeLineBar(Guest& _guest)
{
  ScaleBarValue(_guest);
  DrawBarPixels(_guest, THREE_LINES);
}

void DrawMissileIcons(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.cx, _guest.Get(DS.missileCount));
  if (Low(regs.cx) == _guest.Get(DS.missileCountShown))
  {
    return;
  }
  _guest.Set(DS.missileCountShown, Low(regs.cx));
  regs.di = 0x3E0C;
  regs.cx = static_cast<std::uint16_t>(regs.cx & 3);
  if (regs.cx != 0)
  {
    // missileIcon holds the icon's five lines in bank order: odd, even, odd, even, odd.
    constexpr std::array<std::uint16_t, 5> ICON_WORDS = {0, 4, 8, 2, 6};
    do
    {
      regs.si = DS.missileIcon.offset;
      for (std::size_t index = 0; index < FIVE_LINES.size(); ++index)
      {
        regs.ax = _guest.Word(Plus(regs.si, ICON_WORDS[index]));
        _guest.SetFarWord(regs.es, Plus(regs.di, FIVE_LINES[index]), regs.ax);
      }
      regs.di = Plus(regs.di, 2);
    } while (--regs.cx != 0);
  }
  SetLow(regs.cx, 4 - _guest.Get(DS.missileCount));
  if (Low(regs.cx) == 0)
  {
    return;
  }
  regs.ax = BACKGROUND_WORD;
  do
  {
    FillFiveLineWord(_guest, regs.ax);
    regs.di = Plus(regs.di, 2);
  } while (--regs.cx != 0);
}

void DrawMissileLockIndicator(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.bx, _guest.Get(DS.missileState));
  if (Low(regs.bx) == _guest.Get(DS.missileLockShown))
  {
    return;
  }
  _guest.Set(DS.missileLockShown, Low(regs.bx));
  regs.bx = Plus(static_cast<std::uint16_t>(regs.bx & 3), DS.colorFillBytes.offset);
  SetLow(regs.ax, _guest.Byte(regs.bx));
  SetHigh(regs.ax, Low(regs.ax));
  regs.cx = 3;
  regs.di = 0x1E15;
  do
  {
    _guest.SetFarWord(regs.es, regs.di, regs.ax);
    _guest.SetFarByte(regs.es, Plus(regs.di, 2), Low(regs.ax));
    _guest.SetFarWord(regs.es, Plus(regs.di, EVEN_BANK), regs.ax);
    _guest.SetFarByte(regs.es, Plus(regs.di, EVEN_BANK + 2), Low(regs.ax));
    regs.di = Plus(regs.di, 0x50);
  } while (--regs.cx != 0);
}

void DrawEnergyBanks(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  // Each whole 100h of playerEnergy fills a bank from the top one down, the rest goes in the next, and
  // the banks below it are empty.
  regs.ax = _guest.Get(DS.playerEnergy);
  regs.di = DS.energyBankFill.At(3);
  regs.cx = 4;
  bool partial = false;
  do
  {
    if (High(regs.ax) == 0)
    {
      partial = true;
      break;
    }
    _guest.SetByte(regs.di, 0xFF);
    --regs.di;
    SetHigh(regs.ax, High(regs.ax) - 1);
  } while (--regs.cx != 0);
  if (partial)
  {
    _guest.SetByte(regs.di, Low(regs.ax));
    --regs.di;
    if (--regs.cx != 0)
    {
      do
      {
        _guest.SetByte(regs.di, 0);
        --regs.di;
      } while (--regs.cx != 0);
    }
  }

  // Each bank against its shown value, from the top bank's bar upwards.
  regs.si = static_cast<std::uint16_t>(DS.energyBankFill.offset - 1);
  regs.cx = 4;
  regs.di = 0x3E38;
  do
  {
    const std::uint16_t count = regs.cx;
    const std::uint16_t line = regs.di;
    const std::uint16_t shown = regs.si;
    SetLow(regs.ax, _guest.Byte(Plus(regs.si, 4)));
    if (Low(regs.ax) != _guest.Byte(regs.si))
    {
      _guest.SetByte(regs.si, Low(regs.ax));
      DrawFiveLineBar(_guest);
    }
    regs.si = static_cast<std::uint16_t>(shown - 1);
    regs.di = static_cast<std::uint16_t>(line - 0x140);
    regs.cx = count;
  } while (--regs.cx != 0);
}

void UpdateEnergyAndLaserHeat(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.gameOverFrames) == 0 && _guest.Get(DS.escapePodFrames) == 0)
  {
    _guest.Set(DS.laserTemperature, SaturatingSubtract(_guest.Get(DS.laserTemperature), 2));
    if (_guest.Get(DS.playerEnergy) == 0x3FF)
    {
      _guest.Set(DS.aftShield, SaturatingAdd(_guest.Get(DS.aftShield), 1));
      _guest.Set(DS.foreShield, SaturatingAdd(_guest.Get(DS.foreShield), 1));
    }
    else
    {
      SetLow(regs.ax, (_guest.Get(DS.energyUnitFitted) << 1) + 1);
      regs.ax = SignExtend(Low(regs.ax));
      const std::uint16_t energy = Plus(_guest.Get(DS.playerEnergy), regs.ax);
      _guest.Set(DS.playerEnergy, energy >= 0x400 ? std::uint16_t{0x3FF} : energy);
    }
  }
  if (_guest.Get(DS.playerEnergy) >= 0x100)
  {
    return;
  }
  // Below one bank, 50 in 65536 a frame, one of the 13 equipment bytes from missileCount is lost.
  NextRandom(_guest);
  if (regs.ax >= 0x32)
  {
    return;
  }
  NextRandom(_guest);
  SetHigh(regs.ax, 0);
  SetLow(regs.bx, 0x14);
  DivideByte(_guest, 0x14);
  regs.bx = Low(regs.ax);
  const std::uint16_t equipment = Plus(DS.missileCount.offset, regs.bx);
  if (_guest.Byte(equipment) == 0)
  {
    return;
  }
  _guest.SetByte(equipment, static_cast<std::uint8_t>(_guest.Byte(equipment) - 1));
  SetMessage(_guest, DS.equipmentLossMessage, 0x1E);
}

void DrawConditionLight(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.bx, _guest.Get(DS.conditionColor));
  if (Low(regs.bx) == 0)
  {
    // Flashing: black and red, on bit 9 of millisecondCounter.
    regs.bx = _guest.Get(DS.millisecondCounter);
    SetLow(regs.bx, High(regs.bx) & 2);
  }
  if (Low(regs.bx) == _guest.Get(DS.conditionColorShown))
  {
    return;
  }
  _guest.Set(DS.conditionColorShown, Low(regs.bx));
  SetHigh(regs.bx, 0);
  SetLow(regs.bx, _guest.Byte(Plus(DS.colorFillBytes.offset, regs.bx)));
  SetHigh(regs.bx, Low(regs.bx));
  regs.si = DS.conditionLightBitmap.offset;
  regs.di = 0x176C;
  regs.cx = 4;
  const auto swapped = [&](std::uint16_t _offset)
  {
    const std::uint16_t word = _guest.Word(_offset);
    return static_cast<std::uint16_t>(Pair(Low(word), High(word)) & regs.bx);
  };
  do
  {
    regs.ax = swapped(regs.si);
    _guest.SetFarWord(regs.es, regs.di, regs.ax);
    regs.di = Plus(regs.di, EVEN_BANK);
    regs.ax = swapped(Plus(regs.si, 2));
    _guest.SetFarWord(regs.es, regs.di, regs.ax);
    regs.di = Plus(regs.di, 0xE050);
    regs.si = Plus(regs.si, 4);
  } while (--regs.cx != 0);
}

void UpdateConditionColor(Guest& _guest)
{
  const std::uint16_t energy = _guest.Get(DS.playerEnergy);
  const std::uint8_t cabin = _guest.Get(DS.cabinTemperature);
  const std::uint8_t altitude = _guest.Get(DS.altitude);
  const std::uint8_t fore = _guest.Get(DS.foreShield);
  const std::uint8_t aft = _guest.Get(DS.aftShield);
  std::uint8_t color = 1;
  if (energy < 0x100 || cabin >= 0xE0 || altitude < 0x20)
  {
    color = 0;
  }
  else if (fore == 0 || aft == 0 || energy < 0x200 || cabin >= 0xC0 || altitude < 0x28)
  {
    color = 2;
  }
  else if (cabin >= 0x80 || altitude < 0x80 || aft < 0x80 || fore < 0x80 || energy < 0x300)
  {
    color = 3;
  }
  SetLow(_guest.Regs().ax, color);
  _guest.Set(DS.conditionColor, color);
}

void SetUpLocalSpace(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.cx, _guest.Get(DS.currentSystemIndex));
  _guest.Call(LOAD_SYSTEM_SEEDS);
  InvalidateDashboard(_guest);
  _guest.Set(DS.objectSlotCount, 0x14);
  _guest.Set(DS.debrisSlotCount, 0x10);
  _guest.Set(DS.shipSlotCount, 0x24);
  _guest.Call(CLEAR_ALL_OBJECTS);
  _guest.Call(SHOW_COCKPIT_SCREEN);
  ResetStardust(_guest);
  _guest.Set(DS.hyperspaceCountdown, 0);
  _guest.Set(DS.dockingComputerOn, 0);
  _guest.Set(DS.dataA137, 0);
  _guest.Set(DS.viewAngle, 0);
  _guest.Set(DS.viewLocked, 0);
  _guest.Set(DS.escapePodFrames, 0);
  _guest.Set(DS.playerDocked, 0);
  _guest.Set(DS.playerVelocityX, 0);
  _guest.Set(DS.playerVelocityY, 0);
  _guest.Set(DS.playerVelocityZ, 0x14);
  _guest.Set(DS.playerSpeed, 0x14);
  _guest.Set(DS.altitude, 0xFF);
  _guest.Set(DS.missileJammed, 0);
  _guest.Set(DS.playerPitchAngle, 0);
  _guest.Set(DS.playerYawAngle, 0);
  _guest.Set(DS.playerRollAngle, 0);
  if (_guest.Get(DS.witchspaceCountdown) != 0)
  {
    _guest.Set(DS.spawnGovernment, 0);
    return;
  }
  PlaceSunPlanetAndStation(_guest);
}

void CheckCollisions(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.di = DS.shipSlots.offset;
  regs.cx = _guest.Get(DS.objectSlotCount);
  do
  {
    const std::uint16_t count = regs.cx;
    SetLow(regs.bx, _guest.Byte(regs.di));
    if ((Low(regs.bx) & 1) != 0)
    {
      CheckCollision(_guest);
    }
    regs.di = Plus(regs.di, SLOT_BYTES);
    regs.cx = count;
  } while (--regs.cx != 0);
}

void InSafeZone(Guest& _guest)
{
  const std::uint8_t flags = _guest.Get(DS.safeZoneFlags);
  SetLow(_guest.Regs().ax, flags >> 1);
  _guest.SetFlag(FLAG_CARRY, (flags & 1) != 0);
}

void UpdateSafeZone(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.di = DS.stationSlot.offset;
  if ((_guest.Byte(regs.di) & 1) == 0)
  {
    _guest.Set(DS.safeZoneFlags, 0);
    return;
  }
  _guest.Call(IS_STATION);
  if (!Zero(_guest))
  {
    _guest.Set(DS.safeZoneFlags, 0);
    return;
  }
  _guest.Call(IS_OBJECT_NEAR);
  bool inside = false;
  if (Carry(_guest))
  {
    regs.ax = _guest.Word(Plus(regs.di, SLOT_X));
    regs.bx = _guest.Word(Plus(regs.di, SLOT_Y));
    regs.cx = _guest.Word(Plus(regs.di, SLOT_Z));
    _guest.Call(VECTOR_LENGTH);
    inside = regs.ax < 0x32C8;
  }
  // rcl al,1: bit 0 the answer, the rest whatever AL held.
  SetLow(regs.ax, (Low(regs.ax) << 1) | (inside ? 1 : 0));
  _guest.Set(DS.safeZoneFlags, Low(regs.ax));
}

void UpdateWarnings(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.gameOverFrames) != 0)
  {
    return;
  }
  SetLow(regs.ax, _guest.Get(DS.warningFrames));
  if (Low(regs.ax) != 0)
  {
    SetHigh(regs.ax, 0);
    _guest.Set(DS.messageFrames, regs.ax);
    regs.ax = _guest.Get(DS.warningMessage);
    _guest.Set(DS.messagePointer, regs.ax);
    _guest.Set(DS.warningFrames, static_cast<std::uint8_t>(_guest.Get(DS.warningFrames) - 1));
    return;
  }
  // The four checks round-robin from the one after the last warning (jmp [bx+warningChecks]), each
  // falling on to the next with loop.
  regs.cx = 4;
  SetLow(regs.bx, _guest.Get(DS.warningIndex) + 1);
  regs.bx = static_cast<std::uint16_t>((regs.bx & 3) << 1);
  auto check = static_cast<std::uint16_t>(regs.bx >> 1);
  for (;;)
  {
    if (WarningApplies(_guest, check))
    {
      SetWarning(_guest);
      return;
    }
    if (--regs.cx == 0)
    {
      return;
    }
    check = static_cast<std::uint16_t>((check + 1) & 3);
  }
}

void UpdateScannerBlip(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (regs.di >= DEBRIS_SLOTS)
  {
    return;
  }
  regs.dx = regs.ax;
  _guest.Call(IS_STATION);
  regs.ax = regs.dx;
  if (Zero(_guest))
  {
    return;
  }
  _guest.Call(IS_SUN_OR_PLANET);
  regs.ax = regs.dx;
  if (Zero(_guest))
  {
    return;
  }
  const std::uint16_t x = regs.ax;
  const std::uint16_t y = regs.bx;
  const std::uint16_t z = regs.cx;
  const std::uint16_t slot = regs.di;
  _guest.SetByte(Plus(regs.di, SLOT_SCANNED), 1);
  // The scanner's y and z are 1.25 times the camera's.
  regs.dx = Sar(regs.bx, 2);
  regs.bx = Plus(regs.bx, regs.dx);
  regs.dx = Sar(regs.cx, 2);
  regs.cx = Plus(regs.cx, regs.dx);
  const std::uint16_t blip = Plus(regs.di, SLOT_BLIP);
  const std::uint8_t oldX = _guest.Byte(blip);
  const std::uint8_t oldY = _guest.Byte(Plus(blip, 1));
  const std::uint8_t oldZ = _guest.Byte(Plus(blip, 2));
  _guest.SetByte(blip, High(regs.ax));
  _guest.SetByte(Plus(blip, 1), High(regs.bx));
  _guest.SetByte(Plus(blip, 2), High(regs.cx));
  SetHigh(regs.ax, oldX);
  SetHigh(regs.bx, oldY);
  SetHigh(regs.cx, oldZ);
  if ((_guest.Byte(Plus(regs.di, SLOT_FLAGS)) & FLAG_BLIP_DRAWN) != 0)
  {
    XorScannerBlip(_guest);
    regs.di = slot;
  }
  SetHigh(regs.ax, _guest.Byte(blip));
  SetHigh(regs.bx, _guest.Byte(Plus(blip, 1)));
  SetHigh(regs.cx, _guest.Byte(Plus(blip, 2)));
  XorScannerBlip(_guest);
  regs.di = slot;
  const std::uint16_t flags = Plus(regs.di, SLOT_FLAGS);
  _guest.SetByte(flags, static_cast<std::uint8_t>(_guest.Byte(flags) | FLAG_BLIP_DRAWN));
  regs.cx = z;
  regs.bx = y;
  regs.ax = x;
}

void UpdateCompass(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.titleShown) == 0)
  {
    return;
  }
  // The target: the station when the planet is near, else the planet.
  regs.di = DS.planetSlot.offset;
  _guest.Set(DS.compassTargetIsStation, 0);
  _guest.Call(IS_OBJECT_NEAR_KEEP_BLIP);
  if (Carry(_guest))
  {
    regs.di = Plus(regs.di, SLOT_BYTES);
    _guest.Set(DS.compassTargetIsStation, 1);
  }
  _guest.Call(GET_POSITION_SCALE_SHIFT);
  _guest.SetByte(Plus(regs.di, SLOT_SCALE_SHIFT), Low(regs.cx));
  SetHigh(regs.dx, Low(regs.cx));
  _guest.Call(SCALE_POSITION_DOWN);
  regs.di = DS.stationSlot.offset;
  const std::uint16_t x = regs.ax;
  const std::uint16_t y = regs.bx;
  const std::uint16_t z = regs.cx;
  _guest.Call(GET_POSITION_SCALE_SHIFT);
  _guest.SetByte(Plus(regs.di, SLOT_SCALE_SHIFT), Low(regs.cx));
  // mov bp, sp, below the three words the original pushed.
  regs.bp = static_cast<std::uint16_t>(regs.sp - 6);
  regs.cx = z;
  regs.bx = y;
  regs.ax = x;
  _guest.Call(TRANSFORM_TO_VIEW);
  _guest.SetWord(Plus(regs.di, SLOT_VIEW_X), regs.ax);
  _guest.SetWord(Plus(regs.di, SLOT_VIEW_Y), regs.bx);
  _guest.SetWord(Plus(regs.di, SLOT_VIEW_Z), regs.cx);
  regs.cx = z;
  regs.bx = y;
  regs.ax = x;
  _guest.Call(ROTATE_PITCH_YAW_ROLL);

  // The dot: x and y as 8 * |c| / (|z| + 1000), at most 7, normalised when they reach 8 together.
  // BP gathers z >= 0 in bit 15 and the signs of x and y in bits 0 and 1.
  regs.bp = 0x8000;
  if (Negative(regs.cx))
  {
    regs.cx = Negate(regs.cx);
    regs.bp = 0;
  }
  regs.cx = Plus(regs.cx, 0x3E8);
  if (Negative(regs.ax))
  {
    regs.ax = Negate(regs.ax);
    regs.bp = Plus(regs.bp, 1);
  }
  if (Negative(regs.bx))
  {
    regs.bx = Negate(regs.bx);
    regs.bp = Plus(regs.bp, 2);
  }
  ScaleCompassAxis(_guest);
  std::swap(regs.ax, regs.bx);
  ScaleCompassAxis(_guest);
  SetHigh(regs.dx, Low(regs.ax));
  SetLow(regs.dx, Low(regs.bx));
  MultiplyByte(regs, Low(regs.ax));
  SetLow(regs.bx, Low(regs.ax));
  SetLow(regs.ax, Low(regs.dx));
  MultiplyByte(regs, Low(regs.ax));
  SetLow(regs.bx, Low(regs.bx) + Low(regs.ax));
  if (Low(regs.bx) >= 0x41)
  {
    SetHigh(regs.bx, 0);
    SetLow(regs.bx, _guest.Byte(Plus(DS.sqrtTable.offset, regs.bx)));
    SetLow(regs.ax, Low(regs.dx));
    regs.ax = static_cast<std::uint16_t>(SignExtend(Low(regs.ax)) << 3);
    DivideByte(_guest, Low(regs.bx));
    SetLow(regs.ax, Low(regs.ax) & 7);
    SetLow(regs.dx, Low(regs.ax));
    // AH still holds the remainder, so this divide can overflow, and the trap's 7Fh comes out as 7.
    SetLow(regs.ax, High(regs.dx));
    regs.ax = static_cast<std::uint16_t>(regs.ax << 3);
    DivideByte(_guest, Low(regs.bx));
    SetLow(regs.ax, Low(regs.ax) & 7);
    SetHigh(regs.dx, Low(regs.ax));
  }
  if ((regs.bp & 1) != 0)
  {
    SetLow(regs.dx, -Low(regs.dx));
  }
  regs.bp = static_cast<std::uint16_t>(regs.bp >> 1);
  if ((regs.bp & 1) != 0)
  {
    SetHigh(regs.dx, -High(regs.dx));
  }
  regs.bp = static_cast<std::uint16_t>(regs.bp >> 1);
  SetHigh(regs.dx, High(regs.dx) + 0x27);
  SetLow(regs.dx, Low(regs.dx) + 0xCF);

  // The new dot replaces the old in the station slot's blip bytes: the old is erased, the new drawn.
  regs.di = DS.stationSlot.offset;
  const std::uint16_t dot = Plus(regs.di, SLOT_BLIP);
  const std::uint16_t front = Plus(regs.di, SLOT_BLIP + 2);
  const std::uint16_t oldDot = _guest.Word(dot);
  _guest.SetWord(dot, regs.dx);
  regs.dx = oldDot;
  regs.ax = regs.bp;
  const std::uint8_t oldFront = _guest.Byte(front);
  _guest.SetByte(front, High(regs.ax));
  SetHigh(regs.ax, oldFront);
  regs.bp = regs.ax;
  if ((_guest.Byte(Plus(regs.di, SLOT_FLAGS)) & FLAG_BLIP_DRAWN) != 0)
  {
    XorCompassDot(_guest);
  }
  regs.ax = SignExtend(_guest.Byte(front));
  regs.bp = regs.ax;
  regs.dx = _guest.Word(dot);
  const std::uint16_t flags = Plus(regs.di, SLOT_FLAGS);
  _guest.SetByte(flags, static_cast<std::uint8_t>(_guest.Byte(flags) | FLAG_BLIP_DRAWN));
  XorCompassDot(_guest);
}

void XorCompassDot(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  if (regs.bp != 0)
  {
    XorDashboardPixel(_guest);
  }
  for (const DotStep step : COMPASS_RING)
  {
    SetLow(regs.dx, Low(regs.dx) + step.x);
    SetHigh(regs.dx, High(regs.dx) + step.y);
    XorDashboardPixel(_guest);
  }
  regs.di = slot;
}

void EraseScannerBlip(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint16_t saved = regs.ax;
  _guest.Call(IS_STATION);
  regs.ax = saved;
  if (Zero(_guest))
  {
    return;
  }
  const std::uint16_t flags = Plus(regs.di, SLOT_FLAGS);
  if ((_guest.Byte(flags) & FLAG_BLIP_DRAWN) == 0)
  {
    return;
  }
  SetHigh(regs.ax, _guest.Byte(Plus(regs.di, SLOT_BLIP)));
  SetHigh(regs.bx, _guest.Byte(Plus(regs.di, SLOT_BLIP + 1)));
  SetHigh(regs.cx, _guest.Byte(Plus(regs.di, SLOT_BLIP + 2)));
  const std::uint16_t slot = regs.di;
  XorScannerBlip(_guest);
  regs.di = slot;
  _guest.SetByte(flags, static_cast<std::uint8_t>(_guest.Byte(flags) & ~FLAG_BLIP_DRAWN));
}

void XorScannerBlip(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetHigh(regs.bx, SarByte(High(regs.bx), 2));
  SetHigh(regs.cx, SarByte(High(regs.cx), 2));
  SetLow(regs.dx, High(regs.ax) + 0x3D);
  SetHigh(regs.dx, 0x1F - High(regs.cx));
  // A stick of |y| pixels from the dot, then one more to the right at its end.
  SetLow(regs.cx, High(regs.bx));
  SetHigh(regs.cx, 1);
  if ((Low(regs.cx) & 0x80) != 0)
  {
    SetLow(regs.cx, -Low(regs.cx));
    SetHigh(regs.cx, -1);
  }
  for (;;)
  {
    XorDashboardPixel(_guest);
    if (Low(regs.cx) == 0)
    {
      break;
    }
    SetHigh(regs.dx, High(regs.dx) + High(regs.cx));
    SetLow(regs.cx, Low(regs.cx) - 1);
    if ((Low(regs.cx) & 0x80) != 0)
    {
      break;
    }
  }
  SetLow(regs.dx, Low(regs.dx) + 1);
  XorDashboardPixel(_guest);
}

void XorDashboardPixel(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.es = Guest::VIDEO_SEGMENT;
  regs.ax = regs.dx;
  const std::uint8_t x = Low(regs.dx);
  const std::uint8_t y = High(regs.dx);
  // The bank from y's parity, 80 bytes a pair of lines, 4 pixels a byte.
  regs.di = static_cast<std::uint16_t>(((y & 1) != 0 ? EVEN_BANK : 0) + (y >> 1) * 80 + (x >> 2) + DASHBOARD_ORIGIN);
  regs.bx = _guest.Byte(Plus(DS.dashboardPixelMasks.offset, x & 3));
  _guest.SetFarByte(regs.es, regs.di, static_cast<std::uint8_t>(_guest.FarByte(regs.es, regs.di) ^ Low(regs.bx)));
}

void EraseCompassAndBlips(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.di = DS.stationSlot.offset;
  const std::uint16_t flags = Plus(regs.di, SLOT_FLAGS);
  if ((_guest.Byte(flags) & FLAG_BLIP_DRAWN) != 0)
  {
    regs.ax = SignExtend(_guest.Byte(Plus(regs.di, SLOT_BLIP + 2)));
    regs.bp = regs.ax;
    regs.dx = _guest.Word(Plus(regs.di, SLOT_BLIP));
    _guest.SetByte(flags, static_cast<std::uint8_t>(_guest.Byte(flags) & ~FLAG_BLIP_DRAWN));
    XorCompassDot(_guest);
  }
  regs.di = DS.firstShipSlot.offset;
  regs.cx = static_cast<std::uint8_t>(_guest.Get(DS.objectSlotCount) - 3);
  do
  {
    const std::uint16_t count = regs.cx;
    const std::uint16_t slot = regs.di;
    EraseScannerBlip(_guest);
    regs.di = Plus(slot, SLOT_BYTES);
    regs.cx = count;
  } while (--regs.cx != 0);
}

void UpdateFuelLeak(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.fuelLeakDelayFrames) != 0)
  {
    _guest.Set(DS.fuelLeakDelayFrames, static_cast<std::uint8_t>(_guest.Get(DS.fuelLeakDelayFrames) - 1));
    if (_guest.Get(DS.fuelLeakDelayFrames) == 0)
    {
      _guest.Set(DS.fuelLeakFrames, 0x33);
      return;
    }
    SetLow(regs.ax, _guest.Get(DS.maskingBackgroundColor));
  }
  else if (_guest.Get(DS.fuelLeakFrames) != 0)
  {
    _guest.Set(DS.fuelLeakFrames, static_cast<std::uint8_t>(_guest.Get(DS.fuelLeakFrames) - 1));
    _guest.Set(DS.fuel, SaturatingSubtract(_guest.Get(DS.fuel), 5));
    regs.ax = SignExtend(_guest.Get(DS.fuelLeakFrames));
    _guest.Set(DS.messageFrames, regs.ax);
    regs.ax = DS.fuelLeakText.offset;
    _guest.Set(DS.messagePointer, regs.ax);
    SetLow(regs.ax, 4); // red
  }
  else
  {
    SetLow(regs.ax, _guest.Get(DS.maskingBackgroundColor));
  }
  // The CGA's colour select register: the border and background, with intensity.
  regs.dx = 0x3D9;
  SetLow(regs.ax, Low(regs.ax) | 0x10);
  _guest.Out8(regs.dx, Low(regs.ax));
}

void TickEscapePod(Guest& _guest)
{
  if (_guest.Get(DS.escapePodFrames) == 0)
  {
    return;
  }
  _guest.Set(DS.escapePodFrames, static_cast<std::uint8_t>(_guest.Get(DS.escapePodFrames) - 1));
  if (_guest.Get(DS.escapePodFrames) == 0)
  {
    // pop ax: the return address goes, and the RET after it returns from RunFlight.
    _guest.Regs().ax = _guest.Pop();
  }
}

void ProcessFlightKeys(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.gameOverFrames) != 0 || _guest.Get(DS.escapePodFrames) != 0)
  {
    return;
  }
  HandleFlightFunctionKeys(_guest);
  if (High(regs.ax) != 0 && _guest.Get(DS.viewLocked) != 1)
  {
    ChangeView(_guest);
  }
  if (_guest.Get(DS.keyDownG) == 1 && _guest.Get(DS.galacticHyperdriveFitted) == 1 && _guest.Get(DS.galacticDriveReadyFrames) == 0 &&
      _guest.Get(DS.hyperspaceCountdown) == 0)
  {
    _guest.Call(START_BEEP);
    SetMessage(_guest, DS.galacticDriveReadyMessage, 0x28);
    _guest.Set(DS.galacticDriveReadyFrames, 0x28);
  }
  if (_guest.Get(DS.keyDownH) == 1 && _guest.Get(DS.dockingComputerOn) != 1 && _guest.Get(DS.hyperspaceCountdown) == 0 &&
      PressHyperspace(_guest))
  {
    return;
  }
  // D, released, toggles the docking computer: not in mission 3 until the invaded station is destroyed.
  if ((_guest.Get(DS.missionNumber) != 3 || _guest.Get(DS.invadedStationDestroyed) == 1) && _guest.Get(DS.dockingKeyReleased) == 1 &&
      _guest.Get(DS.dockingComputerFitted) == 1)
  {
    if (_guest.Get(DS.hyperspaceCountdown) != 0)
    {
      _guest.Set(DS.dockingKeyReleased, 0);
    }
    else
    {
      _guest.Set(DS.jumpDriveEngaged, 0);
      _guest.Call(TOGGLE_DOCKING_COMPUTER);
    }
  }
  if (_guest.Get(DS.keyDownJ) == 1)
  {
    _guest.Call(ENGAGE_JUMP_DRIVE);
  }
  if (_guest.Get(DS.keyDownEsc) == 1)
  {
    _guest.Set(DS.gamePaused, 1);
    if (_guest.Get(DS.keyDownCtrl) == 1)
    {
      // Ctrl+Esc freezes the game until a key: a wait, which the original runs to its return.
      _guest.Call(RESET_KEYBOARD);
      _guest.Call(FROZEN_KEY_WAIT);
      return;
    }
    SetMessage(_guest, DS.gamePausedMessage, 1);
    _guest.Call(UPDATE_MESSAGE_LINE);
    _guest.Call(RUN_PAUSE_SCREEN);
    _guest.Set(DS.gamePaused, 0);
  }
  HandleMissileKeys(_guest);
  HandleFireButton(_guest);
  if (_guest.Get(DS.keyDownE) == 1 && _guest.Get(DS.ecmFitted) == 1)
  {
    _guest.Set(DS.ecmFired, 1);
    SetMessage(_guest, DS.ecmActiveMessage, 5);
    _guest.Call(REMOVE_ALL_MISSILES);
    SetLow(regs.ax, 0x14);
    _guest.Call(DRAIN_ENERGY);
  }
  if (_guest.Get(DS.keyDownB) == 1 && _guest.Get(DS.energyBombFitted) == 1)
  {
    _guest.Set(DS.energyBombFitted, 0);
    _guest.Call(DETONATE_ENERGY_BOMB);
  }
  if (_guest.Get(DS.keyDownC) == 1 && _guest.Get(DS.escapePodFitted) == 1 && _guest.Get(DS.escapePodFrames) == 0)
  {
    _guest.Call(START_BEEP);
    _guest.Call(LAUNCH_ESCAPE_POD);
  }
  HandleIdentifyKey(_guest);
  if (_guest.Get(DS.keyDownN) == 1 && _guest.Get(DS.maskingDeviceFitted) == 1)
  {
    _guest.Call(USE_MASKING_DEVICE);
  }
  HandleAntiEcmKey(_guest);
}

void UpdatePlayerMotion(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint8_t gameOver = _guest.Get(DS.gameOverFrames);
  if (gameOver != 0 && gameOver != 0x28)
  {
    return;
  }
  if (_guest.Get(DS.escapePodFrames) != 0)
  {
    MoveObjectsByVelocity(_guest);
    return;
  }
  if (_guest.Get(DS.dockingComputerOn) != 0)
  {
    _guest.Call(RUN_DOCKING_COMPUTER);
    regs.ax = _guest.Get(DS.dockingComputerSteering);
  }
  else
  {
    // . and , change the speed by 4, within 4-48.
    regs.ax = _guest.Get(DS.playerSpeed);
    if (_guest.Get(DS.keyDownPeriod) == 1)
    {
      SetLow(regs.ax, Low(regs.ax) + 4);
      _guest.Set(DS.velocityDirty, 1);
      if (Low(regs.ax) >= 0x31)
      {
        SetLow(regs.ax, 0x30);
      }
    }
    if (_guest.Get(DS.keyDownComma) == 1)
    {
      SetLow(regs.ax, Low(regs.ax) - 4);
      _guest.Set(DS.velocityDirty, 1);
      if (Low(regs.ax) < 4)
      {
        SetLow(regs.ax, 4);
      }
    }
    _guest.Set(DS.playerSpeed, regs.ax);
    _guest.Call(READ_STEERING);
    _guest.Set(DS.rollRate, regs.ax);
    _guest.Call(APPLY_REVERSE_CONTROLS);
  }

  // Roll, -23 to 23, turns the roll angle by twice as much.
  SetLow(regs.bx, High(regs.ax));
  SetLow(regs.ax, -Low(regs.ax));
  if (Low(regs.ax) != 0)
  {
    auto roll = static_cast<std::int8_t>(Low(regs.ax));
    if (roll <= -24)
    {
      roll = -23;
    }
    if (roll >= 24)
    {
      roll = 23;
    }
    regs.ax = static_cast<std::uint16_t>(SignExtend(static_cast<std::uint8_t>(roll)) << 1);
    _guest.Set(DS.playerRollAngle, Plus(_guest.Get(DS.playerRollAngle), regs.ax));
  }
  if (Low(regs.bx) != 0)
  {
    SetLow(regs.ax, Low(regs.bx));
    ApplyPitch(_guest);
  }
  if (_guest.Get(DS.dockingComputerOn) == 1)
  {
    return;
  }
  UpdatePlayerVelocity(_guest);
  MoveObjectsByVelocity(_guest);
}

void UpdatePlayerVelocity(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.velocityDirty) != 1)
  {
    return;
  }
  _guest.Set(DS.velocityDirty, 0);
  regs.ax = Negate(Plus(_guest.Get(DS.playerPitchAngle), 0x400));
  SetSinCosPair(_guest, 6);
  regs.ax = Plus(_guest.Get(DS.playerYawAngle), 0x400);
  SetSinCosPair(_guest, 7);
  regs.ax = 0;
  regs.bx = _guest.Get(DS.playerSpeed);
  if (_guest.Get(DS.jumpDriveEngaged) == 1)
  {
    // The jump drive: 32 times the speed, for this frame only.
    _guest.Set(DS.velocityDirty, 1);
    _guest.Set(DS.jumpDriveEngaged, 0);
    regs.bx = static_cast<std::uint16_t>(regs.bx << 5);
  }
  RotateByPair(_guest, 7);
  _guest.Set(DS.playerVelocityX, regs.ax);
  regs.ax = 0;
  RotateByPair(_guest, 6);
  _guest.Set(DS.playerVelocityY, regs.ax);
  _guest.Set(DS.playerVelocityZ, regs.bx);
}

void MoveObjectsByVelocity(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.cx = _guest.Get(DS.shipSlotCount);
  regs.si = DS.shipSlots.offset;
  do
  {
    regs.ax = _guest.Get(DS.playerVelocityX);
    regs.dx = SignOf(regs.ax);
    SubtractVelocity(_guest, SLOT_X, 1, SLOT_VIEW_X);
    regs.ax = _guest.Get(DS.playerVelocityY);
    regs.dx = SignOf(regs.ax);
    SubtractVelocity(_guest, SLOT_Y, 2, SLOT_VIEW_Y);
    regs.ax = _guest.Get(DS.playerVelocityZ);
    regs.dx = SignOf(regs.ax);
    SubtractVelocity(_guest, SLOT_Z, 3, SLOT_VIEW_Z);
    regs.si = Plus(regs.si, SLOT_BYTES);
  } while (--regs.cx != 0);
}

namespace
{

using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract Clobbers(std::uint16_t _registers) noexcept
{
  return Machine::NativeContract{_registers, 0};
}

constexpr Machine::NativeContract CARRY_OUT{0, FLAG_CARRY};

constexpr std::array ENTRIES = {
  NativeEntry{0x068F, "UpdateStardust", &UpdateStardust,
              Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP)},
  NativeEntry{0x0887, "ComputeStardustShift", &ComputeStardustShift, Clobbers(REGISTER_AX)},
  NativeEntry{0x08B9, "ComputeDustStripMask", &ComputeDustStripMask, Clobbers(REGISTER_AX)},
  NativeEntry{0x0927, "RollStardust", &RollStardust, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI)},
  NativeEntry{0x0940, "ShiftStardustVertically", &ShiftStardustVertically,
              Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_BP)},
  NativeEntry{0x0969, "RespawnDustAtVerticalEdge", &RespawnDustAtVerticalEdge, PRESERVES_ALL},
  NativeEntry{0x09A3, "RespawnDustAnywhere", &RespawnDustAnywhere, PRESERVES_ALL},
  NativeEntry{0x09D6, "IsDustOnScreen", &IsDustOnScreen, CARRY_OUT},
  NativeEntry{0x0A06, "ScaleDustStep", &ScaleDustStep, PRESERVES_ALL},
  NativeEntry{0x0A13, "LoadDustPosition", &LoadDustPosition, PRESERVES_ALL},
  NativeEntry{0x0A19, "StoreDustPosition", &StoreDustPosition, PRESERVES_ALL},
  NativeEntry{0x0A1F, "StorePreviousDustPosition", &StorePreviousDustPosition, PRESERVES_ALL},
  NativeEntry{0x0A28, "DustToScreen", &DustToScreen, PRESERVES_ALL},
  NativeEntry{0x0A43, "ResetStardust", &ResetStardust, Clobbers(REGISTER_AX | REGISTER_CX | REGISTER_DI)},
  NativeEntry{0x0BB3, "HandleFlightFunctionKeys", &HandleFlightFunctionKeys, PRESERVES_ALL},
  NativeEntry{0x2540, "InvalidateDashboard", &InvalidateDashboard, Clobbers(REGISTER_AX | REGISTER_CX | REGISTER_DI)},
  NativeEntry{0x254F, "UpdateDashboard", &UpdateDashboard, Clobbers(static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_ES))},
  NativeEntry{0x2645, "DrawFiveLineBar", &DrawFiveLineBar, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI)},
  NativeEntry{0x26BE, "DrawSignedIndicator", &DrawSignedIndicator, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DI)},
  NativeEntry{0x273E, "DrawThreeLineBar", &DrawThreeLineBar, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI)},
  NativeEntry{0x2799, "DrawMissileIcons", &DrawMissileIcons, Clobbers(REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI)},
  NativeEntry{0x2804, "DrawMissileLockIndicator", &DrawMissileLockIndicator,
              Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DI)},
  NativeEntry{0x283B, "DrawEnergyBanks", &DrawEnergyBanks, Clobbers(REGISTER_ALL)},
  NativeEntry{0x2882, "UpdateEnergyAndLaserHeat", &UpdateEnergyAndLaserHeat, Clobbers(REGISTER_AX | REGISTER_BX)},
  NativeEntry{0x290C, "DrawConditionLight", &DrawConditionLight,
              Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_DI)},
  NativeEntry{0x2959, "UpdateConditionColor", &UpdateConditionColor, PRESERVES_ALL},
  NativeEntry{0x29D0, "SetUpLocalSpace", &SetUpLocalSpace, Clobbers(REGISTER_ALL)},
  NativeEntry{0x2BC5, "CheckCollisions", &CheckCollisions, Clobbers(REGISTER_ALL)},
  NativeEntry{0x2E63, "InSafeZone", &InSafeZone, CARRY_OUT},
  NativeEntry{0x2E69, "UpdateSafeZone", &UpdateSafeZone, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX)},
  NativeEntry{0x36B6, "UpdateWarnings", &UpdateWarnings, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX)},
  NativeEntry{0x40EC, "UpdateScannerBlip", &UpdateScannerBlip, PRESERVES_ALL},
  NativeEntry{0x418F, "UpdateCompass", &UpdateCompass, PRESERVES_ALL},
  NativeEntry{0x42A4, "XorCompassDot", &XorCompassDot, Clobbers(REGISTER_AX | REGISTER_BX)},
  NativeEntry{0x42D6, "EraseScannerBlip", &EraseScannerBlip, Clobbers(REGISTER_BX | REGISTER_CX | REGISTER_DX)},
  NativeEntry{0x42F6, "XorScannerBlip", &XorScannerBlip, Clobbers(REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI | REGISTER_ES)},
  NativeEntry{0x43C4, "XorDashboardPixel", &XorDashboardPixel, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_DI | REGISTER_ES)},
  NativeEntry{0x4594, "EraseCompassAndBlips", &EraseCompassAndBlips, Clobbers(REGISTER_ALL)},
  NativeEntry{0x499F, "UpdateFuelLeak", &UpdateFuelLeak, Clobbers(REGISTER_AX | REGISTER_DX)},
  NativeEntry{0x7F69, "TickEscapePod", &TickEscapePod, PRESERVES_ALL},
  NativeEntry{0x7FA8, "ProcessFlightKeys", &ProcessFlightKeys, Clobbers(REGISTER_ALL)},
  NativeEntry{0x8472, "UpdatePlayerMotion", &UpdatePlayerMotion, Clobbers(REGISTER_ALL)},
  NativeEntry{0x8599, "UpdatePlayerVelocity", &UpdatePlayerVelocity, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_DX)},
  NativeEntry{0x85EC, "MoveObjectsByVelocity", &MoveObjectsByVelocity, Clobbers(REGISTER_AX | REGISTER_CX | REGISTER_DX | REGISTER_SI)},
};

} // namespace

std::span<const NativeEntry> FlightEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
