#include "pch.h"

#include "StartUp.h"

#include "Arithmetic.h"
#include "Docked.h"
#include "Flight.h"
#include "Input.h"
#include "Maths.h"
#include "SaveLoad.h"
#include "Text.h"
#include "Timer.h"
#include "Video.h"

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
constexpr std::uint16_t EXIT_KEY_DRAIN = 0x00B9;
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

// REP MOVSB: _count bytes from _sourceSegment:_source to _destinationSegment:_destination, forwards or, with DF set
// (_backward), backwards. Returns how far each offset moved, which the original leaves added to SI and DI.
std::uint16_t MoveBytes(GameState& _state, std::uint16_t _sourceSegment, std::uint16_t _source, std::uint16_t _destinationSegment,
                        std::uint16_t _destination, std::uint16_t _count, bool _backward)
{
  const auto step = static_cast<std::uint16_t>(_backward ? 0xFFFF : 1);
  std::uint16_t moved = 0;
  for (std::uint16_t left = _count; left != 0; --left)
  {
    _state.SetFarByte(_destinationSegment, Offset(_destination, moved), _state.FarByte(_sourceSegment, Offset(_source, moved)));
    moved = Offset(moved, step);
  }
  return moved;
}

// ExitToDos (CS:00AD): the BIOS's text mode, the farewell, and the BIOS's key buffer emptied. The RETF that follows, to
// PSP:0000 where Start pushed it, is Start's own return, and the program ends there: of what the services leave in the
// registers, the original reads only ZF, the loop's test, which is PeekBiosKey's answer here.
void ExitToDos(GameState& _state, Hardware& _hardware)
{
  _hardware.SetVideoMode(VIDEO_MODE_TEXT_80);
  _hardware.PrintDosString(_state.DataSegment(), _state.OffsetOf(_state.Data().exitMessage));
  // At the jump back the original holds in AX the key int 16h took out, and the next turn reads no register before it writes
  // it but DX, the message, which does not change. Every turn takes a key, a change paced time sees, so no turn idles.
  while (_hardware.PeekBiosKey().has_value())
  {
    const std::uint16_t key = _hardware.ReadBiosKey();
    _hardware.LoopTurn(EXIT_KEY_DRAIN, {key});
  }
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
  _state.Data().cheatEnabled = 0;
  // The command tail DOS gave the program, which the original reads from its PSP: its length, then the text, which
  // cheatArgumentKey holds XORed with AAh.
  const std::string_view tail = _hardware.CommandTail();
  if (tail.size() != CHEAT_ARGUMENT_BYTES)
  {
    return;
  }
  for (std::uint16_t at = 0; at < CHEAT_ARGUMENT_BYTES; ++at)
  {
    const auto expected = static_cast<std::uint8_t>(_state.Data().cheatArgumentKey[at] ^ CHEAT_KEY);
    if (static_cast<std::uint8_t>(tail[at]) != expected)
    {
      return;
    }
  }
  _state.Data().cheatEnabled = 1;
}

void StartNewGame(GameState& _state, bool _backward)
{
  // REP MOVSB from DS to ES = DS.
  const std::uint16_t data = _state.DataSegment();
  MoveBytes(_state, data, _state.OffsetOf(_state.Data().startupCommander), data, _state.OffsetOf(_state.Data().commanderBlock),
            _state.Data().commanderFileBytes, _backward);
  _state.Data().commanderBlock.witchspaceCountdown = 0;
  _state.Data().playerEnergy = NEW_GAME_ENERGY;
  _state.Data().foreShield = FULL_SHIELD;
  _state.Data().aftShield = FULL_SHIELD;
  _state.Data().cabinTemperature = NEW_GAME_CABIN_TEMPERATURE;
  _state.Data().altitude = NEW_GAME_ALTITUDE;
  _state.Data().playerDead = 0;
  _state.Data().commanderBlock.missionJumpCount = 0;
  _state.Data().commanderBlock.data7629 = 0;
  _state.Data().commanderBlock.missionNumber = 0;
  _state.Data().warningFrames = 0;
  _state.Data().incomingMissileAlert = 0;
  _state.Data().commanderBlock.missionStage = 0;
  _state.Data().commanderBlock.fuelLeakDelayFrames = 0;
  _state.Data().commanderBlock.fuelLeakFrames = 0;
  _state.Data().commanderBlock.supernovaFrames = 0;
  _state.Data().commanderBlock.supernovaHeat = 0;
  _state.Data().commanderBlock.maskShipDestroyed = 0;
  _state.Data().commanderBlock.thargoidInvasionActive = 0;
  _state.Data().commanderBlock.maskMissionShipsLeft = 0;
  _state.Data().commanderBlock.invadedStationDestroyed = 0;
  _state.Data().forceMisjump = 0;
  _state.Data().messageFrames = 0;
  _state.Data().missileState = 0;
  _state.Data().commanderBlock.fledMaskShip = 0;
  _state.Data().commanderBlock.maskSystemJumps = 0;
}

