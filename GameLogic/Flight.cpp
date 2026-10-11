#include "pch.h"

#include "Flight.h"

#include "Ai.h"
#include "Arithmetic.h"
#include "Combat.h"
#include "Docked.h"
#include "Docking.h"
#include "Equipment.h"
#include "FlightTables.h"
#include "Galaxy.h"
#include "Hyperspace.h"
#include "Input.h"
#include "Market.h"
#include "Maths.h"
#include "SpaceObject.h"
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

// RunFlight's frame loop (FlightFrameLoop), where its four backward jumps land (ADR-015). The frame reads no register before it writes
// it, but the direction flag, so its turns carry nothing.
constexpr std::uint16_t FLIGHT_FRAME_LOOP = 0x7EA4;

// What RunFlight hands its callees for the registers the original passes them by accident and no value routine computes (ADR-012).
// Each reaches nothing the game can show:
// - ProcessFlightKeys' ES is DS, as UpdateObjectsAndSpawn's PUSH DS / POP ES leaves it, or B800h, once a blip was XORed after it
//   (XorDashboardPixel). Only a chart drawn while its frame shows already reads it (ChartSegment), and in flight the cockpit always
//   shows when ProcessFlightKeys runs: SetUpLocalSpace shows it, and so does every leaving of the function keys' screens
//   (RestoreFlightScreen).
// - BP is PresentSpaceView's 20h, as FinishSpaceViewFrame leaves it each frame, unless one of UpdateObjectsAndSpawn's handlers
//   leaves its own: ComputeAnglesToObject's first angle, TurnTowardAngles' pitch error, the last hunter CountOtherHuntersOnScanner
//   found, or a quarter of the hunter's z from GetVectorToObject; and after ProcessFlightKeys, what its screens and its crosshair
//   search leave, which TickHyperspaceCountdown is called with. It reaches only the count SelectSystemAtCursor makes an index from
//   when no system is on the chart, through the function keys' screens, GalacticJump and, after the escape pod, the docked status
//   screen; and a system always is. On the galactic chart every system is, and from any cursor one of each galaxy, 0 to 8, is near
//   enough for its distance not to carry (at worst 19962, in galaxy 8, measured over every cursor with a scratch model of the search
//   outside the repository); on the short-range chart, the current system at its centre is.
// ClearMessageLine counts its eight rows down in BP (MOV BP,8 / DEC BP), and leaves it 0: after GAME OVER, and after the pause
// screen's A, which clears the message line.
constexpr std::uint16_t CLEARED_MESSAGE_LINE_BP = 0;

// The stardust: 30 particles of 6 bytes, x and y words, a lifetime byte and a spare; stardustPrevious follows with the same
// layout, its spare the new-particle flag.
constexpr std::uint16_t STARDUST_COUNT = 30;
constexpr std::uint16_t PARTICLE_BYTES = 6;
constexpr std::size_t PARTICLE_X = 0;
constexpr std::size_t PARTICLE_Y = 2;
constexpr std::size_t PARTICLE_LIFETIME = 4;
constexpr std::size_t PARTICLE_NEW = 5;
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

// mov [messagePointer], ax; mov [messageFrames], _frames, after mov ax, text: _message posted for _frames frames. Its callers
// load AX with its text's address themselves.
void SetMessage(GameState& _state, Message _message, std::uint16_t _frames)
{
  _state.Data().messagePointer = _message;
  _state.Data().messageFrames = _frames;
}

// ---- Stardust -------------------------------------------------------------------------------------

