#include "pch.h"

#include "Flight.h"

#include "Arithmetic.h"
#include "Combat.h"
#include "DataOverlay.h"
#include "Docked.h"
#include "Docking.h"
#include "Equipment.h"
#include "Galaxy.h"
#include "Hyperspace.h"
#include "Input.h"
#include "Market.h"
#include "Maths.h"
#include "ObjectSlot.h"
#include "Scene.h"
#include "Ships.h"
#include "Sound.h"
#include "Text.h"
#include "Video.h"

#include <utility>

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::Registers;

// The routines these call through their entries: the original's, or a native routine hooked there.
constexpr std::uint16_t FINISH_SPACE_VIEW_FRAME = 0x0570;
constexpr std::uint16_t UPDATE_STARDUST = 0x068F;
constexpr std::uint16_t UPDATE_DASHBOARD = 0x254F;
constexpr std::uint16_t SET_UP_LOCAL_SPACE = 0x29D0;
constexpr std::uint16_t CHECK_COLLISIONS = 0x2BC5;
constexpr std::uint16_t PLAY_STATION_TUNNEL = 0x2D5B;
constexpr std::uint16_t SPAWN_PLAYER_WRECKAGE = 0x2FE3;
constexpr std::uint16_t DRAW_SCREEN_STRING = 0x32D8;
constexpr std::uint16_t UPDATE_MESSAGE_LINE = 0x35A3;
constexpr std::uint16_t CLEAR_MESSAGE_LINE = 0x3609;
constexpr std::uint16_t TRANSFORM_AND_DRAW_OBJECTS = 0x3D25;
constexpr std::uint16_t UPDATE_FUEL_LEAK = 0x499F;
constexpr std::uint16_t UPDATE_OBJECTS_AND_SPAWN = 0x4A10;
constexpr std::uint16_t STOP_ALL_SOUND = 0x7423;
constexpr std::uint16_t RESET_KEYBOARD = 0x7668;
constexpr std::uint16_t STOP_SOUND_EFFECTS = 0x7A63;
constexpr std::uint16_t STOP_CONTINUOUS_NOISE = 0x7B6B;
constexpr std::uint16_t POLL_SCREEN_DUMP_KEY = 0x7F3D;
constexpr std::uint16_t RESET_MOUSE_IF_SELECTED = 0x7F5D;
constexpr std::uint16_t TICK_ESCAPE_POD = 0x7F69;
constexpr std::uint16_t TICK_HYPERSPACE_COUNTDOWN = 0x7F79;
constexpr std::uint16_t PROCESS_FLIGHT_KEYS = 0x7FA8;
constexpr std::uint16_t UPDATE_PLAYER_MOTION = 0x8472;
constexpr std::uint16_t RESOLVE_LASER_FIRE = 0x8AC2;
constexpr std::uint16_t APPLY_ENEMY_LASER_HIT = 0x8C8E;

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
// stardustShift: (34h - playerSpeed) / 12 + 4.
constexpr std::uint16_t STARDUST_SHIFT_SPEED = 0x34;
constexpr std::uint8_t STARDUST_SHIFT_STEP = 12;

// EngageJumpDrive: the speed the jump drive needs, and its messages' frames.
constexpr std::uint16_t FULL_SPEED = 0x30;
constexpr std::uint16_t JUMP_DRIVE_MESSAGE_FRAMES = 5;

// viewAngle for each view, in 2048ths of a turn; the front view is 0.
constexpr std::uint16_t LEFT_VIEW = 0x200;
constexpr std::uint16_t REAR_VIEW = 0x400;
constexpr std::uint16_t RIGHT_VIEW = 0x600;

// An object slot's flag (+1Eh) bit that says its scanner blip, or the station's compass dot, is drawn.
constexpr std::uint8_t FLAG_BLIP_DRAWN = 0x02;

// The compass: the dot's x and y run 0 to 7 from its centre (CFh, 27h), each 8 * |c| / (|z| + 1000), normalised by sqrtTable
// once their squares reach 41h; the station slot's BlipZ is 20h while the target is in front.
constexpr std::uint16_t COMPASS_STEPS = 8;
constexpr std::uint16_t COMPASS_LAST_STEP = COMPASS_STEPS - 1;
constexpr std::uint16_t COMPASS_DEPTH = 0x3E8;
constexpr std::uint8_t COMPASS_NORMALIZED_FROM = 0x41;
constexpr std::uint8_t COMPASS_STEP_MASK = 7;
constexpr std::uint8_t COMPASS_CENTER_X = 0xCF;
constexpr std::uint8_t COMPASS_CENTER_Y = 0x27;
constexpr std::uint8_t COMPASS_IN_FRONT = 0x20;

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
constexpr std::uint16_t SPEED_BAR_LINE = 0x36B8;
constexpr std::uint16_t PITCH_INDICATOR_LINE = 0x3938;
constexpr std::uint16_t ROLL_INDICATOR_LINE = 0x37F8;
// The S and E glyphs: the safe zone and the ECM, at B800:1658 and B800:1656.
constexpr std::uint8_t SAFE_ZONE_GLYPH = 0x82;
constexpr std::uint8_t ECM_GLYPH = 0x81;
constexpr std::uint16_t SAFE_ZONE_GLYPH_CELL = 0x1658;
constexpr std::uint16_t ECM_GLYPH_CELL = 0x1656;
// The four energy banks' bars, from B800:3E38 upwards, 8 scanlines apart, each full at 100h of playerEnergy.
constexpr std::uint16_t ENERGY_BANKS = 4;
constexpr std::uint8_t FULL_BANK = 0xFF;
constexpr std::uint16_t ENERGY_BANK_LINE = 0x3E38;
constexpr std::uint16_t ENERGY_BANK_STEP = 0x140;
constexpr std::uint16_t ONE_BANK = 0x100;
constexpr std::uint16_t MOST_ENERGY = 0x3FF;
constexpr std::uint8_t LASER_COOLING = 2;
constexpr std::uint16_t EQUIPMENT_LOSS_ODDS = 0x32; // in 65536 a frame, below one bank
constexpr std::uint8_t EQUIPMENT_LOSS_DIVISOR = 0x14;
constexpr std::uint16_t EQUIPMENT_LOSS_FRAMES = 0x1E;
// What ShowCockpitScreen leaves once it draws: SI past the 16000 bytes it copies, and, once it sets the mode, SetGraphicsMode's
// BX and DX.
constexpr std::uint16_t COCKPIT_IMAGE_END = 0x3E80;
constexpr std::uint16_t GRAPHICS_MODE_BX = 0x0100;
constexpr std::uint16_t GRAPHICS_MODE_DX = 0x03D9;
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
constexpr std::uint8_t SCAN_F7 = 0x41;
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
constexpr std::uint16_t PAUSE_OPTIONS_LOOP = 0x8D98;
constexpr std::uint16_t HYPERSPACE_REFUSAL_TAIL = 0x80A3; // posts the message in AX for 25 frames
constexpr std::uint16_t PAST_HYPERSPACE_KEY = 0x8065;     // JMP 8101h, past H's handling

// The backward jumps of the flight keys, which a de-assembled loop reports as its turns (ADR-015): the scan of keyDown for F1-F10;
// the function keys' screens, from their leaving, their key wait, their tests, the F9 and F10 tests and the navigation computer's;
// ProcessFlightKeys' RET while the escape pod flies, and the Ctrl+Esc freeze's wait; the pause screen's menu, its lines, its
// options and its key wait.
constexpr std::uint16_t FUNCTION_KEY_LOOP = 0x0BBE;
constexpr std::uint16_t LEAVE_FLIGHT_SCREENS = 0x0BDE;
constexpr std::uint16_t FLIGHT_SCREEN_KEY_WAIT = 0x0BF3;
constexpr std::uint16_t FLIGHT_SCREEN_DISPATCH = 0x0BF8;
constexpr std::uint16_t STATUS_OR_INVENTORY_KEY = 0x0C45;
constexpr std::uint16_t NAVIGATION_KEYS = 0x0C6D;
constexpr std::uint16_t PROCESS_FLIGHT_KEYS_RETURN = 0x7FAF;
constexpr std::uint16_t FREEZE_KEY_WAIT = 0x8153;
constexpr std::uint16_t PAUSE_MENU = 0x8D6D;
constexpr std::uint16_t PAUSE_MENU_LOOP = 0x8D7C;
constexpr std::uint16_t PAUSE_SHOW_OPTIONS = 0x8D8F;
constexpr std::uint16_t PAUSE_KEY_WAIT = 0x8DC7;
constexpr std::uint16_t FUNCTION_KEYS = 10; // F1-F10

// E: the ECM's energy.
constexpr std::int8_t ECM_ENERGY = 0x14;

// H: a distance of 47h tenths of a light year or more is out of range; the fuel, times 10 over 24h, must reach the distance's
// low byte, and the jump costs that times 24h over 10, at least 1.
constexpr std::uint16_t OUT_OF_RANGE_TENTHS = 0x47;
constexpr std::uint8_t FUEL_FACTOR = 0x0A;
constexpr std::uint8_t FUEL_DIVISOR = 0x24;

// The missile keys: a launch jams on a random word below 1F4h, and no missile locks on a ship that carries the masking device.
constexpr std::uint16_t MISSILE_JAM_ODDS = 0x1F4;
constexpr std::uint8_t FLAG_MASKED = 0x20;
constexpr std::uint16_t ALL_COLORS = 0xFFFF;
constexpr std::uint16_t TITLE_TEXT_POSITION = 0x65; // on the message line, in CGA memory
constexpr std::uint8_t DIGIT_ZERO = 0x30;
constexpr std::uint8_t DIGIT_ONE = 0x31;
constexpr std::uint8_t DIGIT_NINE = 0x39;
constexpr std::uint8_t CLOSING_BRACKET = 0x29;
constexpr std::uint8_t SPACE = 0x20;

// The fire button: a laser at F0h or hotter does not fire, and each shot heats it by 5; a pulse laser fires every other
// frame.
constexpr std::uint8_t LASER_TOO_HOT = 0xF0;

// The player's motion: . and , step the speed by 4, up to 48; the roll and the pitch run -23 to 23; GAME OVER's first frame is
// its 40th.
constexpr std::uint8_t SPEED_KEY_STEP = 4;
constexpr std::uint8_t TOP_SPEED = 0x30;
constexpr std::int8_t MOST_ROLL = 23;
constexpr std::uint8_t GAME_OVER_FRAMES = 0x28;
constexpr std::uint8_t LASER_HEAT_PER_SHOT = 5;
constexpr std::uint8_t PULSE_LASER = 0;

// The station's safe zone: a distance below 32C8h.
constexpr std::uint16_t SAFE_ZONE_RADIUS = 0x32C8;

// A fuel leak: frames of leaking once its delay runs out, the fuel each takes, and the border it shows while it leaks, red,
// with the bright palette's bit.
constexpr std::uint8_t FUEL_LEAK_FRAMES = 0x33;
constexpr std::uint8_t FUEL_LEAK_STEP = 5;
constexpr std::uint8_t FUEL_LEAK_BORDER = 4;
constexpr std::uint8_t BRIGHT_PALETTE = 0x10;

// The warnings: four checks, round from the one after the last warning.
constexpr std::uint16_t WARNING_CHECKS = 4;

// The player's wreckage drifts at 40 along y before the view and the angles turn it.
constexpr std::int16_t DEATH_DEBRIS_SPEED = 0x28;

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

