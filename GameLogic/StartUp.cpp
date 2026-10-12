#include "pch.h"

#include "StartUp.h"

#include "Arithmetic.h"
#include "Docked.h"
#include "Flight.h"
#include "Input.h"
#include "SaveLoad.h"
#include "SharedTables.h"
#include "StartUpTables.h"
#include "Text.h"
#include "Timer.h"
#include "Video.h"

#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace Elite
{

namespace
{

// The game's int 0 handler, the divide trap. Nothing raises int 0 here: no divide runs on the CPU, and each divide calls
// DivideOverflowInterrupt itself when its quotient overflows. So no native routine stands at the vector's address.
constexpr std::uint16_t DIVIDE_OVERFLOW_INTERRUPT = 0x025E;
constexpr std::uint16_t KEYBOARD_INTERRUPT = 0x0201;

// The vectors the game installs its own handlers on: the divide trap, the timer's tick and the keyboard.
constexpr std::uint8_t DIVIDE_VECTOR = 0x00;
constexpr std::uint8_t TIMER_VECTOR = 0x08;
constexpr std::uint8_t KEYBOARD_VECTOR = 0x09;

constexpr std::uint8_t CHEAT_KEY = 0xAA;
constexpr std::uint8_t CHEAT_ARGUMENT_BYTES = 6; // ' cheat'

constexpr std::uint16_t NEW_GAME_ENERGY = 0x3FF;
constexpr std::uint8_t FULL_SHIELD = 0xFF;
constexpr std::uint8_t NEW_GAME_CABIN_TEMPERATURE = 0x0C;
constexpr std::uint8_t NEW_GAME_ALTITUDE = 0xFF;

// The game's int 24h handler, which Start puts in the interrupt table round a disk request. Nothing raises int 24h here: DOS
// answers a failed call with CF set and an error. So no native routine stands at the vector's address.
constexpr std::uint16_t CRITICAL_ERROR_INTERRUPT = 0x02F0;

// Where the original jumps back (Hardware::LoopTurn).
constexpr std::uint16_t RESTART_PLAY = 0x003B;
constexpr std::uint16_t GAME_LOOP = 0x7D30;
constexpr std::uint16_t CREDITS_LINE = 0x8F0E;
constexpr std::uint16_t CREDITS_WAIT = 0x8F2B;

constexpr std::uint8_t CRITICAL_ERROR_VECTOR = 0x24;
constexpr std::uint16_t AMSTRAD_ROM_SEGMENT = 0xFC00;
constexpr std::uint16_t AMSTRAD_ROM_SIGNATURE = 0x0016;
constexpr std::uint16_t AMSTRAD_SIGNATURE_BYTES = 7;
constexpr std::uint8_t VIDEO_MODE_TEXT_80 = 0x02; // 80x25 text

constexpr std::uint16_t CREDITS_FIRST_LINE = 0x202;
constexpr std::uint16_t CREDITS_LINES = 9;
constexpr std::uint16_t CREDITS_LINE_STEP = 0x200;
constexpr std::uint16_t WHITE_MASK = 0xFFFF;
constexpr std::uint16_t CREDITS_TIMER_TICKS = 3000; // 3 s

// A table of the reference's bytes as text, all of it: what DOS and the text routines read through.
[[nodiscard]] std::string_view TextOf(std::span<const std::uint8_t> _bytes) noexcept
{
  return std::string_view(reinterpret_cast<const char*>(_bytes.data()), _bytes.size());
}

// creditsScreenText's line from its byte _at, up to its NUL.
[[nodiscard]] std::string_view CreditsLine(std::size_t _at) noexcept
{
  const std::string_view credits = TextOf(CREDITS_SCREEN_TEXT);
  return credits.substr(_at, credits.find('\0', _at) - _at);
}

// ExitToDos (CS:00AD): the BIOS's text mode and the farewell, exitMessage up to its '$'. The original then empties the BIOS's
// key buffer, which the ROM's int 9 handler keeps empty. The RETF that follows, to PSP:0000 where Start pushed it, is Start's
// own return, and the program ends there.
void ExitToDos(Hardware& _hardware)
{
  _hardware.SetVideoMode(VIDEO_MODE_TEXT_80);
  _hardware.PrintDosString(TextOf(EXIT_MESSAGE));
}

} // namespace

void InstallDivideAndKeyboardInterrupts(GameState& _state, Hardware& _hardware)
{
  // The original keeps the BIOS's int 0 and int 9 in savedDivideVector and savedKeyboardVector to put back; native code puts
  // back the BIOS's own (RestoreDivideAndKeyboardInterrupts).
  // With interrupts off, the game's own handlers installed, and int 8's segment written again.
  _hardware.DisableInterrupts();
  const std::uint16_t code = _state.CodeSegment();
  _hardware.SetInterruptVector(DIVIDE_VECTOR, FarAddress{code, DIVIDE_OVERFLOW_INTERRUPT});
  _hardware.SetInterruptVector(KEYBOARD_VECTOR, FarAddress{code, KEYBOARD_INTERRUPT});
  _hardware.SetInterruptVector(TIMER_VECTOR, FarAddress{code, _hardware.InterruptVector(TIMER_VECTOR).offset});
  _hardware.EnableInterrupts();
  ResetKeyboard(_state, _hardware);
}

void RestoreDivideAndKeyboardInterrupts(Hardware& _hardware)
{
  // With interrupts off, the BIOS's int 9 and int 0 put back.
  _hardware.DisableInterrupts();
  _hardware.SetInterruptVector(KEYBOARD_VECTOR, _hardware.BiosInterruptVector(KEYBOARD_VECTOR));
  _hardware.SetInterruptVector(DIVIDE_VECTOR, _hardware.BiosInterruptVector(DIVIDE_VECTOR));
  _hardware.EnableInterrupts();
}

void CheckCheatArgument(GameState& _state, const Hardware& _hardware)
{
  _state.Data().startUp.cheatEnabled = 0;
  // The command tail DOS gave the program, which the original reads from its PSP: its length, then the text, which
  // cheatArgumentKey holds XORed with AAh.
  const std::string_view tail = _hardware.CommandTail();
  if (tail.size() != CHEAT_ARGUMENT_BYTES)
  {
    return;
  }
  for (std::uint16_t at = 0; at < CHEAT_ARGUMENT_BYTES; ++at)
  {
    const auto expected = static_cast<std::uint8_t>(CHEAT_ARGUMENT_KEY[at] ^ CHEAT_KEY);
    if (static_cast<std::uint8_t>(tail[at]) != expected)
    {
      return;
    }
  }
  _state.Data().startUp.cheatEnabled = 1;
}

void StartNewGame(GameState& _state)
{
  // REP MOVSB from DS to ES = DS, a byte at a time: startupCommander's bytes into commanderBlock's, upwards, as the direction flag is clear
  // wherever the game calls it: the only STD, MaskOutsideTunnel's, has its CLD before the routine returns (change 3c).
  const std::span<Counted<std::uint8_t>> to = BytesOf(_state.Data().commander);
  const std::span<const Counted<std::uint8_t>> from(std::as_const(_state).Data().saveLoad.startupCommander);
  for (std::size_t index = 0; index < COMMANDER_FILE_BYTES; ++index)
  {
    to[index] = std::uint8_t{from[index]};
  }
  _state.Data().commander.witchspaceCountdown = 0;
  _state.Data().playerShip.playerEnergy = NEW_GAME_ENERGY;
  _state.Data().playerShip.foreShield = FULL_SHIELD;
  _state.Data().playerShip.aftShield = FULL_SHIELD;
  _state.Data().playerShip.cabinTemperature = NEW_GAME_CABIN_TEMPERATURE;
  _state.Data().playerShip.altitude = NEW_GAME_ALTITUDE;
  _state.Data().playerShip.playerDead = 0;
  _state.Data().commander.missionJumpCount = 0;
  _state.Data().commander.data7629 = 0;
  _state.Data().commander.missionNumber = 0;
  _state.Data().messages.warningFrames = 0;
  _state.Data().playerShip.incomingMissileAlert = 0;
  _state.Data().commander.missionStage = 0;
  _state.Data().commander.fuelLeakDelayFrames = 0;
  _state.Data().commander.fuelLeakFrames = 0;
  _state.Data().commander.supernovaFrames = 0;
  _state.Data().commander.supernovaHeat = 0;
  _state.Data().commander.maskShipDestroyed = 0;
  _state.Data().commander.thargoidInvasionActive = 0;
  _state.Data().commander.maskMissionShipsLeft = 0;
  _state.Data().commander.invadedStationDestroyed = 0;
  _state.Data().jump.forceMisjump = 0;
  _state.Data().messages.messageFrames = 0;
  _state.Data().playerShip.missileState = 0;
  _state.Data().commander.fledMaskShip = 0;
  _state.Data().commander.maskSystemJumps = 0;
}

void Start(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone)
{
  // The original keeps the PSP's segment in pspSegment to read its command tail from, and asks DOS's version, which is 3.30
  // (ADR-006 item 3): the platform gives the tail (CheckCheatArgument), and DOS 1's refusal never comes.
  SaveStartupCommander(_state);

  // CLD / REPE CMPSB of amstradSignature against the ROM at FC00:0016: not an Amstrad if a byte before the last differs.
  std::uint16_t bytesLeft = AMSTRAD_SIGNATURE_BYTES;
  for (std::uint16_t at = 0; bytesLeft != 0;)
  {
    const bool same = AMSTRAD_SIGNATURE[at] == _hardware.RomByte(AMSTRAD_ROM_SEGMENT, Offset(AMSTRAD_ROM_SIGNATURE, at));
    ++at;
    --bytesLeft;
    if (!same)
    {
      break;
    }
  }
  if (bytesLeft != 0)
  {
    _state.Data().unused.amstradPresent = 0;
  }

  // From here on the direction flag is as the CLD left it, and as GameLoop leaves it; BP as DOS left it, and as GameLoop leaves it.
  std::uint16_t countIfNone = _countIfNone;
  bool backward = false;
  for (;;)
  {
    InstallTimerInterrupt(_state, _hardware);
    // CopyProtection (CS:04A3) came here: it does nothing once protectionShown is set, as the D5 byte sets it (ADR-001).
    InstallDivideAndKeyboardInterrupts(_state, _hardware);
    CheckCheatArgument(_state, _hardware);
    const GameLoopExit loopExit = GameLoop(_state, _hardware, countIfNone, backward);
    countIfNone = loopExit.countLeft;
    backward = loopExit.backward;

    // HandleDiskRequest (CS:0065): GameLoop comes back only for diskOperation, 0 to leave.
    RestoreDivideAndKeyboardInterrupts(_hardware);
    const BiosClock clock = RestoreTimerInterrupt(_state, _hardware);
    const std::uint8_t request = _state.Data().saveLoad.diskOperation;
    if (request == 0)
    {
      WipeProgram(_hardware);
      ExitToDos(_hardware);
      return;
    }
    // The request, with the game's own critical-error handler on int 24h, and the handler that was there put back after it.
    // A listing searches with the attributes RestoreTimerInterrupt leaves in CX.
    _state.Data().disk.diskError = 0;
    const FarAddress criticalError = _hardware.InterruptVector(CRITICAL_ERROR_VECTOR);
    _hardware.SetInterruptVector(CRITICAL_ERROR_VECTOR, FarAddress{_state.CodeSegment(), CRITICAL_ERROR_INTERRUPT});
    PerformDiskRequest(_state, _hardware, request, ClockLessADayHigh(clock));
    _hardware.SetInterruptVector(CRITICAL_ERROR_VECTOR, criticalError);
    // JMP RestartPlay: the next turn reads BP, in GameLoop, before it writes it.
    _hardware.LoopTurn(RESTART_PLAY, {countIfNone});
  }
}

void WipeProgram(Hardware& _hardware)
{
  _hardware.DisableInterrupts();
}

GameLoopExit GameLoop(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward)
{
  // BP and the direction flag go round: RunTitleAndDocked's screens read them, and RunFlight leaves them.
  std::uint16_t countIfNone = _countIfNone;
  bool backward = _backward;
  for (;;)
  {
    _state.Data().flow.inFlight = 0;
    const DockedExit docked = RunTitleAndDocked(_state, _hardware, countIfNone, backward);
    if (docked.leaves)
    {
      // LeaveGameLoopForDisk: the original drops the disc menu's return address and RunTitleAndDocked's, so that its RET returns
      // into Start.
      return GameLoopExit{docked.countLeft, docked.backward};
    }
    _state.Data().flow.inFlight = 1;
    const FlightExit flight = RunFlight(_state, _hardware, docked.backward);
    countIfNone = flight.countLeft;
    backward = flight.backward;
    // After a death the title runs again.
    if (_state.Data().playerShip.playerDead == 1)
    {
      _state.Data().flow.titleShown = 0;
    }
    // JMP GameLoop: the next turn reads BP, in RunTitleAndDocked, before it writes it.
    _hardware.LoopTurn(GAME_LOOP, {countIfNone});
  }
}

void ShowCredits(GameState& _state, Hardware& _hardware, bool _backward)
{
  ClearDrawBuffer(_state, _backward);
  // Each line from SI, one past the last line's NUL, at DI, a row of characters below the last line's start. The original pushes
  // and pops the count and the place round the print; its LOOP's turns carry the count, the text and the place, SI as the
  // line's place in the table (D38).
  std::size_t line = 0;
  std::string_view text = CreditsLine(line);
  std::uint16_t place = CREDITS_FIRST_LINE;
  for (std::uint16_t linesLeft = CREDITS_LINES;;)
  {
    _state.Data().drawing.textPaperPattern = 0;
    (void)DrawViewString(_state, text, WHITE_MASK, place);
    place = Offset(place, CREDITS_LINE_STEP);
    if (--linesLeft == 0)
    {
      break;
    }
    // SI past the line's NUL; past the last line, the table's end, nothing reads it.
    line += text.size() + 1;
    text = CreditsLine(line);
    _hardware.LoopTurn(CREDITS_LINE, {linesLeft, static_cast<std::uint16_t>(line), place});
  }
  FinishSpaceViewFrame(_state, _hardware);
  // WaitForTimerTick keeps every register; the LOOP's turns carry the ticks left.
  for (std::uint16_t ticksLeft = CREDITS_TIMER_TICKS;;)
  {
    WaitForTimerTick(_state, _hardware);
    if (--ticksLeft == 0)
    {
      break;
    }
    _hardware.LoopTurn(CREDITS_WAIT, {ticksLeft});
  }
}

std::array<std::uint8_t, SpaceObject::BYTES> BytesAfterDrawBuffer(const GameState& _state)
{
  std::array<std::uint8_t, SpaceObject::BYTES> bytes{};
  std::size_t at = 0;
  const auto append = [&bytes, &at](std::uint8_t _byte) { bytes.at(at++) = _byte; };
  append(_state.Data().unused.amstradPresent);
  for (const std::uint8_t byte : AMSTRAD_SIGNATURE)
  {
    append(byte);
  }
  append(_state.Data().audio.speakerPortImage);
  append(_state.Data().saveLoad.diskOperation);
  for (const std::uint8_t byte : EXIT_MESSAGE)
  {
    append(byte);
  }
  for (std::size_t index = 0; at < bytes.size(); ++index)
  {
    append(_state.Data().unused.dosVersionMessage[index]);
  }
  return bytes;
}

// ── The entry of the program ──

void StartEntry(Guest& _guest)
{
  // DOS leaves BP as Start takes it, and the direction flag clear. Start's first instructions push the far return to PSP:0000,
  // where DOS's INT 20h ends the program, for its RETF to take once Start returns; on the platform the program ends when Start
  // returns.
  Start(_guest.State(), _guest.Devices(), Guest::EntryBasePointer());
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x0000, &StartEntry, EntryReturn::Far},
};

} // namespace

std::span<const NativeEntry> StartUpEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