void Start(GameState& _state, Hardware& _hardware, std::uint16_t _psp, std::uint16_t _countIfNone, bool _backward)
{
  // The original keeps _psp in pspSegment to read its command tail from; the platform gives the tail (CheckCheatArgument).
  // MOV AX,DS / MOV AH,30h: AL goes in as the PSP's low byte.
  if (_hardware.ReadDosVersion(Low(_psp)).major == 0)
  {
    // DOS 1 reports no major version, and is not enough.
    _hardware.PrintDosString(_state.DataSegment(), _state.OffsetOf(_state.Data().dosVersionMessage));
    ExitToDos(_state, _hardware);
    return;
  }
  SaveStartupCommander(_state, _backward);

  // CLD / REPE CMPSB of amstradSignature against the ROM at FC00:0016: not an Amstrad if a byte before the last differs.
  std::uint16_t bytesLeft = AMSTRAD_SIGNATURE_BYTES;
  for (std::uint16_t at = 0; bytesLeft != 0;)
  {
    const bool same = _state.Data().amstradSignature[at] == _hardware.RomByte(AMSTRAD_ROM_SEGMENT, Offset(AMSTRAD_ROM_SIGNATURE, at));
    ++at;
    --bytesLeft;
    if (!same)
    {
      break;
    }
  }
  if (bytesLeft != 0)
  {
    _state.Data().amstradPresent = 0;
  }

  // From here on the direction flag is as the CLD left it, and as GameLoop leaves it; BP as DOS left it, and as GameLoop leaves it.
  std::uint16_t countIfNone = _countIfNone;
  bool backward = false;
  for (;;)
  {
    // RestartPlay: the DOS clock read twice for a 7-second wait, whose closing branch the NOPs at StartupWaitPatched replaced, so
    // only the reads remain, and nothing reads what they give.
    (void)_hardware.ReadDosTime();
    (void)_hardware.ReadDosTime();
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
    const std::uint8_t request = _state.Data().diskOperation;
    if (request == 0)
    {
      WipeProgram(_hardware);
      ExitToDos(_state, _hardware);
      return;
    }
    // The request, with the game's own critical-error handler on int 24h, and the handler that was there put back after it.
    // A listing searches with the attributes RestoreTimerInterrupt leaves in CX.
    _state.Data().diskError = 0;
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
    _state.Data().inFlight = 0;
    const DockedExit docked = RunTitleAndDocked(_state, _hardware, countIfNone, backward);
    if (docked.leaves)
    {
      // LeaveGameLoopForDisk: the original drops the disc menu's return address and RunTitleAndDocked's, so that its RET returns
      // into Start.
      return GameLoopExit{docked.countLeft, docked.backward};
    }
    _state.Data().inFlight = 1;
    const FlightExit flight = RunFlight(_state, _hardware, docked.backward);
    countIfNone = flight.countLeft;
    backward = flight.backward;
    // After a death the title runs again.
    if (_state.Data().playerDead == 1)
    {
      _state.Data().titleShown = 0;
    }
    // JMP GameLoop: the next turn reads BP, in RunTitleAndDocked, before it writes it.
    _hardware.LoopTurn(GAME_LOOP, {countIfNone});
  }
}

void ShowCredits(GameState& _state, Hardware& _hardware, bool _backward)
{
  ClearDrawBuffer(_state, _backward);
  // Each line from SI, one past the last line's NUL, at DI, a row of characters below the last line's start. The original pushes
  // and pops the count and the place round the print; its LOOP's turns carry the count, the text and the place.
  std::string_view text = _state.TextAt(_state.Data().creditsScreenText);
  std::uint16_t place = CREDITS_FIRST_LINE;
  for (std::uint16_t linesLeft = CREDITS_LINES;;)
  {
    _state.Data().textPaperPattern = 0;
    (void)DrawViewString(_state, text, WHITE_MASK, place);
    text = _state.TextAfter(text);
    place = Offset(place, CREDITS_LINE_STEP);
    if (--linesLeft == 0)
    {
      break;
    }
    _hardware.LoopTurn(CREDITS_LINE, {linesLeft, _state.OffsetOf(*text.data()), place});
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

// ── The entry of the program ──

void StartEntry(Guest& _guest)
{
  // DOS leaves the PSP's segment in DS, which the original saves in pspSegment, BP as Start takes it, and the direction flag
  // clear. Its first instructions push the far return to PSP:0000, where DOS's INT 20h ends the program, for its RETF to take
  // once Start returns; on the platform the program ends when Start returns.
  Start(_guest.State(), _guest.Devices(), _guest.PspSegment(), Guest::EntryBasePointer(), false);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x0000, "Start", &StartEntry, EntryReturn::Far},
};

} // namespace

std::span<const NativeEntry> StartUpEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