// mov [messagePointer], ax; mov [messageFrames], _frames, after mov ax, _text: the message at DS:_text, posted for
// _frames frames. Its callers load AX with _text themselves.
void SetMessage(GameState& _state, std::uint16_t _text, std::uint16_t _frames)
{
  _state.Set(DS.messagePointer, _text);
  _state.Set(DS.messageFrames, _frames);
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

// What ResetStardust leaves in the registers, from the last random number it drew: AX that number with the last lifetime in
// AL, and DI past the particles.
void ResetStardustOut(Registers& _regs, std::uint16_t _lifetimeRandom) noexcept
{
  _regs.ax = WithLow(_lifetimeRandom, RandomLifetime(_lifetimeRandom));
  _regs.di = Plus(DS.stardust.offset, STARDUST_BYTES);
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

// The particle at DS:_particle, at _position, as a point, or while the jump drive is engaged a streak from where it was last
// frame, unless it has just respawned. Returns whether DrawLine filled bytes with REP STOSB (DrawLineOut).
bool DrawDust(GameState& _state, std::uint16_t _particle, DustPosition _position)
{
  const DustScreenPosition screen = DustToScreen(_position);
  const std::uint8_t x = Low(Word(screen.x));
  const std::uint8_t row = Low(Word(screen.row));
  if (_state.Get(DS.jumpDriveEngaged) == 0)
  {
    PlotPixel(_state, x, row);
    return false;
  }
  if (_state.Byte(Plus(_particle, PREVIOUS_NEW)) != 0)
  {
    return false;
  }
  const std::optional<DustScreenPosition> previous = GetPreviousDustScreenPosition(_state, _particle);
  return previous && DrawLine(_state, x, row, Low(Word(previous->x)), Low(Word(previous->row)));
}

// What RollStardust and ShiftStardustVertically leave in BX: the last particle's y, as their last LoadDustPosition read it or
// their last StoreDustPosition wrote it.
[[nodiscard]] std::uint16_t LastDustY(const GameState& _state)
{
  return Word(LoadDustPosition(_state, DS.stardust.At(STARDUST_COUNT - 1)).y);
}

// The particle at DS:_particle, which LoadDustPosition found on screen at _position, moved by _step (ScaleDustStep's), outwards
// for the front view and inwards for the rear; then stored, and born again anywhere (RespawnDustAnywhere) while it is off
// screen, or for the rear view near the centre, or at the end of its life, which the rear view counts down. Returns where it is.
DustPosition MoveDust(GameState& _state, std::uint16_t _particle, DustPosition _position, bool _rear)
{
  DustPosition position = _position;
  if (_state.Get(DS.playerSpeed) != 0)
  {
    // ADD AX,BP / ADD BX,DX, or SUB for the rear view.
    const DustStep step = ScaleDustStep(_state, position);
    const auto move = [_rear](std::int16_t _coordinate, std::int16_t _by)
    { return Signed(_rear ? static_cast<std::uint16_t>(Word(_coordinate) - Word(_by)) : Plus(Word(_coordinate), _by)); };
    position = DustPosition{move(position.x, step.x), move(position.y, step.y)};
  }
  for (;;)
  {
    StoreDustPosition(_state, _particle, position);
    if (!_rear)
    {
      if (IsDustOnScreen(position))
      {
        return position;
      }
    }
    else if (!IsDustNearCenter(position))
    {
      // DEC BYTE [SI+4] / JE: the lifetime counted down, the particle born again at 0.
      const std::uint16_t lifetime = Plus(_particle, PARTICLE_LIFETIME);
      _state.SetByte(lifetime, static_cast<std::uint8_t>(_state.Byte(lifetime) - 1));
      if (_state.Byte(lifetime) != 0)
      {
        return position;
      }
    }
    position = RespawnDustAnywhere(_state, _particle);
    StorePreviousDustPosition(_state, _particle, position);
  }
}

// UpdateFrontStardust (0x06C0) and UpdateRearStardust (0x0742), which UpdateStardust jumps to: the pitch shifts the dust
// vertically and the roll rolls it, _rear reversing both; then each of the 30 particles on screen moves (MoveDust) and is drawn
// (DrawDust), PUSH CX and POP CX keeping the count round each. _bx is the BX UpdateStardust was called with, which
// ComputeStardustShift's divide would save in the trap with what the shift and the roll leave over it. Returns whether a DrawLine
// filled bytes with REP STOSB.
bool UpdateFrontOrRearStardust(GameState& _state, bool _rear, std::uint16_t _bx)
{
  std::uint16_t bx = _bx;
  // MOV DX,rollRate / ApplyReverseControlsToDx / AND DH,DH / XOR DL,DL / NEG DX for the front view / SAR DX,1.
  const std::uint16_t pitchRate = _state.Get(DS.rollRate);
  const std::uint8_t pitch = ApplyReverseControls(_state, Steering{Low(pitchRate), High(pitchRate)}).pitch;
  if (pitch != 0)
  {
    const std::uint16_t step = Join(pitch, 0);
    ShiftStardustVertically(_state, Signed(Sar(_rear ? step : Negate(step), 1)));
    bx = LastDustY(_state);
  }
  // MOV AX,rollRate / ApplyReverseControls / CBW / NEG AX for the front view / SHL AX,1: the roll.
  const std::uint16_t rollRate = _state.Get(DS.rollRate);
  const std::uint16_t roll = SignExtend(ApplyReverseControls(_state, Steering{Low(rollRate), High(rollRate)}).roll);
  if (const auto angle = static_cast<std::uint16_t>((_rear ? roll : Negate(roll)) << 1); angle != 0)
  {
    (void)SetSinCos(_state, 7, angle);
    RollStardust(_state);
    bx = LastDustY(_state);
  }
  (void)ComputeStardustShift(_state, High(bx));
  bool filled = false;
  std::uint16_t particle = DS.stardust.offset;
  for (std::uint16_t count = STARDUST_COUNT; count != 0; --count)
  {
    if (const DustPosition position = LoadDustPosition(_state, particle); IsDustOnScreen(position))
    {
      filled = DrawDust(_state, particle, MoveDust(_state, particle, position, _rear)) || filled;
    }
    particle = Plus(particle, PARTICLE_BYTES);
  }
  return filled;
}

// 0x084D: the side views only draw; ShiftStardustSideways has moved the dust. Each particle on screen is drawn (DrawDust),
// PUSH CX and PUSH SI keeping the count and the particle round it. Returns whether a DrawLine filled bytes with REP STOSB.
bool DrawSideStardust(GameState& _state)
{
  bool filled = false;
  std::uint16_t particle = DS.stardust.offset;
  for (std::uint16_t count = STARDUST_COUNT; count != 0; --count)
  {
    const DustPosition position = LoadDustPosition(_state, particle);
    if (IsDustOnScreen(position))
    {
      filled = DrawDust(_state, particle, position) || filled;
    }
    particle = Plus(particle, PARTICLE_BYTES);
  }
  return filled;
}

// UpdateLeftStardust (0x07C5) and UpdateRightStardust (0x0809), which UpdateStardust jumps to: pitch rolls the dust and speed
// moves it sideways, _left mirroring both, and then they draw it (DrawSideStardust). Returns whether a DrawLine filled bytes
// with REP STOSB.
bool UpdateSideStardust(GameState& _state, bool _left)
{
  // MOV DX,rollRate / ApplyReverseControlsToDx / MOV DH,DL: the reversed roll byte, as DX = roll:00, negated for the right view
  // and halved, shifts the dust vertically.
  const std::uint16_t rollRate = _state.Get(DS.rollRate);
  const std::uint8_t roll = ApplyReverseControls(_state, Steering{Low(rollRate), High(rollRate)}).roll;
  if (roll != 0)
  {
    const std::uint16_t step = Join(roll, 0);
    ShiftStardustVertically(_state, Signed(Sar(_left ? step : Negate(step), 1)));
  }
  // The speed, negated for the left view, as DX = its low byte:00, shifted right three times, moves it sideways.
  if (const std::uint16_t speed = _state.Get(DS.playerSpeed); speed != 0)
  {
    const std::uint16_t step = Join(Low(_left ? Negate(speed) : speed), 0);
    ShiftStardustSideways(_state, Signed(Sar(step, 3)));
  }
  // MOV AX,rollRate / ApplyReverseControls / MOV AL,AH / CBW / SHL AX,1: the reversed pitch byte, doubled and negated for the
  // left view, rolls it.
  const std::uint16_t pitchRate = _state.Get(DS.rollRate);
  const auto angle =
    static_cast<std::uint16_t>(SignExtend(ApplyReverseControls(_state, Steering{Low(pitchRate), High(pitchRate)}).pitch) << 1);
  if (angle != 0)
  {
    (void)SetSinCos(_state, 7, _left ? Negate(angle) : angle);
    RollStardust(_state);
  }
  return DrawSideStardust(_state);
}

// ---- The dashboard --------------------------------------------------------------------------------

// _word on the five scanlines of the dashboard line at B800:_line.
void FillFiveLineWord(GameState& _state, std::uint16_t _line, std::uint16_t _word)
{
  for (const std::uint16_t scanline : FIVE_LINES)
  {
    _state.SetVideoWord(Plus(_line, scanline), _word);
  }
}

// DrawFiveLineBarPixels (0x264D) and its copy in DrawThreeLineBar: _pixels, 0-48, on the _lines scanlines of the dashboard line
// at B800:_line, a column of bytes at a time: colour 1 for each four, the partial byte from barPartialBytes, then colour 2 to
// pixel 48. When there are fewer than four, the first run's loop leaves CH alone, so the second runs _countHigh:CL times, CH as
// the caller left it.
template <std::size_t Lines>
void DrawBar(GameState& _state, const std::array<std::uint16_t, Lines>& _lines, std::uint16_t _line, std::uint8_t _pixels,
             std::uint8_t _countHigh)
{
  std::uint16_t column = _line;
  const auto fill = [&_state, &_lines, &column](std::uint8_t _byte)
  {
    for (const std::uint16_t line : _lines)
    {
      _state.SetVideoByte(Plus(column, line), _byte);
    }
    column = Plus(column, 1);
  };
  std::uint8_t countHigh = _countHigh;
  if (const auto full = static_cast<std::uint8_t>(_pixels >> 2); full != 0)
  {
    countHigh = 0;
    for (std::uint8_t bytes = full; bytes != 0; --bytes)
    {
      fill(BAR_VALUE_BYTE);
    }
  }
  if (const auto partial = static_cast<std::uint8_t>(_pixels & 3); partial != 0)
  {
    fill(_state.Byte(Plus(DS.barPartialBytes.offset, partial)));
  }
  const auto rest = static_cast<std::uint8_t>(static_cast<std::uint8_t>(BAR_PIXELS - _pixels) >> 2);
  if (rest == 0)
  {
    return;
  }
  for (std::uint16_t bytes = Join(countHigh, rest); bytes != 0; --bytes)
  {
    fill(BAR_REST_BYTE);
  }
}

// MOV BL,12 / MUL BL / MOV BL,3Fh / DIV BL: _value * 12 / 63, 0-48, which cannot overflow.
[[nodiscard]] std::uint8_t ScaleBarValue(std::uint8_t _value) noexcept
{
  return static_cast<std::uint8_t>((_value * 12u) / 63u);
}

// MOV AL,[_value] / CMP AL,[_shown] / JE / (MOV [_shown],AL) / MOV DI,_line / CALL DrawThreeLineBar, with CH = 0 as the earlier
// draws leave it (UpdateDashboard).
void RedrawThreeLineBar(GameState& _state, DataField<std::uint8_t> _value, DataField<std::uint8_t> _shown, std::uint16_t _line,
                        bool _remember)
{
  const std::uint8_t value = _state.Get(_value);
  if (value == _state.Get(_shown))
  {
    return;
  }
  if (_remember)
  {
    _state.Set(_shown, value);
  }
  DrawThreeLineBar(_state, _line, value, 0);
}

// ADD byte,_value / JAE / MOV byte,0FFh on _field: the sum written, and FFh over it on a carry.
void AddSaturating(GameState& _state, DataField<std::uint8_t> _field, std::uint8_t _value)
{
  const unsigned sum = _state.Get(_field) + unsigned{_value};
  _state.Set(_field, static_cast<std::uint8_t>(sum));
  if (sum > 0xFF)
  {
    _state.Set(_field, 0xFF);
  }
}

// SUB byte,_value / JAE / MOV byte,0 on _field: the difference written, and 0 over it on a borrow.
void SubtractFloored(GameState& _state, DataField<std::uint8_t> _field, std::uint8_t _value)
{
  const std::uint8_t before = _state.Get(_field);
  _state.Set(_field, static_cast<std::uint8_t>(before - _value));
  if (before < _value)
  {
    _state.Set(_field, 0);
  }
}

// cmp [_shown], al; mov [_shown], al; je, with AL = _glyph: whether the glyph changed.
[[nodiscard]] bool ExchangeShown(GameState& _state, DataField<std::uint8_t> _shown, std::uint8_t _glyph)
{
  const bool changed = _state.Get(_shown) != _glyph;
  _state.Set(_shown, _glyph);
  return changed;
}

// ---- Collisions -----------------------------------------------------------------------------------

// The station's x and y, with z 0, within _halfSize: inside the docking slot.
[[nodiscard]] bool InsideDockingSlot(const ObjectSlot& _station, std::uint16_t _halfSize)
{
  return VectorWithinBox(Vector{Signed(_station.Get(SlotWord::X)), Signed(_station.Get(SlotWord::Y)), 0}, _halfSize);
}

// 0x2BD9-0x2C8D: _slot, an active one, against the player. Outside its type's collisionRanges box, a station's collided bit
// is cleared. Inside, a ship costs 450 and is removed. The station docks the player when it is drawn, the player is aligned
// within 100 (CheckDockingAlignment), inside a box of 90 about its x and y, not invaded and not hostile; nearly aligned, within
// 250, it scrapes for 30 inside a box of 110 and crashes for 400 outside it; otherwise, or when it has collided already, it
// costs 1500 and is removed. Each hit is credited (CreditKill), unlocks a missile on it (CheckMissileTargetDestroyed), takes
// the damage (TakeDamage) and starts the impact sound, whose STI the hit's every path ends with, as KillPlayer's does when it
// kills.
void CheckCollision(GameState& _state, Hardware& _hardware, ObjectSlot _slot)
{
  // MOV BL,[DI] / AND BX,3Eh: the type, doubled, indexes the box's half sizes.
  const std::uint16_t range = _state.Word(Plus(DS.collisionRanges.offset, _slot.Get(SlotByte::Type) & 0x3E));
  const auto clearCollided = [&_slot] { _slot.Set(SlotByte::Collided, static_cast<std::uint8_t>(_slot.Get(SlotByte::Collided) & 0xFE)); };
  if (!ObjectWithinBox(_slot, range))
  {
    if (IsStation(_slot).station)
    {
      clearCollided();
    }
    return;
  }
  std::uint16_t damage = 0x5DC;
  if (!IsStation(_slot).station)
  {
    damage = 0x1C2;
  }
  else if ((_slot.Get(SlotByte::Collided) & 1) == 0)
  {
    _slot.Set(SlotByte::Collided, static_cast<std::uint8_t>(_slot.Get(SlotByte::Collided) | 1));
    if ((_slot.Get(SlotByte::Type) & 0x80) != 0)
    {
      if (CheckDockingAlignment(_state, _slot, 0x64))
      {
        // Aligned: docked, if inside the slot and the station is not the Thargoids'.
        if (_state.Get(DS.thargoidInvasionActive) != 1 && InsideDockingSlot(_slot, 0x5A) && (_slot.Get(SlotByte::Flags) & 1) == 0)
        {
          _state.Set(DS.playerDocked, 1);
          _state.Set(DS.dockingComputerOn, 0);
          _state.Set(DS.rollRate, 0);
          _state.Set(DS.viewLocked, 0);
          return;
        }
      }
      else if (CheckDockingAlignment(_state, _slot, 0xFA))
      {
        // Nearly aligned: a scrape if inside the slot, a crash if not.
        damage = 0x190;
        if (InsideDockingSlot(_slot, 0x6E))
        {
          clearCollided();
          damage = 0x1E;
        }
      }
    }
  }
  // PUSH AX / POP AX keep the damage round the kill's credit.
  (void)CreditKill(_state, _slot);
  (void)CheckMissileTargetDestroyed(_state, _slot.Offset());
  if (damage == 0x5DC || !IsStation(_slot).station)
  {
    (void)RemoveObject(_state, _slot);
  }
  if (TakeDamage(_state, damage))
  {
    _hardware.EnableInterrupts();
  }
  (void)StartImpactSound(_state);
  _hardware.EnableInterrupts();
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

// ---- The scanner and the compass ------------------------------------------------------------------

// ScaleCompassAxis (0x41FF-0x4217, and again at 0x421B-0x4233): CMP CX,AX / JB, then CWD, three SHL AX / RCL DX and DIV CX:
// 8 * _magnitude / _range when _range is not below _magnitude, else _magnitude; then at most 7. A magnitude of 8000h, which
// NEG leaves so, is negative to CWD, and the divide traps, with BX = _bx.
[[nodiscard]] std::uint16_t ScaleCompassAxis(GameState& _state, std::uint16_t _magnitude, std::uint16_t _range, std::uint16_t _bx)
{
  std::uint16_t scaled = _magnitude;
  if (_range >= _magnitude)
  {
    const std::uint32_t dividend = ((std::uint32_t{SignWord(_magnitude)} << 16) | _magnitude) << 3;
    scaled = DivideWord(_state, dividend, _range, _bx).quotient;
  }
  return scaled >= COMPASS_STEPS ? COMPASS_LAST_STEP : scaled;
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

// What the routines before M's launch leave in DI, from which the launch copies the missile (LaunchPlayerMissile): InvalidateDashboard
// past the cache it fills; ResetStardust past the particles; ClearDrawBuffer past the buffer its REP STOSW of 1000h words fills from
// 0, upwards or, _backward, down; and IsMassLocked on the last slot it looked at, as MassLockOut leaves it: the sun's, the
// planet's, then where its look at the ships stopped.
constexpr auto DASHBOARD_CACHE_END = static_cast<std::uint16_t>(DS.missileCountShown.offset + DASHBOARD_CACHE_BYTES);
constexpr auto STARDUST_END = static_cast<std::uint16_t>(DS.stardust.offset + STARDUST_BYTES);
constexpr std::uint16_t DRAW_BUFFER_BYTES = 0x2000;

[[nodiscard]] std::uint16_t DrawBufferEnd(bool _backward) noexcept
{
  return _backward ? Negate(DRAW_BUFFER_BYTES) : DRAW_BUFFER_BYTES;
}

[[nodiscard]] std::uint16_t DiAfterMassLock(const MassLock& _lock, std::uint16_t _di) noexcept
{
  std::uint16_t di = _di;
  if (_lock.sun)
  {
    di = DS.shipSlots.offset;
  }
  if (_lock.planet)
  {
    di = Plus(DS.shipSlots.offset, ObjectSlot::BYTES);
  }
  if (_lock.ships)
  {
    di = _lock.ships->slot;
  }
  return di;
}

// CALL GetKey / JE back to it, at _call, de-assembled: GetKey until a key comes, each empty turn ending at the jump back (ADR-015):
// the waits at 0x0BF3 (FlightScreenDispatch), 0x8153 (ProcessFlightKeys' Ctrl+Esc) and 0x8DC7 (RunPauseScreen). The turns carry
// nothing, as WaitForKeyPress's do: GetKey writes AH before it reads it, and reads AL only when it takes a code, which changes
// keyBuffer's count. Returns the key, with screenshot set if any of its GetKeys saved one, which leaves ES on the video segment.
KeyPress WaitForKey(GameState& _state, Hardware& _hardware, std::uint16_t _call)
{
  bool screenshot = false;
  for (;;)
  {
    KeyPress key = GetKey(_state, _hardware);
    screenshot = screenshot || key.screenshot;
    if (key.scanCode != 0)
    {
      key.screenshot = screenshot;
      return key;
    }
    _hardware.LoopTurn(_call, {});
  }
}

// PauseShowOptions (0x8D8F): the five option letters, in reverse video when on. The loop at 0x8D98 pushes and pops CX, DI
// and SI round DrawViewChar, and each turn carries them: the options left, the letter's place and the option's flag byte.
void ShowPauseOptions(GameState& _state, Hardware& _hardware)
{
  std::uint16_t position = PAUSE_FIRST_OPTION;
  std::uint16_t option = DS.keyboardRecenter.offset;
  std::uint16_t options = PAUSE_OPTIONS;
  for (;;)
  {
    _state.Set(DS.textPaperPattern, 0);
    std::uint16_t ink = ALL_COLORS;
    if (_state.Byte(option) == 1)
    {
      // xchg [textPaperPattern], bx
      ink = _state.Get(DS.textPaperPattern);
      _state.Set(DS.textPaperPattern, ALL_COLORS);
    }
    (void)DrawViewChar(_state, _state.Byte(Plus(option, 1)), ink, position);
    option = Plus(option, 2);
    position = Plus(position, PAUSE_OPTION_STEP);
    if (--options == 0)
    {
      return;
    }
    _hardware.LoopTurn(PAUSE_OPTIONS_LOOP, {options, position, option});
  }
}

// 0x8DD4-0x8E1E: R, D, Y, B and S toggle their option, with a beep. False for any other key.
[[nodiscard]] bool TogglePauseOption(GameState& _state, std::uint8_t _key)
{
  constexpr std::array<std::uint8_t, PAUSE_OPTIONS> KEYS = {SCAN_R, SCAN_D, SCAN_Y, SCAN_B, SCAN_S};
  for (std::uint16_t index = 0; index < PAUSE_OPTIONS; ++index)
  {
    if (KEYS[index] == _key)
    {
      const auto option = static_cast<std::uint16_t>(DS.keyboardRecenter.offset + index * 2);
      _state.SetByte(option, static_cast<std::uint8_t>(_state.Byte(option) ^ 1));
      StartBeep(_state);
      return true;
    }
  }
  return false;
}

// How the pause screen's keys end it.
enum class PauseExit : std::uint8_t
{
  Resume,      // space: the flight goes on
  Abort,       // A: the title, the original dropping its own return address and ProcessFlightKeys'
  NewFrameRate // an F key set the frame time, and the menu is drawn again (JMP 8D6Dh)
};

// 0x8D6D-0x8D8C: the drawing buffer cleared and the menu's eight lines drawn into it, in every colour on a paper of 0. The loop at
// 0x8D7C pushes and pops CX round each line, and each turn carries it and SI, the next line's place and text.
void DrawPauseMenu(GameState& _state, Hardware& _hardware, bool _backward)
{
  ClearDrawBuffer(_state, _backward);
  _state.Set(DS.textPaperPattern, 0);
  std::uint16_t text = DS.pauseMenuText.offset;
  for (std::uint16_t lines = PAUSE_MENU_LINES;;)
  {
    // MOV DI,[SI] / ADD SI,2: the line's place, then its text; INC SI past its NUL.
    const std::uint16_t at = _state.Word(text);
    text = Plus(DrawViewString(_state, Plus(text, 2), ALL_COLORS, at).end, 1);
    if (--lines == 0)
    {
      return;
    }
    _hardware.LoopTurn(PAUSE_MENU_LOOP, {lines, text});
  }
}

// 0x8E52-0x8E88: F1-F10, the minimum frame time from frameTimeChoices, and its key in the menu's label, 1-9, or 10 with a closing
// bracket.
void SetFrameTime(GameState& _state, std::uint8_t _key)
{
  const auto choice = static_cast<std::uint8_t>(_key - SCAN_F1);
  _state.Set(DS.minimumFrameMs, _state.Byte(Plus(DS.frameTimeChoices.offset, choice)));
  const auto digit = static_cast<std::uint8_t>(choice + DIGIT_ONE);
  if (digit == DIGIT_NINE + 1)
  {
    _state.SetByte(DS.frameRateKeyLabel.offset, DIGIT_ONE);
    _state.Set(DS.dataAC00, DIGIT_ZERO);
    _state.Set(DS.dataAC01, CLOSING_BRACKET);
  }
  else
  {
    _state.SetByte(DS.frameRateKeyLabel.offset, digit);
    _state.Set(DS.dataAC00, CLOSING_BRACKET);
    _state.Set(DS.dataAC01, SPACE);
  }
}

// The pause screen from PauseShowOptions (0x8D8F): the options, presented (PresentSpaceView, after MOV AX,0B800h / MOV ES,AX),
// then its keys, waited for at 0x8DC7, until one ends it. _backward is the direction flag, which nothing here changes. The jumps
// back to PauseShowOptions and to the wait load or write every register they read, and carry nothing.
PauseExit RunPauseOptions(GameState& _state, Hardware& _hardware, bool _backward)
{
  for (;;)
  {
    ShowPauseOptions(_state, _hardware);
    PresentSpaceView(_state, _hardware, _backward);
    for (;;)
    {
      const std::uint8_t key = WaitForKey(_state, _hardware, PAUSE_KEY_WAIT).scanCode;
      if (key == SCAN_SPACE)
      {
        // Resume, with the space that did it not fired.
        StartBeep(_state);
        ResetKeyboard(_state, _hardware);
        ClearDrawBuffer(_state, _backward);
        _state.Set(DS.gamePaused, 0);
        _state.Set(DS.textPaperPattern, 0);
        _state.Set(DS.keyDownSpace, 0);
        return PauseExit::Resume;
      }
      if (TogglePauseOption(_state, key))
      {
        break;
      }
      if (key == SCAN_A)
      {
        // Abort to the title: StopSoundEffects' CLI round its writes, which nothing interrupts in native code, then its STI.
        ResetKeyboard(_state, _hardware);
        ClearMessageLine(_state, GameState::VIDEO_SEGMENT, _backward);
        ClearDrawBuffer(_state, _backward);
        StopSoundEffects(_state);
        _hardware.EnableInterrupts();
        _state.Set(DS.gamePaused, 0);
        _state.Set(DS.textPaperPattern, 0);
        _state.Set(DS.titleShown, 0);
        return PauseExit::Abort;
      }
      if (key < SCAN_F1 || key >= SCAN_PAST_F10)
      {
        _hardware.LoopTurn(PAUSE_KEY_WAIT, {});
        continue;
      }
      SetFrameTime(_state, key);
      return PauseExit::NewFrameRate;
    }
    _hardware.LoopTurn(PAUSE_SHOW_OPTIONS, {});
  }
}

// FlightScreenDispatch (0x0BDE): leaving the function keys: the cockpit back (RestoreFlightScreen, then ResetKeyboard) if a
// screen was shown, then inFlight set. Returns what RestoreFlightScreen did, when it ran; _backward is the direction flag it
// runs by.
std::optional<ScreenChange> LeaveFlightScreens(GameState& _state, Hardware& _hardware, bool _backward)
{
  std::optional<ScreenChange> change;
  if (_state.Get(DS.flightScreenShown) != 0)
  {
    change = RestoreFlightScreen(_state, _hardware, _backward);
    ResetKeyboard(_state, _hardware);
  }
  _state.Set(DS.inFlight, 1);
  return change;
}

// 0x0C13 and 0x0C72: flightScreenShown set, then push ax; call EraseCompassAndBlips; pop ax: a screen is about to be shown.
// Returns whether EraseCompassAndBlips erased anything.
bool EraseForFlightScreen(GameState& _state)
{
  _state.Set(DS.flightScreenShown, 1);
  return EraseCompassAndBlips(_state);
}

// FlightScreenDispatch (0x0BF8): the screen for _key, then the screen for the key that screen returns, until F1-F4 or the
// navigation computer's error leaves (0x0BDE). _al, _countIfNone, _segment and _backward are AL, BP, ES and the direction flag as
// the dispatch finds them, which the screens and their waits read and leave. Every jump back in it is a turn of a loop that can
// wait: those to the tests at 0x0BF8 carry the key and BP, which the next screen reads, and the others, into shared code or to the
// wait, carry nothing.
FlightScreens DispatchFlightScreens(GameState& _state, Hardware& _hardware, std::uint8_t _key, std::uint8_t _al, std::uint16_t _countIfNone,
                                    std::uint16_t _segment, bool _backward)
{
  FlightScreens screens{0, _al, std::nullopt, std::nullopt, _countIfNone, _backward};
  std::uint8_t key = _key;
  std::uint16_t segment = _segment;
  // 0x0C13, 0x0C72 and 0x0CA0: flightScreenShown set and the compass and the blips erased (EraseForFlightScreen). Of what
  // EraseCompassAndBlips leaves, the screens read BP, the compass dot's in-front byte CBW'd once it erases the dot, and the charts
  // ES, the video segment once it erases anything.
  const auto erase = [&_state, &screens, &segment]
  {
    const ObjectSlot station(_state, DS.stationSlot.offset);
    if ((station.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) != 0)
    {
      screens.countLeft = SignExtend(station.Get(SlotByte::BlipZ));
    }
    if (EraseForFlightScreen(_state))
    {
      segment = GameState::VIDEO_SEGMENT;
    }
  };
  // A screen shown, and the key that closed it, with AL as it leaves it: every screen leaves ES on B800h, the charts clear the
  // direction flag (PresentChartFrame), and the screens that select the system at the cursor as they close leave BP its count.
  // Then JMP FlightScreenDispatch.
  const auto shown = [&_hardware, &screens, &key, &segment](const ScreenKey& _closed, bool _chart)
  {
    key = _closed.scanCode;
    screens.al = _closed.al;
    screens.countLeft = _closed.countLeft.value_or(screens.countLeft);
    segment = GameState::VIDEO_SEGMENT;
    screens.backward = screens.backward && !_chart;
    _hardware.LoopTurn(FLIGHT_SCREEN_DISPATCH, {Join(key, screens.al), screens.countLeft});
  };
  // 0x0BDE, jumped back to: the cockpit back if a screen was shown (LeaveFlightScreens), whose CLD, once it draws, clears the
  // direction flag.
  const auto leave = [&_state, &_hardware, &screens]
  {
    _hardware.LoopTurn(LEAVE_FLIGHT_SCREENS, {});
    screens.restored = LeaveFlightScreens(_state, _hardware, screens.backward);
    if (screens.restored && *screens.restored != ScreenChange::None)
    {
      screens.backward = false;
    }
    return screens;
  };
  for (;;)
  {
    if (key >= SCAN_F1 && key <= SCAN_F4)
    {
      screens.view = key;
      return leave();
    }
    if (_state.Get(DS.witchspaceCountdown) == 0)
    {
      erase();
      if (key == SCAN_F5)
      {
        shown(ShowGalacticChart(_state, _hardware, screens.backward, segment), true);
        continue;
      }
      if (key == SCAN_F6)
      {
        shown(ShowShortRangeChart(_state, _hardware, screens.backward, segment), true);
        continue;
      }
      if (key == SCAN_F7)
      {
        shown(ShowSystemDataScreen(_state, _hardware, screens.backward, screens.countLeft), false);
        continue;
      }
      if (key == SCAN_F8)
      {
        shown(ShowMarketPricesScreen(_state, _hardware, screens.backward, screens.countLeft), false);
        continue;
      }
    }
    else
    {
      // In witch space the short-range chart still shows once the countdown is down to 1, and F9 and F10 show; the charts and the
      // market need the navigation computer, which witch space jams.
      if (key == SCAN_F6)
      {
        if (_state.Get(DS.witchspaceCountdown) == 1)
        {
          erase();
          shown(ShowShortRangeChart(_state, _hardware, screens.backward, segment), true);
          continue;
        }
        _hardware.LoopTurn(NAVIGATION_KEYS, {});
      }
      if (key < SCAN_F9)
      {
        if (_state.Get(DS.flightScreenShown) != 1)
        {
          StartBeep(_state);
        }
        // MOV AX,navCompErrorMessage, posted, then XOR AH,AH: no view, and AL the message's low byte.
        SetMessage(_state, DS.navCompErrorMessage.offset, 0x19);
        screens.al = Low(DS.navCompErrorMessage.offset);
        return leave();
      }
      erase();
      _hardware.LoopTurn(STATUS_OR_INVENTORY_KEY, {});
    }
    // 0x0C45: F9 and F10; any other key the screens return is waited past.
    if (key == SCAN_F9)
    {
      ResetKeyboard(_state, _hardware);
      _state.Set(DS.inFlight, 1);
      const ScreenKey closed = ShowCommanderStatusScreen(_state, _hardware, screens.backward, screens.countLeft);
      _state.Set(DS.inFlight, 0);
      shown(closed, false);
      continue;
    }
    if (key == SCAN_F10)
    {
      shown(ShowInventoryScreen(_state, _hardware, screens.backward, screens.countLeft), false);
      continue;
    }
    // JMP 0BF3h, and GetKey until a key comes: a screenshot taken there leaves ES on the video segment (SaveScreenshot).
    _hardware.LoopTurn(FLIGHT_SCREEN_KEY_WAIT, {});
    const KeyPress next = WaitForKey(_state, _hardware, FLIGHT_SCREEN_KEY_WAIT);
    key = next.scanCode;
    screens.al = AlAfterKey(screens.al, next);
    if (next.screenshot)
    {
      segment = GameState::VIDEO_SEGMENT;
    }
  }
}

// 0x7FC5-0x802B: F1-F4 pick the view: a view not shown already is set, with a beep, and the stardust scattered afresh.
// Returns the last random number ResetStardust drew, when it ran.
std::optional<std::uint16_t> ChangeView(GameState& _state, std::uint8_t _key)
{
  if (_key < SCAN_F1 || _key > SCAN_F4)
  {
    return std::nullopt;
  }
  constexpr std::array<std::uint16_t, 4> VIEWS = {0, REAR_VIEW, LEFT_VIEW, RIGHT_VIEW};
  const std::uint16_t view = VIEWS[static_cast<std::size_t>(_key - SCAN_F1)];
  if (_state.Get(DS.viewAngle) == view)
  {
    return std::nullopt;
  }
  _state.Set(DS.viewAngle, view);
  StartBeep(_state);
  return ResetStardust(_state);
}

// mov ax, _text; jmp back to 0x80A3, the tail that posts it for 25 frames: a backward jump into shared code, not a loop, so
// its turn carries nothing.
void PostHyperspaceRefusal(GameState& _state, Hardware& _hardware, std::uint16_t _text)
{
  _hardware.LoopTurn(HYPERSPACE_REFUSAL_TAIL, {});
  SetMessage(_state, _text, 0x19);
}

// 0x8068-0x80FF: H, unless the docking computer is on or a countdown runs. True when the countdown started, which ends
// ProcessFlightKeys. _backward is the direction flag ResetHyperspaceRings' copy runs by.
[[nodiscard]] bool PressHyperspace(GameState& _state, Hardware& _hardware, bool _backward)
{
  if (_state.Get(DS.dockingComputerOn) == 1 || _state.Get(DS.hyperspaceCountdown) != 0)
  {
    // JE or JNE back to the JMP past the key's handling: a backward jump into shared code, not a loop, so its turn carries
    // nothing.
    _hardware.LoopTurn(PAST_HYPERSPACE_KEY, {});
    return false;
  }
  if (((_state.Get(DS.invadedStationDestroyed) ^ 1) & _state.Get(DS.thargoidInvasionActive) & _state.Get(DS.jumpedSinceBriefing)) != 0)
  {
    SetMessage(_state, DS.hyperspaceJammedMessage.offset, 0x19);
    return false;
  }
  if (_state.Get(DS.galacticDriveReadyFrames) != 0)
  {
    _state.Set(DS.galacticJumpPending, 1);
  }
  else
  {
    const std::uint16_t distance = _state.Get(DS.selectedDistanceTenthsLy);
    if (distance == 0)
    {
      SetMessage(_state, DS.noSystemSelectedMessage.offset, 0x19);
      return false;
    }
    if (distance >= OUT_OF_RANGE_TENTHS)
    {
      PostHyperspaceRefusal(_state, _hardware, DS.outOfRangeMessage.offset);
      return false;
    }
    // MOV BL,AL / MOV BH,0Ah / MOV AL,fuel / MUL BH / MOV BH,24h / DIV BH: the fuel times 10, over 24h, against the distance's
    // low byte; then MOV AX,distance / MUL BH / MOV BH,0Ah / DIV BH: the cost, its low byte times 24h over 10, at least 1. BX
    // holds BH and the distance's low byte at each divide.
    const std::uint8_t tenths = Low(distance);
    const ByteQuotient reach =
      DivideByte(_state, static_cast<std::uint16_t>(_state.Get(DS.fuel) * FUEL_FACTOR), FUEL_DIVISOR, Join(FUEL_DIVISOR, tenths));
    if (reach.quotient < tenths)
    {
      PostHyperspaceRefusal(_state, _hardware, DS.notEnoughFuelMessage.offset);
      return false;
    }
    const auto product = static_cast<std::uint16_t>(Low(_state.Get(DS.selectedDistanceTenthsLy)) * FUEL_DIVISOR);
    const ByteQuotient cost = DivideByte(_state, product, FUEL_FACTOR, Join(FUEL_FACTOR, tenths));
    _state.Set(DS.hyperspaceFuelCost, cost.quotient == 0 ? std::uint8_t{1} : cost.quotient);
  }
  _state.Set(DS.hyperspaceCountdown, 0x0A);
  _state.Set(DS.hyperspaceCountdownFrames, 0x0A);
  ShowHyperspaceCountdown(_state);
  ResetHyperspaceRings(_state, _backward);
  _state.Set(DS.missileState, 0);
  _state.Set(DS.shipIdRequested, 0);
  LatchHyperspaceTarget(_state);
  return true;
}

// 0x8171-0x826C: T primes a missile, which locks on what is in the sights; U unarms it; M launches it. _di is the DI
// ProcessFlightKeys holds here, which M's launch copies the missile from (LaunchPlayerMissile), unless the primed missile looked
// in the sights this frame and FindShipInCrosshairs left DI; _backward is the direction flag the copy runs by.
void HandleMissileKeys(GameState& _state, std::uint16_t _di, bool _backward)
{
  std::uint16_t source = _di;
  if (_state.Get(DS.keyDownT) == 1 && _state.Get(DS.missileCount) != 0 && _state.Get(DS.missileState) == 0 &&
      _state.Get(DS.hyperspaceCountdown) == 0 && _state.Get(DS.shipIdRequested) != 1)
  {
    _state.Set(DS.missileState, 1);
    _state.Set(DS.shipIdRequested, 0);
    StartBeep(_state);
    SetMessage(_state, DS.missilePrimedMessage.offset, 0x0F);
  }
  if (_state.Get(DS.missileState) == 1)
  {
    const CrosshairTarget target = FindShipInCrosshairs(_state);
    source = target.di;
    if (target.slot && (ObjectSlot(_state, *target.slot).Get(SlotByte::Flags) & FLAG_MASKED) == 0)
    {
      _state.Set(DS.missileTarget, *target.slot);
      _state.Set(DS.missileState, 2);
      StartBeep(_state);
      SetMessage(_state, DS.missileLockedMessage.offset, 0x14);
    }
  }
  if (_state.Get(DS.missileState) == 2)
  {
    if ((ObjectSlot(_state, _state.Get(DS.missileTarget)).Get(SlotByte::Type) & ObjectSlot::ACTIVE) == 0)
    {
      _state.Set(DS.missileState, 0);
      StartLowBeep(_state);
      SetMessage(_state, DS.missileTargetDestroyedMessage.offset, 0x14);
    }
  }
  if (_state.Get(DS.keyDownU) == 1 && _state.Get(DS.missileState) != 0)
  {
    _state.Set(DS.missileState, 0);
    _state.Set(DS.shipIdRequested, 0);
    StartLowBeep(_state);
    SetMessage(_state, DS.missileUnarmedMessage.offset, 0x0F);
  }
  if (_state.Get(DS.keyDownM) == 1 && _state.Get(DS.missileState) == 2 && _state.Get(DS.missileJammed) != 1)
  {
    if (NextRandom(_state) < MISSILE_JAM_ODDS)
    {
      _state.Set(DS.missileJammed, 1);
      SetMessage(_state, DS.missileJammedMessage.offset, 0x19);
      return;
    }
    _state.Set(DS.missileState, 0);
    _state.Set(DS.shipIdRequested, 0);
    SetMessage(_state, DS.missileLaunchedMessage.offset, 0x14);
    _state.Set(DS.missileCount, static_cast<std::uint8_t>(_state.Get(DS.missileCount) - 1));
    LaunchPlayerMissile(_state, source, _backward);
  }
}

// 0x826F-0x829E: fire heats the laser by 5; a pulse laser (type 0) fires every other frame.
void HandleFireButton(GameState& _state, Hardware& _hardware)
{
  if (!ReadFireButton(_state, _hardware))
  {
    return;
  }
  const std::optional<std::uint8_t> laser = GetViewLaser(_state);
  if (!laser || _state.Get(DS.laserTemperature) >= LASER_TOO_HOT)
  {
    return;
  }
  // ADD [laserTemperature],5, and FFh written over it on a carry, which a temperature below F0h never makes.
  const std::uint8_t temperature = _state.Get(DS.laserTemperature);
  _state.Set(DS.laserTemperature, static_cast<std::uint8_t>(temperature + LASER_HEAT_PER_SHOT));
  if (temperature + LASER_HEAT_PER_SHOT > 0xFF)
  {
    _state.Set(DS.laserTemperature, 0xFF);
  }
  if (*laser == PULSE_LASER)
  {
    _state.Set(DS.laserAlternateFrame, static_cast<std::uint8_t>(_state.Get(DS.laserAlternateFrame) ^ 1));
    if (_state.Get(DS.laserAlternateFrame) != 0)
    {
      return;
    }
  }
  _state.Set(DS.firingLaserType, *laser);
  _state.Set(DS.laserFiring, 1);
}

// 0x82F7-0x8363: I identifies the ship in the sights, and locks a missile on it if there is one.
void HandleIdentifyKey(GameState& _state)
{
  if (_state.Get(DS.shipIdRequested) != 1)
  {
    if (_state.Get(DS.keyDownI) != 1 || _state.Get(DS.missileState) != 0 || _state.Get(DS.hyperspaceCountdown) != 0)
    {
      return;
    }
    StartBeep(_state);
    if (_state.Get(DS.missileCount) != 0)
    {
      _state.Set(DS.missileState, 1);
    }
    _state.Set(DS.shipIdRequested, 1);
    SetMessage(_state, DS.idIndicatorMessage.offset, 0x19);
  }
  const CrosshairTarget target = FindShipInCrosshairs(_state);
  if (!target.slot)
  {
    return;
  }
  const ObjectSlot ship(_state, *target.slot);
  if (IsStation(ship).station)
  {
    return;
  }
  _state.Set(DS.missileState, 0);
  _state.Set(DS.shipIdRequested, 0);
  // PUSH DI / POP DI keep the ship round ShowShipIdentity and StartBeep.
  ShowShipIdentity(_state, static_cast<std::uint8_t>((ship.Get(SlotByte::Type) >> 1) & TYPE_MASK), ship.Get(SlotByte::Class), ship);
  StartBeep(_state);
  if (_state.Get(DS.missileCount) != 0)
  {
    _state.Set(DS.missileTarget, ship.Offset());
    _state.Set(DS.missileState, 2);
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

// The axes ApplyPitch turns are 10000 long: forward is +z, and up is -y.
constexpr std::int16_t AXIS_LENGTH = 0x2710;

// 0x84F2-0x8586: _pitch, -23 to 23, turns the camera frame by twice as much, and the three angles are derived from the
// turned frame again.
void ApplyPitch(GameState& _state, std::int8_t _pitch)
{
  (void)SetSinCos(_state, 7, Negate(static_cast<std::uint16_t>(Word(_pitch) << 1)));
  (void)SetSinCos(_state, 2, Negate(_state.Get(DS.playerRollAngle)));
  (void)SetSinCos(_state, 1, Negate(_state.Get(DS.playerYawAngle)));
  (void)SetSinCos(_state, 0, Negate(_state.Get(DS.playerPitchAngle)));

  // The forward axis, turned: the pitch from its y and z, then the yaw from its x and the z turned by the pitch.
  const Vector forward = RotateBySinCos7210(_state, Vector{0, 0, AXIS_LENGTH});
  const std::uint16_t pitch = ArcTangent2(_state, forward.y, forward.z);
  _state.Set(DS.pitchAngleScratch, Negate(pitch));
  (void)SetSinCos(_state, 6, pitch);
  const Pair pitched = RotateByStoredSinCos(_state, 6, Pair{forward.y, forward.z});
  _state.Set(DS.yawAngleScratch, Negate(ArcTangent2(_state, forward.x, pitched.second)));

  // The up axis, turned back by the pitch and the yaw: the roll from what is left.
  (void)SetSinCos(_state, 6, Negate(Plus(_state.Get(DS.pitchAngleScratch), 0x400)));
  const Vector up = RotateBySinCos7210(_state, Vector{0, -AXIS_LENGTH, 0});
  const Pair upPitched = RotateByStoredSinCos(_state, 6, Pair{up.y, up.z});
  (void)SetSinCos(_state, 7, Plus(_state.Get(DS.yawAngleScratch), 0x400));
  const Pair upYawed = RotateByStoredSinCos(_state, 7, Pair{up.x, upPitched.second});
  _state.Set(DS.playerRollAngle, ArcTangent2(_state, upYawed.first, upPitched.first));
  _state.Set(DS.playerYawAngle, Negate(_state.Get(DS.yawAngleScratch)));
  _state.Set(DS.playerPitchAngle, Negate(_state.Get(DS.pitchAngleScratch)));
  _state.Set(DS.velocityDirty, 1);
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

bool UpdateStardust(GameState& _state, std::uint16_t _bx)
{
  if (_state.Get(DS.jumpDriveEngaged) != 0)
  {
    SaveStardustPositions(_state);
  }
  _state.Set(DS.drawColor, _state.Get(DS.stardustColor));
  switch (_state.Get(DS.viewAngle))
  {
  case RIGHT_VIEW:
  case LEFT_VIEW:
    return UpdateSideStardust(_state, _state.Get(DS.viewAngle) == LEFT_VIEW);
  case REAR_VIEW:
    return UpdateFrontOrRearStardust(_state, true, _bx);
  default:
    return UpdateFrontOrRearStardust(_state, false, _bx);
  }
}

std::uint8_t ComputeStardustShift(GameState& _state, std::uint8_t _bh)
{
  // MOV AX,34h / SUB AX,playerSpeed / MOV BL,0Ch / DIV BL: a speed above 34h divides into the game's trap, which saves BX.
  const auto dividend = static_cast<std::uint16_t>(STARDUST_SHIFT_SPEED - _state.Get(DS.playerSpeed));
  auto shift = static_cast<std::uint8_t>(DivideByte(_state, dividend, STARDUST_SHIFT_STEP, Join(_bh, STARDUST_SHIFT_STEP)).quotient + 4);
  if (_state.Get(DS.jumpDriveEngaged) != 0)
  {
    shift = static_cast<std::uint8_t>(shift - 1);
  }
  _state.Set(DS.stardustShift, shift);
  return shift;
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

void ShiftStardustSideways(GameState& _state, std::int16_t _step)
{
  const std::uint16_t stripMask = ComputeDustStripMask(_step);
  std::uint16_t particle = DS.stardust.offset;
  for (std::uint16_t count = STARDUST_COUNT; count != 0; --count)
  {
    DustPosition position = LoadDustPosition(_state, particle);
    if (IsDustOnScreen(position))
    {
      position.x = Signed(Plus(Word(position.x), _step));
      if (!IsDustOnScreen(position))
      {
        position = RespawnDustAtSideEdge(_state, particle, _step, stripMask);
        StorePreviousDustPosition(_state, particle, position);
      }
      StoreDustPosition(_state, particle, position);
    }
    particle = Plus(particle, PARTICLE_BYTES);
  }
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

void RollStardust(GameState& _state)
{
  std::uint16_t particle = DS.stardust.offset;
  for (std::uint16_t count = STARDUST_COUNT; count != 0; --count)
  {
    const DustPosition position = LoadDustPosition(_state, particle);
    const Pair rolled = RotateByStoredSinCos(_state, 7, Pair{position.x, position.y});
    StoreDustPosition(_state, particle, DustPosition{rolled.first, rolled.second});
    particle = Plus(particle, PARTICLE_BYTES);
  }
}

void ShiftStardustVertically(GameState& _state, std::int16_t _step)
{
  const std::uint16_t stripMask = ComputeDustStripMask(_step);
  std::uint16_t particle = DS.stardust.offset;
  for (std::uint16_t count = STARDUST_COUNT; count != 0; --count)
  {
    DustPosition position = LoadDustPosition(_state, particle);
    if (IsDustOnScreen(position))
    {
      position.y = Signed(Plus(Word(position.y), _step));
      if (!IsDustOnScreen(position))
      {
        position = RespawnDustAtVerticalEdge(_state, particle, _step, stripMask);
        StorePreviousDustPosition(_state, particle, position);
      }
      StoreDustPosition(_state, particle, position);
    }
    particle = Plus(particle, PARTICLE_BYTES);
  }
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

FlightScreens HandleFlightFunctionKeys(GameState& _state, Hardware& _hardware, std::uint8_t _al, std::uint16_t _countIfNone,
                                       std::uint16_t _segment, bool _backward)
{
  _state.Set(DS.inFlight, 0);
  // F1-F10 in keyDown, BX the scan code and LOOP counting CX down from 10: each turn back to 0x0BBE carries both.
  std::uint16_t scan = SCAN_F1;
  for (std::uint16_t keys = FUNCTION_KEYS;;)
  {
    if (_state.Byte(Plus(DS.keyDown.offset, scan)) == 1)
    {
      // MOV AH,BL, and PUSH AX / CALL ResetKeyboard / POP AX keep it.
      ResetKeyboard(_state, _hardware);
      _state.Set(DS.flightScreenShown, 0);
      FlightScreens screens = DispatchFlightScreens(_state, _hardware, Low(scan), _al, _countIfNone, _segment, _backward);
      screens.key = Low(scan);
      return screens;
    }
    ++scan;
    if (--keys == 0)
    {
      break;
    }
    _hardware.LoopTurn(FUNCTION_KEY_LOOP, {keys, scan});
  }
  _state.Set(DS.inFlight, 1);
  return FlightScreens{0, _al, std::nullopt, std::nullopt, _countIfNone, _backward};
}

ScreenChange RestoreFlightScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  const ScreenChange change = ShowCockpitScreen(_state, _hardware, _backward);
  InvalidateDashboard(_state);
  StartBeep(_state);
  _state.Set(DS.messageShown, 0);
  return change;
}

void InvalidateDashboard(GameState& _state)
{
  // REP STOSB with ES = DS, upwards: DF is clear wherever the reference calls it, as for SaveStardustPositions.
  for (std::uint16_t index = 0; index < DASHBOARD_CACHE_BYTES; ++index)
  {
    _state.SetByte(Plus(DS.missileCountShown.offset, index), DASHBOARD_STALE);
  }
}

void UpdateDashboard(GameState& _state)
{
  (void)UpdateConditionColor(_state);
  DrawConditionLight(_state);
  UpdateEnergyAndLaserHeat(_state);
  UpdateSafeZone(_state);

  // The S glyph inside the station's safe zone, and E for a frame when the ECM fires, each in every colour (BX = FFFFh) at
  // B800:1658 and B800:1656.
  const std::uint8_t safeZoneGlyph = InSafeZone(_state).inside ? SAFE_ZONE_GLYPH : SPACE;
  if (ExchangeShown(_state, DS.safeZoneGlyphShown, safeZoneGlyph))
  {
    (void)DrawScreenChar(_state, safeZoneGlyph, ALL_COLORS, GameState::VIDEO_SEGMENT, SAFE_ZONE_GLYPH_CELL);
  }
  std::uint8_t ecmGlyph = SPACE;
  if (_state.Get(DS.ecmFired) == 1)
  {
    _state.Set(DS.ecmFired, 0);
    ecmGlyph = ECM_GLYPH;
  }
  if (ExchangeShown(_state, DS.ecmGlyphShown, ecmGlyph))
  {
    (void)DrawScreenChar(_state, ecmGlyph, ALL_COLORS, GameState::VIDEO_SEGMENT, ECM_GLYPH_CELL);
  }

  DrawEnergyBanks(_state);
  (void)DrawMissileLockIndicator(_state);
  (void)DrawMissileIcons(_state);

  // The pitch, then the roll, each indicator redrawn when it changed.
  const std::uint8_t pitch = High(_state.Get(DS.rollRate));
  if (pitch != _state.Get(DS.pitchRateShown))
  {
    _state.Set(DS.pitchRateShown, pitch);
    DrawSignedIndicator(_state, PITCH_INDICATOR_LINE, static_cast<std::int8_t>(pitch));
  }
  const std::uint8_t roll = Low(_state.Get(DS.rollRate));
  if (roll != _state.Get(DS.rollRateShown))
  {
    _state.Set(DS.rollRateShown, roll);
    DrawSignedIndicator(_state, ROLL_INDICATOR_LINE, static_cast<std::int8_t>(roll));
  }

  // From DrawEnergyBanks' last LOOP on, CX's high byte stays 0: every draw after it leaves CX = 0, or CL a count.
  RedrawThreeLineBar(_state, DS.laserTemperature, DS.laserTemperatureShown, 0x3B8C, true);
  RedrawThreeLineBar(_state, DS.altitude, DS.altitudeShown, 0x3CCC, true);
  RedrawThreeLineBar(_state, DS.cabinTemperature, DS.cabinTemperatureShown, 0x3A4C, true);
  RedrawThreeLineBar(_state, DS.fuel, DS.fuelShown, 0x390C, true);
  // The original never stores the shields' shown values, so a shield bar is drawn every frame.
  RedrawThreeLineBar(_state, DS.foreShield, DS.foreShieldShown, 0x368C, false);
  RedrawThreeLineBar(_state, DS.aftShield, DS.aftShieldShown, 0x37CC, false);

  // The speed, unscaled: MOV AX,playerSpeed / CMP AL,speedShown / JNE, then JMP DrawFiveLineBarPixels.
  const std::uint8_t speed = Low(_state.Get(DS.playerSpeed));
  if (speed == _state.Get(DS.speedShown))
  {
    return;
  }
  _state.Set(DS.speedShown, speed);
  DrawBar(_state, FIVE_LINES, SPEED_BAR_LINE, speed, 0);
}

void DrawFiveLineBar(GameState& _state, std::uint16_t _line, std::uint8_t _value, std::uint8_t _countHigh)
{
  DrawBar(_state, FIVE_LINES, _line, ScaleBarValue(_value), _countHigh);
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

void DrawThreeLineBar(GameState& _state, std::uint16_t _line, std::uint8_t _value, std::uint8_t _countHigh)
{
  DrawBar(_state, THREE_LINES, _line, ScaleBarValue(_value), _countHigh);
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

void DrawEnergyBanks(GameState& _state)
{
  // Each whole 100h of playerEnergy fills a bank from the top one down, by LOOP from CX = 4, the rest goes in the next, and the
  // banks below it are empty.
  const std::uint16_t energy = _state.Get(DS.playerEnergy);
  std::uint8_t wholeBanks = High(energy);
  std::uint16_t bank = DS.energyBankFill.At(ENERGY_BANKS - 1);
  std::uint16_t banks = ENERGY_BANKS;
  for (; banks != 0 && wholeBanks != 0; --banks, --wholeBanks)
  {
    _state.SetByte(bank, FULL_BANK);
    bank = static_cast<std::uint16_t>(bank - 1);
  }
  if (banks != 0)
  {
    _state.SetByte(bank, Low(energy));
    bank = static_cast<std::uint16_t>(bank - 1);
    for (--banks; banks != 0; --banks)
    {
      _state.SetByte(bank, 0);
      bank = static_cast<std::uint16_t>(bank - 1);
    }
  }

  // Each bank against its shown value, from the top bank's bar upwards, PUSH CX, DI and SI round each: the count left in CX, so
  // CH = 0 for the bar.
  std::uint16_t shown = static_cast<std::uint16_t>(DS.energyBankFill.offset - 1);
  std::uint16_t line = ENERGY_BANK_LINE;
  for (std::uint16_t count = ENERGY_BANKS; count != 0; --count)
  {
    const std::uint8_t fill = _state.Byte(Plus(shown, ENERGY_BANKS));
    if (fill != _state.Byte(shown))
    {
      _state.SetByte(shown, fill);
      DrawFiveLineBar(_state, line, fill, 0);
    }
    shown = static_cast<std::uint16_t>(shown - 1);
    line = static_cast<std::uint16_t>(line - ENERGY_BANK_STEP);
  }
}

void UpdateEnergyAndLaserHeat(GameState& _state)
{
  if (_state.Get(DS.gameOverFrames) == 0 && _state.Get(DS.escapePodFrames) == 0)
  {
    SubtractFloored(_state, DS.laserTemperature, LASER_COOLING);
    if (_state.Get(DS.playerEnergy) == MOST_ENERGY)
    {
      AddSaturating(_state, DS.aftShield, 1);
      AddSaturating(_state, DS.foreShield, 1);
    }
    else
    {
      // MOV AL,energyUnitFitted / SHL AL,1 / INC AL / CBW / ADD playerEnergy,AX, then CMP 400h / JB / MOV 3FFh: the sum written,
      // and 3FFh over it from 400h.
      const auto recharge = SignExtend(static_cast<std::uint8_t>((_state.Get(DS.energyUnitFitted) << 1) + 1));
      const std::uint16_t energy = Plus(_state.Get(DS.playerEnergy), recharge);
      _state.Set(DS.playerEnergy, energy);
      if (energy >= MOST_ENERGY + 1)
      {
        _state.Set(DS.playerEnergy, MOST_ENERGY);
      }
    }
  }
  if (_state.Get(DS.playerEnergy) >= ONE_BANK)
  {
    return;
  }
  // Below one bank, 50 in 65536 a frame, one of the 13 equipment bytes from missileCount is lost: XOR AH,AH / MOV BL,14h / DIV BL
  // on a second random word, which cannot overflow.
  if (NextRandom(_state) >= EQUIPMENT_LOSS_ODDS)
  {
    return;
  }
  const std::uint16_t equipment = Plus(DS.missileCount.offset, Low(NextRandom(_state)) / EQUIPMENT_LOSS_DIVISOR);
  if (_state.Byte(equipment) == 0)
  {
    return;
  }
  _state.SetByte(equipment, static_cast<std::uint8_t>(_state.Byte(equipment) - 1));
  SetMessage(_state, DS.equipmentLossMessage.offset, EQUIPMENT_LOSS_FRAMES);
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

ScreenChange SetUpLocalSpace(GameState& _state, Hardware& _hardware, bool _backward)
{
  LoadSystemSeeds(_state, _state.Get(DS.currentSystemIndex));
  InvalidateDashboard(_state);
  _state.Set(DS.objectSlotCount, 0x14);
  _state.Set(DS.debrisSlotCount, 0x10);
  _state.Set(DS.shipSlotCount, 0x24);
  ClearAllObjects(_state, _backward);
  const ScreenChange change = ShowCockpitScreen(_state, _hardware, _backward);
  (void)ResetStardust(_state);
  _state.Set(DS.hyperspaceCountdown, 0);
  _state.Set(DS.dockingComputerOn, 0);
  _state.Set(DS.dataA137, 0);
  _state.Set(DS.viewAngle, 0);
  _state.Set(DS.viewLocked, 0);
  _state.Set(DS.escapePodFrames, 0);
  _state.Set(DS.playerDocked, 0);
  _state.Set(DS.playerVelocityX, 0);
  _state.Set(DS.playerVelocityY, 0);
  _state.Set(DS.playerVelocityZ, 0x14);
  _state.Set(DS.playerSpeed, 0x14);
  _state.Set(DS.altitude, 0xFF);
  _state.Set(DS.missileJammed, 0);
  _state.Set(DS.playerPitchAngle, 0);
  _state.Set(DS.playerYawAngle, 0);
  _state.Set(DS.playerRollAngle, 0);
  if (_state.Get(DS.witchspaceCountdown) != 0)
  {
    _state.Set(DS.spawnGovernment, 0);
    return change;
  }
  PlaceSunPlanetAndStation(_state);
  return change;
}

void CheckCollisions(GameState& _state, Hardware& _hardware)
{
  // MOV CL,objectSlotCount / XOR CH,CH, PUSH CX and POP CX round each slot, then DEC CX / JE: a count of 0 runs 65,536 times.
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint32_t count = LoopCount(_state.Get(DS.objectSlotCount)); count != 0; --count)
  {
    const ObjectSlot object(_state, slot);
    if ((object.Get(SlotByte::Type) & ObjectSlot::ACTIVE) != 0)
    {
      CheckCollision(_state, _hardware, object);
    }
    slot = Plus(slot, SLOT_BYTES);
  }
}

SafeZone InSafeZone(const GameState& _state)
{
  const std::uint8_t flags = _state.Get(DS.safeZoneFlags);
  return SafeZone{(flags & 1) != 0, static_cast<std::uint8_t>(flags >> 1)};
}

void UpdateSafeZone(GameState& _state)
{
  const ObjectSlot station(_state, DS.stationSlot.offset);
  if ((station.Get(SlotByte::Type) & ObjectSlot::ACTIVE) == 0 || !IsStation(station).station)
  {
    _state.Set(DS.safeZoneFlags, 0);
    return;
  }
  // RCL AL,1: bit 0 the answer, over what AL held: IsObjectNear's last high byte, or the low byte of the station's distance.
  const NearTest near = IsObjectNear(_state, station);
  std::uint8_t held = near.lastHigh;
  bool inside = false;
  if (near.nearby)
  {
    const std::uint16_t distance = VectorLength(GetObjectPosition(station));
    held = Low(distance);
    inside = distance < SAFE_ZONE_RADIUS;
  }
  _state.Set(DS.safeZoneFlags, static_cast<std::uint8_t>((held << 1) | (inside ? 1 : 0)));
}

Vector ComputeDeathDebrisVector(GameState& _state)
{
  (void)SetSinCos(_state, 8, Negate(_state.Get(DS.viewAngle)));
  (void)SetSinCos(_state, 6, Negate(Plus(_state.Get(DS.playerPitchAngle), 0x400)));
  (void)SetSinCos(_state, 7, Plus(_state.Get(DS.playerYawAngle), 0x400));
  // (0, 40, 0), turned by the view and, off the front view, by the roll, which SetSinCos8 puts in the view's place.
  std::int16_t x = 0;
  std::int16_t y = DEATH_DEBRIS_SPEED;
  std::int16_t z = 0;
  if (_state.Get(DS.viewAngle) != 0)
  {
    const Pair viewed = RotateByStoredSinCos(_state, 8, Pair{x, y});
    (void)SetSinCos(_state, 8, Negate(_state.Get(DS.playerRollAngle)));
    // PUSH AX, BX, CX round SetSinCos8, and POP BX, CX, AX: y and z change places.
    const Pair rolled = RotateByStoredSinCos(_state, 8, Pair{viewed.first, z});
    x = rolled.first;
    y = viewed.second;
    z = rolled.second;
  }
  // Then the yaw turns (x, y) and the pitch (z, y), and the XCHGs leave x, the pitch's first and its second in AX, BX
  // and CX.
  const Pair yawed = RotateByStoredSinCos(_state, 7, Pair{x, y});
  const Pair pitched = RotateByStoredSinCos(_state, 6, Pair{z, yawed.second});
  return Vector{yawed.first, pitched.first, pitched.second};
}

void UpdateWarnings(GameState& _state)
{
  if (_state.Get(DS.gameOverFrames) != 0)
  {
    return;
  }
  const std::uint8_t frames = _state.Get(DS.warningFrames);
  if (frames != 0)
  {
    // XOR AH,AH / MOV [messageFrames],AX: the frames left, as a word.
    _state.Set(DS.messageFrames, frames);
    _state.Set(DS.messagePointer, _state.Get(DS.warningMessage));
    _state.Set(DS.warningFrames, static_cast<std::uint8_t>(_state.Get(DS.warningFrames) - 1));
    return;
  }
  // The four checks from the one after the last warning (JMP [BX+warningChecks]).
  (void)RunWarningChecks(_state, static_cast<std::uint16_t>((_state.Get(DS.warningIndex) + 1) & 3), WARNING_CHECKS);
}

WarningChecks CheckMissileWarning(GameState& _state, std::uint16_t _checks)
{
  return RunWarningChecks(_state, 0, _checks);
}

WarningChecks CheckAltitudeWarning(GameState& _state, std::uint16_t _checks)
{
  return RunWarningChecks(_state, 1, _checks);
}

WarningChecks CheckTemperatureWarning(GameState& _state, std::uint16_t _checks)
{
  return RunWarningChecks(_state, 2, _checks);
}

WarningChecks CheckEnergyWarning(GameState& _state, std::uint16_t _checks)
{
  return RunWarningChecks(_state, 3, _checks);
}

std::optional<DashboardPixel> UpdateScannerBlip(GameState& _state, ObjectSlot _slot, Vector _camera)
{
  if (_slot.Offset() >= DS.debrisSlots.offset || IsStation(_slot).station || IsSunOrPlanet(_slot))
  {
    return std::nullopt;
  }
  _slot.Set(SlotByte::Scanned, 1);
  // The scanner's y and z are 1.25 times the camera's. The blip is the high bytes, each XCHGed with the old blip's.
  const auto scaled = [](std::int16_t _coordinate) { return High(Plus(Word(_coordinate), Signed(Sar(Word(_coordinate), 2)))); };
  const std::uint8_t oldX = _slot.Get(SlotByte::BlipX);
  _slot.Set(SlotByte::BlipX, High(Word(_camera.x)));
  const std::uint8_t oldY = _slot.Get(SlotByte::BlipY);
  _slot.Set(SlotByte::BlipY, scaled(_camera.y));
  const std::uint8_t oldZ = _slot.Get(SlotByte::BlipZ);
  _slot.Set(SlotByte::BlipZ, scaled(_camera.z));
  if ((_slot.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) != 0)
  {
    (void)XorScannerBlip(_state, oldX, oldY, oldZ);
  }
  const DashboardPixel last = XorScannerBlip(_state, _slot.Get(SlotByte::BlipX), _slot.Get(SlotByte::BlipY), _slot.Get(SlotByte::BlipZ));
  _slot.Set(SlotByte::Flags, static_cast<std::uint8_t>(_slot.Get(SlotByte::Flags) | FLAG_BLIP_DRAWN));
  return last;
}

std::optional<CompassUpdate> UpdateCompass(GameState& _state)
{
  if (_state.Get(DS.titleShown) == 0)
  {
    return std::nullopt;
  }
  // The target: the station when the planet is near, else the planet. Its scale shift goes to its depth byte.
  std::uint16_t target = DS.planetSlot.offset;
  _state.Set(DS.compassTargetIsStation, 0);
  if (IsObjectNearKeepBlip(ObjectSlot(_state, target)))
  {
    target = Plus(target, ObjectSlot::BYTES);
    _state.Set(DS.compassTargetIsStation, 1);
  }
  ObjectSlot aim(_state, target);
  const std::uint8_t shift = GetPositionScaleShift(aim).shift;
  aim.Set(SlotByte::Depth, shift);
  const Vector position = ScalePositionDown(aim, shift);
  // PUSH AX / PUSH BX / PUSH CX round the station's scale shift, read back through MOV BP,SP: the position turned to the view
  // for the station's compass words, then, popped, turned to the camera's frame for the dot.
  ObjectSlot station(_state, DS.stationSlot.offset);
  station.Set(SlotByte::Depth, GetPositionScaleShift(station).shift);
  const Vector view = TransformToView(_state, position);
  station.Set(SlotWord::CompassX, Word(view.x));
  station.Set(SlotWord::CompassY, Word(view.y));
  station.Set(SlotWord::CompassZ, Word(view.z));
  const Vector camera = RotatePitchYawRoll(_state, position);

  // BP gathers z >= 0 in bit 15 and the signs of x and y in bits 0 and 1, each coordinate NEGed to its magnitude.
  const bool inFront = !Negative(Word(camera.z));
  const std::uint16_t range = Plus(inFront ? Word(camera.z) : Negate(Word(camera.z)), COMPASS_DEPTH);
  const bool left = Negative(Word(camera.x));
  const bool below = Negative(Word(camera.y));
  const std::uint16_t xMagnitude = left ? Negate(Word(camera.x)) : Word(camera.x);
  const std::uint16_t yMagnitude = below ? Negate(Word(camera.y)) : Word(camera.y);
  // XCHG BX,AX between the two: x's divide has |y| in BX, and y's has x's step.
  const std::uint16_t xStep = ScaleCompassAxis(_state, xMagnitude, range, yMagnitude);
  const std::uint16_t yStep = ScaleCompassAxis(_state, yMagnitude, range, xStep);
  auto dotX = static_cast<std::uint8_t>(xStep);
  auto dotY = static_cast<std::uint8_t>(yStep);
  // MUL AL twice: the dot's distance squared; from 41h, each step CBW'd or under the remainder AH still holds, times 8, divided
  // by its square root from sqrtTable (BX = the root), its low three bits kept. The second divide can overflow, and the trap's
  // 7Fh comes out as 7.
  const auto squared =
    static_cast<std::uint8_t>(Low(static_cast<std::uint16_t>(dotY * dotY)) + Low(static_cast<std::uint16_t>(dotX * dotX)));
  if (squared >= COMPASS_NORMALIZED_FROM)
  {
    const std::uint8_t root = _state.Byte(Plus(DS.sqrtTable.offset, squared));
    const ByteQuotient first = DivideByte(_state, static_cast<std::uint16_t>(SignExtend(dotX) << 3), root, root);
    dotX = static_cast<std::uint8_t>(first.quotient & COMPASS_STEP_MASK);
    const ByteQuotient second = DivideByte(_state, static_cast<std::uint16_t>(Join(first.remainder, dotY) << 3), root, root);
    dotY = static_cast<std::uint8_t>(second.quotient & COMPASS_STEP_MASK);
  }
  if (left)
  {
    dotX = Negate(dotX);
  }
  if (below)
  {
    dotY = Negate(dotY);
  }
  dotY = static_cast<std::uint8_t>(dotY + COMPASS_CENTER_Y);
  dotX = static_cast<std::uint8_t>(dotX + COMPASS_CENTER_X);

  // XCHG [DI+26h],DX and XCHG [DI+28h],AH: the new dot and whether it is in front go into the station's blip bytes, the old
  // ones come out, and the old dot is XORed out if it is drawn; then the new one in, from what the bytes hold.
  const std::uint16_t oldDot = station.Get(SlotWord::CompassDot);
  station.Set(SlotWord::CompassDot, Join(dotY, dotX));
  const std::uint8_t wasInFront = station.Get(SlotByte::BlipZ);
  station.Set(SlotByte::BlipZ, inFront ? COMPASS_IN_FRONT : std::uint8_t{0});
  if ((station.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) != 0)
  {
    (void)XorCompassDot(_state, Low(oldDot), High(oldDot), wasInFront != 0);
  }
  const std::uint8_t nowInFront = station.Get(SlotByte::BlipZ);
  const std::uint16_t dot = station.Get(SlotWord::CompassDot);
  station.Set(SlotByte::Flags, static_cast<std::uint8_t>(station.Get(SlotByte::Flags) | FLAG_BLIP_DRAWN));
  return CompassUpdate{XorCompassDot(_state, Low(dot), High(dot), SignExtend(nowInFront) != 0), range, nowInFront};
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

std::uint16_t PixelPlace(DashboardPixel _pixel) noexcept
{
  return Join(_pixel.y, _pixel.x);
}

std::optional<DashboardPixel> EraseScannerBlip(GameState& _state, ObjectSlot _slot)
{
  if (IsStation(_slot).station || (_slot.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) == 0)
  {
    return std::nullopt;
  }
  const DashboardPixel last = XorScannerBlip(_state, _slot.Get(SlotByte::BlipX), _slot.Get(SlotByte::BlipY), _slot.Get(SlotByte::BlipZ));
  _slot.Set(SlotByte::Flags, static_cast<std::uint8_t>(_slot.Get(SlotByte::Flags) & ~FLAG_BLIP_DRAWN));
  return last;
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

bool EraseCompassAndBlips(GameState& _state)
{
  bool erased = false;
  ObjectSlot station(_state, DS.stationSlot.offset);
  if ((station.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) != 0)
  {
    // The dot at BlipX and BlipY, solid while BlipZ is not 0 (CBW into BP), its flag cleared first.
    const bool inFront = station.Get(SlotByte::BlipZ) != 0;
    const std::uint8_t x = station.Get(SlotByte::BlipX);
    const std::uint8_t y = station.Get(SlotByte::BlipY);
    station.Set(SlotByte::Flags, static_cast<std::uint8_t>(station.Get(SlotByte::Flags) & ~FLAG_BLIP_DRAWN));
    (void)XorCompassDot(_state, x, y, inFront);
    erased = true;
  }
  // MOV CL,objectSlotCount / XOR CH,CH / SUB CL,3, and LOOP: 65,536 slots for a count of 3.
  std::uint16_t slot = DS.firstShipSlot.offset;
  const auto slots = static_cast<std::uint8_t>(_state.Get(DS.objectSlotCount) - 3);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    erased = EraseScannerBlip(_state, ObjectSlot(_state, slot)).has_value() || erased;
    slot = Plus(slot, SLOT_BYTES);
  }
  return erased;
}

void UpdateFuelLeak(GameState& _state, Hardware& _hardware)
{
  std::uint8_t border = 0;
  if (_state.Get(DS.fuelLeakDelayFrames) != 0)
  {
    _state.Set(DS.fuelLeakDelayFrames, static_cast<std::uint8_t>(_state.Get(DS.fuelLeakDelayFrames) - 1));
    if (_state.Get(DS.fuelLeakDelayFrames) == 0)
    {
      _state.Set(DS.fuelLeakFrames, FUEL_LEAK_FRAMES);
      return;
    }
    border = _state.Get(DS.maskingBackgroundColor);
  }
  else if (_state.Get(DS.fuelLeakFrames) != 0)
  {
    _state.Set(DS.fuelLeakFrames, static_cast<std::uint8_t>(_state.Get(DS.fuelLeakFrames) - 1));
    // SUB [fuel],5, and 0 written over it on a borrow.
    const std::uint8_t fuel = _state.Get(DS.fuel);
    _state.Set(DS.fuel, static_cast<std::uint8_t>(fuel - FUEL_LEAK_STEP));
    if (fuel < FUEL_LEAK_STEP)
    {
      _state.Set(DS.fuel, 0);
    }
    _state.Set(DS.messageFrames, SignExtend(_state.Get(DS.fuelLeakFrames)));
    _state.Set(DS.messagePointer, DS.fuelLeakText.offset);
    border = FUEL_LEAK_BORDER;
  }
  else
  {
    border = _state.Get(DS.maskingBackgroundColor);
  }
  _hardware.SetColorSelect(static_cast<std::uint8_t>(border | BRIGHT_PALETTE));
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

bool TickEscapePod(GameState& _state)
{
  if (_state.Get(DS.escapePodFrames) == 0)
  {
    return false;
  }
  _state.Set(DS.escapePodFrames, static_cast<std::uint8_t>(_state.Get(DS.escapePodFrames) - 1));
  return _state.Get(DS.escapePodFrames) == 0;
}

FlightKeysExit ProcessFlightKeys(GameState& _state, Hardware& _hardware, std::uint8_t _al, std::uint16_t _di, std::uint16_t _countIfNone,
                                 std::uint16_t _segment, bool _backward)
{
  if (_state.Get(DS.gameOverFrames) != 0)
  {
    return FlightKeysExit{false, _backward};
  }
  if (_state.Get(DS.escapePodFrames) != 0)
  {
    // JNE back to the RET above: a backward jump into shared code, not a loop, so its turn carries nothing.
    _hardware.LoopTurn(PROCESS_FLIGHT_KEYS_RETURN, {});
    return FlightKeysExit{false, _backward};
  }
  const FlightScreens screens = HandleFlightFunctionKeys(_state, _hardware, _al, _countIfNone, _segment, _backward);
  const bool backward = screens.backward;
  // DI, from which M's launch copies the missile, as the routines before it leave it: InvalidateDashboard once the cockpit came
  // back, ResetStardust once the view changed, IsMassLocked once J asked it, and ClearDrawBuffer once the pause screen resumed.
  std::uint16_t di = screens.restored ? DASHBOARD_CACHE_END : _di;
  if (screens.view != 0 && _state.Get(DS.viewLocked) != 1 && ChangeView(_state, screens.view))
  {
    di = STARDUST_END;
  }
  if (_state.Get(DS.keyDownG) == 1 && _state.Get(DS.galacticHyperdriveFitted) == 1 && _state.Get(DS.galacticDriveReadyFrames) == 0 &&
      _state.Get(DS.hyperspaceCountdown) == 0)
  {
    StartBeep(_state);
    SetMessage(_state, DS.galacticDriveReadyMessage.offset, 0x28);
    _state.Set(DS.galacticDriveReadyFrames, 0x28);
  }
  if (_state.Get(DS.keyDownH) == 1 && PressHyperspace(_state, _hardware, backward))
  {
    return FlightKeysExit{false, backward};
  }
  // D, released, toggles the docking computer: not in mission 3 until the invaded station is destroyed.
  if ((_state.Get(DS.missionNumber) != 3 || _state.Get(DS.invadedStationDestroyed) == 1) && _state.Get(DS.dockingKeyReleased) == 1 &&
      _state.Get(DS.dockingComputerFitted) == 1)
  {
    if (_state.Get(DS.hyperspaceCountdown) != 0)
    {
      _state.Set(DS.dockingKeyReleased, 0);
    }
    else
    {
      _state.Set(DS.jumpDriveEngaged, 0);
      (void)ToggleDockingComputer(_state, _hardware);
    }
  }
  if (_state.Get(DS.keyDownJ) == 1)
  {
    if (const JumpDriveRequest request = EngageJumpDrive(_state); request.lock)
    {
      di = DiAfterMassLock(*request.lock, di);
    }
  }
  if (_state.Get(DS.keyDownEsc) == 1)
  {
    _state.Set(DS.gamePaused, 1);
    if (_state.Get(DS.keyDownCtrl) == 1)
    {
      // Ctrl+Esc freezes the game until a key, without the menu.
      ResetKeyboard(_state, _hardware);
      (void)WaitForKey(_state, _hardware, FREEZE_KEY_WAIT);
    }
    else
    {
      SetMessage(_state, DS.gamePausedMessage.offset, 1);
      (void)UpdateMessageLine(_state, backward);
      if (RunPauseScreen(_state, _hardware, backward))
      {
        return FlightKeysExit{true, backward};
      }
      di = DrawBufferEnd(backward);
    }
    _state.Set(DS.gamePaused, 0);
  }
  HandleMissileKeys(_state, di, backward);
  HandleFireButton(_state, _hardware);
  if (_state.Get(DS.keyDownE) == 1 && _state.Get(DS.ecmFitted) == 1)
  {
    _state.Set(DS.ecmFired, 1);
    SetMessage(_state, DS.ecmActiveMessage.offset, 5);
    (void)RemoveAllMissiles(_state);
    DrainEnergy(_state, ECM_ENERGY);
  }
  if (_state.Get(DS.keyDownB) == 1 && _state.Get(DS.energyBombFitted) == 1)
  {
    _state.Set(DS.energyBombFitted, 0);
    (void)DetonateEnergyBomb(_state, _hardware, backward);
  }
  if (_state.Get(DS.keyDownC) == 1 && _state.Get(DS.escapePodFitted) == 1 && _state.Get(DS.escapePodFrames) == 0)
  {
    StartBeep(_state);
    LaunchEscapePod(_state);
  }
  HandleIdentifyKey(_state);
  if (_state.Get(DS.keyDownN) == 1 && _state.Get(DS.maskingDeviceFitted) == 1)
  {
    UseMaskingDevice(_state);
  }
  HandleAntiEcmKey(_state);
  return FlightKeysExit{false, backward};
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

JumpDriveRequest EngageJumpDrive(GameState& _state)
{
  // MOV AX,message, then the shared tail at 8450 posts it for 5 frames.
  const auto post = [&_state](DataAt _message) -> std::uint16_t
  {
    SetMessage(_state, _message.offset, JUMP_DRIVE_MESSAGE_FRAMES);
    return _message.offset;
  };
  std::optional<MassLock> lock;
  if (_state.Get(DS.dockingComputerOn) != 1)
  {
    if (_state.Get(DS.playerSpeed) != FULL_SPEED)
    {
      _state.Set(DS.jumpDriveEngaged, 0);
      return JumpDriveRequest{post(DS.jumpDriveVelocityLockedMessage), lock};
    }
    lock = IsMassLocked(_state);
    if (!lock->locked)
    {
      _state.Set(DS.jumpDriveEngaged, 1);
      _state.Set(DS.velocityDirty, 1);
      return JumpDriveRequest{post(DS.jumpDriveEngagedMessage), lock};
    }
  }
  _state.Set(DS.jumpDriveEngaged, 0);
  return JumpDriveRequest{post(DS.jumpDriveMassLockedMessage), lock};
}

void UpdatePlayerMotion(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t gameOver = _state.Get(DS.gameOverFrames);
  if (gameOver != 0 && gameOver != GAME_OVER_FRAMES)
  {
    return;
  }
  if (_state.Get(DS.escapePodFrames) != 0)
  {
    MoveObjectsByVelocity(_state);
    return;
  }
  Steering steering{};
  if (_state.Get(DS.dockingComputerOn) != 0)
  {
    (void)RunDockingComputer(_state);
    const std::uint16_t steered = _state.Get(DS.dockingComputerSteering);
    steering = Steering{Low(steered), High(steered)};
  }
  else
  {
    // . and , change the speed's low byte by 4, within 4-48 unless it wraps, and the word is written back.
    std::uint16_t speed = _state.Get(DS.playerSpeed);
    if (_state.Get(DS.keyDownPeriod) == 1)
    {
      speed = WithLow(speed, static_cast<std::uint8_t>(Low(speed) + SPEED_KEY_STEP));
      _state.Set(DS.velocityDirty, 1);
      if (Low(speed) >= TOP_SPEED + 1)
      {
        speed = WithLow(speed, TOP_SPEED);
      }
    }
    if (_state.Get(DS.keyDownComma) == 1)
    {
      speed = WithLow(speed, static_cast<std::uint8_t>(Low(speed) - SPEED_KEY_STEP));
      _state.Set(DS.velocityDirty, 1);
      if (Low(speed) < SPEED_KEY_STEP)
      {
        speed = WithLow(speed, SPEED_KEY_STEP);
      }
    }
    _state.Set(DS.playerSpeed, speed);
    // ReadSteering fires the stick with AL, the speed's low byte.
    const Steering read = ReadSteering(_state, _hardware, Low(speed));
    _state.Set(DS.rollRate, Join(read.pitch, read.roll));
    steering = ApplyReverseControls(_state, read);
  }

  // Roll, -23 to 23, negated, turns the roll angle by twice as much: NEG AL / JE, then the limits, CBW / SHL AX,1.
  if (const auto roll = static_cast<std::int8_t>(Negate(steering.roll)); roll != 0)
  {
    const std::int8_t held = roll <= -MOST_ROLL - 1 ? static_cast<std::int8_t>(-MOST_ROLL) : roll >= MOST_ROLL + 1 ? MOST_ROLL : roll;
    _state.Set(DS.playerRollAngle, Plus(_state.Get(DS.playerRollAngle), SignExtend(static_cast<std::uint8_t>(held)) << 1));
  }
  if (steering.pitch != 0)
  {
    ApplyPitch(_state, static_cast<std::int8_t>(steering.pitch));
  }
  if (_state.Get(DS.dockingComputerOn) == 1)
  {
    return;
  }
  UpdatePlayerVelocity(_state);
  MoveObjectsByVelocity(_state);
}

void UpdatePlayerVelocity(GameState& _state)
{
  if (_state.Get(DS.velocityDirty) != 1)
  {
    return;
  }
  // DEC: the 1 becomes 0.
  _state.Set(DS.velocityDirty, 0);
  (void)SetSinCos(_state, 6, Negate(Plus(_state.Get(DS.playerPitchAngle), 0x400)));
  (void)SetSinCos(_state, 7, Plus(_state.Get(DS.playerYawAngle), 0x400));
  std::uint16_t speed = _state.Get(DS.playerSpeed);
  if (_state.Get(DS.jumpDriveEngaged) == 1)
  {
    // The jump drive: 32 times the speed, for this frame only.
    _state.Set(DS.velocityDirty, 1);
    _state.Set(DS.jumpDriveEngaged, 0);
    speed = static_cast<std::uint16_t>(speed << 5);
  }
  // (0, speed) turned by the yaw, then (0, what that leaves of the speed) by the pitch.
  const Pair yawed = RotateByStoredSinCos(_state, 7, Pair{0, Signed(speed)});
  _state.Set(DS.playerVelocityX, Word(yawed.first));
  const Pair pitched = RotateByStoredSinCos(_state, 6, Pair{0, yawed.second});
  _state.Set(DS.playerVelocityY, Word(pitched.first));
  _state.Set(DS.playerVelocityZ, Word(pitched.second));
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

bool RunPauseScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  SilenceSpeakerTimer(_hardware);
  for (;;)
  {
    DrawPauseMenu(_state, _hardware, _backward);
    ResetKeyboard(_state, _hardware);
    const PauseExit exit = RunPauseOptions(_state, _hardware, _backward);
    if (exit != PauseExit::NewFrameRate)
    {
      return exit == PauseExit::Abort;
    }
    // JMP 8D6Dh, the menu again: its turn loads or writes every register it reads, and carries nothing.
    _hardware.LoopTurn(PAUSE_MENU, {});
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
using Machine::REGISTER_DS;
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
// CheckCollisions': all but DS, which the original leaves alone and the flight loop goes on with, and DI, past the slots, from
// which LaunchPlayerMissile copies (CheckCollisionsEntry).
constexpr Machine::NativeContract COLLISIONS_CHECKED = Clobbers(REGISTER_ALL & ~REGISTER_DS & ~REGISTER_DI);
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
constexpr Machine::NativeContract CLOBBERS_AX_BX_DX = Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_DX);
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX = Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX);
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX = Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX);
constexpr Machine::NativeContract CLOBBERS_AX_DX = Clobbers(REGISTER_AX | REGISTER_DX);
constexpr Machine::NativeContract CLOBBERS_DX_DI_BP = Clobbers(REGISTER_DX | REGISTER_DI | REGISTER_BP);
constexpr Machine::NativeContract SHIFTS_STARDUST = Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_BP);
constexpr Machine::NativeContract ROLLS_STARDUST = Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI);
// EraseCompassAndBlips': all but DS, which the original leaves alone, and ES, which ShowShortRangeChart goes on with
// (EraseCompassAndBlipsEntry).
constexpr Machine::NativeContract ERASES_COMPASS_AND_BLIPS =
  Clobbers(static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_DS & ~REGISTER_ES));

constexpr Machine::NativeContract UPDATES_STARDUST =
  Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP);
// UpdateDashboard's: ES = B800h, and DS, which the original leaves alone and the flight loop goes on with.
constexpr Machine::NativeContract UPDATES_DASHBOARD = Clobbers(static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_ES & ~REGISTER_DS));
constexpr Machine::NativeContract DRAWS_BAR = Clobbers(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI);
constexpr Machine::NativeContract CLOBBERS_AX_BX = Clobbers(REGISTER_AX | REGISTER_BX);
constexpr Machine::NativeContract CLOBBERS_EVERY_REGISTER = Clobbers(REGISTER_ALL);
// SetUpLocalSpace's, UpdatePlayerMotion's and ProcessFlightKeys': all but DS, which the original leaves alone and the flight loop
// goes on with.
constexpr Machine::NativeContract CLOBBERS_ALL_BUT_DS = Clobbers(static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_DS));
// HandleFlightFunctionKeys': BX, DX and SI, which the screens leave and no value routine computes; ProcessFlightKeys, its one caller,
// writes each before it reads it (HandleFlightFunctionKeysEntry).
constexpr Machine::NativeContract FUNCTION_KEY_SCREENS = Clobbers(REGISTER_BX | REGISTER_DX | REGISTER_SI);

// MOV AX,DS / MOV ES,AX / MOV DI,missileCountShown / MOV CX,16h / MOV AL,80h / REP STOSB: what InvalidateDashboard leaves, ES =
// DS, and AX, CX and DI, which RestoreFlightScreen's contract compares.
void InvalidateDashboardOut(Registers& _regs) noexcept
{
  _regs.es = _regs.ds;
  _regs.ax = WithLow(_regs.ds, DASHBOARD_STALE);
  _regs.cx = 0;
  _regs.di = Plus(DS.missileCountShown.offset, DASHBOARD_CACHE_BYTES);
}

// What ShowCockpitScreen leaves once it draws, CLD and SI past the image, and BX and DX as SetGraphicsMode leaves them once it
// sets the mode; then what InvalidateDashboard leaves.
void RestoreFlightScreenOut(Guest& _guest, ScreenChange _change) noexcept
{
  Registers& regs = _guest.Regs();
  if (_change != ScreenChange::None)
  {
    _guest.SetFlag(Machine::FLAG_DIRECTION, false);
    regs.si = COCKPIT_IMAGE_END;
    if (_change == ScreenChange::ModeSet)
    {
      regs.bx = GRAPHICS_MODE_BX;
      regs.dx = GRAPHICS_MODE_DX;
    }
  }
  InvalidateDashboardOut(regs);
}

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

// What the last XorDashboardPixel of a scanner blip leaves: AX = DX the last pixel, BX its mask and ES the video segment;
// and what the stick's loop leaves, CL = 0 and CH its step, up for a negative _y, the blip's y byte.
void ScannerBlipOut(Registers& _regs, DashboardPixel _last, std::uint8_t _y) noexcept
{
  _regs.dx = PixelPlace(_last);
  _regs.ax = _regs.dx;
  _regs.bx = _last.mask;
  _regs.cx = Join((Sar(_y, 2) & 0x80) != 0 ? std::uint8_t{0xFF} : std::uint8_t{0x01}, 0);
  _regs.es = GameState::VIDEO_SEGMENT;
}

// A point as the register code holds it: x in AX, y in BX, z in CX.
[[nodiscard]] Vector VectorIn(const Registers& _regs) noexcept
{
  return Vector{Signed(_regs.ax), Signed(_regs.bx), Signed(_regs.cx)};
}

void VectorOut(Registers& _regs, Vector _vector) noexcept
{
  _regs.ax = Word(_vector.x);
  _regs.bx = Word(_vector.y);
  _regs.cx = Word(_vector.z);
}

// The warning checks' registers: each check loads AL with its number and LOOP counts CX down, and SetWarning leaves BX the
// warning's byte offset in warningTexts and AX its text.
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

void ShiftStardustSidewaysEntry(Guest& _guest)
{
  ShiftStardustSideways(_guest.State(), Signed(_guest.Regs().dx));
  _guest.Clobber(SHIFTS_STARDUST);
}

void RespawnDustAtSideEdgeEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  DustOut(regs, RespawnDustAtSideEdge(_guest.State(), regs.si, Signed(regs.dx), regs.bp));
  _guest.Clobber(PRESERVES_ALL);
}

void RollStardustEntry(Guest& _guest)
{
  RollStardust(_guest.State());
  _guest.Clobber(ROLLS_STARDUST);
}

void ShiftStardustVerticallyEntry(Guest& _guest)
{
  ShiftStardustVertically(_guest.State(), Signed(_guest.Regs().dx));
  _guest.Clobber(SHIFTS_STARDUST);
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
  // The original leaves AX the last random with the last lifetime in AL, and DI past the particles, which
  // RunDockingComputer's contract compares.
  ResetStardustOut(_guest.Regs(), ResetStardust(_guest.State()));
  _guest.Clobber(CLOBBERS_CX);
}

void SaveStardustPositionsEntry(Guest& _guest)
{
  SaveStardustPositions(_guest.State());
  _guest.Clobber(CLOBBERS_AX_CX_SI_DI);
}

void InvalidateDashboardEntry(Guest& _guest)
{
  InvalidateDashboard(_guest.State());
  InvalidateDashboardOut(_guest.Regs());
  _guest.Clobber(PRESERVES_ALL);
}

void UpdateStardustEntry(Guest& _guest)
{
  // DrawLine's ES = DS and CLD once a streak filled bytes: the contract compares ES.
  DrawLineOut(_guest, UpdateStardust(_guest.State(), _guest.Regs().bx));
  _guest.Clobber(UPDATES_STARDUST);
}

void ComputeStardustShiftEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  (void)ComputeStardustShift(_guest.State(), High(regs.bx));
  // MOV BL,0Ch, and the contract compares BX.
  SetLow(regs.bx, STARDUST_SHIFT_STEP);
  _guest.Clobber(CLOBBERS_AX);
}

void RestoreFlightScreenEntry(Guest& _guest)
{
  // The contract compares every register.
  RestoreFlightScreenOut(_guest, RestoreFlightScreen(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION)));
  _guest.Clobber(PRESERVES_ALL);
}

void UpdateDashboardEntry(Guest& _guest)
{
  UpdateDashboard(_guest.State());
  // MOV AX,0B800h / MOV ES,AX first: the contract compares ES.
  _guest.Regs().es = GameState::VIDEO_SEGMENT;
  _guest.Clobber(UPDATES_DASHBOARD);
}

void DrawFiveLineBarEntry(Guest& _guest)
{
  const Registers& regs = _guest.Regs();
  DrawFiveLineBar(_guest.State(), regs.di, Low(regs.ax), High(regs.cx));
  _guest.Clobber(DRAWS_BAR);
}

void DrawThreeLineBarEntry(Guest& _guest)
{
  const Registers& regs = _guest.Regs();
  DrawThreeLineBar(_guest.State(), regs.di, Low(regs.ax), High(regs.cx));
  _guest.Clobber(DRAWS_BAR);
}

void DrawEnergyBanksEntry(Guest& _guest)
{
  DrawEnergyBanks(_guest.State());
  _guest.Clobber(CLOBBERS_EVERY_REGISTER);
}

void UpdateEnergyAndLaserHeatEntry(Guest& _guest)
{
  UpdateEnergyAndLaserHeat(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX);
}

void UpdatePlayerMotionEntry(Guest& _guest)
{
  UpdatePlayerMotion(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
}

void SetUpLocalSpaceEntry(Guest& _guest)
{
  // ShowCockpitScreen's CLD, once it drew.
  if (SetUpLocalSpace(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION)) != ScreenChange::None)
  {
    _guest.SetFlag(Machine::FLAG_DIRECTION, false);
  }
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
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

void ComputeDeathDebrisVectorEntry(Guest& _guest)
{
  VectorOut(_guest.Regs(), ComputeDeathDebrisVector(_guest.State()));
  _guest.Clobber(CLOBBERS_DX_DI_BP);
}

void EraseCompassAndBlipsEntry(Guest& _guest)
{
  // XorDashboardPixel leaves ES on the video segment once anything is erased, and ShowShortRangeChart goes on with ES.
  if (EraseCompassAndBlips(_guest.State()))
  {
    _guest.Regs().es = GameState::VIDEO_SEGMENT;
  }
  _guest.Clobber(ERASES_COMPASS_AND_BLIPS);
}

void UpdateWarningsEntry(Guest& _guest)
{
  UpdateWarnings(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX_CX);
}

void CheckMissileWarningEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  WarningChecksOut(regs, CheckMissileWarning(_guest.State(), regs.cx));
  _guest.Clobber(PRESERVES_ALL);
}

void CheckAltitudeWarningEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  WarningChecksOut(regs, CheckAltitudeWarning(_guest.State(), regs.cx));
  _guest.Clobber(PRESERVES_ALL);
}

void CheckTemperatureWarningEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  WarningChecksOut(regs, CheckTemperatureWarning(_guest.State(), regs.cx));
  _guest.Clobber(PRESERVES_ALL);
}

void CheckEnergyWarningEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  WarningChecksOut(regs, CheckEnergyWarning(_guest.State(), regs.cx));
  _guest.Clobber(PRESERVES_ALL);
}

void UpdateScannerBlipEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::optional<DashboardPixel> last = UpdateScannerBlip(_guest.State(), ObjectSlot(_guest.State(), regs.di), VectorIn(regs));
  // PUSH and POP keep AX, BX, CX and DI, but not DX, which the contract compares: past the debris slots the original holds AX
  // in it across the type tests, and a blip drawn leaves it the last pixel, with ES the video segment.
  if (last)
  {
    regs.dx = PixelPlace(*last);
    regs.es = GameState::VIDEO_SEGMENT;
  }
  else if (regs.di < DS.debrisSlots.offset)
  {
    regs.dx = regs.ax;
  }
  _guest.Clobber(PRESERVES_ALL);
}

void UpdateCompassEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  // The contract compares every register. Once it runs, the original leaves what its last XorCompassDot leaves
  // (XorCompassDotEntry): DX one right of and one above the dot, AX = DX, BX the last mask and ES the video segment; CX the
  // dot's divisor, which no divide changes; BP the in-front byte, CBW'd; and DI = stationSlot, which TransformAndDrawObjects
  // goes on with. Nothing it calls touches SI.
  if (const std::optional<CompassUpdate> update = UpdateCompass(_guest.State()))
  {
    regs.dx = PixelPlace(update->last);
    regs.ax = regs.dx;
    regs.bx = update->last.mask;
    regs.es = GameState::VIDEO_SEGMENT;
    regs.cx = update->range;
    regs.bp = SignExtend(update->inFront);
    regs.di = DS.stationSlot.offset;
  }
  _guest.Clobber(PRESERVES_ALL);
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

void EraseScannerBlipEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  // PUSH AX / POP AX keep AX round IsStation, and PUSH DI / POP DI keep DI. A blip erased leaves AX, BX, CX, DX and ES as the
  // last XorScannerBlip does, and the contract compares them all: through IsObjectNear, EngageJumpDrive, CheckShipInRange and
  // IsMassLocked compare BX, CX and DX, UpdateMissileAi, TryScoopObject and TransformShip compare DX, and ExplodeObject,
  // through RemoveObject, stores what it finds there.
  EraseScannerBlipOut(_guest, slot, EraseScannerBlip(_guest.State(), slot));
  _guest.Clobber(PRESERVES_ALL);
}