// The lifetime byte of particle _particle, its copy in stardustPrevious, and the copy marked new, for a respawned particle.
void MarkDustRespawned(GameState& _state, std::size_t _particle, std::uint8_t _lifetime)
{
  _state.Data().stardust[_particle][PARTICLE_LIFETIME] = _lifetime;
  _state.Data().stardustPrevious[_particle][PARTICLE_LIFETIME] = _lifetime;
  _state.Data().stardustPrevious[_particle][PARTICLE_NEW] = 1;
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

// Particle _particle, at _position, as a point, or while the jump drive is engaged a streak from where it was last
// frame, unless it has just respawned. Returns whether DrawLine filled bytes with REP STOSB.
bool DrawDust(GameState& _state, std::size_t _particle, DustPosition _position)
{
  const DustScreenPosition screen = DustToScreen(_position);
  const std::uint8_t x = Low(Word(screen.x));
  const std::uint8_t row = Low(Word(screen.row));
  if (_state.Data().jumpDriveEngaged == 0)
  {
    PlotPixel(_state, x, row);
    return false;
  }
  if (_state.Data().stardustPrevious[_particle][PARTICLE_NEW] != 0)
  {
    return false;
  }
  const std::optional<DustScreenPosition> previous = GetPreviousDustScreenPosition(_state, _particle);
  return previous && DrawLine(_state, x, row, Low(Word(previous->x)), Low(Word(previous->row)));
}

// Particle _particle, which LoadDustPosition found on screen at _position, moved by _step (ScaleDustStep's), outwards
// for the front view and inwards for the rear; then stored, and born again anywhere (RespawnDustAnywhere) while it is off
// screen, or for the rear view near the centre, or at the end of its life, which the rear view counts down. Returns where it is.
DustPosition MoveDust(GameState& _state, std::size_t _particle, DustPosition _position, bool _rear)
{
  DustPosition position = _position;
  if (_state.Data().playerSpeed != 0)
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
      Counted<std::uint8_t>& lifetime = _state.Data().stardust[_particle][PARTICLE_LIFETIME];
      lifetime = static_cast<std::uint8_t>(lifetime - 1);
      if (lifetime != 0)
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
// (DrawDust), PUSH CX and POP CX keeping the count round each. Returns whether a DrawLine filled bytes with REP STOSB.
bool UpdateFrontOrRearStardust(GameState& _state, bool _rear)
{
  // MOV DX,rollRate / ApplyReverseControlsToDx / AND DH,DH / XOR DL,DL / NEG DX for the front view / SAR DX,1.
  const std::uint16_t pitchRate = _state.Data().rollRate;
  const std::uint8_t pitch = ApplyReverseControls(_state, Steering{Low(pitchRate), High(pitchRate)}).pitch;
  if (pitch != 0)
  {
    const std::uint16_t step = Join(pitch, 0);
    ShiftStardustVertically(_state, Signed(Sar(_rear ? step : Negate(step), 1)));
  }
  // MOV AX,rollRate / ApplyReverseControls / CBW / NEG AX for the front view / SHL AX,1: the roll.
  const std::uint16_t rollRate = _state.Data().rollRate;
  const std::uint16_t roll = SignExtend(ApplyReverseControls(_state, Steering{Low(rollRate), High(rollRate)}).roll);
  if (const auto angle = static_cast<std::uint16_t>((_rear ? roll : Negate(roll)) << 1); angle != 0)
  {
    (void)SetSinCos(_state, 7, angle);
    RollStardust(_state);
  }
  (void)ComputeStardustShift(_state);
  bool filled = false;
  for (std::size_t particle = 0; particle < STARDUST_COUNT; ++particle)
  {
    if (const DustPosition position = LoadDustPosition(_state, particle); IsDustOnScreen(position))
    {
      filled = DrawDust(_state, particle, MoveDust(_state, particle, position, _rear)) || filled;
    }
  }
  return filled;
}

// 0x084D: the side views only draw; ShiftStardustSideways has moved the dust. Each particle on screen is drawn (DrawDust),
// PUSH CX and PUSH SI keeping the count and the particle round it. Returns whether a DrawLine filled bytes with REP STOSB.
bool DrawSideStardust(GameState& _state)
{
  bool filled = false;
  for (std::size_t particle = 0; particle < STARDUST_COUNT; ++particle)
  {
    const DustPosition position = LoadDustPosition(_state, particle);
    if (IsDustOnScreen(position))
    {
      filled = DrawDust(_state, particle, position) || filled;
    }
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
  const std::uint16_t rollRate = _state.Data().rollRate;
  const std::uint8_t roll = ApplyReverseControls(_state, Steering{Low(rollRate), High(rollRate)}).roll;
  if (roll != 0)
  {
    const std::uint16_t step = Join(roll, 0);
    ShiftStardustVertically(_state, Signed(Sar(_left ? step : Negate(step), 1)));
  }
  // The speed, negated for the left view, as DX = its low byte:00, shifted right three times, moves it sideways.
  if (const std::uint16_t speed = _state.Data().playerSpeed; speed != 0)
  {
    const std::uint16_t step = Join(Low(_left ? Negate(speed) : speed), 0);
    ShiftStardustSideways(_state, Signed(Sar(step, 3)));
  }
  // MOV AX,rollRate / ApplyReverseControls / MOV AL,AH / CBW / SHL AX,1: the reversed pitch byte, doubled and negated for the
  // left view, rolls it.
  const std::uint16_t pitchRate = _state.Data().rollRate;
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
    fill(_state.Data().barPartialBytes[partial]);
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
void RedrawThreeLineBar(GameState& _state, const Counted<std::uint8_t>& _value, Counted<std::uint8_t>& _shown, std::uint16_t _line,
                        bool _remember)
{
  const std::uint8_t value = _value;
  if (value == _shown)
  {
    return;
  }
  if (_remember)
  {
    _shown = value;
  }
  DrawThreeLineBar(_state, _line, value, 0);
}

// SUB byte,_value / JAE / MOV byte,0 on _field: the difference written, and 0 over it on a borrow.
void SubtractFloored(Counted<std::uint8_t>& _field, std::uint8_t _value)
{
  const std::uint8_t before = _field;
  _field = static_cast<std::uint8_t>(before - _value);
  if (before < _value)
  {
    _field = 0;
  }
}

// cmp [_shown], al; mov [_shown], al; je, with AL = _glyph: whether the glyph changed.
[[nodiscard]] bool ExchangeShown(Counted<std::uint8_t>& _shown, std::uint8_t _glyph)
{
  const bool changed = _shown != _glyph;
  _shown = _glyph;
  return changed;
}

// ---- Collisions -----------------------------------------------------------------------------------

// The station's x and y, with z 0, within _halfSize: inside the docking slot.
[[nodiscard]] bool InsideDockingSlot(const SpaceObject& _station, std::uint16_t _halfSize)
{
  return VectorWithinBox(Vector{Signed(_station.x), Signed(_station.y), 0}, _halfSize);
}

// 0x2BD9-0x2C8D: _slot, an active one, against the player. Outside its type's collisionRanges box, a station's collided bit
// is cleared. Inside, a ship costs 450 and is removed. The station docks the player when it is drawn, the player is aligned
// within 100 (CheckDockingAlignment), inside a box of 90 about its x and y, not invaded and not hostile; nearly aligned, within
// 250, it scrapes for 30 inside a box of 110 and crashes for 400 outside it; otherwise, or when it has collided already, it
// costs 1500 and is removed. Each hit is credited (CreditKill), unlocks a missile on it (CheckMissileTargetDestroyed), takes
// the damage (TakeDamage) and starts the impact sound, whose STI the hit's every path ends with, as KillPlayer's does when it
// kills.
void CheckCollision(GameState& _state, Hardware& _hardware, SpaceObject& _slot)
{
  // MOV BL,[DI] / AND BX,3Eh: the type, doubled, indexes the box's half sizes.
  const std::uint16_t range = _state.Data().collisionRanges[(_slot.type & 0x3E) >> 1];
  // The station's collided bit is bit 0 of its yaw's low byte.
  const auto clearCollided = [&_slot] { _slot.yaw = WithLow(_slot.yaw, static_cast<std::uint8_t>(Low(_slot.yaw) & 0xFE)); };
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
  else if ((Low(_slot.yaw) & 1) == 0)
  {
    _slot.yaw = WithLow(_slot.yaw, static_cast<std::uint8_t>(Low(_slot.yaw) | 1));
    if ((_slot.type & 0x80) != 0)
    {
      if (CheckDockingAlignment(_state, _slot, 0x64))
      {
        // Aligned: docked, if inside the slot and the station is not the Thargoids'.
        if (_state.Data().commanderBlock.thargoidInvasionActive != 1 && InsideDockingSlot(_slot, 0x5A) && (_slot.flags & 1) == 0)
        {
          _state.Data().playerDocked = 1;
          _state.Data().dockingComputerOn = 0;
          _state.Data().rollRate = 0;
          _state.Data().viewLocked = 0;
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
  (void)CheckMissileTargetDestroyed(_state, _slot);
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

// SetWarning (0x36E7): posts warning _warning, its message WARNING_MESSAGES[_warning] (warningTexts), for 20 frames.
void SetWarning(GameState& _state, std::uint8_t _warning)
{
  _state.Data().warningFrames = 0x14;
  _state.Data().warningIndex = _warning;
  _state.Data().warningMessage = WARNING_MESSAGES[_warning];
}

// CheckMissileWarning, CheckAltitudeWarning, CheckTemperatureWarning and CheckEnergyWarning (0x36FD,
// 0x370E, 0x371A, 0x3726): whether warning _check applies. The missile check clears its alert.
[[nodiscard]] bool WarningApplies(GameState& _state, std::uint16_t _check)
{
  switch (_check)
  {
  case 0:
  {
    const bool alert = _state.Data().incomingMissileAlert == 1;
    _state.Data().incomingMissileAlert = 0;
    return alert;
  }
  case 1:
    return _state.Data().altitude < 0x32;
  case 2:
    return _state.Data().cabinTemperature >= 0xE1;
  default:
    return _state.Data().playerEnergy < 0x100;
  }
}

// The four checks round-robin from _first, each falling on to the next with LOOP while _checks lasts, until one posts its
// warning.
void RunWarningChecks(GameState& _state, std::uint16_t _first, std::uint16_t _checks)
{
  std::uint16_t check = _first;
  std::uint16_t checks = _checks;
  for (;;)
  {
    if (WarningApplies(_state, check))
    {
      SetWarning(_state, static_cast<std::uint8_t>(check));
      return;
    }
    if (--checks == 0)
    {
      return;
    }
    check = static_cast<std::uint16_t>((check + 1) & 3);
  }
}

// ---- The scanner and the compass ------------------------------------------------------------------

// ScaleCompassAxis (0x41FF-0x4217, and again at 0x421B-0x4233): CMP CX,AX / JB, then CWD, three SHL AX / RCL DX and DIV CX:
// 8 * _magnitude / _range when _range is not below _magnitude, else _magnitude; then at most 7. A magnitude of 8000h, which
// NEG leaves so, is negative to CWD, and the divide traps.
[[nodiscard]] std::uint16_t ScaleCompassAxis(std::uint16_t _magnitude, std::uint16_t _range)
{
  std::uint16_t scaled = _magnitude;
  if (_range >= _magnitude)
  {
    const std::uint32_t dividend = ((std::uint32_t{SignWord(_magnitude)} << 16) | _magnitude) << 3;
    scaled = DivideWord(dividend, _range).quotient;
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
// 0, upwards or, _backward, down; and IsMassLocked on the last slot it looked at, as the original leaves it: the sun's, the
// planet's, then where its look at the ships stopped.
[[nodiscard]] std::uint16_t DashboardCacheEnd(const GameState& _state) noexcept
{
  return static_cast<std::uint16_t>(_state.OffsetOf(_state.Data().missileCountShown) + DASHBOARD_CACHE_BYTES);
}

[[nodiscard]] std::uint16_t StardustEnd(const GameState& _state) noexcept
{
  return static_cast<std::uint16_t>(_state.OffsetOf(_state.Data().stardust) + STARDUST_BYTES);
}

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
    di = GameState::SlotOffset(SlotIndex::Sun);
  }
  if (_lock.planet)
  {
    di = GameState::SlotOffset(SlotIndex::Planet);
  }
  if (_lock.ships)
  {
    di = GameState::SlotOffset(_lock.ships->slot);
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

// The pause screen's five options from keyboardRecenter, two bytes each: the option's flag, then its letter.
[[nodiscard]] std::span<Counted<std::uint8_t>> PauseOptions(GameState& _state)
{
  return _state.BytesFrom(_state.Data().keyboardRecenter, std::size_t{PAUSE_OPTIONS} * 2);
}

// PauseShowOptions (0x8D8F): the five option letters, in reverse video when on. The loop at 0x8D98 pushes and pops CX, DI
// and SI round DrawViewChar, and each turn carries them: the options left, the letter's place and the option's flag byte.
void ShowPauseOptions(GameState& _state, Hardware& _hardware)
{
  const std::span<Counted<std::uint8_t>> options = PauseOptions(_state);
  std::uint16_t position = PAUSE_FIRST_OPTION;
  std::size_t option = 0;
  std::uint16_t optionsLeft = PAUSE_OPTIONS;
  for (;;)
  {
    _state.Data().textPaperPattern = 0;
    std::uint16_t ink = ALL_COLORS;
    if (options[option] == 1)
    {
      // xchg [textPaperPattern], bx
      ink = _state.Data().textPaperPattern;
      _state.Data().textPaperPattern = ALL_COLORS;
    }
    (void)DrawViewChar(_state, options[option + 1], ink, position);
    option += 2;
    position = Plus(position, PAUSE_OPTION_STEP);
    if (--optionsLeft == 0)
    {
      return;
    }
    _hardware.LoopTurn(PAUSE_OPTIONS_LOOP, {optionsLeft, position, _state.OffsetOf(options[option])});
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
      Counted<std::uint8_t>& option = PauseOptions(_state)[std::size_t{index} * 2];
      option = static_cast<std::uint8_t>(option ^ 1);
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

// 0x8D6D-0x8D8C: the drawing buffer cleared and the menu's eight lines drawn into it, in every colour on a paper of 0: each line of
// pauseMenuText its place in spaceViewBuffer, a word, then its text. The loop at 0x8D7C pushes and pops CX round each line, and
// each turn carries it and SI, the next line's place and text.
void DrawPauseMenu(GameState& _state, Hardware& _hardware, bool _backward)
{
  ClearDrawBuffer(_state, _backward);
  _state.Data().textPaperPattern = 0;
  std::size_t line = 0;
  for (std::uint16_t lines = PAUSE_MENU_LINES;;)
  {
    // MOV DI,[SI] / ADD SI,2: the line's place, then its text; INC SI past its NUL.
    const std::uint16_t at = WordIn(_state.BytesFrom(_state.Data().pauseMenuText, line + 2), line);
    const std::string_view text = _state.TextAt(_state.Data().pauseMenuText, static_cast<std::uint16_t>(line + 2));
    (void)DrawViewString(_state, text, ALL_COLORS, at);
    line += 2 + text.size() + 1;
    if (--lines == 0)
    {
      return;
    }
    _hardware.LoopTurn(PAUSE_MENU_LOOP, {lines, static_cast<std::uint16_t>(_state.OffsetOf(_state.Data().pauseMenuText) + line)});
  }
}

// 0x8E52-0x8E88: F1-F10, the minimum frame time from frameTimeChoices, and its key in the menu's label, 1-9, or 10 with a closing
// bracket.
void SetFrameTime(GameState& _state, std::uint8_t _key)
{
  const auto choice = static_cast<std::uint8_t>(_key - SCAN_F1);
  _state.Data().minimumFrameMs = std::uint8_t{_state.BytesFrom(_state.Data().frameTimeChoices, std::size_t{choice} + 1)[choice]};
  const auto digit = static_cast<std::uint8_t>(choice + DIGIT_ONE);
  if (digit == DIGIT_NINE + 1)
  {
    _state.Data().frameRateKeyLabel.gapABFF[0] = DIGIT_ONE;
    _state.Data().frameRateKeyLabel.dataAC00 = DIGIT_ZERO;
    _state.Data().frameRateKeyLabel.dataAC01 = CLOSING_BRACKET;
  }
  else
  {
    _state.Data().frameRateKeyLabel.gapABFF[0] = digit;
    _state.Data().frameRateKeyLabel.dataAC00 = CLOSING_BRACKET;
    _state.Data().frameRateKeyLabel.dataAC01 = SPACE;
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
        _state.Data().gamePaused = 0;
        _state.Data().textPaperPattern = 0;
        _state.Data().keyDown.keyDownSpace = 0;
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
        _state.Data().gamePaused = 0;
        _state.Data().textPaperPattern = 0;
        _state.Data().titleShown = 0;
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
  if (_state.Data().flightScreenShown != 0)
  {
    change = RestoreFlightScreen(_state, _hardware, _backward);
    ResetKeyboard(_state, _hardware);
  }
  _state.Data().inFlight = 1;
  return change;
}

// 0x0C13 and 0x0C72: flightScreenShown set, then push ax; call EraseCompassAndBlips; pop ax: a screen is about to be shown.
// Returns whether EraseCompassAndBlips erased anything.
bool EraseForFlightScreen(GameState& _state)
{
  _state.Data().flightScreenShown = 1;
  return EraseCompassAndBlips(_state);
}

// FlightScreenDispatch (0x0BF8): the screen for _key, then the screen for the key that screen returns, until F1-F4 or the
// navigation computer's error leaves (0x0BDE). _countIfNone, _segment and _backward are BP, ES and the direction flag as the
// dispatch finds them, which the screens and their waits read and leave. Every jump back in it is a turn of a loop that can wait:
// those to the tests at 0x0BF8 carry the key, the AL the last screen left, and BP, which the next screen reads, and the others,
// into shared code or to the wait, carry nothing.
FlightScreens DispatchFlightScreens(GameState& _state, Hardware& _hardware, std::uint8_t _key, std::uint16_t _countIfNone,
                                    std::uint16_t _segment, bool _backward)
{
  FlightScreens screens{0, std::nullopt, _backward};
  std::uint8_t key = _key;
  std::uint16_t countLeft = _countIfNone;
  std::uint16_t segment = _segment;
  // 0x0C13, 0x0C72 and 0x0CA0: flightScreenShown set and the compass and the blips erased (EraseForFlightScreen). Of what
  // EraseCompassAndBlips leaves, the screens read BP, the compass dot's in-front byte CBW'd once it erases the dot, and the charts
  // ES, the video segment once it erases anything.
  const auto erase = [&_state, &countLeft, &segment]
  {
    const SpaceObject& station = _state.Slot(SlotIndex::Station);
    if ((station.flags & FLAG_BLIP_DRAWN) != 0)
    {
      countLeft = SignExtend(station.blipZ);
    }
    if (EraseForFlightScreen(_state))
    {
      segment = GameState::VIDEO_SEGMENT;
    }
  };
  // A screen shown, and the key that closed it, with AL as it leaves it: every screen leaves ES on B800h, the charts clear the
  // direction flag (PresentChartFrame), and the screens that select the system at the cursor as they close leave BP its count.
  // Then JMP FlightScreenDispatch.
  const auto shown = [&_hardware, &screens, &key, &countLeft, &segment](const ScreenKey& _closed, bool _chart)
  {
    key = _closed.scanCode;
    countLeft = _closed.countLeft.value_or(countLeft);
    segment = GameState::VIDEO_SEGMENT;
    screens.backward = screens.backward && !_chart;
    _hardware.LoopTurn(FLIGHT_SCREEN_DISPATCH, {Join(key, _closed.al), countLeft});
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
    if (_state.Data().commanderBlock.witchspaceCountdown == 0)
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
        shown(ShowSystemDataScreen(_state, _hardware, screens.backward, countLeft), false);
        continue;
      }
      if (key == SCAN_F8)
      {
        shown(ShowMarketPricesScreen(_state, _hardware, screens.backward, countLeft), false);
        continue;
      }
    }
    else
    {
      // In witch space the short-range chart still shows once the countdown is down to 1, and F9 and F10 show; the charts and the
      // market need the navigation computer, which witch space jams.
      if (key == SCAN_F6)
      {
        if (_state.Data().commanderBlock.witchspaceCountdown == 1)
        {
          erase();
          shown(ShowShortRangeChart(_state, _hardware, screens.backward, segment), true);
          continue;
        }
        _hardware.LoopTurn(NAVIGATION_KEYS, {});
      }
      if (key < SCAN_F9)
      {
        if (_state.Data().flightScreenShown != 1)
        {
          StartBeep(_state);
        }
        // MOV AX,navCompErrorMessage, posted, then XOR AH,AH: no view.
        SetMessage(_state, Message::NavCompError, 0x19);
        return leave();
      }
      erase();
      _hardware.LoopTurn(STATUS_OR_INVENTORY_KEY, {});
    }
    // 0x0C45: F9 and F10; any other key the screens return is waited past.
    if (key == SCAN_F9)
    {
      ResetKeyboard(_state, _hardware);
      _state.Data().inFlight = 1;
      const ScreenKey closed = ShowCommanderStatusScreen(_state, _hardware, screens.backward, countLeft);
      _state.Data().inFlight = 0;
      shown(closed, false);
      continue;
    }
    if (key == SCAN_F10)
    {
      shown(ShowInventoryScreen(_state, _hardware, screens.backward, countLeft), false);
      continue;
    }
    // JMP 0BF3h, and GetKey until a key comes: a screenshot taken there leaves ES on the video segment (SaveScreenshot).
    _hardware.LoopTurn(FLIGHT_SCREEN_KEY_WAIT, {});
    const KeyPress next = WaitForKey(_state, _hardware, FLIGHT_SCREEN_KEY_WAIT);
    key = next.scanCode;
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
  if (_state.Data().viewAngle == view)
  {
    return std::nullopt;
  }
  _state.Data().viewAngle = view;
  StartBeep(_state);
  return ResetStardust(_state);
}

// mov ax, text; jmp back to 0x80A3, the tail that posts _message for 25 frames: a backward jump into shared code, not a loop,
// so its turn carries nothing.
void PostHyperspaceRefusal(GameState& _state, Hardware& _hardware, Message _message)
{
  _hardware.LoopTurn(HYPERSPACE_REFUSAL_TAIL, {});
  SetMessage(_state, _message, 0x19);
}

// 0x8068-0x80FF: H, unless the docking computer is on or a countdown runs. True when the countdown started, which ends
// ProcessFlightKeys. _backward is the direction flag ResetHyperspaceRings' copy runs by.
[[nodiscard]] bool PressHyperspace(GameState& _state, Hardware& _hardware, bool _backward)
{
  if (_state.Data().dockingComputerOn == 1 || _state.Data().hyperspaceCountdown != 0)
  {
    // JE or JNE back to the JMP past the key's handling: a backward jump into shared code, not a loop, so its turn carries
    // nothing.
    _hardware.LoopTurn(PAST_HYPERSPACE_KEY, {});
    return false;
  }
  if (((_state.Data().commanderBlock.invadedStationDestroyed ^ 1) & _state.Data().commanderBlock.thargoidInvasionActive &
       _state.Data().commanderBlock.jumpedSinceBriefing) != 0)
  {
    SetMessage(_state, Message::HyperspaceJammed, 0x19);
    return false;
  }
  if (_state.Data().galacticDriveReadyFrames != 0)
  {
    _state.Data().galacticJumpPending = 1;
  }
  else
  {
    const std::uint16_t distance = _state.Data().commanderBlock.selectedDistanceTenthsLy;
    if (distance == 0)
    {
      SetMessage(_state, Message::NoSystemSelected, 0x19);
      return false;
    }
    if (distance >= OUT_OF_RANGE_TENTHS)
    {
      PostHyperspaceRefusal(_state, _hardware, Message::OutOfRange);
      return false;
    }
    // MOV BL,AL / MOV BH,0Ah / MOV AL,fuel / MUL BH / MOV BH,24h / DIV BH: the fuel times 10, over 24h, against the distance's
    // low byte; then MOV AX,distance / MUL BH / MOV BH,0Ah / DIV BH: the cost, its low byte times 24h over 10, at least 1. BX
    // holds BH and the distance's low byte at each divide.
    const std::uint8_t tenths = Low(distance);
    const ByteQuotient reach = DivideByte(static_cast<std::uint16_t>(_state.Data().commanderBlock.fuel * FUEL_FACTOR), FUEL_DIVISOR);
    if (reach.quotient < tenths)
    {
      PostHyperspaceRefusal(_state, _hardware, Message::NotEnoughFuel);
      return false;
    }
    const auto product = static_cast<std::uint16_t>(Low(_state.Data().commanderBlock.selectedDistanceTenthsLy) * FUEL_DIVISOR);
    const ByteQuotient cost = DivideByte(product, FUEL_FACTOR);
    _state.Data().hyperspaceFuelCost = cost.quotient == 0 ? std::uint8_t{1} : cost.quotient;
  }
  _state.Data().hyperspaceCountdown = 0x0A;
  _state.Data().hyperspaceCountdownFrames = 0x0A;
  ShowHyperspaceCountdown(_state);
  ResetHyperspaceRings(_state, _backward);
  _state.Data().missileState = 0;
  _state.Data().shipIdRequested = 0;
  LatchHyperspaceTarget(_state);
  return true;
}

// 0x8171-0x826C: T primes a missile, which locks on what is in the sights; U unarms it; M launches it. _di is the DI
// ProcessFlightKeys holds here, which M's launch copies the missile from (LaunchPlayerMissile), unless the primed missile looked
// in the sights this frame and FindShipInCrosshairs left DI; _backward is the direction flag the copy runs by.
void HandleMissileKeys(GameState& _state, std::uint16_t _di, bool _backward)
{
  std::uint16_t source = _di;
  if (_state.Data().keyDown.keyDownT == 1 && _state.Data().commanderBlock.missileCount != 0 && _state.Data().missileState == 0 &&
      _state.Data().hyperspaceCountdown == 0 && _state.Data().shipIdRequested != 1)
  {
    _state.Data().missileState = 1;
    _state.Data().shipIdRequested = 0;
    StartBeep(_state);
    SetMessage(_state, Message::MissilePrimed, 0x0F);
  }
  if (_state.Data().missileState == 1)
  {
    const CrosshairTarget target = FindShipInCrosshairs(_state);
    source = target.di;
    if (target.slot && (_state.Slot(*target.slot).flags & FLAG_MASKED) == 0)
    {
      _state.Data().missileTarget = *target.slot;
      _state.Data().missileState = 2;
      StartBeep(_state);
      SetMessage(_state, Message::MissileLocked, 0x14);
    }
  }
  if (_state.Data().missileState == 2)
  {
    if ((_state.Slot(_state.Data().missileTarget).type & SpaceObject::ACTIVE) == 0)
    {
      _state.Data().missileState = 0;
      StartLowBeep(_state);
      SetMessage(_state, Message::MissileTargetDestroyed, 0x14);
    }
  }
  if (_state.Data().keyDown.keyDownU == 1 && _state.Data().missileState != 0)
  {
    _state.Data().missileState = 0;
    _state.Data().shipIdRequested = 0;
    StartLowBeep(_state);
    SetMessage(_state, Message::MissileUnarmed, 0x0F);
  }
  if (_state.Data().keyDown.keyDownM == 1 && _state.Data().missileState == 2 && _state.Data().missileJammed != 1)
  {
    if (NextRandom(_state) < MISSILE_JAM_ODDS)
    {
      _state.Data().missileJammed = 1;
      SetMessage(_state, Message::MissileJammed, 0x19);
      return;
    }
    _state.Data().missileState = 0;
    _state.Data().shipIdRequested = 0;
    SetMessage(_state, Message::MissileLaunched, 0x14);
    _state.Data().commanderBlock.missileCount = static_cast<std::uint8_t>(_state.Data().commanderBlock.missileCount - 1);
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
  if (!laser || _state.Data().laserTemperature >= LASER_TOO_HOT)
  {
    return;
  }
  // ADD [laserTemperature],5, and FFh written over it on a carry, which a temperature below F0h never makes.
  const std::uint8_t temperature = _state.Data().laserTemperature;
  _state.Data().laserTemperature = static_cast<std::uint8_t>(temperature + LASER_HEAT_PER_SHOT);
  if (temperature + LASER_HEAT_PER_SHOT > 0xFF)
  {
    _state.Data().laserTemperature = 0xFF;
  }
  if (*laser == PULSE_LASER)
  {
    _state.Data().laserAlternateFrame = static_cast<std::uint8_t>(_state.Data().laserAlternateFrame ^ 1);
    if (_state.Data().laserAlternateFrame != 0)
    {
      return;
    }
  }
  _state.Data().firingLaserType = *laser;
  _state.Data().laserFiring = 1;
}

// 0x82F7-0x8363: I identifies the ship in the sights, and locks a missile on it if there is one.
void HandleIdentifyKey(GameState& _state)
{
  if (_state.Data().shipIdRequested != 1)
  {
    if (_state.Data().keyDown.keyDownI != 1 || _state.Data().missileState != 0 || _state.Data().hyperspaceCountdown != 0)
    {
      return;
    }
    StartBeep(_state);
    if (_state.Data().commanderBlock.missileCount != 0)
    {
      _state.Data().missileState = 1;
    }
    _state.Data().shipIdRequested = 1;
    SetMessage(_state, Message::IdIndicator, 0x19);
  }
  const CrosshairTarget target = FindShipInCrosshairs(_state);
  if (!target.slot)
  {
    return;
  }
  const SpaceObject& ship = _state.Slot(*target.slot);
  if (IsStation(ship).station)
  {
    return;
  }
  _state.Data().missileState = 0;
  _state.Data().shipIdRequested = 0;
  // PUSH DI / POP DI keep the ship round ShowShipIdentity and StartBeep.
  ShowShipIdentity(_state, static_cast<std::uint8_t>((ship.type >> 1) & TYPE_MASK), ship.behaviorClass, ship);
  StartBeep(_state);
  if (_state.Data().commanderBlock.missileCount != 0)
  {
    _state.Data().missileTarget = _state.IndexOf(ship);
    _state.Data().missileState = 2;
  }
}

// 0x8379-0x839E: L runs the anti-ECM emulator, at one unit of energy a frame: SUB WORD [playerEnergy],1, and 0
// written over it on a borrow.
void HandleAntiEcmKey(GameState& _state)
{
  _state.Data().antiEcmActive = 0;
  if (_state.Data().keyDown.keyDownL != 1 || _state.Data().commanderBlock.antiEcmEmulatorFitted != 1)
  {
    return;
  }
  _state.Data().antiEcmActive = 1;
  const std::uint16_t energy = _state.Data().playerEnergy;
  _state.Data().playerEnergy = static_cast<std::uint16_t>(energy - 1);
  if (energy == 0)
  {
    _state.Data().playerEnergy = 0;
  }
}

// The axes ApplyPitch turns are 10000 long: forward is +z, and up is -y.
constexpr std::int16_t AXIS_LENGTH = 0x2710;

// 0x84F2-0x8586: _pitch, -23 to 23, turns the camera frame by twice as much, and the three angles are derived from the
// turned frame again.
void ApplyPitch(GameState& _state, std::int8_t _pitch)
{
  (void)SetSinCos(_state, 7, Negate(static_cast<std::uint16_t>(Word(_pitch) << 1)));
  (void)SetSinCos(_state, 2, Negate(_state.Data().playerRollAngle));
  (void)SetSinCos(_state, 1, Negate(_state.Data().playerYawAngle));
  (void)SetSinCos(_state, 0, Negate(_state.Data().playerPitchAngle));

  // The forward axis, turned: the pitch from its y and z, then the yaw from its x and the z turned by the pitch.
  const Vector forward = RotateBySinCos7210(_state, Vector{0, 0, AXIS_LENGTH});
  const std::uint16_t pitch = ArcTangent2(_state, forward.y, forward.z);
  _state.Data().pitchAngleScratch = Negate(pitch);
  (void)SetSinCos(_state, 6, pitch);
  const Pair pitched = RotateByStoredSinCos(_state, 6, Pair{forward.y, forward.z});
  _state.Data().yawAngleScratch = Negate(ArcTangent2(_state, forward.x, pitched.second));

  // The up axis, turned back by the pitch and the yaw: the roll from what is left.
  (void)SetSinCos(_state, 6, Negate(Plus(_state.Data().pitchAngleScratch, 0x400)));
  const Vector up = RotateBySinCos7210(_state, Vector{0, -AXIS_LENGTH, 0});
  const Pair upPitched = RotateByStoredSinCos(_state, 6, Pair{up.y, up.z});
  (void)SetSinCos(_state, 7, Plus(_state.Data().yawAngleScratch, 0x400));
  const Pair upYawed = RotateByStoredSinCos(_state, 7, Pair{up.x, upPitched.second});
  _state.Data().playerRollAngle = ArcTangent2(_state, upYawed.first, upPitched.first);
  _state.Data().playerYawAngle = Negate(_state.Data().yawAngleScratch);
  _state.Data().playerPitchAngle = Negate(_state.Data().pitchAngleScratch);
  _state.Data().velocityDirty = 1;
}

// 0x2A57-0x2B53: the sun in slot 0, the planet in slot 1 and the station in slot 2, each byte written as the original
// writes it.
void PlaceSunPlanetAndStation(GameState& _state)
{
  // The sun, at random far behind: the top 16 bits of x and y within 200h of 0, of z C00h to 801h behind.
  SpaceObject& sun = _state.Slot(SlotIndex::Sun);
  sun.type = SUN_TYPE;
  const auto randomTop = [&_state](std::uint16_t _less) { return static_cast<std::uint16_t>((NextRandom(_state) & 0x3FF) - _less); };
  std::uint16_t top = randomTop(0x200);
  sun.x = WithHigh(sun.x, Low(top));
  sun.xHigh = High(top);
  top = randomTop(0x200);
  sun.y = WithHigh(sun.y, Low(top));
  sun.yHigh = High(top);
  top = randomTop(0xC00);
  sun.zHigh = High(top);
  sun.z = WithHigh(sun.z, Low(top));
  // Colour 1 or 2 from bit 8 of the system's first seed, written as a word.
  const auto sunColor = static_cast<std::uint16_t>((High(_state.Data().systemSeed0) & 1) + 1);
  sun.pitch = WithHigh(sun.pitch, Low(sunColor));
  sun.yaw = WithLow(sun.yaw, High(sunColor));
  sun.behaviorClass = 0;
  sun.flags = SUN_OR_PLANET_FLAGS;
  sun.detail = 0xFF;

  // The planet, straight ahead at z = 6E00h, its colour from bit 0 of the seed.
  SpaceObject& planet = _state.ObjectAt(Plus(_state.OffsetOf(sun), SpaceObject::BYTES));
  planet.type = PLANET_TYPE;
  planet.x = 0;
  planet.xHigh = 0;
  planet.y = 0;
  planet.yHigh = 0;
  planet.z = WithHigh(planet.z, PLANET_Z_TOP);
  planet.zHigh = 0;
  planet.z = WithLow(planet.z, 0);
  const auto planetColor = static_cast<std::uint16_t>((Low(_state.Data().systemSeed0) & 1) + 1);
  planet.pitch = WithHigh(planet.pitch, Low(planetColor));
  planet.yaw = WithLow(planet.yaw, High(planetColor));
  planet.behaviorClass = 0;
  planet.flags = SUN_OR_PLANET_FLAGS;
  planet.detail = 0xFF;

  // The station, just behind at z = -300: a Dodo (type 0) at tech level 9 and up, else a Coriolis (type 1), with
  // 10-17 in its Thargons byte. While the Thargoids invade it has no missiles, Thargons or aggression, and is not
  // hostile.
  SpaceObject& station = _state.ObjectAt(Plus(_state.OffsetOf(planet), SpaceObject::BYTES));
  const std::uint8_t stationType = _state.Data().commanderBlock.currentTechLevel >= 9 ? 0 : 1;
  station.type = static_cast<std::uint8_t>((stationType << 1) | SpaceObject::ACTIVE);
  station.yaw = STATION_YAW;
  station.roll = 0;
  station.pitch = 0;
  station.x = 0;
  station.xHigh = 0;
  station.y = 0;
  station.yHigh = 0;
  station.z = STATION_Z;
  station.zHigh = Low(SignWord(STATION_Z));
  station.flags = STATION_FLAGS;
  station.behaviorClass = 1;
  station.thargons = static_cast<std::uint8_t>((High(NextRandom(_state)) & 7) + 0x0A);
  station.detail = 0xFF;
  station.energy = STATION_ENERGY;
  station.bounty = 0;
  if (_state.Data().commanderBlock.thargoidInvasionActive == 1)
  {
    station.missiles = 0;
    station.aggression = 0;
    station.energy = STATION_ENERGY;
    station.thargons = 0;
    station.fragments = 0x0A;
    station.flags = static_cast<std::uint8_t>(station.flags & 0xFE);
  }
  _state.Data().spawnGovernment = std::uint8_t{_state.Data().commanderBlock.currentGovernment};
}

// sub word [si+_low], ax; sbb byte [si+_high], dl; sub word [si+_compass], ax, with DL:AX = _velocityHigh:_velocity:
// a 24-bit coordinate of _slot and its compass word, less the velocity. Every slot has its compass words moved,
// though only the station's are the compass's.
void SubtractVelocity(SpaceObject& _slot, ObjectByte _high, ObjectWord _low, ObjectWord _compass, std::uint16_t _velocity,
                      std::uint8_t _velocityHigh)
{
  const std::uint16_t before = _slot.*_low;
  const int borrow = before < _velocity ? 1 : 0;
  _slot.*_low = static_cast<std::uint16_t>(before - _velocity);
  _slot.*_high = static_cast<std::uint8_t>(_slot.*_high - _velocityHigh - borrow);
  _slot.*_compass = static_cast<std::uint16_t>(_slot.*_compass - _velocity);
}

} // namespace

bool UpdateStardust(GameState& _state)
{
  if (_state.Data().jumpDriveEngaged != 0)
  {
    SaveStardustPositions(_state);
  }
  _state.Data().drawColor = std::uint8_t{_state.Data().stardustColor};
  switch (_state.Data().viewAngle)
  {
  case RIGHT_VIEW:
  case LEFT_VIEW:
    return UpdateSideStardust(_state, _state.Data().viewAngle == LEFT_VIEW);
  case REAR_VIEW:
    return UpdateFrontOrRearStardust(_state, true);
  default:
    return UpdateFrontOrRearStardust(_state, false);
  }
}

std::uint8_t ComputeStardustShift(GameState& _state)
{
  // MOV AX,34h / SUB AX,playerSpeed / MOV BL,0Ch / DIV BL: a speed above 34h divides into the game's trap.
  const auto dividend = static_cast<std::uint16_t>(STARDUST_SHIFT_SPEED - _state.Data().playerSpeed);
  auto shift = static_cast<std::uint8_t>(DivideByte(dividend, STARDUST_SHIFT_STEP).quotient + 4);
  if (_state.Data().jumpDriveEngaged != 0)
  {
    shift = static_cast<std::uint8_t>(shift - 1);
  }
  _state.Data().stardustShift = shift;
  return shift;
}

std::optional<DustScreenPosition> GetPreviousDustScreenPosition(const GameState& _state, std::size_t _particle)
{
  const Counted<std::uint8_t>(&copy)[PARTICLE_BYTES] = _state.Data().stardustPrevious[_particle];
  const DustPosition previous{Signed(WordIn(copy, PARTICLE_X)), Signed(WordIn(copy, PARTICLE_Y))};
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
  for (std::size_t particle = 0; particle < STARDUST_COUNT; ++particle)
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
  }
}

DustPosition RespawnDustAtSideEdge(GameState& _state, std::size_t _particle, std::int16_t _step, std::uint16_t _stripMask)
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
  for (std::size_t particle = 0; particle < STARDUST_COUNT; ++particle)
  {
    const DustPosition position = LoadDustPosition(_state, particle);
    const Pair rolled = RotateByStoredSinCos(_state, 7, Pair{position.x, position.y});
    StoreDustPosition(_state, particle, DustPosition{rolled.first, rolled.second});
  }
}

void ShiftStardustVertically(GameState& _state, std::int16_t _step)
{
  const std::uint16_t stripMask = ComputeDustStripMask(_step);
  for (std::size_t particle = 0; particle < STARDUST_COUNT; ++particle)
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
  }
}

DustPosition RespawnDustAtVerticalEdge(GameState& _state, std::size_t _particle, std::int16_t _step, std::uint16_t _stripMask)
{
  const std::uint16_t first = NextRandom(_state);
  MarkDustRespawned(_state, _particle, RandomLifetime(first));
  const std::uint16_t x = RandomDustX(first);
  const auto strip = static_cast<std::uint16_t>(NextRandom(_state) & _stripMask);
  // Moving down the particle enters at the top edge, moving up at the bottom.
  const std::uint16_t y = _step < 0 ? static_cast<std::uint16_t>(0x0F00 - strip) : Plus(0xF000, strip);
  return DustPosition{Signed(x), Signed(y)};
}

DustPosition RespawnDustAnywhere(GameState& _state, std::size_t _particle)
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
  const std::uint8_t shift = _state.Data().stardustShift;
  return DustStep{Signed(Sar(Word(_position.x), shift)), Signed(Sar(Word(_position.y), shift)), shift};
}

DustPosition LoadDustPosition(const GameState& _state, std::size_t _particle)
{
  const Counted<std::uint8_t>(&particle)[PARTICLE_BYTES] = _state.Data().stardust[_particle];
  return DustPosition{Signed(WordIn(particle, PARTICLE_X)), Signed(WordIn(particle, PARTICLE_Y))};
}

void StoreDustPosition(GameState& _state, std::size_t _particle, DustPosition _position)
{
  SetWordIn(_state.Data().stardust[_particle], PARTICLE_X, Word(_position.x));
  SetWordIn(_state.Data().stardust[_particle], PARTICLE_Y, Word(_position.y));
}

void StorePreviousDustPosition(GameState& _state, std::size_t _particle, DustPosition _position)
{
  SetWordIn(_state.Data().stardustPrevious[_particle], PARTICLE_X, Word(_position.x));
  SetWordIn(_state.Data().stardustPrevious[_particle], PARTICLE_Y, Word(_position.y));
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
  if (Low(_state.Data().messageFrames) == 0)
  {
    _state.Data().messageFrames = 0;
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
  for (Counted<std::uint8_t>(&particle)[PARTICLE_BYTES] : _state.Data().stardust)
  {
    SetWordIn(particle, PARTICLE_X, randomCoordinate());
    SetWordIn(particle, PARTICLE_Y, randomCoordinate());
    lifetimeRandom = NextRandom(_state);
    particle[PARTICLE_LIFETIME] = RandomLifetime(lifetimeRandom);
  }
  return lifetimeRandom;
}

void SaveStardustPositions(GameState& _state)
{
  // REP MOVSB with ES = DS, upwards: DF is clear wherever the reference calls it (its one STD, at 2E4A, is
  // cleared again at 2E5C, with no call between).
  for (std::size_t particle = 0; particle < STARDUST_COUNT; ++particle)
  {
    for (std::size_t index = 0; index < PARTICLE_BYTES; ++index)
    {
      _state.Data().stardustPrevious[particle][index] = std::uint8_t{_state.Data().stardust[particle][index]};
    }
  }
}

FlightScreens HandleFlightFunctionKeys(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, std::uint16_t _segment,
                                       bool _backward)
{
  _state.Data().inFlight = 0;
  // F1-F10 in keyDown, BX the scan code and LOOP counting CX down from 10: each turn back to 0x0BBE carries both.
  std::uint16_t scan = SCAN_F1;
  for (std::uint16_t keys = FUNCTION_KEYS;;)
  {
    if (_state.BytesFrom(_state.Data().keyDown, std::size_t{scan} + 1)[scan] == 1)
    {
      // MOV AH,BL, and PUSH AX / CALL ResetKeyboard / POP AX keep it.
      ResetKeyboard(_state, _hardware);
      _state.Data().flightScreenShown = 0;
      return DispatchFlightScreens(_state, _hardware, Low(scan), _countIfNone, _segment, _backward);
    }
    ++scan;
    if (--keys == 0)
    {
      break;
    }
    _hardware.LoopTurn(FUNCTION_KEY_LOOP, {keys, scan});
  }
  _state.Data().inFlight = 1;
  return FlightScreens{0, std::nullopt, _backward};
}

ScreenChange RestoreFlightScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  const ScreenChange change = ShowCockpitScreen(_state, _hardware, _backward);
  InvalidateDashboard(_state);
  StartBeep(_state);
  _state.Data().messageShown = Message::None;
  return change;
}

void InvalidateDashboard(GameState& _state)
{
  // REP STOSB with ES = DS, upwards: DF is clear wherever the reference calls it, as for SaveStardustPositions.
  for (Counted<std::uint8_t>& shown : _state.BytesFrom(_state.Data().missileCountShown, DASHBOARD_CACHE_BYTES))
  {
    shown = DASHBOARD_STALE;
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
  if (ExchangeShown(_state.Data().safeZoneGlyphShown, safeZoneGlyph))
  {
    (void)DrawScreenChar(_state, safeZoneGlyph, ALL_COLORS, GameState::VIDEO_SEGMENT, SAFE_ZONE_GLYPH_CELL);
  }
  std::uint8_t ecmGlyph = SPACE;
  if (_state.Data().ecmFired == 1)
  {
    _state.Data().ecmFired = 0;
    ecmGlyph = ECM_GLYPH;
  }
  if (ExchangeShown(_state.Data().ecmGlyphShown, ecmGlyph))
  {
    (void)DrawScreenChar(_state, ecmGlyph, ALL_COLORS, GameState::VIDEO_SEGMENT, ECM_GLYPH_CELL);
  }

  DrawEnergyBanks(_state);
  (void)DrawMissileLockIndicator(_state);
  (void)DrawMissileIcons(_state);

  // The pitch, then the roll, each indicator redrawn when it changed.
  const std::uint8_t pitch = High(_state.Data().rollRate);
  if (pitch != _state.Data().pitchRateShown)
  {
    _state.Data().pitchRateShown = pitch;
    DrawSignedIndicator(_state, PITCH_INDICATOR_LINE, static_cast<std::int8_t>(pitch));
  }
  const std::uint8_t roll = Low(_state.Data().rollRate);
  if (roll != _state.Data().rollRateShown)
  {
    _state.Data().rollRateShown = roll;
    DrawSignedIndicator(_state, ROLL_INDICATOR_LINE, static_cast<std::int8_t>(roll));
  }

  // From DrawEnergyBanks' last LOOP on, CX's high byte stays 0: every draw after it leaves CX = 0, or CL a count.
  RedrawThreeLineBar(_state, _state.Data().laserTemperature, _state.Data().laserTemperatureShown, 0x3B8C, true);
  RedrawThreeLineBar(_state, _state.Data().altitude, _state.Data().altitudeShown, 0x3CCC, true);
  RedrawThreeLineBar(_state, _state.Data().cabinTemperature, _state.Data().cabinTemperatureShown, 0x3A4C, true);
  RedrawThreeLineBar(_state, _state.Data().commanderBlock.fuel, _state.Data().fuelShown, 0x390C, true);
  // The original never stores the shields' shown values, so a shield bar is drawn every frame.
  RedrawThreeLineBar(_state, _state.Data().foreShield, _state.Data().foreShieldShown, 0x368C, false);
  RedrawThreeLineBar(_state, _state.Data().aftShield, _state.Data().aftShieldShown, 0x37CC, false);

  // The speed, unscaled: MOV AX,playerSpeed / CMP AL,speedShown / JNE, then JMP DrawFiveLineBarPixels.
  const std::uint8_t speed = Low(_state.Data().playerSpeed);
  if (speed == _state.Data().speedShown)
  {
    return;
  }
  _state.Data().speedShown = speed;
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
  const std::uint16_t marker = _state.Data().indicatorMarkers[pixel & 3];
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
  const std::uint8_t count = _state.Data().commanderBlock.missileCount;
  if (count == _state.Data().missileCountShown)
  {
    return false;
  }
  _state.Data().missileCountShown = count;
  // A word a cell: the icons for the count's low two bits, then the background to the fourth cell, a byte count that
  // wraps past four missiles.
  std::uint16_t cell = MISSILE_ICONS_LINE;
  for (std::uint16_t icon = 0; icon < (count & 3u); ++icon)
  {
    // missileIcon holds the icon's five lines in bank order: odd, even, odd, even, odd.
    constexpr std::array<std::size_t, 5> ICON_LINES = {0, 2, 4, 1, 3};
    for (std::size_t index = 0; index < FIVE_LINES.size(); ++index)
    {
      _state.SetVideoWord(Plus(cell, FIVE_LINES[index]), _state.Data().missileIcon[ICON_LINES[index]]);
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
  const std::uint8_t missileState = _state.Data().missileState;
  if (missileState == _state.Data().missileLockShown)
  {
    return false;
  }
  _state.Data().missileLockShown = missileState;
  const std::uint8_t fill = _state.Data().colorFillBytes[missileState & 3u];
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
  // banks below it are empty: the bank written is the last of those the count has left.
  const std::uint16_t energy = _state.Data().playerEnergy;
  std::uint8_t wholeBanks = High(energy);
  Counted<std::uint8_t>(&fills)[ENERGY_BANKS] = _state.Data().energyBankFill;
  std::uint16_t banks = ENERGY_BANKS;
  for (; banks != 0 && wholeBanks != 0; --banks, --wholeBanks)
  {
    fills[banks - 1] = FULL_BANK;
  }
  if (banks != 0)
  {
    fills[banks - 1] = Low(energy);
    for (--banks; banks != 0; --banks)
    {
      fills[banks - 1] = 0;
    }
  }

  // Each bank against its shown value, from the top bank's bar upwards, PUSH CX, DI and SI round each: the count left in CX, so
  // CH = 0 for the bar.
  Counted<std::uint8_t>(&shown)[ENERGY_BANKS] = _state.Data().energyBanksShown;
  std::uint16_t line = ENERGY_BANK_LINE;
  for (std::uint16_t count = ENERGY_BANKS; count != 0; --count)
  {
    const std::uint8_t fill = fills[count - 1];
    if (fill != shown[count - 1])
    {
      shown[count - 1] = fill;
      DrawFiveLineBar(_state, line, fill, 0);
    }
    line = static_cast<std::uint16_t>(line - ENERGY_BANK_STEP);
  }
}

void UpdateEnergyAndLaserHeat(GameState& _state)
{
  if (_state.Data().gameOverFrames == 0 && _state.Data().escapePodFrames == 0)
  {
    SubtractFloored(_state.Data().laserTemperature, LASER_COOLING);
    if (_state.Data().playerEnergy == MOST_ENERGY)
    {
      AddSaturating(_state.Data().aftShield, 1);
      AddSaturating(_state.Data().foreShield, 1);
    }
    else
    {
      // MOV AL,energyUnitFitted / SHL AL,1 / INC AL / CBW / ADD playerEnergy,AX, then CMP 400h / JB / MOV 3FFh: the sum written,
      // and 3FFh over it from 400h.
      const auto recharge = SignExtend(static_cast<std::uint8_t>((_state.Data().commanderBlock.energyUnitFitted << 1) + 1));
      const std::uint16_t energy = Plus(_state.Data().playerEnergy, recharge);
      _state.Data().playerEnergy = energy;
      if (energy >= MOST_ENERGY + 1)
      {
        _state.Data().playerEnergy = MOST_ENERGY;
      }
    }
  }
  if (_state.Data().playerEnergy >= ONE_BANK)
  {
    return;
  }
  // Below one bank, 50 in 65536 a frame, one of the 13 equipment bytes from missileCount is lost: XOR AH,AH / MOV BL,14h / DIV BL
  // on a second random word, which cannot overflow.
  if (NextRandom(_state) >= EQUIPMENT_LOSS_ODDS)
  {
    return;
  }
  const auto lost = static_cast<std::size_t>(Low(NextRandom(_state)) / EQUIPMENT_LOSS_DIVISOR);
  Counted<std::uint8_t>& equipment = _state.BytesFrom(_state.Data().commanderBlock.missileCount, lost + 1)[lost];
  if (equipment == 0)
  {
    return;
  }
  equipment = static_cast<std::uint8_t>(equipment - 1);
  SetMessage(_state, Message::EquipmentLoss, EQUIPMENT_LOSS_FRAMES);
}

void DrawConditionLight(GameState& _state)
{
  std::uint8_t color = _state.Data().conditionColor;
  if (color == 0)
  {
    // Flashing: black and red, on bit 9 of millisecondCounter.
    color = static_cast<std::uint8_t>(High(_state.Data().millisecondCounter) & 2);
  }
  if (color == _state.Data().conditionColorShown)
  {
    return;
  }
  _state.Data().conditionColorShown = color;
  const std::uint8_t fill = _state.Data().colorFillBytes[color];
  const std::uint16_t mask = Join(fill, fill);
  // The bitmap's words, byte-swapped and masked by the colour: a line in the odd bank, then the even line below.
  std::uint16_t line = CONDITION_LIGHT_LINE;
  for (std::size_t pair = 0; pair < CONDITION_LIGHT_LINE_PAIRS; ++pair)
  {
    _state.SetVideoWord(line, static_cast<std::uint16_t>(Swap(_state.Data().conditionLightBitmap[pair * 2]) & mask));
    line = Plus(line, EVEN_BANK);
    _state.SetVideoWord(line, static_cast<std::uint16_t>(Swap(_state.Data().conditionLightBitmap[pair * 2 + 1]) & mask));
    line = Plus(line, NEXT_ODD_LINE);
  }
}

std::uint8_t UpdateConditionColor(GameState& _state)
{
  const std::uint16_t energy = _state.Data().playerEnergy;
  const std::uint8_t cabin = _state.Data().cabinTemperature;
  const std::uint8_t altitude = _state.Data().altitude;
  const std::uint8_t fore = _state.Data().foreShield;
  const std::uint8_t aft = _state.Data().aftShield;
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
  _state.Data().conditionColor = color;
  return color;
}

ScreenChange SetUpLocalSpace(GameState& _state, Hardware& _hardware, bool _backward)
{
  LoadSystemSeeds(_state, _state.Data().commanderBlock.currentSystemIndex);
  InvalidateDashboard(_state);
  _state.Data().objectSlotCount = 0x14;
  _state.Data().debrisSlotCount = 0x10;
  _state.Data().shipSlotCount = 0x24;
  ClearAllObjects(_state, _backward);
  const ScreenChange change = ShowCockpitScreen(_state, _hardware, _backward);
  (void)ResetStardust(_state);
  _state.Data().hyperspaceCountdown = 0;
  _state.Data().dockingComputerOn = 0;
  _state.Data().dataA137 = 0;
  _state.Data().viewAngle = 0;
  _state.Data().viewLocked = 0;
  _state.Data().escapePodFrames = 0;
  _state.Data().playerDocked = 0;
  _state.Data().playerVelocityX = 0;
  _state.Data().playerVelocityY = 0;
  _state.Data().playerVelocityZ = 0x14;
  _state.Data().playerSpeed = 0x14;
  _state.Data().altitude = 0xFF;
  _state.Data().missileJammed = 0;
  _state.Data().playerPitchAngle = 0;
  _state.Data().playerYawAngle = 0;
  _state.Data().playerRollAngle = 0;
  if (_state.Data().commanderBlock.witchspaceCountdown != 0)
  {
    _state.Data().spawnGovernment = 0;
    return change;
  }
  PlaceSunPlanetAndStation(_state);
  return change;
}

std::uint16_t CheckCollisions(GameState& _state, Hardware& _hardware)
{
  // MOV CL,objectSlotCount / XOR CH,CH, PUSH CX and POP CX round each slot, then DEC CX / JE: a count of 0 runs 65,536 times.
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint32_t count = LoopCount(_state.Data().objectSlotCount); count != 0; --count)
  {
    SpaceObject& object = _state.Slot(slot);
    if ((object.type & SpaceObject::ACTIVE) != 0)
    {
      CheckCollision(_state, _hardware, object);
    }
    slot = SlotAfter(slot);
  }
  return GameState::SlotOffset(slot);
}

SafeZone InSafeZone(const GameState& _state)
{
  return SafeZone{(_state.Data().safeZoneFlags & 1) != 0};
}

void UpdateSafeZone(GameState& _state)
{
  SpaceObject& station = _state.Slot(SlotIndex::Station);
  if ((station.type & SpaceObject::ACTIVE) == 0 || !IsStation(station).station)
  {
    _state.Data().safeZoneFlags = 0;
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
  _state.Data().safeZoneFlags = static_cast<std::uint8_t>((held << 1) | (inside ? 1 : 0));
}

Vector ComputeDeathDebrisVector(GameState& _state)
{
  (void)SetSinCos(_state, 8, Negate(_state.Data().viewAngle));
  (void)SetSinCos(_state, 6, Negate(Plus(_state.Data().playerPitchAngle, 0x400)));
  (void)SetSinCos(_state, 7, Plus(_state.Data().playerYawAngle, 0x400));
  // (0, 40, 0), turned by the view and, off the front view, by the roll, which SetSinCos8 puts in the view's place.
  std::int16_t x = 0;
  std::int16_t y = DEATH_DEBRIS_SPEED;
  std::int16_t z = 0;
  if (_state.Data().viewAngle != 0)
  {
    const Pair viewed = RotateByStoredSinCos(_state, 8, Pair{x, y});
    (void)SetSinCos(_state, 8, Negate(_state.Data().playerRollAngle));
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
  if (_state.Data().gameOverFrames != 0)
  {
    return;
  }
  const std::uint8_t frames = _state.Data().warningFrames;
  if (frames != 0)
  {
    // XOR AH,AH / MOV [messageFrames],AX: the frames left, as a word.
    _state.Data().messageFrames = frames;
    _state.Data().messagePointer = Message{_state.Data().warningMessage};
    _state.Data().warningFrames = static_cast<std::uint8_t>(_state.Data().warningFrames - 1);
    return;
  }
  // The four checks from the one after the last warning (JMP [BX+warningChecks]).
  RunWarningChecks(_state, static_cast<std::uint16_t>((_state.Data().warningIndex + 1) & 3), WARNING_CHECKS);
}

void UpdateScannerBlip(GameState& _state, SpaceObject& _slot, Vector _camera)
{
  if (_state.IndexOf(_slot) >= SlotIndex::FirstDebris || IsStation(_slot).station || IsSunOrPlanet(_slot))
  {
    return;
  }
  _slot.scanned = 1;
  // The scanner's y and z are 1.25 times the camera's. The blip is the high bytes, each XCHGed with the old blip's.
  const auto scaled = [](std::int16_t _coordinate) { return High(Plus(Word(_coordinate), Signed(Sar(Word(_coordinate), 2)))); };
  const std::uint8_t oldX = _slot.blipX;
  _slot.blipX = High(Word(_camera.x));
  const std::uint8_t oldY = _slot.blipY;
  _slot.blipY = scaled(_camera.y);
  const std::uint8_t oldZ = _slot.blipZ;
  _slot.blipZ = scaled(_camera.z);
  if ((_slot.flags & FLAG_BLIP_DRAWN) != 0)
  {
    (void)XorScannerBlip(_state, oldX, oldY, oldZ);
  }
  (void)XorScannerBlip(_state, _slot.blipX, _slot.blipY, _slot.blipZ);
  _slot.flags = static_cast<std::uint8_t>(_slot.flags | FLAG_BLIP_DRAWN);
}

bool UpdateCompass(GameState& _state)
{
  if (_state.Data().titleShown == 0)
  {
    return false;
  }
  // The target: the station when the planet is near, else the planet. Its scale shift goes to its depth byte.
  SlotIndex target = SlotIndex::Planet;
  _state.Data().compassTargetIsStation = 0;
  if (IsObjectNearKeepBlip(_state.Slot(target)))
  {
    target = SlotIndex::Station;
    _state.Data().compassTargetIsStation = 1;
  }
  SpaceObject& aim = _state.Slot(target);
  const std::uint8_t shift = GetPositionScaleShift(aim).shift;
  aim.depth = shift;
  const Vector position = ScalePositionDown(aim, shift);
  // PUSH AX / PUSH BX / PUSH CX round the station's scale shift, read back through MOV BP,SP: the position turned to the view
  // for the station's compass words, then, popped, turned to the camera's frame for the dot.
  SpaceObject& station = _state.Slot(SlotIndex::Station);
  station.depth = GetPositionScaleShift(station).shift;
  const Vector view = TransformToView(_state, position);
  station.compassX = Word(view.x);
  station.compassY = Word(view.y);
  station.compassZ = Word(view.z);
  const Vector camera = RotatePitchYawRoll(_state, position);

  // BP gathers z >= 0 in bit 15 and the signs of x and y in bits 0 and 1, each coordinate NEGed to its magnitude.
  const bool inFront = !Negative(Word(camera.z));
  const std::uint16_t range = Plus(inFront ? Word(camera.z) : Negate(Word(camera.z)), COMPASS_DEPTH);
  const bool left = Negative(Word(camera.x));
  const bool below = Negative(Word(camera.y));
  const std::uint16_t xMagnitude = left ? Negate(Word(camera.x)) : Word(camera.x);
  const std::uint16_t yMagnitude = below ? Negate(Word(camera.y)) : Word(camera.y);
  const std::uint16_t xStep = ScaleCompassAxis(xMagnitude, range);
  const std::uint16_t yStep = ScaleCompassAxis(yMagnitude, range);
  auto dotX = static_cast<std::uint8_t>(xStep);
  auto dotY = static_cast<std::uint8_t>(yStep);
  // MUL AL twice: the dot's distance squared; from 41h, each step CBW'd or under the remainder AH still holds, times 8, divided
  // by its square root from sqrtTable (BX = the root), its low three bits kept: each step is at most 7, so the square is within
  // the table. The second divide can overflow, and the trap's 7Fh comes out as 7.
  const auto squared =
    static_cast<std::uint8_t>(Low(static_cast<std::uint16_t>(dotY * dotY)) + Low(static_cast<std::uint16_t>(dotX * dotX)));
  if (squared >= COMPASS_NORMALIZED_FROM)
  {
    const std::uint8_t root = _state.Data().sqrtTable[squared];
    const ByteQuotient first = DivideByte(static_cast<std::uint16_t>(SignExtend(dotX) << 3), root);
    dotX = static_cast<std::uint8_t>(first.quotient & COMPASS_STEP_MASK);
    const ByteQuotient second = DivideByte(static_cast<std::uint16_t>(Join(first.remainder, dotY) << 3), root);
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
  // The dot is blipX and blipY, exchanged as one word.
  const std::uint16_t oldDot = Join(station.blipY, station.blipX);
  station.blipX = dotX;
  station.blipY = dotY;
  const std::uint8_t wasInFront = station.blipZ;
  station.blipZ = inFront ? COMPASS_IN_FRONT : std::uint8_t{0};
  if ((station.flags & FLAG_BLIP_DRAWN) != 0)
  {
    XorCompassDot(_state, Low(oldDot), High(oldDot), wasInFront != 0);
  }
  const std::uint8_t nowInFront = station.blipZ;
  const std::uint16_t dot = Join(station.blipY, station.blipX);
  station.flags = static_cast<std::uint8_t>(station.flags | FLAG_BLIP_DRAWN);
  XorCompassDot(_state, Low(dot), High(dot), SignExtend(nowInFront) != 0);
  return true;
}

void XorCompassDot(GameState& _state, std::uint8_t _x, std::uint8_t _y, bool _inFront)
{
  if (_inFront)
  {
    (void)XorDashboardPixel(_state, _x, _y);
  }
  std::uint8_t x = _x;
  std::uint8_t y = _y;
  for (const DotStep step : COMPASS_RING)
  {
    x = static_cast<std::uint8_t>(x + step.x);
    y = static_cast<std::uint8_t>(y + step.y);
    (void)XorDashboardPixel(_state, x, y);
  }
}

std::uint16_t PixelPlace(DashboardPixel _pixel) noexcept
{
  return Join(_pixel.y, _pixel.x);
}

std::optional<DashboardPixel> EraseScannerBlip(GameState& _state, SpaceObject& _slot)
{
  if (IsStation(_slot).station || (_slot.flags & FLAG_BLIP_DRAWN) == 0)
  {
    return std::nullopt;
  }
  const DashboardPixel last = XorScannerBlip(_state, _slot.blipX, _slot.blipY, _slot.blipZ);
  _slot.flags = static_cast<std::uint8_t>(_slot.flags & ~FLAG_BLIP_DRAWN);
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
  const std::uint8_t mask = _state.Data().dashboardPixelMasks[_x & 3u];
  _state.SetVideoByte(offset, static_cast<std::uint8_t>(_state.VideoByte(offset) ^ mask));
  return mask;
}

bool EraseCompassAndBlips(GameState& _state)
{
  bool erased = false;
  SpaceObject& station = _state.Slot(SlotIndex::Station);
  if ((station.flags & FLAG_BLIP_DRAWN) != 0)
  {
    // The dot at BlipX and BlipY, solid while BlipZ is not 0 (CBW into BP), its flag cleared first.
    const bool inFront = station.blipZ != 0;
    const std::uint8_t x = station.blipX;
    const std::uint8_t y = station.blipY;
    station.flags = static_cast<std::uint8_t>(station.flags & ~FLAG_BLIP_DRAWN);
    XorCompassDot(_state, x, y, inFront);
    erased = true;
  }
  // MOV CL,objectSlotCount / XOR CH,CH / SUB CL,3, and LOOP: 65,536 slots for a count of 3.
  SlotIndex slot = SlotIndex::FirstShip;
  const auto slots = static_cast<std::uint8_t>(_state.Data().objectSlotCount - 3);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    erased = EraseScannerBlip(_state, _state.Slot(slot)).has_value() || erased;
    slot = SlotAfter(slot);
  }
  return erased;
}

void UpdateFuelLeak(GameState& _state, Hardware& _hardware)
{
  std::uint8_t border = 0;
  if (_state.Data().commanderBlock.fuelLeakDelayFrames != 0)
  {
    _state.Data().commanderBlock.fuelLeakDelayFrames = static_cast<std::uint8_t>(_state.Data().commanderBlock.fuelLeakDelayFrames - 1);
    if (_state.Data().commanderBlock.fuelLeakDelayFrames == 0)
    {
      _state.Data().commanderBlock.fuelLeakFrames = FUEL_LEAK_FRAMES;
      return;
    }
    border = _state.Data().maskingBackgroundColor;
  }
  else if (_state.Data().commanderBlock.fuelLeakFrames != 0)
  {
    _state.Data().commanderBlock.fuelLeakFrames = static_cast<std::uint8_t>(_state.Data().commanderBlock.fuelLeakFrames - 1);
    // SUB [fuel],5, and 0 written over it on a borrow.
    const std::uint8_t fuel = _state.Data().commanderBlock.fuel;
    _state.Data().commanderBlock.fuel = static_cast<std::uint8_t>(fuel - FUEL_LEAK_STEP);
    if (fuel < FUEL_LEAK_STEP)
    {
      _state.Data().commanderBlock.fuel = 0;
    }
    _state.Data().messageFrames = SignExtend(_state.Data().commanderBlock.fuelLeakFrames);
    _state.Data().messagePointer = Message::FuelLeak;
    border = FUEL_LEAK_BORDER;
  }
  else
  {
    border = _state.Data().maskingBackgroundColor;
  }
  _hardware.SetColorSelect(static_cast<std::uint8_t>(border | BRIGHT_PALETTE));
}

FlightExit RunFlight(GameState& _state, Hardware& _hardware, bool _backward)
{
  ResetMouseIfSelected(_state, _hardware);
  // ShowCockpitScreen's CLD, once it drew.
  const ScreenChange cockpit = SetUpLocalSpace(_state, _hardware, _backward);
  bool backward = _backward && cockpit == ScreenChange::None;
  PlayStationTunnel(_state, _hardware, backward);
  // The tunnel's last FinishSpaceViewFrame: its CLD, and PresentSpaceView's MOV BP,20h.
  backward = false;
  std::uint16_t countLeft = PRESENT_SPACE_VIEW_BP;
  for (;;)
  {
    // FlightFrameLoop: one frame. The direction flag goes as each routine leaves it: TransformAndDrawObjects', and the CLD of a
    // DrawLine that fills a horizontal line's bytes, a beam's or a streak's.
    UpdateDashboard(_state);
    backward = TransformAndDrawObjects(_state, _hardware, backward);
    if (ApplyEnemyLaserHit(_state, _hardware))
    {
      backward = false;
    }
    if (ResolveLaserFire(_state, _hardware, backward))
    {
      backward = false;
    }
    if (UpdateStardust(_state))
    {
      backward = false;
    }
    UpdateFuelLeak(_state, _hardware);
    (void)UpdateMessageLine(_state, backward);
    (void)PollScreenDumpKey(_state, _hardware);
    FinishSpaceViewFrame(_state, _hardware);
    // Its CLD, and PresentSpaceView's MOV BP,20h and MOV DX,1FF0h, which UpdateObjectsAndSpawn hands to its first handler.
    backward = false;
    countLeft = PRESENT_SPACE_VIEW_BP;
    UpdateObjectsAndSpawn(_state, _hardware, backward, PRESENT_SPACE_VIEW_DX);
    UpdatePlayerMotion(_state, _hardware);
    // DI past the slots, from which M's launch copies the missile unless a routine before it moves DI (ProcessFlightKeys).
    const std::uint16_t slotsEnd = CheckCollisions(_state, _hardware);
    if (_state.Data().playerDocked == 1)
    {
      PlayStationTunnel(_state, _hardware, backward);
      ResetKeyboard(_state, _hardware);
      StopSoundEffects(_state);
      StopAllSound(_state, _hardware);
      // The tunnel's last frame's CLD and BP of 20h, which the docked status screen goes on with.
      return FlightExit{FlightEnd::Docked, PRESENT_SPACE_VIEW_BP, false};
    }
    const FlightKeysExit keys = ProcessFlightKeys(_state, _hardware, slotsEnd, countLeft, _state.DataSegment(), backward);
    backward = keys.backward;
    if (keys.aborted)
    {
      return FlightExit{FlightEnd::Aborted, CLEARED_MESSAGE_LINE_BP, backward};
    }
    // CompleteHyperspaceJump's tunnel ends with a CLD.
    if (TickHyperspaceCountdown(_state, _hardware, countLeft, backward))
    {
      backward = false;
    }
    if (TickEscapePod(_state))
    {
      return FlightExit{FlightEnd::EscapePod, countLeft, backward};
    }
    if (_state.Data().playerDead != 1 || _state.Data().cheatEnabled == 1)
    {
      _hardware.LoopTurn(FLIGHT_FRAME_LOOP, {});
      continue;
    }
    // Dead: GAME OVER for 40 frames, then the title's ELITE on the message line, and back.
    _state.Data().hyperspaceCountdown = 0;
    if (_state.Data().gameOverFrames == 0)
    {
      SpawnPlayerWreckage(_state);
      _state.Data().gameOverFrames = GAME_OVER_FRAMES;
      SetMessage(_state, Message::GameOver, GAME_OVER_FRAMES);
      StopContinuousNoise(_state);
      _hardware.LoopTurn(FLIGHT_FRAME_LOOP, {});
      continue;
    }
    _state.Data().gameOverFrames = static_cast<std::uint8_t>(_state.Data().gameOverFrames - 1);
    if (_state.Data().gameOverFrames != 0)
    {
      _hardware.LoopTurn(FLIGHT_FRAME_LOOP, {});
      continue;
    }
    ResetKeyboard(_state, _hardware);
    // MOV AX,0B800h / MOV ES,AX for the message line and the title.
    ClearMessageLine(_state, GameState::VIDEO_SEGMENT, backward);
    _state.Data().textPaperPattern = 0;
    (void)DrawScreenString(_state, _state.TextAt(_state.Data().eliteTitleText), ALL_COLORS, GameState::VIDEO_SEGMENT, TITLE_TEXT_POSITION);
    return FlightExit{FlightEnd::GameOver, CLEARED_MESSAGE_LINE_BP, backward};
  }
}

bool TickEscapePod(GameState& _state)
{
  if (_state.Data().escapePodFrames == 0)
  {
    return false;
  }
  _state.Data().escapePodFrames = static_cast<std::uint8_t>(_state.Data().escapePodFrames - 1);
  return _state.Data().escapePodFrames == 0;
}

FlightKeysExit ProcessFlightKeys(GameState& _state, Hardware& _hardware, std::uint16_t _di, std::uint16_t _countIfNone,
                                 std::uint16_t _segment, bool _backward)
{
  if (_state.Data().gameOverFrames != 0)
  {
    return FlightKeysExit{false, _backward};
  }
  if (_state.Data().escapePodFrames != 0)
  {
    // JNE back to the RET above: a backward jump into shared code, not a loop, so its turn carries nothing.
    _hardware.LoopTurn(PROCESS_FLIGHT_KEYS_RETURN, {});
    return FlightKeysExit{false, _backward};
  }
  const FlightScreens screens = HandleFlightFunctionKeys(_state, _hardware, _countIfNone, _segment, _backward);
  const bool backward = screens.backward;
  // DI, from which M's launch copies the missile, as the routines before it leave it: InvalidateDashboard once the cockpit came
  // back, ResetStardust once the view changed, IsMassLocked once J asked it, and ClearDrawBuffer once the pause screen resumed.
  std::uint16_t di = screens.restored ? DashboardCacheEnd(_state) : _di;
  if (screens.view != 0 && _state.Data().viewLocked != 1 && ChangeView(_state, screens.view))
  {
    di = StardustEnd(_state);
  }
  if (_state.Data().keyDown.keyDownG == 1 && _state.Data().commanderBlock.galacticHyperdriveFitted == 1 &&
      _state.Data().galacticDriveReadyFrames == 0 && _state.Data().hyperspaceCountdown == 0)
  {
    StartBeep(_state);
    SetMessage(_state, Message::GalacticDriveReady, 0x28);
    _state.Data().galacticDriveReadyFrames = 0x28;
  }
  if (_state.Data().keyDown.keyDownH == 1 && PressHyperspace(_state, _hardware, backward))
  {
    return FlightKeysExit{false, backward};
  }
  // D, released, toggles the docking computer: not in mission 3 until the invaded station is destroyed.
  if ((_state.Data().commanderBlock.missionNumber != 3 || _state.Data().commanderBlock.invadedStationDestroyed == 1) &&
      _state.Data().dockingKeyReleased == 1 && _state.Data().commanderBlock.dockingComputerFitted == 1)
  {
    if (_state.Data().hyperspaceCountdown != 0)
    {
      _state.Data().dockingKeyReleased = 0;
    }
    else
    {
      _state.Data().jumpDriveEngaged = 0;
      (void)ToggleDockingComputer(_state, _hardware);
    }
  }
  if (_state.Data().keyDown.keyDownJ == 1)
  {
    if (const JumpDriveRequest request = EngageJumpDrive(_state); request.lock)
    {
      di = DiAfterMassLock(*request.lock, di);
    }
  }
  if (_state.Data().keyDown.keyDownEsc == 1)
  {
    _state.Data().gamePaused = 1;
    if (_state.Data().keyDown.keyDownCtrl == 1)
    {
      // Ctrl+Esc freezes the game until a key, without the menu.
      ResetKeyboard(_state, _hardware);
      (void)WaitForKey(_state, _hardware, FREEZE_KEY_WAIT);
    }
    else
    {
      SetMessage(_state, Message::GamePaused, 1);
      (void)UpdateMessageLine(_state, backward);
      if (RunPauseScreen(_state, _hardware, backward))
      {
        return FlightKeysExit{true, backward};
      }
      di = DrawBufferEnd(backward);
    }
    _state.Data().gamePaused = 0;
  }
  HandleMissileKeys(_state, di, backward);
  HandleFireButton(_state, _hardware);
  if (_state.Data().keyDown.keyDownE == 1 && _state.Data().commanderBlock.ecmFitted == 1)
  {
    _state.Data().ecmFired = 1;
    SetMessage(_state, Message::EcmActive, 5);
    (void)RemoveAllMissiles(_state);
    DrainEnergy(_state, ECM_ENERGY);
  }
  if (_state.Data().keyDown.keyDownB == 1 && _state.Data().commanderBlock.energyBombFitted == 1)
  {
    _state.Data().commanderBlock.energyBombFitted = 0;
    (void)DetonateEnergyBomb(_state, _hardware, backward);
  }
  if (_state.Data().keyDown.keyDownC == 1 && _state.Data().commanderBlock.escapePodFitted == 1 && _state.Data().escapePodFrames == 0)
  {
    StartBeep(_state);
    LaunchEscapePod(_state);
  }
  HandleIdentifyKey(_state);
  if (_state.Data().keyDown.keyDownN == 1 && _state.Data().commanderBlock.maskingDeviceFitted == 1)
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
  const std::uint16_t energy = _state.Data().playerEnergy;
  _state.Data().playerEnergy = static_cast<std::uint16_t>(energy - amount);
  if (energy < amount)
  {
    _state.Data().playerEnergy = 0;
    _state.Data().playerDead = 1;
  }
}

JumpDriveRequest EngageJumpDrive(GameState& _state)
{
  // MOV AX,message, then the shared tail at 8450 posts it for 5 frames.
  const auto post = [&_state](Message _message) { SetMessage(_state, _message, JUMP_DRIVE_MESSAGE_FRAMES); };
  std::optional<MassLock> lock;
  if (_state.Data().dockingComputerOn != 1)
  {
    if (_state.Data().playerSpeed != FULL_SPEED)
    {
      _state.Data().jumpDriveEngaged = 0;
      post(Message::JumpDriveVelocityLocked);
      return JumpDriveRequest{lock};
    }
    lock = IsMassLocked(_state);
    if (!lock->locked)
    {
      _state.Data().jumpDriveEngaged = 1;
      _state.Data().velocityDirty = 1;
      post(Message::JumpDriveEngaged);
      return JumpDriveRequest{lock};
    }
  }
  _state.Data().jumpDriveEngaged = 0;
  post(Message::JumpDriveMassLocked);
  return JumpDriveRequest{lock};
}

void UpdatePlayerMotion(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t gameOver = _state.Data().gameOverFrames;
  if (gameOver != 0 && gameOver != GAME_OVER_FRAMES)
  {
    return;
  }
  if (_state.Data().escapePodFrames != 0)
  {
    MoveObjectsByVelocity(_state);
    return;
  }
  Steering steering{};
  if (_state.Data().dockingComputerOn != 0)
  {
    RunDockingComputer(_state);
    const std::uint16_t steered = _state.Data().dockingComputerSteering;
    steering = Steering{Low(steered), High(steered)};
  }
  else
  {
    // . and , change the speed's low byte by 4, within 4-48 unless it wraps, and the word is written back.
    std::uint16_t speed = _state.Data().playerSpeed;
    if (_state.Data().keyDown.keyDownPeriod == 1)
    {
      speed = WithLow(speed, static_cast<std::uint8_t>(Low(speed) + SPEED_KEY_STEP));
      _state.Data().velocityDirty = 1;
      if (Low(speed) >= TOP_SPEED + 1)
      {
        speed = WithLow(speed, TOP_SPEED);
      }
    }
    if (_state.Data().keyDown.keyDownComma == 1)
    {
      speed = WithLow(speed, static_cast<std::uint8_t>(Low(speed) - SPEED_KEY_STEP));
      _state.Data().velocityDirty = 1;
      if (Low(speed) < SPEED_KEY_STEP)
      {
        speed = WithLow(speed, SPEED_KEY_STEP);
      }
    }
    _state.Data().playerSpeed = speed;
    // ReadSteering fires the stick with AL, the speed's low byte.
    const Steering read = ReadSteering(_state, _hardware, Low(speed));
    _state.Data().rollRate = Join(read.pitch, read.roll);
    steering = ApplyReverseControls(_state, read);
  }

  // Roll, -23 to 23, negated, turns the roll angle by twice as much: NEG AL / JE, then the limits, CBW / SHL AX,1.
  if (const auto roll = static_cast<std::int8_t>(Negate(steering.roll)); roll != 0)
  {
    const std::int8_t held = roll <= -MOST_ROLL - 1 ? static_cast<std::int8_t>(-MOST_ROLL) : roll >= MOST_ROLL + 1 ? MOST_ROLL : roll;
    _state.Data().playerRollAngle = Plus(_state.Data().playerRollAngle, SignExtend(static_cast<std::uint8_t>(held)) << 1);
  }
  if (steering.pitch != 0)
  {
    ApplyPitch(_state, static_cast<std::int8_t>(steering.pitch));
  }
  if (_state.Data().dockingComputerOn == 1)
  {
    return;
  }
  UpdatePlayerVelocity(_state);
  MoveObjectsByVelocity(_state);
}

void UpdatePlayerVelocity(GameState& _state)
{
  if (_state.Data().velocityDirty != 1)
  {
    return;
  }
  // DEC: the 1 becomes 0.
  _state.Data().velocityDirty = 0;
  (void)SetSinCos(_state, 6, Negate(Plus(_state.Data().playerPitchAngle, 0x400)));
  (void)SetSinCos(_state, 7, Plus(_state.Data().playerYawAngle, 0x400));
  std::uint16_t speed = _state.Data().playerSpeed;
  if (_state.Data().jumpDriveEngaged == 1)
  {
    // The jump drive: 32 times the speed, for this frame only.
    _state.Data().velocityDirty = 1;
    _state.Data().jumpDriveEngaged = 0;
    speed = static_cast<std::uint16_t>(speed << 5);
  }
  // (0, speed) turned by the yaw, then (0, what that leaves of the speed) by the pitch.
  const Pair yawed = RotateByStoredSinCos(_state, 7, Pair{0, Signed(speed)});
  _state.Data().playerVelocityX = Word(yawed.first);
  const Pair pitched = RotateByStoredSinCos(_state, 6, Pair{0, yawed.second});
  _state.Data().playerVelocityY = Word(pitched.first);
  _state.Data().playerVelocityZ = Word(pitched.second);
}

void MoveObjectsByVelocity(GameState& _state)
{
  // LOOP from CX = shipSlotCount: a count of 0 runs 65,536 times.
  SlotIndex index = SlotIndex::Sun;
  for (std::uint32_t slots = LoopCount(_state.Data().shipSlotCount); slots != 0; --slots)
  {
    SpaceObject& slot = _state.Slot(index);
    const std::uint16_t x = _state.Data().playerVelocityX;
    SubtractVelocity(slot, &SpaceObject::xHigh, &SpaceObject::x, &SpaceObject::compassX, x, Low(SignWord(x)));
    const std::uint16_t y = _state.Data().playerVelocityY;
    SubtractVelocity(slot, &SpaceObject::yHigh, &SpaceObject::y, &SpaceObject::compassY, y, Low(SignWord(y)));
    const std::uint16_t z = _state.Data().playerVelocityZ;
    SubtractVelocity(slot, &SpaceObject::zHigh, &SpaceObject::z, &SpaceObject::compassZ, z, Low(SignWord(z)));
    index = SlotAfter(index);
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

} // namespace Elite
