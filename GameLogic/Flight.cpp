#include "pch.h"

#include "Flight.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Maths.h"
#include "ObjectSlot.h"

#include <utility>

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_ZERO;
using Machine::Registers;

// The routines these call through their entries: the original's, or a native routine hooked there.
constexpr std::uint16_t FINISH_SPACE_VIEW_FRAME = 0x0570;
constexpr std::uint16_t PRESENT_SPACE_VIEW = 0x0599;
constexpr std::uint16_t CLEAR_DRAW_BUFFER = 0x060D;
constexpr std::uint16_t NEXT_RANDOM = 0x061C;
constexpr std::uint16_t GET_VIEW_LASER = 0x066C;
constexpr std::uint16_t UPDATE_STARDUST = 0x068F;
constexpr std::uint16_t RESET_STARDUST = 0x0A43;
constexpr std::uint16_t HANDLE_FLIGHT_FUNCTION_KEYS = 0x0BB3;
constexpr std::uint16_t SHOW_GALACTIC_CHART = 0x0CAE;
constexpr std::uint16_t SHOW_SHORT_RANGE_CHART = 0x0E52;
constexpr std::uint16_t LOAD_SYSTEM_SEEDS = 0x139C;
constexpr std::uint16_t RESTORE_FLIGHT_SCREEN = 0x15CF;
constexpr std::uint16_t PLOT_PIXEL = 0x15E0;
constexpr std::uint16_t DRAW_LINE = 0x16D1;
constexpr std::uint16_t UPDATE_DASHBOARD = 0x254F;
constexpr std::uint16_t SET_UP_LOCAL_SPACE = 0x29D0;
constexpr std::uint16_t CHECK_COLLISIONS = 0x2BC5;
constexpr std::uint16_t TAKE_DAMAGE = 0x2C9B;
constexpr std::uint16_t CHECK_DOCKING_ALIGNMENT = 0x2D0F;
constexpr std::uint16_t PLAY_STATION_TUNNEL = 0x2D5B;
constexpr std::uint16_t VECTOR_LENGTH = 0x2E96;
constexpr std::uint16_t DETONATE_ENERGY_BOMB = 0x2ED6;
constexpr std::uint16_t LAUNCH_ESCAPE_POD = 0x2F0F;
constexpr std::uint16_t OBJECT_WITHIN_BOX = 0x2F65;
constexpr std::uint16_t VECTOR_WITHIN_BOX = 0x2F6E;
constexpr std::uint16_t SPAWN_PLAYER_WRECKAGE = 0x2FE3;
constexpr std::uint16_t DRAW_VIEW_CHAR = 0x3130;
constexpr std::uint16_t DRAW_VIEW_STRING = 0x31EC;
constexpr std::uint16_t DRAW_SCREEN_CHAR = 0x31F9;
constexpr std::uint16_t DRAW_SCREEN_STRING = 0x32D8;
constexpr std::uint16_t UPDATE_MESSAGE_LINE = 0x35A3;
constexpr std::uint16_t CLEAR_MESSAGE_LINE = 0x3609;
constexpr std::uint16_t SHOW_SHIP_IDENTITY = 0x364D;
constexpr std::uint16_t IS_OBJECT_NEAR = 0x3B9A;
constexpr std::uint16_t TRANSFORM_AND_DRAW_OBJECTS = 0x3D25;
constexpr std::uint16_t ROTATE_PITCH_YAW_ROLL = 0x3EAC;
constexpr std::uint16_t TRANSFORM_TO_VIEW = 0x3EE3;
constexpr std::uint16_t ROTATE_BY_SIN_COS_7210 = 0x3F02;
constexpr std::uint16_t IS_SUN_OR_PLANET = 0x3F2A;
constexpr std::uint16_t IS_STATION = 0x3F40;
constexpr std::uint16_t IS_MASS_LOCKED = 0x4144;
constexpr std::uint16_t GET_POSITION_SCALE_SHIFT = 0x4333;
constexpr std::uint16_t SCALE_POSITION_DOWN = 0x439E;
constexpr std::uint16_t ERASE_COMPASS_AND_BLIPS = 0x4594;
constexpr std::uint16_t IS_OBJECT_NEAR_KEEP_BLIP = 0x460E;
constexpr std::uint16_t RESET_HYPERSPACE_RINGS = 0x48AB;
constexpr std::uint16_t UPDATE_FUEL_LEAK = 0x499F;
constexpr std::uint16_t LATCH_HYPERSPACE_TARGET = 0x49F6;
constexpr std::uint16_t UPDATE_OBJECTS_AND_SPAWN = 0x4A10;
constexpr std::uint16_t REMOVE_OBJECT = 0x4F98;
constexpr std::uint16_t REMOVE_ALL_MISSILES = 0x4F9F;
constexpr std::uint16_t LAUNCH_PLAYER_MISSILE = 0x5242;
constexpr std::uint16_t CLEAR_ALL_OBJECTS = 0x52B2;
constexpr std::uint16_t SHOW_SYSTEM_DATA_SCREEN = 0x5CDE;
constexpr std::uint16_t SHOW_MARKET_PRICES_SCREEN = 0x5E2C;
constexpr std::uint16_t SHOW_COMMANDER_STATUS_SCREEN = 0x5EA9;
constexpr std::uint16_t SHOW_INVENTORY_SCREEN = 0x6020;
constexpr std::uint16_t STOP_ALL_SOUND = 0x7423;
constexpr std::uint16_t SILENCE_SPEAKER_TIMER = 0x7436;
constexpr std::uint16_t READ_FIRE_BUTTON = 0x74E0;
constexpr std::uint16_t READ_STEERING = 0x7536;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t RESET_KEYBOARD = 0x7668;
constexpr std::uint16_t START_BEEP = 0x7A57;
constexpr std::uint16_t START_LOW_BEEP = 0x7A5D;
constexpr std::uint16_t STOP_SOUND_EFFECTS = 0x7A63;
constexpr std::uint16_t START_IMPACT_SOUND = 0x7AC3;
constexpr std::uint16_t STOP_CONTINUOUS_NOISE = 0x7B6B;
constexpr std::uint16_t SHOW_COCKPIT_SCREEN = 0x7BC0;
constexpr std::uint16_t POLL_SCREEN_DUMP_KEY = 0x7F3D;
constexpr std::uint16_t RESET_MOUSE_IF_SELECTED = 0x7F5D;
constexpr std::uint16_t TICK_ESCAPE_POD = 0x7F69;
constexpr std::uint16_t TICK_HYPERSPACE_COUNTDOWN = 0x7F79;
constexpr std::uint16_t PROCESS_FLIGHT_KEYS = 0x7FA8;
constexpr std::uint16_t DRAIN_ENERGY = 0x839F;
constexpr std::uint16_t TOGGLE_DOCKING_COMPUTER = 0x83B2;
constexpr std::uint16_t ENGAGE_JUMP_DRIVE = 0x8430;
constexpr std::uint16_t UPDATE_PLAYER_MOTION = 0x8472;
constexpr std::uint16_t RUN_DOCKING_COMPUTER = 0x8622;
constexpr std::uint16_t FIND_SHIP_IN_CROSSHAIRS = 0x8A46;
constexpr std::uint16_t RESOLVE_LASER_FIRE = 0x8AC2;
constexpr std::uint16_t CHECK_MISSILE_TARGET_DESTROYED = 0x8B8B;
constexpr std::uint16_t CREDIT_KILL = 0x8BC6;
constexpr std::uint16_t SHOW_HYPERSPACE_COUNTDOWN = 0x8C62;
constexpr std::uint16_t APPLY_ENEMY_LASER_HIT = 0x8C8E;
constexpr std::uint16_t RUN_PAUSE_SCREEN = 0x8D6A;
constexpr std::uint16_t APPLY_REVERSE_CONTROLS = 0x8EA5;
constexpr std::uint16_t APPLY_REVERSE_CONTROLS_TO_DX = 0x8EBA;
constexpr std::uint16_t USE_MASKING_DEVICE = 0x8ECF;

// The stardust: 30 particles of 6 bytes, x and y words, a lifetime byte and a spare; stardustPrevious
// follows at +0xB4 with the same layout, its lifetime copy at +0xB8 and its new-particle flag at +0xB9.
constexpr std::uint16_t STARDUST_COUNT = 30;
constexpr std::uint16_t PARTICLE_BYTES = 6;
constexpr std::uint16_t PARTICLE_LIFETIME = 4;
constexpr std::uint16_t PREVIOUS_X = 0xB4;
constexpr std::uint16_t PREVIOUS_Y = 0xB6;
constexpr std::uint16_t PREVIOUS_LIFETIME = 0xB8;
constexpr std::uint16_t PREVIOUS_NEW = 0xB9;
constexpr std::uint16_t STARDUST_BYTES = STARDUST_COUNT * PARTICLE_BYTES;

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

// What SetUpLocalSpace puts in the first three slots: the sun's and the planet's type bytes and flags, the planet's
// z (its middle byte), and the station's heading, z, flags and energy.
constexpr std::uint8_t SUN_TYPE = 0x3D;
constexpr std::uint8_t PLANET_TYPE = 0x3F;
constexpr std::uint8_t SUN_OR_PLANET_FLAGS = 0x06; // blip drawn, indestructible
constexpr std::uint8_t PLANET_Z_TOP = 0x6E;
constexpr std::uint16_t STATION_YAW = 0x400;
constexpr std::uint16_t STATION_Z = 0xFED4;  // -300
constexpr std::uint8_t STATION_FLAGS = 0x04; // indestructible
constexpr std::uint8_t STATION_ENERGY = 0x96;

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
constexpr std::uint16_t NEXT_LINE_PAIR = 0x50;        // a line on, in the same bank
constexpr std::uint16_t NEXT_ODD_LINE = 0xE050;       // from an even-bank line to the odd-bank line below it
constexpr std::uint16_t DASHBOARD_CACHE_BYTES = 0x16; // missileCountShown to conditionColorShown
constexpr std::uint8_t DASHBOARD_STALE = 0x80;        // a cached value no gauge shows
constexpr std::uint16_t MISSILE_LOCK_LINE = 0x1E15;
constexpr std::uint16_t MISSILE_LOCK_LINE_PAIRS = 3;
constexpr std::uint16_t CONDITION_LIGHT_LINE = 0x176C;
constexpr std::uint16_t CONDITION_LIGHT_LINE_PAIRS = 4;
// The roll and pitch indicators: six words of strip, the marker at pixel value + 23, and the column of the strip's
// last byte, where the marker's word would run past it.
constexpr std::uint16_t INDICATOR_WORDS = 6;
constexpr std::uint8_t INDICATOR_CENTER = 0x17;
constexpr std::uint16_t INDICATOR_LAST_COLUMN = 0x0B;
// The missile icons: four cells of a word, from x=48, y=193.
constexpr std::uint16_t MISSILE_ICONS_LINE = 0x3E0C;
constexpr std::uint8_t MISSILE_CELLS = 4;

constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F4 = 0x3E;
constexpr std::uint8_t SCAN_F5 = 0x3F;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F8 = 0x42;
constexpr std::uint8_t SCAN_F9 = 0x43;
constexpr std::uint8_t SCAN_F10 = 0x44;

// The pause screen: its keys, and the option letters it shows in reverse video when they are on, a flag
// byte and a letter each from keyboardRecenter.
constexpr std::uint8_t SCAN_A = 0x1E;
constexpr std::uint8_t SCAN_B = 0x30;
constexpr std::uint8_t SCAN_D = 0x20;
constexpr std::uint8_t SCAN_R = 0x13;
constexpr std::uint8_t SCAN_S = 0x1F;
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_SPACE = 0x39;
constexpr std::uint8_t SCAN_PAST_F10 = 0x45;
constexpr std::uint16_t PAUSE_MENU_LINES = 8;
constexpr std::uint16_t PAUSE_OPTIONS = 5;
constexpr std::uint16_t PAUSE_FIRST_OPTION = 0x0C08; // in spaceViewBuffer
constexpr std::uint16_t PAUSE_OPTION_STEP = 0x200;
constexpr std::uint16_t ALL_COLORS = 0xFFFF;
constexpr std::uint16_t TITLE_TEXT_POSITION = 0x65; // on the message line, in CGA memory
constexpr std::uint8_t DIGIT_ZERO = 0x30;
constexpr std::uint8_t DIGIT_ONE = 0x31;
constexpr std::uint8_t DIGIT_NINE = 0x39;
constexpr std::uint8_t CLOSING_BRACKET = 0x29;
constexpr std::uint8_t SPACE = 0x20;