void EraseScannerBlipOut(Guest& _guest, const ObjectSlot& _slot, const std::optional<DashboardPixel>& _erased)
{
  if (_erased)
  {
    ScannerBlipOut(_guest.Regs(), *_erased, _slot.Get(SlotByte::BlipY));
  }
}

void XorScannerBlipEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint8_t y = High(regs.bx);
  const DashboardPixel last = XorScannerBlip(_guest.State(), High(regs.ax), y, High(regs.cx));
  // The last XorDashboardPixel leaves AX = DX, the last pixel, BX its mask and ES the video segment, and the stick's loop
  // CX = its step and 0: UpdateScannerBlip's contract compares DX and ES, and EngageJumpDrive's and CheckShipInRange's, through
  // EraseScannerBlip, BX and CX as well.
  ScannerBlipOut(regs, last, y);
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

void UpdatePlayerVelocityEntry(Guest& _guest)
{
  UpdatePlayerVelocity(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX_DX);
}

void CheckCollisionsEntry(Guest& _guest)
{
  // DI past the slots it looked at, by the count it loaded, which nothing it does changes: LaunchPlayerMissile, through
  // ProcessFlightKeys, copies the 64 bytes there.
  CheckCollisions(_guest.State(), _guest.Devices());
  _guest.Regs().di = static_cast<std::uint16_t>(DS.shipSlots.offset + LoopCount(_guest.Get(DS.objectSlotCount)) * ObjectSlot::BYTES);
  _guest.Clobber(COLLISIONS_CHECKED);
}

