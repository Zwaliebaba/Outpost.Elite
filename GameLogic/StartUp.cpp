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

constexpr std::uint16_t NEW_GAME_ENERGY = 0x3FF;
constexpr std::uint8_t FULL_SHIELD = 0xFF;
constexpr std::uint8_t NEW_GAME_CABIN_TEMPERATURE = 0x0C;
constexpr std::uint8_t NEW_GAME_ALTITUDE = 0xFF;

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

} // namespace

void InstallDivideAndKeyboardInterrupts(GameState& _state, Host& _host)
{
  // The keys to the game's handler (KeyboardInterrupt, D91), where the original installed it on int 9. Nothing raises int 0,
  // the divide trap, which the original installs too.
  _host.SetKeysToGame(true);
  ResetKeyboard(_state);
}

void RestoreDivideAndKeyboardInterrupts(Host& _host)
{
  // The keys no longer the game's, where the original put the BIOS's int 9 back.
  _host.SetKeysToGame(false);
}

void CheckCheatArgument(GameState& _state, const Host& _host)
{
  // The shell's --cheat (D80), where the original read ' cheat' from the command tail DOS gave it.
  _state.Data().startUp.cheatEnabled = _host.CheatRequested() ? 1 : 0;
}

void StartNewGame(GameState& _state)
{
  // REP MOVSB from DS to ES = DS, a byte at a time: startupCommander into commanderBlock, the whole record, as the direction flag is
  // clear wherever the game calls it: the only STD, MaskOutsideTunnel's, has its CLD before the routine returns (change 3c).
  _state.Data().commander = _state.Data().saveLoad.startupCommander;
  _state.Data().commander.witchspaceCountdown = 0;
  _state.Data().playerShip.playerEnergy = NEW_GAME_ENERGY;
  _state.Data().playerShip.foreShield = FULL_SHIELD;
  _state.Data().playerShip.aftShield = FULL_SHIELD;
  _state.Data().playerShip.cabinTemperature = NEW_GAME_CABIN_TEMPERATURE;
  _state.Data().playerShip.altitude = NEW_GAME_ALTITUDE;
  _state.Data().playerShip.playerDead = 0;
  _state.Data().commander.missionJumpCount = 0;
  _state.Data().commander.missionsInFirstGalaxy = 0;
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

[[nodiscard]] Task<void> Start(GameState& _state, Host& _host)
{
  // The original keeps the PSP's segment in pspSegment to read its command tail from, and asks DOS's version, which is 3.30
  // (ADR-006 item 3): the platform gives the tail (CheckCheatArgument), and DOS 1's refusal never comes.
  SaveStartupCommander(_state);

  for (;;)
  {
    InstallTimerInterrupt(_state, _host);
    // CopyProtection came here: it does nothing once protectionShown is set, as the D5 byte sets it (ADR-001).
    InstallDivideAndKeyboardInterrupts(_state, _host);
    CheckCheatArgument(_state, _host);
    co_await GameLoop(_state, _host);

    // HandleDiskRequest: GameLoop comes back only for diskOperation, 0 to leave.
    RestoreDivideAndKeyboardInterrupts(_host);
    RestoreTimerInterrupt(_state, _host);
    const std::uint8_t request = _state.Data().saveLoad.diskOperation;
    if (request == 0)
    {
      // Exit to DOS is a quit (D80): the program ends, with no farewell.
      co_return;
    }
    // The request, through the platform's file store (D78), where the original went through DOS with its own critical-error
    // handler on int 24h.
    _state.Data().disk.diskError = 0;
    PerformDiskRequest(_state, _host, request);
    // JMP RestartPlay.
  }
}

[[nodiscard]] Task<void> GameLoop(GameState& _state, Host& _host)
{
  for (;;)
  {
    _state.Data().flow.inFlight = 0;
    if (co_await RunTitleAndDocked(_state, _host))
    {
      // LeaveGameLoopForDisk: the original drops the disc menu's return address and RunTitleAndDocked's, so that its RET returns
      // into Start.
      co_return;
    }
    _state.Data().flow.inFlight = 1;
    co_await RunFlight(_state, _host);
    // After a death the title runs again.
    if (_state.Data().playerShip.playerDead == 1)
    {
      _state.Data().flow.titleShown = 0;
    }
    // JMP GameLoop.
  }
}

[[nodiscard]] Task<void> ShowCredits(GameState& _state, Host& _host)
{
  // The space view's layers watch the frame (ADR-033), which holds only the credits' words (D66).
  _host.BeginSpaceViewFrame();
  ClearDrawBuffer(_state);
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
  co_await FinishSpaceViewFrame(_state, _host);
  // The frame as it was drawn, for a renderer that draws it in the title's cockpit (D64, D66): the words and nothing of the scene.
  _host.PublishFlightSnapshot(WithoutScene(TakeTitleSnapshot(_state, ELITE_TITLE_TEXT, SnapshotDisc{})), false);
  // WaitForTimerTick keeps every register; the LOOP counts the ticks left.
  for (std::uint16_t ticksLeft = CREDITS_TIMER_TICKS;;)
  {
    co_await WaitForTimerTick(_state, _host);
    if (--ticksLeft == 0)
    {
      break;
    }
  }
}

} // namespace Elite