[[nodiscard]] std::uint16_t Plus(std::uint16_t _word, int _value) noexcept
{
  return static_cast<std::uint16_t>(_word + _value);
}

[[nodiscard]] bool Negative(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0;
}

[[nodiscard]] std::uint16_t Word(std::int16_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value);
}

[[nodiscard]] std::int16_t Signed(std::uint16_t _value) noexcept
{
  return static_cast<std::int16_t>(_value);
}

// The high byte of a signed 8.8 word: its whole part.
[[nodiscard]] std::int8_t WholePart(std::int16_t _value) noexcept
{
  return static_cast<std::int8_t>(High(Word(_value)));
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

// mov [messagePointer], ax; mov [messageFrames], _frames, after mov ax, _text: the message at DS:_text, posted for
// _frames frames. Its callers load AX with _text themselves.
void SetMessage(GameState& _state, std::uint16_t _text, std::uint16_t _frames)
{
  _state.Set(DS.messagePointer, _text);
  _state.Set(DS.messageFrames, _frames);
}

// The register code's side of SetSinCos0-8 and RotateBySinCos0-8, which it calls by pair number as Maths' SetSinCos and
// RotateByStoredSinCos: the pair a rotation turns is in AX and BX, and SetSinCos leaves the sine in AX and the cosine
// in BX.
[[nodiscard]] Pair PairIn(const Registers& _regs) noexcept
{
  return Pair{Signed(_regs.ax), Signed(_regs.bx)};
}

void PairOut(Registers& _regs, Pair _pair) noexcept
{
  _regs.ax = Word(_pair.first);
  _regs.bx = Word(_pair.second);
}

void SinCosOut(Registers& _regs, SinCos _sinCos) noexcept
{
  _regs.ax = Word(_sinCos.sine);
  _regs.bx = Word(_sinCos.cosine);
}

// ---- Stardust -------------------------------------------------------------------------------------

// The lifetime byte of the particle at DS:_particle, its copy in stardustPrevious, and the copy marked new, for a
// respawned particle.
void MarkDustRespawned(GameState& _state, std::uint16_t _particle, std::uint8_t _lifetime)
{
  _state.SetByte(Plus(_particle, PARTICLE_LIFETIME), _lifetime);
  _state.SetByte(Plus(_particle, PREVIOUS_LIFETIME), _lifetime);
  _state.SetByte(Plus(_particle, PREVIOUS_NEW), 1);
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

// sar 3, keeping BH off 10h (which a 16-bit value shifted three times cannot reach).
[[nodiscard]] std::uint16_t RandomDustY(std::uint16_t _random) noexcept
{
  std::uint16_t y = Sar(_random, 3);
  if (High(y) == 0x10)
  {
    SetHigh(y, 0x0F);
  }
  return y;
}

// The particle as a point, or while the jump drive is engaged a streak from where it was last frame,
// unless it has just respawned.
void DrawDust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  DustToScreenEntry(_guest);
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
  GetPreviousDustScreenPositionEntry(_guest);
  if (!_guest.Flag(FLAG_CARRY))
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
    SinCosOut(regs, SetSinCos(_guest.State(), 7, regs.ax));
    RollStardust(_guest);
  }
  regs.cx = STARDUST_COUNT;
  regs.si = DS.stardust.offset;
  ComputeStardustShift(_guest);
  do
  {
    const std::uint16_t count = regs.cx;
    LoadDustPositionEntry(_guest);
    IsDustOnScreenEntry(_guest);
    if (_guest.Flag(FLAG_CARRY))
    {
      if (_guest.Get(DS.playerSpeed) != 0)
      {
        ScaleDustStepEntry(_guest);
        regs.ax = Plus(regs.ax, regs.bp);
        regs.bx = Plus(regs.bx, regs.dx);
      }
      for (;;)
      {
        StoreDustPositionEntry(_guest);
        IsDustOnScreenEntry(_guest);
        if (_guest.Flag(FLAG_CARRY))
        {
          break;
        }
        RespawnDustAnywhereEntry(_guest);
        StorePreviousDustPositionEntry(_guest);
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
    SinCosOut(regs, SetSinCos(_guest.State(), 7, regs.ax));
    RollStardust(_guest);
  }
  regs.cx = STARDUST_COUNT;
  regs.si = DS.stardust.offset;
  ComputeStardustShift(_guest);
  do
  {
    const std::uint16_t count = regs.cx;
    LoadDustPositionEntry(_guest);
    IsDustOnScreenEntry(_guest);
    if (_guest.Flag(FLAG_CARRY))
    {
      if (_guest.Get(DS.playerSpeed) != 0)
      {
        ScaleDustStepEntry(_guest);
        regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bp);
        regs.bx = static_cast<std::uint16_t>(regs.bx - regs.dx);
      }
      for (;;)
      {
        StoreDustPositionEntry(_guest);
        IsDustNearCenterEntry(_guest);
        if (!_guest.Flag(FLAG_CARRY))
        {
          const std::uint16_t lifetime = Plus(regs.si, PARTICLE_LIFETIME);
          _guest.SetByte(lifetime, static_cast<std::uint8_t>(_guest.Byte(lifetime) - 1));
          if (_guest.Byte(lifetime) != 0)
          {
            break;
          }
        }
        RespawnDustAnywhereEntry(_guest);
        StorePreviousDustPositionEntry(_guest);
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
    LoadDustPositionEntry(_guest);
    IsDustOnScreenEntry(_guest);
    if (_guest.Flag(FLAG_CARRY))
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
    ShiftStardustSideways(_guest);
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
    SinCosOut(regs, SetSinCos(_guest.State(), 7, regs.ax));
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

// _word on the five scanlines of the dashboard line at B800:_line.
void FillFiveLineWord(GameState& _state, std::uint16_t _line, std::uint16_t _word)
{
  for (const std::uint16_t scanline : FIVE_LINES)
  {
    _state.SetVideoWord(Plus(_line, scanline), _word);
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

// cmp [_shown], al; mov [_shown], al; je, with AL = _glyph: whether the glyph changed.
[[nodiscard]] bool ExchangeShown(GameState& _state, DataField<std::uint8_t> _shown, std::uint8_t _glyph)
{
  const bool changed = _state.Get(_shown) != _glyph;
  _state.Set(_shown, _glyph);
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
  if (!_guest.Flag(FLAG_CARRY))
  {
    _guest.Call(IS_STATION);
    if (_guest.Flag(FLAG_ZERO))
    {
      _guest.SetByte(collided, static_cast<std::uint8_t>(_guest.Byte(collided) & 0xFE));
    }
    return;
  }
  std::uint16_t damage = 0x5DC;
  _guest.Call(IS_STATION);
  if (!_guest.Flag(FLAG_ZERO))
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
      if (_guest.Flag(FLAG_CARRY))
      {
        // Aligned: docked, if inside the slot and the station is not the Thargoids'.
        if (_guest.Get(DS.thargoidInvasionActive) != 1)
        {
          regs.ax = _guest.Word(Plus(regs.di, SLOT_X));
          regs.bx = _guest.Word(Plus(regs.di, SLOT_Y));
          regs.cx = 0;
          regs.dx = 0x5A;
          _guest.Call(VECTOR_WITHIN_BOX);
          if (_guest.Flag(FLAG_CARRY) && (_guest.Byte(Plus(regs.di, SLOT_FLAGS)) & 1) == 0)
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
        if (_guest.Flag(FLAG_CARRY))
        {
          // Nearly aligned: a scrape if inside the slot, a crash if not.
          regs.ax = _guest.Word(Plus(regs.di, SLOT_X));
          regs.bx = _guest.Word(Plus(regs.di, SLOT_Y));
          regs.cx = 0;
          regs.dx = 0x6E;
          _guest.Call(VECTOR_WITHIN_BOX);
          damage = 0x190;
          if (_guest.Flag(FLAG_CARRY))
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
    remove = !_guest.Flag(FLAG_ZERO);
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

// SetWarning (0x36E7): posts warning _warning for 20 frames. Returns its text, warningTexts[_warning], which the
// original leaves in AX.
std::uint16_t SetWarning(GameState& _state, std::uint8_t _warning)
{
  _state.Set(DS.warningFrames, 0x14);
  _state.Set(DS.warningIndex, _warning);
  const std::uint16_t text = _state.Word(DS.warningTexts.At(_warning));
  _state.Set(DS.warningMessage, text);
  return text;
}

// CheckMissileWarning, CheckAltitudeWarning, CheckTemperatureWarning and CheckEnergyWarning (0x36FD,
// 0x370E, 0x371A, 0x3726): whether warning _check applies. The missile check clears its alert.
[[nodiscard]] bool WarningApplies(GameState& _state, std::uint16_t _check)
{
  switch (_check)
  {
  case 0:
  {
    const bool alert = _state.Get(DS.incomingMissileAlert) == 1;
    _state.Set(DS.incomingMissileAlert, 0);
    return alert;
  }
  case 1:
    return _state.Get(DS.altitude) < 0x32;
  case 2:
    return _state.Get(DS.cabinTemperature) >= 0xE1;
  default:
    return _state.Get(DS.playerEnergy) < 0x100;
  }
}

// What RunWarningChecks finds: the check it stopped at, the count LOOP leaves, and the text of the warning that check
// posted, if it posted one.
struct WarningChecks
{
  std::uint8_t check;
  std::uint16_t checksLeft;
  std::optional<std::uint16_t> text;
};

// The four checks round-robin from _first, each falling on to the next with LOOP while _checks lasts, until one posts its
// warning.
[[nodiscard]] WarningChecks RunWarningChecks(GameState& _state, std::uint16_t _first, std::uint16_t _checks)
{
  std::uint16_t check = _first;
  std::uint16_t checks = _checks;
  for (;;)
  {
    if (WarningApplies(_state, check))
    {
      const auto warning = static_cast<std::uint8_t>(check);
      return WarningChecks{warning, checks, SetWarning(_state, warning)};
    }
    if (--checks == 0)
    {
      return WarningChecks{static_cast<std::uint8_t>(check), checks, std::nullopt};
    }
    check = static_cast<std::uint16_t>((check + 1) & 3);
  }
}

// The register code's side of RunWarningChecks: each check loads AL with its number and LOOP counts CX down, and
// SetWarning leaves BX the warning's byte offset in warningTexts and AX its text.
void WarningChecksOut(Registers& _regs, const WarningChecks& _checks) noexcept
{
  SetLow(_regs.ax, _checks.check);
  _regs.cx = _checks.checksLeft;
  if (_checks.text)
  {
    _regs.bx = static_cast<std::uint16_t>(_checks.check << 1);
    _regs.ax = *_checks.text;
  }
}

// ---- The scanner and the compass ------------------------------------------------------------------

// 0x41FF-0x4217: AX = 8 * AX / CX, at most 7, where CX is at least AX.
void ScaleCompassAxis(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (regs.cx >= regs.ax)
  {
    regs.dx = SignWord(regs.ax);
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

// GetKey until a key comes: the waits at 0x0BF3 (HandleFlightFunctionKeys), 0x8153 (ProcessFlightKeys'
// Ctrl+Esc) and 0x8DC7 (RunPauseScreen), each a CALL at _call and a JE back to it. Out: AH = the key.
void WaitForKey(Guest& _guest, std::uint16_t _call)
{
  for (;;)
  {
    _guest.Call(GET_KEY);
    if (!_guest.Flag(FLAG_ZERO))
    {
      return;
    }
    _guest.JumpBack(_call);
  }
}

// PauseShowOptions (0x8D8F): the five option letters, in reverse video when on.
void ShowPauseOptions(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.di = PAUSE_FIRST_OPTION;
  regs.cx = PAUSE_OPTIONS;
  regs.si = DS.keyboardRecenter.offset;
  for (;;)
  {
    const std::uint16_t options = regs.cx;
    const std::uint16_t position = regs.di;
    const std::uint16_t option = regs.si;
    _guest.Set(DS.textPaperPattern, 0);
    regs.bx = ALL_COLORS;
    if (_guest.Byte(regs.si) == 1)
    {
      // xchg [textPaperPattern], bx
      regs.bx = _guest.Get(DS.textPaperPattern);
      _guest.Set(DS.textPaperPattern, ALL_COLORS);
    }
    SetLow(regs.ax, _guest.Byte(Plus(regs.si, 1)));
    _guest.Call(DRAW_VIEW_CHAR);
    regs.si = Plus(option, 2);
    regs.di = Plus(position, PAUSE_OPTION_STEP);
    regs.cx = options;
    if (--regs.cx == 0)
    {
      return;
    }
    _guest.JumpBack(0x8D98);
  }
}

// 0x8DD4-0x8E1E: R, D, Y, B and S toggle their option, with a beep. False for any other key.
[[nodiscard]] bool TogglePauseOption(Guest& _guest, std::uint8_t _key)
{
  constexpr std::array<std::uint8_t, PAUSE_OPTIONS> KEYS = {SCAN_R, SCAN_D, SCAN_Y, SCAN_B, SCAN_S};
  for (std::uint16_t index = 0; index < PAUSE_OPTIONS; ++index)
  {
    if (KEYS[index] == _key)
    {
      const auto option = static_cast<std::uint16_t>(DS.keyboardRecenter.offset + index * 2);
      _guest.SetByte(option, static_cast<std::uint8_t>(_guest.Byte(option) ^ 1));
      _guest.Call(START_BEEP);
      return true;
    }
  }
  return false;
}

// The pause screen from PauseShowOptions (0x8D8F): the options, then its keys. True when RunPauseScreen
// returns, false when an F key changed the frame rate and the menu is drawn again.
[[nodiscard]] bool RunPauseOptions(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  for (;;)
  {
    ShowPauseOptions(_guest);
    regs.ax = Guest::VIDEO_SEGMENT;
    regs.es = regs.ax;
    _guest.Call(PRESENT_SPACE_VIEW);
    for (;;)
    {
      WaitForKey(_guest, 0x8DC7);
      const std::uint8_t key = High(regs.ax);
      if (key == SCAN_SPACE)
      {
        // Resume, with the space that did it not fired.
        _guest.Call(START_BEEP);
        _guest.Call(RESET_KEYBOARD);
        _guest.Call(CLEAR_DRAW_BUFFER);
        _guest.Set(DS.gamePaused, 0);
        _guest.Set(DS.textPaperPattern, 0);
        _guest.Set(DS.keyDownSpace, 0);
        return true;
      }
      if (TogglePauseOption(_guest, key))
      {
        break;
      }
      if (key == SCAN_A)
      {
        // Abort to the title: its own return address and ProcessFlightKeys' are dropped, so the RET
        // leaves RunFlight for GameLoop.
        _guest.Call(RESET_KEYBOARD);
        _guest.Call(CLEAR_MESSAGE_LINE);
        _guest.Call(CLEAR_DRAW_BUFFER);
        _guest.Call(STOP_SOUND_EFFECTS);
        _guest.Set(DS.gamePaused, 0);
        _guest.Set(DS.textPaperPattern, 0);
        _guest.Set(DS.titleShown, 0);
        regs.ax = _guest.Pop();
        regs.ax = _guest.Pop();
        return true;
      }
      if (key < SCAN_F1 || key >= SCAN_PAST_F10)
      {
        _guest.JumpBack(0x8DC7);
        continue;
      }
      // F1-F10: the minimum frame time, and its key in the menu, 1-9 or 10 with a closing bracket.
      SetHigh(regs.ax, static_cast<std::uint8_t>(key - SCAN_F1));
      regs.bx = High(regs.ax);
      SetLow(regs.ax, _guest.Byte(Plus(DS.frameTimeChoices.offset, regs.bx)));
      _guest.Set(DS.minimumFrameMs, Low(regs.ax));
      SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + DIGIT_ONE));
      if (Low(regs.bx) == DIGIT_NINE + 1)
      {
        _guest.SetByte(DS.frameRateKeyLabel.offset, DIGIT_ONE);
        _guest.Set(DS.dataAC00, DIGIT_ZERO);
        _guest.Set(DS.dataAC01, CLOSING_BRACKET);
      }
      else
      {
        _guest.SetByte(DS.frameRateKeyLabel.offset, Low(regs.bx));
        _guest.Set(DS.dataAC00, CLOSING_BRACKET);
        _guest.Set(DS.dataAC01, SPACE);
      }
      _guest.JumpBack(0x8D6D);
      return false;
    }
    _guest.JumpBack(0x8D8F); // PauseShowOptions
  }
}

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

// push ax; call EraseCompassAndBlips; pop ax: a screen is about to be shown.
void EraseForFlightScreen(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.flightScreenShown, 1);
  const std::uint16_t key = regs.ax;
  _guest.Call(ERASE_COMPASS_AND_BLIPS);
  regs.ax = key;
}

// FlightScreenDispatch (0x0BF8): the screen for the key in AH, then the key that screen returns, until
// F1-F4. Every jump back in it is a turn of a loop that can wait.
void DispatchFlightScreens(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  for (;;)
  {
    const std::uint8_t key = High(regs.ax);
    if (key >= SCAN_F1 && key <= SCAN_F4)
    {
      _guest.JumpBack(0x0BDE);
      LeaveFlightScreens(_guest);
      return;
    }
    if (_guest.Get(DS.witchspaceCountdown) == 0)
    {
      EraseForFlightScreen(_guest);
      constexpr std::array<std::uint16_t, 4> SCREENS = {SHOW_GALACTIC_CHART, SHOW_SHORT_RANGE_CHART, SHOW_SYSTEM_DATA_SCREEN,
                                                        SHOW_MARKET_PRICES_SCREEN};
      if (key >= SCAN_F5 && key <= SCAN_F8)
      {
        _guest.Call(SCREENS[static_cast<std::size_t>(key - SCAN_F5)]);
        _guest.JumpBack(0x0BF8); // FlightScreenDispatch
        continue;
      }
    }
    else
    {
      // In witch space the short-range chart still shows once the countdown is down to 1, and F9 and
      // F10 show; the charts and the market need the navigation computer, which witch space jams.
      if (key == SCAN_F6)
      {
        if (_guest.Get(DS.witchspaceCountdown) == 1)
        {
          _guest.Set(DS.flightScreenShown, 1);
          _guest.Call(ERASE_COMPASS_AND_BLIPS);
          _guest.Call(SHOW_SHORT_RANGE_CHART);
          _guest.JumpBack(0x0BF8);
          continue;
        }
        _guest.JumpBack(0x0C6D);
      }
      if (key < SCAN_F9)
      {
        if (_guest.Get(DS.flightScreenShown) != 1)
        {
          _guest.Call(START_BEEP);
        }
        regs.ax = DS.navCompErrorMessage.offset;
        SetMessage(_guest.State(), regs.ax, 0x19);
        SetHigh(regs.ax, 0);
        _guest.JumpBack(0x0BDE);
        LeaveFlightScreens(_guest);
        return;
      }
      EraseForFlightScreen(_guest);
      _guest.JumpBack(0x0C45);
    }
    // 0x0C45: F9 and F10; any other key the screens return is waited past.
    if (key == SCAN_F9)
    {
      _guest.Call(RESET_KEYBOARD);
      _guest.Set(DS.inFlight, 1);
      _guest.Call(SHOW_COMMANDER_STATUS_SCREEN);
      _guest.Set(DS.inFlight, 0);
      _guest.JumpBack(0x0BF8);
      continue;
    }
    if (key == SCAN_F10)
    {
      _guest.Call(SHOW_INVENTORY_SCREEN);
      _guest.JumpBack(0x0BF8);
      continue;
    }
    _guest.JumpBack(0x0BF3);
    WaitForKey(_guest, 0x0BF3);
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
  _guest.Call(RESET_STARDUST);
}

// mov ax, _text; jmp back to 0x80A3, which posts it for 25 frames.
void PostHyperspaceRefusal(Guest& _guest, DataAt _text)
{
  _guest.Regs().ax = _text.offset;
  _guest.JumpBack(0x80A3);
  SetMessage(_guest.State(), _text.offset, 0x19);
}

// 0x8068-0x80FF: H, unless the docking computer is on or a countdown runs. True when the countdown
// started, which ends ProcessFlightKeys.
[[nodiscard]] bool PressHyperspace(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.dockingComputerOn) == 1 || _guest.Get(DS.hyperspaceCountdown) != 0)
  {
    _guest.JumpBack(0x8065); // past H
    return false;
  }
  SetLow(regs.ax,
         (_guest.Get(DS.invadedStationDestroyed) ^ 1) & _guest.Get(DS.thargoidInvasionActive) & _guest.Get(DS.jumpedSinceBriefing));
  if (Low(regs.ax) != 0)
  {
    regs.ax = DS.hyperspaceJammedMessage.offset;
    SetMessage(_guest.State(), regs.ax, 0x19);
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
      regs.ax = DS.noSystemSelectedMessage.offset;
      SetMessage(_guest.State(), regs.ax, 0x19);
      return false;
    }
    if (regs.ax >= 0x47)
    {
      PostHyperspaceRefusal(_guest, DS.outOfRangeMessage);
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
      PostHyperspaceRefusal(_guest, DS.notEnoughFuelMessage);
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
    regs.ax = DS.missilePrimedMessage.offset;
    SetMessage(_guest.State(), regs.ax, 0x0F);
  }
  if (_guest.Get(DS.missileState) == 1)
  {
    _guest.Call(FIND_SHIP_IN_CROSSHAIRS);
    if (_guest.Flag(FLAG_CARRY) && (_guest.Byte(Plus(regs.di, SLOT_FLAGS)) & 0x20) == 0)
    {
      _guest.Set(DS.missileTarget, regs.di);
      _guest.Set(DS.missileState, 2);
      _guest.Call(START_BEEP);
      regs.ax = DS.missileLockedMessage.offset;
      SetMessage(_guest.State(), regs.ax, 0x14);
    }
  }
  if (_guest.Get(DS.missileState) == 2)
  {
    regs.bx = _guest.Get(DS.missileTarget);
    if ((_guest.Byte(regs.bx) & 1) == 0)
    {
      _guest.Set(DS.missileState, 0);
      _guest.Call(START_LOW_BEEP);
      regs.ax = DS.missileTargetDestroyedMessage.offset;
      SetMessage(_guest.State(), regs.ax, 0x14);
    }
  }
  if (_guest.Get(DS.keyDownU) == 1 && _guest.Get(DS.missileState) != 0)
  {
    _guest.Set(DS.missileState, 0);
    _guest.Set(DS.shipIdRequested, 0);
    _guest.Call(START_LOW_BEEP);
    regs.ax = DS.missileUnarmedMessage.offset;
    SetMessage(_guest.State(), regs.ax, 0x0F);
  }
  if (_guest.Get(DS.keyDownM) == 1 && _guest.Get(DS.missileState) == 2 && _guest.Get(DS.missileJammed) != 1)
  {
    _guest.Call(NEXT_RANDOM);
    if (regs.ax < 0x1F4)
    {
      _guest.Set(DS.missileJammed, 1);
      regs.ax = DS.missileJammedMessage.offset;
      SetMessage(_guest.State(), regs.ax, 0x19);
      return;
    }
    _guest.Set(DS.missileState, 0);
    _guest.Set(DS.shipIdRequested, 0);
    regs.ax = DS.missileLaunchedMessage.offset;
    SetMessage(_guest.State(), regs.ax, 0x14);
    _guest.Set(DS.missileCount, static_cast<std::uint8_t>(_guest.Get(DS.missileCount) - 1));
    _guest.Call(LAUNCH_PLAYER_MISSILE);
  }
}

// 0x826F-0x829A: fire heats the laser by 5; a pulse laser (type 0) fires every other frame.
void HandleFireButton(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(READ_FIRE_BUTTON);
  if (!_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  _guest.Call(GET_VIEW_LASER);
  if (!_guest.Flag(FLAG_CARRY) || _guest.Get(DS.laserTemperature) >= 0xF0)
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
    regs.ax = DS.idIndicatorMessage.offset;
    SetMessage(_guest.State(), regs.ax, 0x19);
  }
  _guest.Call(FIND_SHIP_IN_CROSSHAIRS);
  if (!_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  _guest.Call(IS_STATION);
  if (_guest.Flag(FLAG_ZERO))
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

// 0x8379-0x839E: L runs the anti-ECM emulator, at one unit of energy a frame: SUB WORD [playerEnergy],1, and 0
// written over it on a borrow.
void HandleAntiEcmKey(GameState& _state)
{
  _state.Set(DS.antiEcmActive, 0);
  if (_state.Get(DS.keyDownL) != 1 || _state.Get(DS.antiEcmEmulatorFitted) != 1)
  {
    return;
  }
  _state.Set(DS.antiEcmActive, 1);
  const std::uint16_t energy = _state.Get(DS.playerEnergy);
  _state.Set(DS.playerEnergy, static_cast<std::uint16_t>(energy - 1));
  if (energy == 0)
  {
    _state.Set(DS.playerEnergy, 0);
  }
}

// 0x84F2-0x8586: pitch rotates the camera frame, from which the three angles are derived again.
void ApplyPitch(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = Negate(static_cast<std::uint16_t>(SignExtend(Low(regs.ax)) << 1));
  SinCosOut(regs, SetSinCos(_guest.State(), 7, regs.ax));
  regs.ax = Negate(_guest.Get(DS.playerRollAngle));
  SinCosOut(regs, SetSinCos(_guest.State(), 2, regs.ax));
  regs.ax = Negate(_guest.Get(DS.playerYawAngle));
  SinCosOut(regs, SetSinCos(_guest.State(), 1, regs.ax));
  regs.ax = Negate(_guest.Get(DS.playerPitchAngle));
  SinCosOut(regs, SetSinCos(_guest.State(), 0, regs.ax));

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
  ArcTangent2Entry(_guest);
  regs.ax = Negate(regs.ax);
  _guest.Set(DS.pitchAngleScratch, regs.ax);
  regs.ax = Negate(regs.ax);
  SinCosOut(regs, SetSinCos(_guest.State(), 6, regs.ax));
  regs.bx = forwardZ;
  regs.ax = forwardY;
  PairOut(regs, RotateByStoredSinCos(_guest.State(), 6, PairIn(regs)));
  regs.ax = forwardX;
  ArcTangent2Entry(_guest);
  regs.ax = Negate(regs.ax);
  _guest.Set(DS.yawAngleScratch, regs.ax);

  // The up axis, unrotated by pitch and yaw: roll from what is left.
  regs.ax = Negate(Plus(_guest.Get(DS.pitchAngleScratch), 0x400));
  SinCosOut(regs, SetSinCos(_guest.State(), 6, regs.ax));
  regs.ax = 0;
  regs.bx = 0xD8F0;
  regs.cx = 0;
  _guest.Call(ROTATE_BY_SIN_COS_7210);
  const std::uint16_t upX = regs.ax;
  regs.ax = regs.bx;
  regs.bx = regs.cx;
  PairOut(regs, RotateByStoredSinCos(_guest.State(), 6, PairIn(regs)));
  const std::uint16_t upY = regs.ax;
  const std::uint16_t upZ = regs.bx;
  regs.ax = Plus(_guest.Get(DS.yawAngleScratch), 0x400);
  SinCosOut(regs, SetSinCos(_guest.State(), 7, regs.ax));
  regs.bx = upZ;
  regs.cx = upY;
  regs.ax = upX;
  PairOut(regs, RotateByStoredSinCos(_guest.State(), 7, PairIn(regs)));
  regs.bx = upY;
  ArcTangent2Entry(_guest);
  _guest.Set(DS.playerRollAngle, regs.ax);
  regs.ax = Negate(_guest.Get(DS.yawAngleScratch));
  _guest.Set(DS.playerYawAngle, regs.ax);
  regs.ax = Negate(_guest.Get(DS.pitchAngleScratch));
  _guest.Set(DS.playerPitchAngle, regs.ax);
  _guest.Set(DS.velocityDirty, 1);
}

// 0x2A57-0x2B53: the sun in slot 0, the planet in slot 1 and the station in slot 2, each byte written as the original
// writes it.
void PlaceSunPlanetAndStation(GameState& _state)
{
  // The sun, at random far behind: the top 16 bits of x and y within 200h of 0, of z C00h to 801h behind.
  ObjectSlot sun(_state, DS.shipSlots.offset);
  sun.Set(SlotByte::Type, SUN_TYPE);
  const auto randomTop = [&_state](std::uint16_t _less) { return static_cast<std::uint16_t>((NextRandom(_state) & 0x3FF) - _less); };
  std::uint16_t top = randomTop(0x200);
  sun.Set(SlotByte::XMiddle, Low(top));
  sun.Set(SlotByte::XHigh, High(top));
  top = randomTop(0x200);
  sun.Set(SlotByte::YMiddle, Low(top));
  sun.Set(SlotByte::YHigh, High(top));
  top = randomTop(0xC00);
  sun.Set(SlotByte::ZHigh, High(top));
  sun.Set(SlotByte::ZMiddle, Low(top));
  // Colour 1 or 2 from bit 8 of the system's first seed, written as a word.
  sun.Set(SlotWord::Color, static_cast<std::uint16_t>((High(_state.Get(DS.systemSeed0)) & 1) + 1));
  sun.Set(SlotByte::Class, 0);
  sun.Set(SlotByte::Flags, SUN_OR_PLANET_FLAGS);
  sun.Set(SlotByte::Detail, 0xFF);

  // The planet, straight ahead at z = 6E00h, its colour from bit 0 of the seed.
  ObjectSlot planet(_state, Plus(sun.Offset(), ObjectSlot::BYTES));
  planet.Set(SlotByte::Type, PLANET_TYPE);
  planet.Set(SlotWord::X, 0);
  planet.Set(SlotByte::XHigh, 0);
  planet.Set(SlotWord::Y, 0);
  planet.Set(SlotByte::YHigh, 0);
  planet.Set(SlotByte::ZMiddle, PLANET_Z_TOP);
  planet.Set(SlotByte::ZHigh, 0);
  planet.Set(SlotByte::ZLow, 0);
  planet.Set(SlotWord::Color, static_cast<std::uint16_t>((Low(_state.Get(DS.systemSeed0)) & 1) + 1));
  planet.Set(SlotByte::Class, 0);
  planet.Set(SlotByte::Flags, SUN_OR_PLANET_FLAGS);
  planet.Set(SlotByte::Detail, 0xFF);

  // The station, just behind at z = -300: a Dodo (type 0) at tech level 9 and up, else a Coriolis (type 1), with
  // 10-17 in its Thargons byte. While the Thargoids invade it has no missiles, Thargons or aggression, and is not
  // hostile.
  ObjectSlot station(_state, Plus(planet.Offset(), ObjectSlot::BYTES));
  const std::uint8_t stationType = _state.Get(DS.currentTechLevel) >= 9 ? 0 : 1;
  station.Set(SlotByte::Type, static_cast<std::uint8_t>((stationType << 1) | ObjectSlot::ACTIVE));
  station.Set(SlotWord::Yaw, STATION_YAW);
  station.Set(SlotWord::Roll, 0);
  station.Set(SlotWord::Pitch, 0);
  station.Set(SlotWord::X, 0);
  station.Set(SlotByte::XHigh, 0);
  station.Set(SlotWord::Y, 0);
  station.Set(SlotByte::YHigh, 0);
  station.Set(SlotWord::Z, STATION_Z);
  station.Set(SlotByte::ZHigh, Low(SignWord(STATION_Z)));
  station.Set(SlotByte::Flags, STATION_FLAGS);
  station.Set(SlotByte::Class, 1);
  station.Set(SlotByte::Thargons, static_cast<std::uint8_t>((High(NextRandom(_state)) & 7) + 0x0A));
  station.Set(SlotByte::Detail, 0xFF);
  station.Set(SlotByte::Energy, STATION_ENERGY);
  station.Set(SlotByte::Bounty, 0);
  if (_state.Get(DS.thargoidInvasionActive) == 1)
  {
    station.Set(SlotByte::Missiles, 0);
    station.Set(SlotByte::Aggression, 0);
    station.Set(SlotByte::Energy, STATION_ENERGY);
    station.Set(SlotByte::Thargons, 0);
    station.Set(SlotByte::Fragments, 0x0A);
    station.Set(SlotByte::Flags, static_cast<std::uint8_t>(station.Get(SlotByte::Flags) & 0xFE));
  }
  _state.Set(DS.spawnGovernment, _state.Get(DS.currentGovernment));
}

// sub word [si+_low], ax; sbb byte [si+_high], dl; sub word [si+_compass], ax, with DL:AX = _velocityHigh:_velocity:
// a 24-bit coordinate of _slot and its compass word, less the velocity. Every slot has its compass words moved,
// though only the station's are the compass's.
void SubtractVelocity(ObjectSlot& _slot, SlotByte _high, SlotWord _low, SlotWord _compass, std::uint16_t _velocity,
                      std::uint8_t _velocityHigh)
{
  const std::uint16_t before = _slot.Get(_low);
  const int borrow = before < _velocity ? 1 : 0;
  _slot.Set(_low, static_cast<std::uint16_t>(before - _velocity));
  _slot.Set(_high, static_cast<std::uint8_t>(_slot.Get(_high) - _velocityHigh - borrow));
  _slot.Set(_compass, static_cast<std::uint16_t>(_slot.Get(_compass) - _velocity));
}

} // namespace

void UpdateStardust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.jumpDriveEngaged) != 0)
  {
    SaveStardustPositionsEntry(_guest);
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

std::optional<DustScreenPosition> GetPreviousDustScreenPosition(const GameState& _state, std::uint16_t _particle)
{
  const DustPosition previous{Signed(_state.Word(Plus(_particle, PREVIOUS_X))), Signed(_state.Word(Plus(_particle, PREVIOUS_Y)))};
  if (!IsDustOnScreen(previous))
  {
    return std::nullopt;
  }
  return DustToScreen(previous);
}

std::uint16_t ComputeDustStripMask(std::int16_t _step)
{
  // SAR until the step is 0 or -1, a bit more of the mask for each halving before that.
  std::uint16_t step = Word(_step);
  std::uint16_t mask = 0;
  for (;;)
  {
    step = Sar(step, 1);
    if (step == 0xFFFF || step == 0)
    {
      return mask;
    }
    mask = static_cast<std::uint16_t>((mask << 1) | 1);
  }
}

void ShiftStardustSideways(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  ComputeDustStripMaskEntry(_guest);
  regs.cx = STARDUST_COUNT;
  regs.si = DS.stardust.offset;
  do
  {
    LoadDustPositionEntry(_guest);
    IsDustOnScreenEntry(_guest);
    if (_guest.Flag(FLAG_CARRY))
    {
      regs.ax = Plus(regs.ax, regs.dx);
      IsDustOnScreenEntry(_guest);
      if (!_guest.Flag(FLAG_CARRY))
      {
        RespawnDustAtSideEdgeEntry(_guest);
        StorePreviousDustPositionEntry(_guest);
      }
      StoreDustPositionEntry(_guest);
    }
    regs.si = Plus(regs.si, PARTICLE_BYTES);
  } while (--regs.cx != 0);
}

DustPosition RespawnDustAtSideEdge(GameState& _state, std::uint16_t _particle, std::int16_t _step, std::uint16_t _stripMask)
{
  const std::uint16_t first = NextRandom(_state);
  MarkDustRespawned(_state, _particle, RandomLifetime(first));
  const std::uint16_t y = RandomDustY(first);
  const auto strip = static_cast<std::uint16_t>(NextRandom(_state) & _stripMask);
  // Moving right the particle enters at the left edge, moving left at the right.
  const std::uint16_t x = _step < 0 ? static_cast<std::uint16_t>(0x1F00 - strip) : Plus(strip, 0xE000);
  return DustPosition{Signed(x), Signed(y)};
}

void RollStardust(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.cx = STARDUST_COUNT;
  regs.si = DS.stardust.offset;
  do
  {
    LoadDustPositionEntry(_guest);
    PairOut(regs, RotateByStoredSinCos(_guest.State(), 7, PairIn(regs)));
    StoreDustPositionEntry(_guest);
    regs.si = Plus(regs.si, PARTICLE_BYTES);
  } while (--regs.cx != 0);
}

void ShiftStardustVertically(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  ComputeDustStripMaskEntry(_guest);
  regs.si = DS.stardust.offset;
  regs.cx = STARDUST_COUNT;
  regs.ax = 0;
  do
  {
    LoadDustPositionEntry(_guest);
    IsDustOnScreenEntry(_guest);
    if (_guest.Flag(FLAG_CARRY))
    {
      regs.bx = Plus(regs.bx, regs.dx);
      IsDustOnScreenEntry(_guest);
      if (!_guest.Flag(FLAG_CARRY))
      {
        RespawnDustAtVerticalEdgeEntry(_guest);
        StorePreviousDustPositionEntry(_guest);
      }
      StoreDustPositionEntry(_guest);
    }
    regs.si = Plus(regs.si, PARTICLE_BYTES);
  } while (--regs.cx != 0);
}

DustPosition RespawnDustAtVerticalEdge(GameState& _state, std::uint16_t _particle, std::int16_t _step, std::uint16_t _stripMask)
{
  const std::uint16_t first = NextRandom(_state);
  MarkDustRespawned(_state, _particle, RandomLifetime(first));
  const std::uint16_t x = RandomDustX(first);
  const auto strip = static_cast<std::uint16_t>(NextRandom(_state) & _stripMask);
  // Moving down the particle enters at the top edge, moving up at the bottom.
  const std::uint16_t y = _step < 0 ? static_cast<std::uint16_t>(0x0F00 - strip) : Plus(0xF000, strip);
  return DustPosition{Signed(x), Signed(y)};
}

DustPosition RespawnDustAnywhere(GameState& _state, std::uint16_t _particle)
{
  MarkDustRespawned(_state, _particle, RandomLifetime(NextRandom(_state)));
  const std::uint16_t random = NextRandom(_state);
  return DustPosition{Signed(RandomDustX(random)), Signed(RandomDustY(Swap(random)))};
}

bool IsDustOnScreen(DustPosition _position)
{
  const std::int8_t x = WholePart(_position.x);
  const std::int8_t y = WholePart(_position.y);
  return x >= -0x20 && x <= 0x1F && y >= -0x10 && y <= 0x0F;
}

bool IsDustNearCenter(DustPosition _position)
{
  const std::int8_t x = WholePart(_position.x);
  const std::int8_t y = WholePart(_position.y);
  return x >= -6 && x <= 6 && y >= -3 && y <= 3;
}

DustStep ScaleDustStep(const GameState& _state, DustPosition _position)
{
  const std::uint8_t shift = _state.Get(DS.stardustShift);
  return DustStep{Signed(Sar(Word(_position.x), shift)), Signed(Sar(Word(_position.y), shift)), shift};
}

DustPosition LoadDustPosition(const GameState& _state, std::uint16_t _particle)
{
  return DustPosition{Signed(_state.Word(_particle)), Signed(_state.Word(Plus(_particle, 2)))};
}

void StoreDustPosition(GameState& _state, std::uint16_t _particle, DustPosition _position)
{
  _state.SetWord(_particle, Word(_position.x));
  _state.SetWord(Plus(_particle, 2), Word(_position.y));
}

void StorePreviousDustPosition(GameState& _state, std::uint16_t _particle, DustPosition _position)
{
  _state.SetWord(Plus(_particle, PREVIOUS_X), Word(_position.x));
  _state.SetWord(Plus(_particle, PREVIOUS_Y), Word(_position.y));
}

DustScreenPosition DustToScreen(DustPosition _position)
{
  // shl ax,1 / xchg ah,al / shl ah,1 / cbw / rcl al,1: bits 13-6 in AL, AH the sign of bit 14.
  const auto toScreen = [](std::int16_t _value, std::uint16_t _center)
  {
    const auto doubled = static_cast<std::uint16_t>(Word(_value) << 1);
    const std::uint8_t high = High(doubled);
    std::uint16_t result = SignExtend(high);
    SetLow(result, static_cast<std::uint8_t>((high << 1) | ((doubled & 0x80) != 0 ? 1 : 0)));
    return Signed(Plus(result, _center));
  };
  return DustScreenPosition{toScreen(_position.x, 0x80), toScreen(_position.y, 0x40)};
}

std::uint16_t ResetStardust(GameState& _state)
{
  // MOV AX,[messageFrames] / AND AL,AL / JNZ / XOR AH,AH / MOV [messageFrames],AX: the word cleared when its low byte is 0.
  if (Low(_state.Get(DS.messageFrames)) == 0)
  {
    _state.Set(DS.messageFrames, 0);
  }
  // Coordinates within 23h of the centre in the high byte, retried until one is.
  const auto randomCoordinate = [&_state]()
  {
    for (;;)
    {
      const std::uint16_t coordinate = Sar(NextRandom(_state), 1);
      const auto high = static_cast<std::int8_t>(High(coordinate));
      if (high >= -0x23 && high <= 0x23)
      {
        return coordinate;
      }
    }
  };
  std::uint16_t lifetimeRandom = 0;
  for (std::uint16_t particle = DS.stardust.offset; particle != Plus(DS.stardust.offset, STARDUST_BYTES);
       particle = Plus(particle, PARTICLE_BYTES))
  {
    _state.SetWord(particle, randomCoordinate());
    _state.SetWord(Plus(particle, 2), randomCoordinate());
    lifetimeRandom = NextRandom(_state);
    _state.SetByte(Plus(particle, PARTICLE_LIFETIME), RandomLifetime(lifetimeRandom));
  }
  return lifetimeRandom;
}

void SaveStardustPositions(GameState& _state)
{
  // REP MOVSB with ES = DS, upwards: DF is clear wherever the reference calls it (its one STD, at 2E4A, is
  // cleared again at 2E5C, with no call between).
  for (std::uint16_t index = 0; index < STARDUST_BYTES; ++index)
  {
    _state.SetByte(Plus(DS.stardustPrevious.offset, index), _state.Byte(Plus(DS.stardust.offset, index)));
  }
}

void HandleFlightFunctionKeys(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.inFlight, 0);
  regs.cx = 10;
  regs.bx = SCAN_F1;
  for (;;)
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
    if (--regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(0x0BBE);
  }
  SetHigh(regs.ax, 0);
  _guest.Set(DS.inFlight, 1);
}

void RestoreFlightScreen(Guest& _guest)
{
  _guest.Call(SHOW_COCKPIT_SCREEN);
  InvalidateDashboardEntry(_guest);
  _guest.Call(START_BEEP);
  _guest.Set(DS.messageShown, 0);
}

void InvalidateDashboard(GameState& _state)
{
  // REP STOSB with ES = DS, upwards: DF is clear wherever the reference calls it, as for SaveStardustPositions.
  for (std::uint16_t index = 0; index < DASHBOARD_CACHE_BYTES; ++index)
  {
    _state.SetByte(Plus(DS.missileCountShown.offset, index), DASHBOARD_STALE);
  }
}

void UpdateDashboard(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  UpdateConditionColorEntry(_guest);
  DrawConditionLightEntry(_guest);
  UpdateEnergyAndLaserHeat(_guest);
  UpdateSafeZone(_guest);
  InSafeZoneEntry(_guest);

  // The S glyph inside the station's safe zone, and E for a frame when the ECM fires.
  SetLow(regs.ax, _guest.Flag(FLAG_CARRY) ? 0x82 : 0x20);
  regs.di = 0x1658;
  regs.bx = 0xFFFF;
  if (ExchangeShown(_guest.State(), DS.safeZoneGlyphShown, Low(regs.ax)))
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
  if (ExchangeShown(_guest.State(), DS.ecmGlyphShown, Low(regs.ax)))
  {
    _guest.Call(DRAW_SCREEN_CHAR);
  }

  DrawEnergyBanks(_guest);
  DrawMissileLockIndicatorEntry(_guest);
  DrawMissileIconsEntry(_guest);

  regs.ax = _guest.Get(DS.rollRate);
  SetLow(regs.ax, High(regs.ax));
  if (Low(regs.ax) != _guest.Get(DS.pitchRateShown))
  {
    _guest.Set(DS.pitchRateShown, Low(regs.ax));
    regs.di = 0x3938;
    DrawSignedIndicatorEntry(_guest);
  }
  regs.ax = _guest.Get(DS.rollRate);
  if (Low(regs.ax) != _guest.Get(DS.rollRateShown))
  {
    _guest.Set(DS.rollRateShown, Low(regs.ax));
    regs.di = 0x37F8;
    DrawSignedIndicatorEntry(_guest);
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

void DrawSignedIndicator(GameState& _state, std::uint16_t _line, std::int8_t _value)
{
  std::int8_t value = _value;
  if (value <= -24)
  {
    value = -23;
  }
  if (value >= 24)
  {
    value = 23;
  }
  // The strip: six words of the background, 48 pixels in colour 2.
  for (std::uint16_t word = 0; word < INDICATOR_WORDS; ++word)
  {
    FillFiveLineWord(_state, Plus(_line, word * 2), BACKGROUND_WORD);
  }
  // The marker: a word of indicatorMarkers at pixel value + 23, or its first byte alone at the right end.
  const auto pixel = static_cast<std::uint8_t>(INDICATOR_CENTER + value);
  const auto column = static_cast<std::uint16_t>((pixel >> 2) & 0x0F);
  const std::uint16_t at = Plus(_line, column);
  const std::uint16_t marker = _state.Word(Plus(DS.indicatorMarkers.offset, static_cast<std::uint16_t>((pixel & 3) << 1)));
  if (column == INDICATOR_LAST_COLUMN)
  {
    for (const std::uint16_t scanline : FIVE_LINES)
    {
      _state.SetVideoByte(Plus(at, scanline), Low(marker));
    }
    return;
  }
  FillFiveLineWord(_state, at, marker);
}

void DrawThreeLineBar(Guest& _guest)
{
  ScaleBarValue(_guest);
  DrawBarPixels(_guest, THREE_LINES);
}

bool DrawMissileIcons(GameState& _state)
{
  const std::uint8_t count = _state.Get(DS.missileCount);
  if (count == _state.Get(DS.missileCountShown))
  {
    return false;
  }
  _state.Set(DS.missileCountShown, count);
  // A word a cell: the icons for the count's low two bits, then the background to the fourth cell, a byte count that
  // wraps past four missiles.
  std::uint16_t cell = MISSILE_ICONS_LINE;
  for (std::uint16_t icon = 0; icon < (count & 3u); ++icon)
  {
    // missileIcon holds the icon's five lines in bank order: odd, even, odd, even, odd.
    constexpr std::array<std::uint16_t, 5> ICON_WORDS = {0, 4, 8, 2, 6};
    for (std::size_t index = 0; index < FIVE_LINES.size(); ++index)
    {
      _state.SetVideoWord(Plus(cell, FIVE_LINES[index]), _state.Word(Plus(DS.missileIcon.offset, ICON_WORDS[index])));
    }
    cell = Plus(cell, 2);
  }
  for (auto cells = static_cast<std::uint8_t>(MISSILE_CELLS - count); cells != 0; --cells)
  {
    FillFiveLineWord(_state, cell, BACKGROUND_WORD);
    cell = Plus(cell, 2);
  }
  return true;
}

bool DrawMissileLockIndicator(GameState& _state)
{
  const std::uint8_t missileState = _state.Get(DS.missileState);
  if (missileState == _state.Get(DS.missileLockShown))
  {
    return false;
  }
  _state.Set(DS.missileLockShown, missileState);
  const std::uint8_t fill = _state.Byte(DS.colorFillBytes.At(missileState & 3u));
  const std::uint16_t fillWord = Join(fill, fill);
  // Three bytes on each of six lines: a word and a byte in the odd bank, then the same in the even.
  std::uint16_t line = MISSILE_LOCK_LINE;
  for (std::uint16_t pair = 0; pair < MISSILE_LOCK_LINE_PAIRS; ++pair)
  {
    _state.SetVideoWord(line, fillWord);
    _state.SetVideoByte(Plus(line, 2), fill);
    _state.SetVideoWord(Plus(line, EVEN_BANK), fillWord);
    _state.SetVideoByte(Plus(line, EVEN_BANK + 2), fill);
    line = Plus(line, NEXT_LINE_PAIR);
  }
  return true;
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
      SetLow(regs.ax, static_cast<std::uint8_t>((_guest.Get(DS.energyUnitFitted) << 1) + 1));
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
  NextRandomEntry(_guest);
  if (regs.ax >= 0x32)
  {
    return;
  }
  NextRandomEntry(_guest);
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
  regs.ax = DS.equipmentLossMessage.offset;
  SetMessage(_guest.State(), regs.ax, 0x1E);
}

void DrawConditionLight(GameState& _state)
{
  std::uint8_t color = _state.Get(DS.conditionColor);
  if (color == 0)
  {
    // Flashing: black and red, on bit 9 of millisecondCounter.
    color = static_cast<std::uint8_t>(High(_state.Get(DS.millisecondCounter)) & 2);
  }
  if (color == _state.Get(DS.conditionColorShown))
  {
    return;
  }
  _state.Set(DS.conditionColorShown, color);
  const std::uint8_t fill = _state.Byte(DS.colorFillBytes.At(color));
  const std::uint16_t mask = Join(fill, fill);
  // The bitmap's words, byte-swapped and masked by the colour: a line in the odd bank, then the even line below.
  std::uint16_t line = CONDITION_LIGHT_LINE;
  for (std::size_t pair = 0; pair < CONDITION_LIGHT_LINE_PAIRS; ++pair)
  {
    _state.SetVideoWord(line, static_cast<std::uint16_t>(Swap(_state.Word(DS.conditionLightBitmap.At(pair * 2))) & mask));
    line = Plus(line, EVEN_BANK);
    _state.SetVideoWord(line, static_cast<std::uint16_t>(Swap(_state.Word(DS.conditionLightBitmap.At(pair * 2 + 1))) & mask));
    line = Plus(line, NEXT_ODD_LINE);
  }
}

std::uint8_t UpdateConditionColor(GameState& _state)
{
  const std::uint16_t energy = _state.Get(DS.playerEnergy);
  const std::uint8_t cabin = _state.Get(DS.cabinTemperature);
  const std::uint8_t altitude = _state.Get(DS.altitude);
  const std::uint8_t fore = _state.Get(DS.foreShield);
  const std::uint8_t aft = _state.Get(DS.aftShield);
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
  _state.Set(DS.conditionColor, color);
  return color;
}

void SetUpLocalSpace(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.cx, _guest.Get(DS.currentSystemIndex));
  _guest.Call(LOAD_SYSTEM_SEEDS);
  InvalidateDashboardEntry(_guest);
  _guest.Set(DS.objectSlotCount, 0x14);
  _guest.Set(DS.debrisSlotCount, 0x10);
  _guest.Set(DS.shipSlotCount, 0x24);
  _guest.Call(CLEAR_ALL_OBJECTS);
  _guest.Call(SHOW_COCKPIT_SCREEN);
  ResetStardustEntry(_guest);
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
  // The original leaves AX, DX and DI as the placing leaves them; poisoned, no caller read them.
  PlaceSunPlanetAndStation(_guest.State());
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

SafeZone InSafeZone(const GameState& _state)
{
  const std::uint8_t flags = _state.Get(DS.safeZoneFlags);
  return SafeZone{(flags & 1) != 0, static_cast<std::uint8_t>(flags >> 1)};
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
  if (!_guest.Flag(FLAG_ZERO))
  {
    _guest.Set(DS.safeZoneFlags, 0);
    return;
  }
  _guest.Call(IS_OBJECT_NEAR);
  bool inside = false;
  if (_guest.Flag(FLAG_CARRY))
  {
    regs.ax = _guest.Word(Plus(regs.di, SLOT_X));
    regs.bx = _guest.Word(Plus(regs.di, SLOT_Y));
    regs.cx = _guest.Word(Plus(regs.di, SLOT_Z));
    _guest.Call(VECTOR_LENGTH);
    inside = regs.ax < 0x32C8;
  }
  // rcl al,1: bit 0 the answer, the rest whatever AL held.
  SetLow(regs.ax, static_cast<std::uint8_t>((Low(regs.ax) << 1) | (inside ? 1 : 0)));
  _guest.Set(DS.safeZoneFlags, Low(regs.ax));
}

void ComputeDeathDebrisVector(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = Negate(_guest.Get(DS.viewAngle));
  SinCosOut(regs, SetSinCos(_guest.State(), 8, regs.ax));
  regs.ax = Negate(Plus(_guest.Get(DS.playerPitchAngle), 0x400));
  SinCosOut(regs, SetSinCos(_guest.State(), 6, regs.ax));
  regs.ax = Plus(_guest.Get(DS.playerYawAngle), 0x400);
  SinCosOut(regs, SetSinCos(_guest.State(), 7, regs.ax));
  // (0, 40, 0), turned by the view and, off the front view, by the roll, then by yaw and pitch.
  regs.ax = 0;
  regs.bx = 0x28;
  regs.cx = 0;
  if (_guest.Get(DS.viewAngle) != 0)
  {
    PairOut(regs, RotateByStoredSinCos(_guest.State(), 8, PairIn(regs)));
    const std::uint16_t x = regs.ax;
    const std::uint16_t y = regs.bx;
    const std::uint16_t z = regs.cx;
    regs.ax = Negate(_guest.Get(DS.playerRollAngle));
    SinCosOut(regs, SetSinCos(_guest.State(), 8, regs.ax));
    // push ax, bx, cx; pop bx, cx, ax: y and z change places.
    regs.bx = z;
    regs.cx = y;
    regs.ax = x;
    PairOut(regs, RotateByStoredSinCos(_guest.State(), 8, PairIn(regs)));
    std::swap(regs.cx, regs.bx);
  }
  PairOut(regs, RotateByStoredSinCos(_guest.State(), 7, PairIn(regs)));
  std::swap(regs.cx, regs.ax);
  PairOut(regs, RotateByStoredSinCos(_guest.State(), 6, PairIn(regs)));
  std::swap(regs.cx, regs.ax);
  std::swap(regs.cx, regs.bx);
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
  // The four checks from the one after the last warning (jmp [bx+warningChecks]).
  regs.cx = 4;
  SetLow(regs.bx, _guest.Get(DS.warningIndex) + 1);
  regs.bx = static_cast<std::uint16_t>((regs.bx & 3) << 1);
  WarningChecksOut(regs, RunWarningChecks(_guest.State(), static_cast<std::uint16_t>(regs.bx >> 1), regs.cx));
}

void CheckMissileWarning(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  WarningChecksOut(regs, RunWarningChecks(_guest.State(), 0, regs.cx));
}

void CheckAltitudeWarning(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  WarningChecksOut(regs, RunWarningChecks(_guest.State(), 1, regs.cx));
}

void CheckTemperatureWarning(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  WarningChecksOut(regs, RunWarningChecks(_guest.State(), 2, regs.cx));
}

void CheckEnergyWarning(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  WarningChecksOut(regs, RunWarningChecks(_guest.State(), 3, regs.cx));
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
  if (_guest.Flag(FLAG_ZERO))
  {
    return;
  }
  _guest.Call(IS_SUN_OR_PLANET);
  regs.ax = regs.dx;
  if (_guest.Flag(FLAG_ZERO))
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
    XorScannerBlipEntry(_guest);
    regs.di = slot;
  }
  SetHigh(regs.ax, _guest.Byte(blip));
  SetHigh(regs.bx, _guest.Byte(Plus(blip, 1)));
  SetHigh(regs.cx, _guest.Byte(Plus(blip, 2)));
  XorScannerBlipEntry(_guest);
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
  if (_guest.Flag(FLAG_CARRY))
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
    XorCompassDotEntry(_guest);
  }
  regs.ax = SignExtend(_guest.Byte(front));
  regs.bp = regs.ax;
  regs.dx = _guest.Word(dot);
  const std::uint16_t flags = Plus(regs.di, SLOT_FLAGS);
  _guest.SetByte(flags, static_cast<std::uint8_t>(_guest.Byte(flags) | FLAG_BLIP_DRAWN));
  XorCompassDotEntry(_guest);
}

DashboardPixel XorCompassDot(GameState& _state, std::uint8_t _x, std::uint8_t _y, bool _inFront)
{
  if (_inFront)
  {
    (void)XorDashboardPixel(_state, _x, _y);
  }
  DashboardPixel pixel{_x, _y, 0};
  for (const DotStep step : COMPASS_RING)
  {
    pixel.x = static_cast<std::uint8_t>(pixel.x + step.x);
    pixel.y = static_cast<std::uint8_t>(pixel.y + step.y);
    pixel.mask = XorDashboardPixel(_state, pixel.x, pixel.y);
  }
  return pixel;
}

void EraseScannerBlip(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint16_t saved = regs.ax;
  _guest.Call(IS_STATION);
  regs.ax = saved;
  if (_guest.Flag(FLAG_ZERO))
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
  XorScannerBlipEntry(_guest);
  regs.di = slot;
  _guest.SetByte(flags, static_cast<std::uint8_t>(_guest.Byte(flags) & ~FLAG_BLIP_DRAWN));
}

DashboardPixel XorScannerBlip(GameState& _state, std::uint8_t _x, std::uint8_t _y, std::uint8_t _z)
{
  const std::uint8_t y = Sar(_y, 2);
  const std::uint8_t z = Sar(_z, 2);
  DashboardPixel pixel{static_cast<std::uint8_t>(_x + 0x3D), static_cast<std::uint8_t>(0x1F - z), 0};
  // A stick of |y| pixels from the dot, up or down, then one more to the right at its end. The JNS after DEC CL never
  // falls through: |y| is at most 20h, and the test for 0 stops the stick first.
  auto length = y;
  std::uint8_t step = 1;
  if ((length & 0x80) != 0)
  {
    length = Negate(length);
    step = 0xFF;
  }
  for (;;)
  {
    pixel.mask = XorDashboardPixel(_state, pixel.x, pixel.y);
    if (length == 0)
    {
      break;
    }
    pixel.y = static_cast<std::uint8_t>(pixel.y + step);
    --length;
    if ((length & 0x80) != 0)
    {
      break;
    }
  }
  ++pixel.x;
  pixel.mask = XorDashboardPixel(_state, pixel.x, pixel.y);
  return pixel;
}

std::uint8_t XorDashboardPixel(GameState& _state, std::uint8_t _x, std::uint8_t _y)
{
  // The bank from y's parity, 80 bytes a pair of lines, 4 pixels a byte.
  const auto offset =
    static_cast<std::uint16_t>(((_y & 1) != 0 ? EVEN_BANK : 0) + (_y >> 1) * NEXT_LINE_PAIR + (_x >> 2) + DASHBOARD_ORIGIN);
  const std::uint8_t mask = _state.Byte(DS.dashboardPixelMasks.At(_x & 3u));
  _state.SetVideoByte(offset, static_cast<std::uint8_t>(_state.VideoByte(offset) ^ mask));
  return mask;
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
    XorCompassDotEntry(_guest);
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

void RunFlight(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(RESET_MOUSE_IF_SELECTED);
  _guest.Call(SET_UP_LOCAL_SPACE);
  _guest.Call(PLAY_STATION_TUNNEL);
  for (;;)
  {
    // FlightFrameLoop (0x7EA4): one frame.
    _guest.Call(UPDATE_DASHBOARD);
    _guest.Call(TRANSFORM_AND_DRAW_OBJECTS);
    _guest.Call(APPLY_ENEMY_LASER_HIT);
    _guest.Call(RESOLVE_LASER_FIRE);
    _guest.Call(UPDATE_STARDUST);
    _guest.Call(UPDATE_FUEL_LEAK);
    _guest.Call(UPDATE_MESSAGE_LINE);
    _guest.Call(POLL_SCREEN_DUMP_KEY);
    _guest.Call(FINISH_SPACE_VIEW_FRAME);
    _guest.Call(UPDATE_OBJECTS_AND_SPAWN);
    _guest.Call(UPDATE_PLAYER_MOTION);
    _guest.Call(CHECK_COLLISIONS);
    if (_guest.Get(DS.playerDocked) == 1)
    {
      _guest.Call(PLAY_STATION_TUNNEL);
      _guest.Call(RESET_KEYBOARD);
      _guest.Call(STOP_SOUND_EFFECTS);
      _guest.Call(STOP_ALL_SOUND);
      return;
    }
    _guest.Call(PROCESS_FLIGHT_KEYS);
    _guest.Call(TICK_HYPERSPACE_COUNTDOWN);
    // When the escape pod arrives this returns past RunFlight, to its caller.
    _guest.Call(TICK_ESCAPE_POD);
    if (_guest.Get(DS.playerDead) != 1 || _guest.Get(DS.cheatEnabled) == 1)
    {
      _guest.JumpBack(0x7EA4); // FlightFrameLoop
      continue;
    }
    // Dead: GAME OVER for 40 frames, then the title's ELITE on the message line, and back.
    _guest.Set(DS.hyperspaceCountdown, 0);
    if (_guest.Get(DS.gameOverFrames) == 0)
    {
      _guest.Call(SPAWN_PLAYER_WRECKAGE);
      _guest.Set(DS.gameOverFrames, 0x28);
      regs.ax = DS.gameOverMessage.offset;
      SetMessage(_guest.State(), regs.ax, 0x28);
      _guest.Call(STOP_CONTINUOUS_NOISE);
      _guest.JumpBack(0x7EA4);
      continue;
    }
    _guest.Set(DS.gameOverFrames, static_cast<std::uint8_t>(_guest.Get(DS.gameOverFrames) - 1));
    if (_guest.Get(DS.gameOverFrames) != 0)
    {
      _guest.JumpBack(0x7EA4);
      continue;
    }
    _guest.Call(RESET_KEYBOARD);
    regs.ax = Guest::VIDEO_SEGMENT;
    regs.es = regs.ax;
    _guest.Call(CLEAR_MESSAGE_LINE);
    _guest.Set(DS.textPaperPattern, 0);
    regs.bx = ALL_COLORS;
    regs.si = DS.eliteTitleText.offset;
    regs.di = TITLE_TEXT_POSITION;
    _guest.Call(DRAW_SCREEN_STRING);
    return;
  }
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
  if (_guest.Get(DS.gameOverFrames) != 0)
  {
    return;
  }
  if (_guest.Get(DS.escapePodFrames) != 0)
  {
    _guest.JumpBack(0x7FAF); // to the RET above
    return;
  }
  _guest.Call(HANDLE_FLIGHT_FUNCTION_KEYS);
  if (High(regs.ax) != 0 && _guest.Get(DS.viewLocked) != 1)
  {
    ChangeView(_guest);
  }
  if (_guest.Get(DS.keyDownG) == 1 && _guest.Get(DS.galacticHyperdriveFitted) == 1 && _guest.Get(DS.galacticDriveReadyFrames) == 0 &&
      _guest.Get(DS.hyperspaceCountdown) == 0)
  {
    _guest.Call(START_BEEP);
    regs.ax = DS.galacticDriveReadyMessage.offset;
    SetMessage(_guest.State(), regs.ax, 0x28);
    _guest.Set(DS.galacticDriveReadyFrames, 0x28);
  }
  if (_guest.Get(DS.keyDownH) == 1 && PressHyperspace(_guest))
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
      // Ctrl+Esc freezes the game until a key, without the menu.
      _guest.Call(RESET_KEYBOARD);
      WaitForKey(_guest, 0x8153);
    }
    else
    {
      regs.ax = DS.gamePausedMessage.offset;
      SetMessage(_guest.State(), regs.ax, 1);
      _guest.Call(UPDATE_MESSAGE_LINE);
      _guest.Call(RUN_PAUSE_SCREEN);
    }
    _guest.Set(DS.gamePaused, 0);
  }
  HandleMissileKeys(_guest);
  HandleFireButton(_guest);
  if (_guest.Get(DS.keyDownE) == 1 && _guest.Get(DS.ecmFitted) == 1)
  {
    _guest.Set(DS.ecmFired, 1);
    regs.ax = DS.ecmActiveMessage.offset;
    SetMessage(_guest.State(), regs.ax, 5);
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
  HandleAntiEcmKey(_guest.State());
}

void DrainEnergy(GameState& _state, std::int8_t _amount)
{
  // CBW / SUB [playerEnergy],AX, and 0 written over it on a borrow.
  const std::uint16_t amount = Word(_amount);
  const std::uint16_t energy = _state.Get(DS.playerEnergy);
  _state.Set(DS.playerEnergy, static_cast<std::uint16_t>(energy - amount));
  if (energy < amount)
  {
    _state.Set(DS.playerEnergy, 0);
    _state.Set(DS.playerDead, 1);
  }
}

void EngageJumpDrive(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.dockingComputerOn) != 1)
  {
    if (_guest.Get(DS.playerSpeed) != 0x30)
    {
      _guest.Set(DS.jumpDriveEngaged, 0);
      regs.ax = DS.jumpDriveVelocityLockedMessage.offset;
      SetMessage(_guest.State(), regs.ax, 5);
      return;
    }
    _guest.Call(IS_MASS_LOCKED);
    if (!_guest.Flag(FLAG_CARRY))
    {
      _guest.Set(DS.jumpDriveEngaged, 1);
      _guest.Set(DS.velocityDirty, 1);
      regs.ax = DS.jumpDriveEngagedMessage.offset;
      SetMessage(_guest.State(), regs.ax, 5);
      return;
    }
  }
  _guest.Set(DS.jumpDriveEngaged, 0);
  regs.ax = DS.jumpDriveMassLockedMessage.offset;
  SetMessage(_guest.State(), regs.ax, 5);
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
    MoveObjectsByVelocityEntry(_guest);
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
  MoveObjectsByVelocityEntry(_guest);
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
  SinCosOut(regs, SetSinCos(_guest.State(), 6, regs.ax));
  regs.ax = Plus(_guest.Get(DS.playerYawAngle), 0x400);
  SinCosOut(regs, SetSinCos(_guest.State(), 7, regs.ax));
  regs.ax = 0;
  regs.bx = _guest.Get(DS.playerSpeed);
  if (_guest.Get(DS.jumpDriveEngaged) == 1)
  {
    // The jump drive: 32 times the speed, for this frame only.
    _guest.Set(DS.velocityDirty, 1);
    _guest.Set(DS.jumpDriveEngaged, 0);
    regs.bx = static_cast<std::uint16_t>(regs.bx << 5);
  }
  PairOut(regs, RotateByStoredSinCos(_guest.State(), 7, PairIn(regs)));
  _guest.Set(DS.playerVelocityX, regs.ax);
  regs.ax = 0;
  PairOut(regs, RotateByStoredSinCos(_guest.State(), 6, PairIn(regs)));
  _guest.Set(DS.playerVelocityY, regs.ax);
  _guest.Set(DS.playerVelocityZ, regs.bx);
}

void MoveObjectsByVelocity(GameState& _state)
{
  // LOOP from CX = shipSlotCount: a count of 0 runs 65,536 times.
  std::uint16_t offset = DS.shipSlots.offset;
  for (std::uint32_t slots = LoopCount(_state.Get(DS.shipSlotCount)); slots != 0; --slots)
  {
    ObjectSlot slot(_state, offset);
    const std::uint16_t x = _state.Get(DS.playerVelocityX);
    SubtractVelocity(slot, SlotByte::XHigh, SlotWord::X, SlotWord::CompassX, x, Low(SignWord(x)));
    const std::uint16_t y = _state.Get(DS.playerVelocityY);
    SubtractVelocity(slot, SlotByte::YHigh, SlotWord::Y, SlotWord::CompassY, y, Low(SignWord(y)));
    const std::uint16_t z = _state.Get(DS.playerVelocityZ);
    SubtractVelocity(slot, SlotByte::ZHigh, SlotWord::Z, SlotWord::CompassZ, z, Low(SignWord(z)));
    offset = Plus(offset, ObjectSlot::BYTES);
  }
}

void RunPauseScreen(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(SILENCE_SPEAKER_TIMER);
  for (;;)
  {
    // 0x8D6D: the menu's eight lines.
    _guest.Call(CLEAR_DRAW_BUFFER);
    _guest.Set(DS.textPaperPattern, 0);
    regs.cx = PAUSE_MENU_LINES;
    regs.si = DS.pauseMenuText.offset;
    for (;;)
    {
      const std::uint16_t lines = regs.cx;
      regs.di = _guest.Word(regs.si);
      regs.si = Plus(regs.si, 2);
      regs.bx = ALL_COLORS;
      _guest.Call(DRAW_VIEW_STRING);
      ++regs.si;
      regs.cx = lines;
      if (--regs.cx == 0)
      {
        break;
      }
      _guest.JumpBack(0x8D7C);
    }
    _guest.Call(RESET_KEYBOARD);
    if (RunPauseOptions(_guest))
    {
      return;
    }
  }
}

// ── The entries of the routines de-assembled so far ──

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

using Machine::NativeReturn;
using Machine::NativeWait;

constexpr Machine::NativeContract Clobbers(std::uint16_t _registers) noexcept
{
  return Machine::NativeContract{_registers, 0};
}

constexpr Machine::NativeContract CARRY_OUT{0, FLAG_CARRY};
constexpr Machine::NativeContract CLOBBERS_AX = Clobbers(REGISTER_AX);
constexpr Machine::NativeContract CLOBBERS_AX_CX_SI_DI = Clobbers(REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI);
// DrawMissileLockIndicator's: CX as the original leaves it, which DrawThreeLineBar's second run reads in CH
// through UpdateDashboard (ADR-012 item 6).
constexpr Machine::NativeContract CLOBBERS_AX_BX_DI = Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_DI);
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_SI_DI = Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_DI);
// XorDashboardPixel's: AX, BX and ES as the original leaves them, which its callers' contracts compare.
constexpr Machine::NativeContract CLOBBERS_DI = Clobbers(REGISTER_DI);
constexpr Machine::NativeContract PREVIOUS_DUST_OUT{REGISTER_AX | REGISTER_BX, FLAG_CARRY};
constexpr Machine::NativeContract CLOBBERS_CX = Clobbers(REGISTER_CX);
constexpr Machine::NativeContract CLOBBERS_AX_SI_DI = Clobbers(REGISTER_AX | REGISTER_SI | REGISTER_DI);
constexpr Machine::NativeContract CLOBBERS_CX_DX = Clobbers(REGISTER_CX | REGISTER_DX);

// A particle's position as the dust routines hold it: x in AX, y in BX.
[[nodiscard]] DustPosition DustIn(const Registers& _regs) noexcept
{
  return DustPosition{Signed(_regs.ax), Signed(_regs.bx)};
}

void DustOut(Registers& _regs, DustPosition _position) noexcept
{
  _regs.ax = Word(_position.x);
  _regs.bx = Word(_position.y);
}

// A dashboard pixel's place as DX holds it: x in DL, y in DH.
[[nodiscard]] std::uint16_t PixelPlace(DashboardPixel _pixel) noexcept
{
  return Join(_pixel.y, _pixel.x);
}

} // namespace

void GetPreviousDustScreenPositionEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::optional<DustScreenPosition> previous = GetPreviousDustScreenPosition(_guest.State(), regs.si);
  if (previous)
  {
    regs.cx = Join(Low(Word(previous->row)), Low(Word(previous->x)));
  }
  _guest.SetFlag(FLAG_CARRY, !previous);
  _guest.Clobber(PREVIOUS_DUST_OUT);
}

void ComputeDustStripMaskEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.bp = ComputeDustStripMask(Signed(regs.dx));
  _guest.Clobber(CLOBBERS_AX);
}

void RespawnDustAtSideEdgeEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  DustOut(regs, RespawnDustAtSideEdge(_guest.State(), regs.si, Signed(regs.dx), regs.bp));
  _guest.Clobber(PRESERVES_ALL);
}

void RespawnDustAtVerticalEdgeEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  DustOut(regs, RespawnDustAtVerticalEdge(_guest.State(), regs.si, Signed(regs.dx), regs.bp));
  _guest.Clobber(PRESERVES_ALL);
}

void RespawnDustAnywhereEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  DustOut(regs, RespawnDustAnywhere(_guest.State(), regs.si));
  _guest.Clobber(PRESERVES_ALL);
}

void IsDustOnScreenEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_CARRY, IsDustOnScreen(DustIn(_guest.Regs())));
  _guest.Clobber(CARRY_OUT);
}

void IsDustNearCenterEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_CARRY, IsDustNearCenter(DustIn(_guest.Regs())));
  _guest.Clobber(CARRY_OUT);
}

void ScaleDustStepEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const DustStep step = ScaleDustStep(_guest.State(), DustIn(regs));
  SetLow(regs.cx, step.shift);
  regs.dx = Word(step.y);
  regs.bp = Word(step.x);
  _guest.Clobber(PRESERVES_ALL);
}

void LoadDustPositionEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  DustOut(regs, LoadDustPosition(_guest.State(), regs.si));
  _guest.Clobber(PRESERVES_ALL);
}

void StoreDustPositionEntry(Guest& _guest)
{
  const Registers& regs = _guest.Regs();
  StoreDustPosition(_guest.State(), regs.si, DustIn(regs));
  _guest.Clobber(PRESERVES_ALL);
}