void EngageJumpDriveEntry(Guest& _guest)
{
  const JumpDriveRequest request = EngageJumpDrive(_guest.State());
  // The contract compares every register: what IsMassLocked leaves, once it was asked, then AX the message.
  if (request.lock)
  {
    MassLockOut(_guest, *request.lock);
  }
  _guest.Regs().ax = request.message;
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

void UpdateSafeZoneEntry(Guest& _guest)
{
  UpdateSafeZone(_guest.State());
  // MOV DI,stationSlot: the contract keeps DI.
  _guest.Regs().di = DS.stationSlot.offset;
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX);
}

void UpdateFuelLeakEntry(Guest& _guest)
{
  UpdateFuelLeak(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_AX_DX);
}

void TickEscapePodEntry(Guest& _guest)
{
  if (TickEscapePod(_guest.State()))
  {
    // POP AX: the return address goes, and the RET after it returns from RunFlight.
    _guest.Regs().ax = _guest.Pop();
  }
  _guest.Clobber(PRESERVES_ALL);
}

void HandleFlightFunctionKeysEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const FlightScreens screens =
    HandleFlightFunctionKeys(_guest.State(), _guest.Devices(), Low(regs.ax), regs.bp, regs.es, _guest.Flag(Machine::FLAG_DIRECTION));
  // What the original leaves: CX as the scan of keyDown leaves it, 0 past F10 or the LOOP's count at the key it found; once a
  // screen was shown, what RestoreFlightScreen leaves (RestoreFlightScreenOut), AX but, which PUSH AX / POP AX keep round it; AH
  // the view key or 0 and AL, BP and the direction flag as the screens leave them. BX, DX and SI, which the screens leave and no
  // value routine computes, the contract leaves to it.
  regs.cx = screens.key ? static_cast<std::uint16_t>(FUNCTION_KEYS - (*screens.key - SCAN_F1)) : std::uint16_t{0};
  if (screens.restored)
  {
    RestoreFlightScreenOut(_guest, *screens.restored);
  }
  regs.ax = Join(screens.view, screens.al);
  regs.bp = screens.countLeft;
  _guest.SetFlag(Machine::FLAG_DIRECTION, screens.backward);
  _guest.Clobber(FUNCTION_KEY_SCREENS);
}

void ProcessFlightKeysEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const FlightKeysExit exit =
    ProcessFlightKeys(_guest.State(), _guest.Devices(), Low(regs.ax), regs.di, regs.bp, regs.es, _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.SetFlag(Machine::FLAG_DIRECTION, exit.backward);
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
  if (exit.aborted)
  {
    // The pause screen's A: its POP AX / POP AX drop its own return address, which a value call has none of, and then this
    // routine's, so that the RET returns from RunFlight.
    regs.ax = _guest.Pop();
  }
}

void RunPauseScreenEntry(Guest& _guest)
{
  const bool aborted = RunPauseScreen(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_EVERY_REGISTER);
  if (aborted)
  {
    // A: POP AX / POP AX drop its own return address and its caller's, ProcessFlightKeys', so that the RET returns from RunFlight.
    Registers& regs = _guest.Regs();
    regs.ax = _guest.Pop();
    regs.ax = _guest.Pop();
  }
}

namespace
{

// HandleFlightFunctionKeys and ProcessFlightKeys work once a frame and return; they wait for a key only
// on the paths the F5-F10 screens, the pause and the Ctrl+Esc freeze take, so they wait sometimes.
// RunFlight and RunPauseScreen wait as a rule.
constexpr std::array ENTRIES = {
  NativeEntry{0x068F, "UpdateStardust", &UpdateStardustEntry, UPDATES_STARDUST},
  NativeEntry{0x0887, "ComputeStardustShift", &ComputeStardustShiftEntry, CLOBBERS_AX},
  NativeEntry{0x08A1, "GetPreviousDustScreenPosition", &GetPreviousDustScreenPositionEntry, PREVIOUS_DUST_OUT},
  NativeEntry{0x08B9, "ComputeDustStripMask", &ComputeDustStripMaskEntry, CLOBBERS_AX},
  NativeEntry{0x08CB, "ShiftStardustSideways", &ShiftStardustSidewaysEntry, SHIFTS_STARDUST},
  NativeEntry{0x08F2, "RespawnDustAtSideEdge", &RespawnDustAtSideEdgeEntry, PRESERVES_ALL},
  NativeEntry{0x0927, "RollStardust", &RollStardustEntry, ROLLS_STARDUST},
  NativeEntry{0x0940, "ShiftStardustVertically", &ShiftStardustVerticallyEntry, SHIFTS_STARDUST},
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
  NativeEntry{0x0BB3, "HandleFlightFunctionKeys", &HandleFlightFunctionKeysEntry, FUNCTION_KEY_SCREENS, NativeReturn::Near, 0,
              NativeWait::Sometimes},
  NativeEntry{0x15CF, "RestoreFlightScreen", &RestoreFlightScreenEntry, PRESERVES_ALL},
  NativeEntry{0x2540, "InvalidateDashboard", &InvalidateDashboardEntry, PRESERVES_ALL},
  NativeEntry{0x254F, "UpdateDashboard", &UpdateDashboardEntry, UPDATES_DASHBOARD},
  NativeEntry{0x2645, "DrawFiveLineBar", &DrawFiveLineBarEntry, DRAWS_BAR},
  NativeEntry{0x26BE, "DrawSignedIndicator", &DrawSignedIndicatorEntry, CLOBBERS_AX_BX_DI},
  NativeEntry{0x273E, "DrawThreeLineBar", &DrawThreeLineBarEntry, DRAWS_BAR},
  NativeEntry{0x2799, "DrawMissileIcons", &DrawMissileIconsEntry, CLOBBERS_AX_SI_DI},
  NativeEntry{0x2804, "DrawMissileLockIndicator", &DrawMissileLockIndicatorEntry, CLOBBERS_AX_BX_DI},
  NativeEntry{0x283B, "DrawEnergyBanks", &DrawEnergyBanksEntry, CLOBBERS_EVERY_REGISTER},
  NativeEntry{0x2882, "UpdateEnergyAndLaserHeat", &UpdateEnergyAndLaserHeatEntry, CLOBBERS_AX_BX},
  NativeEntry{0x290C, "DrawConditionLight", &DrawConditionLightEntry, CLOBBERS_AX_BX_CX_SI_DI},
  NativeEntry{0x2959, "UpdateConditionColor", &UpdateConditionColorEntry, PRESERVES_ALL},
  NativeEntry{0x29D0, "SetUpLocalSpace", &SetUpLocalSpaceEntry, CLOBBERS_ALL_BUT_DS},
  NativeEntry{0x2BC5, "CheckCollisions", &CheckCollisionsEntry, COLLISIONS_CHECKED},
  NativeEntry{0x2E63, "InSafeZone", &InSafeZoneEntry, CARRY_OUT},
  NativeEntry{0x2E69, "UpdateSafeZone", &UpdateSafeZoneEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x2F8B, "ComputeDeathDebrisVector", &ComputeDeathDebrisVectorEntry, CLOBBERS_DX_DI_BP},
  NativeEntry{0x36B6, "UpdateWarnings", &UpdateWarningsEntry, CLOBBERS_AX_BX_CX},
  NativeEntry{0x36FD, "CheckMissileWarning", &CheckMissileWarningEntry, PRESERVES_ALL},
  NativeEntry{0x370E, "CheckAltitudeWarning", &CheckAltitudeWarningEntry, PRESERVES_ALL},
  NativeEntry{0x371A, "CheckTemperatureWarning", &CheckTemperatureWarningEntry, PRESERVES_ALL},
  NativeEntry{0x3726, "CheckEnergyWarning", &CheckEnergyWarningEntry, PRESERVES_ALL},
  NativeEntry{0x40EC, "UpdateScannerBlip", &UpdateScannerBlipEntry, PRESERVES_ALL},
  NativeEntry{0x418F, "UpdateCompass", &UpdateCompassEntry, PRESERVES_ALL},
  NativeEntry{0x42A4, "XorCompassDot", &XorCompassDotEntry, PRESERVES_ALL},
  NativeEntry{0x42D6, "EraseScannerBlip", &EraseScannerBlipEntry, PRESERVES_ALL},
  NativeEntry{0x42F6, "XorScannerBlip", &XorScannerBlipEntry, CLOBBERS_DI},
  NativeEntry{0x43C4, "XorDashboardPixel", &XorDashboardPixelEntry, CLOBBERS_DI},
  NativeEntry{0x4594, "EraseCompassAndBlips", &EraseCompassAndBlipsEntry, ERASES_COMPASS_AND_BLIPS},
  NativeEntry{0x499F, "UpdateFuelLeak", &UpdateFuelLeakEntry, CLOBBERS_AX_DX},
  NativeEntry{0x7E9B, "RunFlight", &RunFlight, Clobbers(REGISTER_ALL), NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x7F69, "TickEscapePod", &TickEscapePodEntry, PRESERVES_ALL},
  NativeEntry{0x7FA8, "ProcessFlightKeys", &ProcessFlightKeysEntry, CLOBBERS_ALL_BUT_DS, NativeReturn::Near, 0, NativeWait::Sometimes},
  NativeEntry{0x839F, "DrainEnergy", &DrainEnergyEntry, PRESERVES_ALL},
  NativeEntry{0x8430, "EngageJumpDrive", &EngageJumpDriveEntry, PRESERVES_ALL},
  NativeEntry{0x8472, "UpdatePlayerMotion", &UpdatePlayerMotionEntry, CLOBBERS_ALL_BUT_DS, Machine::NativeReturn::Near, 0,
              Machine::NativeWait::Sometimes},
  NativeEntry{0x8599, "UpdatePlayerVelocity", &UpdatePlayerVelocityEntry, CLOBBERS_AX_BX_DX},
  NativeEntry{0x85EC, "MoveObjectsByVelocity", &MoveObjectsByVelocityEntry, CLOBBERS_CX_DX},
  NativeEntry{0x8D6A, "RunPauseScreen", &RunPauseScreenEntry, CLOBBERS_EVERY_REGISTER, NativeReturn::Near, 0, NativeWait::Always},
};

} // namespace

std::span<const NativeEntry> FlightEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
