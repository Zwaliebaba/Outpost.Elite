#include "pch.h"

#include "StartUp.h"

#include "Arithmetic.h"
#include "Docked.h"
#include "Flight.h"
#include "FlightSnapshot.h"
#include "Input.h"
#include "SaveLoad.h"
#include "SharedTables.h"
#include "StartUpTables.h"
#include "StateWriter.h"
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

constexpr std::uint8_t CHEAT_KEY = 0xAA;
constexpr std::uint8_t CHEAT_ARGUMENT_BYTES = 6; // ' cheat'

constexpr std::uint16_t NEW_GAME_ENERGY = 0x3FF;
constexpr std::uint8_t FULL_SHIELD = 0xFF;
constexpr std::uint8_t NEW_GAME_CABIN_TEMPERATURE = 0x0C;
constexpr std::uint8_t NEW_GAME_ALTITUDE = 0xFF;

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
  // With interrupts off, the game's own handlers installed, and int 8's segment written again. Nothing raises int 0 (the
  // divide trap at CS:025E), so its vector goes nowhere; the keyboard's handler is CS:0201. Int 8's segment written as the code
  // segment keeps the game's handler there; the BIOS's, moved into the code segment, would name code no routine stands in for.
  _hardware.DisableInterrupts();
  _hardware.SetHandler(Irq::Keyboard, IrqHandler::Game);
  if (_hardware.Handler(Irq::Timer) != IrqHandler::Game)
  {
    _hardware.SetHandler(Irq::Timer, IrqHandler::Stray);
  }
  _hardware.EnableInterrupts();
  ResetKeyboard(_state, _hardware);
}

void RestoreDivideAndKeyboardInterrupts(Hardware& _hardware)
{
  // With interrupts off, the BIOS's int 9 and int 0 put back; int 0's goes nowhere, as nothing raises it.
  _hardware.DisableInterrupts();
  _hardware.SetHandler(Irq::Keyboard, IrqHandler::Bios);
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
  // REP MOVSB from DS to ES = DS, a byte at a time: startupCommander into commanderBlock, the whole record, as the direction flag is
  // clear wherever the game calls it: the only STD, MaskOutsideTunnel's, has its CLD before the routine returns (change 3c).
  CopyMembers(_state.Data().commander, _state.Data().saveLoad.startupCommander);
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

  // CLD / REPE CMPSB of amstradSignature against the ROM at FC00:0016: not an Amstrad if a byte before the last differs, as one
  // does on the platform's IBM PC (Hardware::AmstradRom).
  if (!Hardware::AmstradRom())
  {
    _state.Data().startUp.amstradPresent = 0;
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
    const ClockReading clock = RestoreTimerInterrupt(_state, _hardware);
    const std::uint8_t request = _state.Data().saveLoad.diskOperation;
    if (request == 0)
    {
      WipeProgram(_hardware);
      ExitToDos(_hardware);
      return;
    }
    // The request, which the original makes with its own critical-error handler, CS:02F0, on int 24h, and the handler that was
    // there put back after it. Nothing raises int 24h: DOS answers a failed call with CF set and an error. A listing searches
    // with the attributes RestoreTimerInterrupt leaves in CX.
    _state.Data().disk.diskError = 0;
    PerformDiskRequest(_state, _hardware, request, ClockLessADayHigh(clock));
    // JMP RestartPlay: GameLoop reads BP before it writes it.
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
    // JMP GameLoop: RunTitleAndDocked reads BP before it writes it.
  }
}

void ShowCredits(GameState& _state, Hardware& _hardware, bool _backward)
{
  // The space view's layers watch the frame (ADR-033), which holds only the credits' words (D66).
  _hardware.BeginSpaceViewFrame();
  ClearDrawBuffer(_state, _backward);
  // Each line from SI, one past the last line's NUL, at DI, a row of characters below the last line's start. The original pushes
  // and pops the count and the place round the print.
  std::size_t line = 0;
  std::string_view text = CreditsLine(line);
  std::uint16_t place = CREDITS_FIRST_LINE;
  for (std::uint16_t linesLeft = CREDITS_LINES;;)
  {
    _state.Data().drawing.textPaperPattern = 0;
    DrawViewString(_state, text, WHITE_MASK, place);
    place = Offset(place, CREDITS_LINE_STEP);
    if (--linesLeft == 0)
    {
      break;
    }
    // SI past the line's NUL; past the last line, the table's end, nothing reads it.
    line += text.size() + 1;
    text = CreditsLine(line);
  }
  FinishSpaceViewFrame(_state, _hardware);
  // The frame as it was drawn, for a renderer that draws it in the title's cockpit (D64, D66): the words and nothing of the scene.
  _hardware.PublishFlightSnapshot(WithoutScene(TakeTitleSnapshot(_state, ELITE_TITLE_TEXT, SnapshotDisc{})), false);
  // WaitForTimerTick keeps every register; the LOOP counts the ticks left.
  for (std::uint16_t ticksLeft = CREDITS_TIMER_TICKS;;)
  {
    WaitForTimerTick(_state, _hardware);
    if (--ticksLeft == 0)
    {
      break;
    }
  }
}

std::array<std::uint8_t, SpaceObject::BYTES> BytesAfterDrawBuffer(const GameState& _state)
{
  std::array<std::uint8_t, SpaceObject::BYTES> bytes{};
  std::size_t at = 0;
  const auto append = [&bytes, &at](std::uint8_t _byte) { bytes.at(at++) = _byte; };
  append(_state.Data().startUp.amstradPresent);
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
    append(DOS_VERSION_MESSAGE.at(index));
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
  NativeEntry{EntryKind::Start, &StartEntry}, // CS:0000
};

} // namespace

std::span<const NativeEntry> StartUpEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