void StorePreviousDustPositionEntry(Guest& _guest)
{
  const Registers& regs = _guest.Regs();
  StorePreviousDustPosition(_guest.State(), regs.si, DustIn(regs));
  _guest.Clobber(PRESERVES_ALL);
}

void DustToScreenEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const DustScreenPosition position = DustToScreen(DustIn(regs));
  regs.ax = Word(position.x);
  regs.bx = Word(position.row);
  _guest.Clobber(PRESERVES_ALL);
}

void ResetStardustEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint16_t lifetimeRandom = ResetStardust(_guest.State());
  // The original leaves AX the last random with the last lifetime in AL, and DI past the particles, which
  // RunDockingComputer's contract compares.
  regs.ax = WithLow(lifetimeRandom, RandomLifetime(lifetimeRandom));
  regs.di = Plus(DS.stardust.offset, STARDUST_BYTES);
  _guest.Clobber(CLOBBERS_CX);
}

void SaveStardustPositionsEntry(Guest& _guest)
{
  SaveStardustPositions(_guest.State());
  _guest.Clobber(CLOBBERS_AX_CX_SI_DI);
}

void InvalidateDashboardEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  InvalidateDashboard(_guest.State());
  // MOV AX,DS / MOV ES,AX / MOV DI,missileCountShown / MOV CX,16h / MOV AL,80h / REP STOSB: ES = DS, and AX, CX and DI
  // as that leaves them, which RestoreFlightScreen's contract compares.
  regs.es = regs.ds;
  regs.ax = WithLow(regs.ds, DASHBOARD_STALE);
  regs.cx = 0;
  regs.di = Plus(DS.missileCountShown.offset, DASHBOARD_CACHE_BYTES);
  _guest.Clobber(PRESERVES_ALL);
}

void DrawSignedIndicatorEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  DrawSignedIndicator(_guest.State(), regs.di, static_cast<std::int8_t>(Low(regs.ax)));
  // The strip's LOOP leaves CX = 0, whose CH the next DrawThreeLineBar's second run reads through UpdateDashboard.
  regs.cx = 0;
  _guest.Clobber(CLOBBERS_AX_BX_DI);
}

void DrawMissileIconsEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  // The original leaves CX = 0 once it draws, from AND CX,3 and its LOOPs, and otherwise CL = missileCount with CH as it
  // was: the next DrawThreeLineBar's second run reads CH through UpdateDashboard.
  if (DrawMissileIcons(_guest.State()))
  {
    regs.cx = 0;
  }
  else
  {
    SetLow(regs.cx, _guest.Get(DS.missileCount));
  }
  _guest.Clobber(CLOBBERS_AX_SI_DI);
}

void DrawMissileLockIndicatorEntry(Guest& _guest)
{
  // LOOP leaves CX 0 once the block is drawn; unchanged, the original returns before it touches CX.
  if (DrawMissileLockIndicator(_guest.State()))
  {
    _guest.Regs().cx = 0;
  }
  _guest.Clobber(CLOBBERS_AX_BX_DI);
}

void DrawConditionLightEntry(Guest& _guest)
{
  DrawConditionLight(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX_CX_SI_DI);
}

void UpdateConditionColorEntry(Guest& _guest)
{
  SetLow(_guest.Regs().ax, UpdateConditionColor(_guest.State()));
  _guest.Clobber(PRESERVES_ALL);
}

void InSafeZoneEntry(Guest& _guest)
{
  const SafeZone zone = InSafeZone(_guest.State());
  SetLow(_guest.Regs().ax, zone.rest);
  _guest.SetFlag(FLAG_CARRY, zone.inside);
  _guest.Clobber(CARRY_OUT);
}

void XorCompassDotEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const DashboardPixel last = XorCompassDot(_guest.State(), Low(regs.dx), High(regs.dx), regs.bp != 0);
  // DX comes back one right and one up, and PUSH DI / POP DI keeps DI. The last XorDashboardPixel leaves AX = DX, BX its
  // mask and ES the video segment, which UpdateCompass's contract compares: it ends with this.
  regs.dx = PixelPlace(last);
  regs.ax = regs.dx;
  regs.bx = last.mask;
  regs.es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(PRESERVES_ALL);
}

void XorScannerBlipEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  // The stick's step, which the original keeps in CH: up for a negative y.
  const std::uint8_t step = (Sar(High(regs.bx), 2) & 0x80) != 0 ? 0xFF : 0x01;
  const DashboardPixel last = XorScannerBlip(_guest.State(), High(regs.ax), High(regs.bx), High(regs.cx));
  // The last XorDashboardPixel leaves AX = DX, the last pixel, BX its mask and ES the video segment, and the stick's loop
  // CL = 0: UpdateScannerBlip's contract compares DX and ES, and EngageJumpDrive's and CheckShipInRange's, through
  // EraseScannerBlip, BX and CX as well.
  regs.dx = PixelPlace(last);
  regs.ax = regs.dx;
  regs.bx = last.mask;
  regs.cx = Join(step, 0);
  regs.es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_DI);
}

void XorDashboardPixelEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint8_t mask = XorDashboardPixel(_guest.State(), Low(regs.dx), High(regs.dx));
  // The original keeps DX in AX, and leaves the pixel's mask in BX and the video segment in ES: XorScannerBlip
  // passes AX on, and UpdateCompass, CheckShipInRange, TransformShip and DrawSunOrPlanet compare BX or ES.
  regs.ax = regs.dx;
  regs.bx = mask;
  regs.es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_DI);
}

void DrainEnergyEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  // The contract keeps AX, which the original leaves sign-extended (CBW).
  regs.ax = SignExtend(Low(regs.ax));
  DrainEnergy(_guest.State(), static_cast<std::int8_t>(Low(regs.ax)));
  _guest.Clobber(PRESERVES_ALL);
}

void MoveObjectsByVelocityEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint32_t slots = LoopCount(_guest.Get(DS.shipSlotCount));
  MoveObjectsByVelocity(_guest.State());
  // The original leaves the last velocity it subtracted, z, in AX and SI past the last slot, which RunDockingComputer's
  // contract compares.
  regs.ax = _guest.Get(DS.playerVelocityZ);
  regs.si = static_cast<std::uint16_t>(DS.shipSlots.offset + slots * ObjectSlot::BYTES);
  _guest.Clobber(CLOBBERS_CX_DX);
}

namespace
{

// HandleFlightFunctionKeys and ProcessFlightKeys work once a frame and return; they wait for a key only
// on the paths the F5-F10 screens, the pause and the Ctrl+Esc freeze take, so they wait sometimes.
// RunFlight and RunPauseScreen wait as a rule.
constexpr std::array ENTRIES = {
  NativeEntry{0x068F, "UpdateStardust", &UpdateStardust,
              Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP)},
  NativeEntry{0x0887, "ComputeStardustShift", &ComputeStardustShift, Clobbers(REGISTER_AX)},
  NativeEntry{0x08A1, "GetPreviousDustScreenPosition", &GetPreviousDustScreenPositionEntry, PREVIOUS_DUST_OUT},
  NativeEntry{0x08B9, "ComputeDustStripMask", &ComputeDustStripMaskEntry, CLOBBERS_AX},
  NativeEntry{0x08CB, "ShiftStardustSideways", &ShiftStardustSideways,
              Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_BP)},
  NativeEntry{0x08F2, "RespawnDustAtSideEdge", &RespawnDustAtSideEdgeEntry, PRESERVES_ALL},
  NativeEntry{0x0927, "RollStardust", &RollStardust, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI)},
  NativeEntry{0x0940, "ShiftStardustVertically", &ShiftStardustVertically,
              Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_BP)},
  NativeEntry{0x0969, "RespawnDustAtVerticalEdge", &RespawnDustAtVerticalEdgeEntry, PRESERVES_ALL},
  NativeEntry{0x09A3, "RespawnDustAnywhere", &RespawnDustAnywhereEntry, PRESERVES_ALL},
  NativeEntry{0x09D6, "IsDustOnScreen", &IsDustOnScreenEntry, CARRY_OUT},
  NativeEntry{0x09EE, "IsDustNearCenter", &IsDustNearCenterEntry, CARRY_OUT},
  NativeEntry{0x0A06, "ScaleDustStep", &ScaleDustStepEntry, PRESERVES_ALL},
  NativeEntry{0x0A13, "LoadDustPosition", &LoadDustPositionEntry, PRESERVES_ALL},
  NativeEntry{0x0A19, "StoreDustPosition", &StoreDustPositionEntry, PRESERVES_ALL},
  NativeEntry{0x0A1F, "StorePreviousDustPosition", &StorePreviousDustPositionEntry, PRESERVES_ALL},
  NativeEntry{0x0A28, "DustToScreen", &DustToScreenEntry, PRESERVES_ALL},
  NativeEntry{0x0A43, "ResetStardust", &ResetStardustEntry, CLOBBERS_CX},
  NativeEntry{0x0A88, "SaveStardustPositions", &SaveStardustPositionsEntry, CLOBBERS_AX_CX_SI_DI},
  NativeEntry{0x0BB3, "HandleFlightFunctionKeys", &HandleFlightFunctionKeys, PRESERVES_ALL, NativeReturn::Near, 0, NativeWait::Sometimes},
  NativeEntry{0x15CF, "RestoreFlightScreen", &RestoreFlightScreen, PRESERVES_ALL},
  NativeEntry{0x2540, "InvalidateDashboard", &InvalidateDashboardEntry, PRESERVES_ALL},
  NativeEntry{0x254F, "UpdateDashboard", &UpdateDashboard, Clobbers(static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_ES))},
  NativeEntry{0x2645, "DrawFiveLineBar", &DrawFiveLineBar, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI)},
  NativeEntry{0x26BE, "DrawSignedIndicator", &DrawSignedIndicatorEntry, CLOBBERS_AX_BX_DI},
  NativeEntry{0x273E, "DrawThreeLineBar", &DrawThreeLineBar, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI)},
  NativeEntry{0x2799, "DrawMissileIcons", &DrawMissileIconsEntry, CLOBBERS_AX_SI_DI},
  NativeEntry{0x2804, "DrawMissileLockIndicator", &DrawMissileLockIndicatorEntry, CLOBBERS_AX_BX_DI},
  NativeEntry{0x283B, "DrawEnergyBanks", &DrawEnergyBanks, Clobbers(REGISTER_ALL)},
  NativeEntry{0x2882, "UpdateEnergyAndLaserHeat", &UpdateEnergyAndLaserHeat, Clobbers(REGISTER_AX | REGISTER_BX)},
  NativeEntry{0x290C, "DrawConditionLight", &DrawConditionLightEntry, CLOBBERS_AX_BX_CX_SI_DI},
  NativeEntry{0x2959, "UpdateConditionColor", &UpdateConditionColorEntry, PRESERVES_ALL},
  NativeEntry{0x29D0, "SetUpLocalSpace", &SetUpLocalSpace, Clobbers(REGISTER_ALL)},
  NativeEntry{0x2BC5, "CheckCollisions", &CheckCollisions, Clobbers(REGISTER_ALL)},
  NativeEntry{0x2E63, "InSafeZone", &InSafeZoneEntry, CARRY_OUT},
  NativeEntry{0x2E69, "UpdateSafeZone", &UpdateSafeZone, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX)},
  NativeEntry{0x2F8B, "ComputeDeathDebrisVector", &ComputeDeathDebrisVector, Clobbers(REGISTER_DX | REGISTER_DI | REGISTER_BP)},
  NativeEntry{0x36B6, "UpdateWarnings", &UpdateWarnings, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX)},
  NativeEntry{0x36FD, "CheckMissileWarning", &CheckMissileWarning, PRESERVES_ALL},
  NativeEntry{0x370E, "CheckAltitudeWarning", &CheckAltitudeWarning, PRESERVES_ALL},
  NativeEntry{0x371A, "CheckTemperatureWarning", &CheckTemperatureWarning, PRESERVES_ALL},
  NativeEntry{0x3726, "CheckEnergyWarning", &CheckEnergyWarning, PRESERVES_ALL},
  NativeEntry{0x40EC, "UpdateScannerBlip", &UpdateScannerBlip, PRESERVES_ALL},
  NativeEntry{0x418F, "UpdateCompass", &UpdateCompass, PRESERVES_ALL},
  NativeEntry{0x42A4, "XorCompassDot", &XorCompassDotEntry, PRESERVES_ALL},
  NativeEntry{0x42D6, "EraseScannerBlip", &EraseScannerBlip, Clobbers(REGISTER_BX | REGISTER_CX | REGISTER_DX)},
  NativeEntry{0x42F6, "XorScannerBlip", &XorScannerBlipEntry, CLOBBERS_DI},
  NativeEntry{0x43C4, "XorDashboardPixel", &XorDashboardPixelEntry, CLOBBERS_DI},
  NativeEntry{0x4594, "EraseCompassAndBlips", &EraseCompassAndBlips, Clobbers(REGISTER_ALL)},
  NativeEntry{0x499F, "UpdateFuelLeak", &UpdateFuelLeak, Clobbers(REGISTER_AX | REGISTER_DX)},
  NativeEntry{0x7E9B, "RunFlight", &RunFlight, Clobbers(REGISTER_ALL), NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x7F69, "TickEscapePod", &TickEscapePod, PRESERVES_ALL},
  NativeEntry{0x7FA8, "ProcessFlightKeys", &ProcessFlightKeys, Clobbers(REGISTER_ALL), NativeReturn::Near, 0, NativeWait::Sometimes},
  NativeEntry{0x839F, "DrainEnergy", &DrainEnergyEntry, PRESERVES_ALL},
  NativeEntry{0x8430, "EngageJumpDrive", &EngageJumpDrive, PRESERVES_ALL},
  NativeEntry{0x8472, "UpdatePlayerMotion", &UpdatePlayerMotion, Clobbers(REGISTER_ALL), Machine::NativeReturn::Near, 0,
              Machine::NativeWait::Sometimes},
  NativeEntry{0x8599, "UpdatePlayerVelocity", &UpdatePlayerVelocity, Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_DX)},
  NativeEntry{0x85EC, "MoveObjectsByVelocity", &MoveObjectsByVelocityEntry, CLOBBERS_CX_DX},
  NativeEntry{0x8D6A, "RunPauseScreen", &RunPauseScreen, Clobbers(REGISTER_ALL), NativeReturn::Near, 0, NativeWait::Always},
};

} // namespace

std::span<const NativeEntry> FlightEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
