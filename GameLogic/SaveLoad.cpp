#include "pch.h"

#include "SaveLoad.h"

#include "Arithmetic.h"
#include "Docked.h"
#include "Input.h"
#include "SharedTables.h"
#include "StateWriter.h"
#include "Text.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <span>
#include <string_view>
#include <utility>

namespace Elite
{

namespace
{

// commanderFileName up to its NUL: the name the original hands DOS. Every writer leaves a NUL inside it: the image's
// '12345678.CDR', the NUL after the ".CDR" D10 appends to a name of eight characters at most, and the NUL over its '.' once it is
// shown (ShowDiscScreen).
[[nodiscard]] std::string_view CommanderFileName(const GameState& _state)
{
  return TextIn(_state.Data().saveLoad.commanderFileName);
}

// The commander as the .cdr file holds it (D78): its members written in their order (StateWriter).
[[nodiscard]] std::vector<std::uint8_t> CommanderFileBytes(const GameState& _state)
{
  return StateBytes(_state.Data().commander);
}

// A commander file read over the commander (D78): the file's bytes in place of the commander's, as many as the read gave, and
// the commander read back from them; what the read does not reach stays as it was.
[[nodiscard]] DosError ReadCommanderFile(GameState& _state, OpenFile& _file)
{
  const std::vector<std::uint8_t> commander = CommanderFileBytes(_state);
  std::array<std::uint8_t, COMMANDER_FILE_BYTES> bytes{};
  std::copy_n(commander.begin(), bytes.size(), bytes.begin());
  std::uint32_t count = 0;
  const DosError read = _file.Read(bytes, count);
  ReadStateBytes(_state.Data().commander, bytes);
  return read;
}

// A failure of a disk request: diskError, on which D10 offers to retry it.
void Fail(GameState& _state)
{
  _state.Data().disk.diskError = 1;
}

// The most each of a loaded commander's fields holds when the game wrote it (D43): the ninth galaxy, the chart's last row, four
// missiles, the eighth economy and government, and the kills CreditKill stops at.
constexpr std::uint8_t MOST_GALAXY = 8;
constexpr std::uint8_t MOST_CHART_ROW = 0x7F;
constexpr std::uint8_t MOST_MISSILES = 4;
constexpr std::uint8_t MOST_ECONOMY_OR_GOVERNMENT = 7;
constexpr std::uint8_t MOST_KILLS = 0xFE;
// The end of creditBalanceText after FormatCredits's digits and point: the image's bytes there, which no writer changes.
constexpr std::string_view CREDITS_UNIT_TEXT = " Credits";

// _field held to _most.
void HoldTo(std::uint8_t& _field, std::uint8_t _most) noexcept
{
  if (_field > _most)
  {
    _field = _most;
  }
}

// What D43 does to a commander just read from a file, a change on purpose: each field the game's own writers keep in a range
// is brought into it, the name's last byte, which no writer sets, is its NUL, and the cash's text is the cash again, with its unit and
// its NUL.
// A file the game saved holds all of it already, and is left as it is.
void ClampLoadedCommander(GameState& _state)
{
  CommanderState& commander = _state.Data().commander;
  HoldTo(commander.galaxyNumber, MOST_GALAXY);
  HoldTo(commander.currentSystemChartY, MOST_CHART_ROW);
  HoldTo(commander.chartCursorY, MOST_CHART_ROW);
  HoldTo(commander.shortRangeCursorY, MOST_CHART_ROW);
  HoldTo(commander.galacticCursorY, MOST_CHART_ROW);
  HoldTo(commander.currentSystem.government, MOST_ECONOMY_OR_GOVERNMENT);
  HoldTo(commander.currentSystem.economy, MOST_ECONOMY_OR_GOVERNMENT);
  HoldTo(commander.selectedSystem.government, MOST_ECONOMY_OR_GOVERNMENT);
  HoldTo(commander.selectedSystem.economy, MOST_ECONOMY_OR_GOVERNMENT);
  HoldTo(commander.missileCount, MOST_MISSILES);
  HoldTo(commander.killCount, MOST_KILLS);
  commander.defaultCommanderName[sizeof(commander.defaultCommanderName) - 1] = 0;
  FormatCredits(_state);
  // The text's last characters, then its NUL.
  constexpr std::size_t NUL = sizeof(commander.creditBalanceText) - 1;
  constexpr std::size_t UNIT = NUL - CREDITS_UNIT_TEXT.size();
  for (std::size_t character = 0; character < CREDITS_UNIT_TEXT.size(); ++character)
  {
    commander.creditBalanceText[UNIT + character] = static_cast<std::uint8_t>(CREDITS_UNIT_TEXT[character]);
  }
  commander.creditBalanceText[NUL] = 0;
}

// LoadCommanderFile: only a file with no attributes, read whole into commanderBlock, and then held to what the game
// writes (D43).
void LoadCommanderFile(GameState& _state, FileStore& _files)
{
  std::uint8_t attribute = 0;
  if (_files.Attribute(CommanderFileName(_state), attribute) != DosError::None || attribute != 0)
  {
    Fail(_state);
    return;
  }
  std::unique_ptr<OpenFile> file;
  if (_files.Open(CommanderFileName(_state), FileAccess::Read, file) != DosError::None)
  {
    Fail(_state);
    return;
  }
  if (ReadCommanderFile(_state, *file) != DosError::None)
  {
    Fail(_state);
    return;
  }
  ClampLoadedCommander(_state);
}

// SaveCommanderFile: commanderBlock written to a new file, stamped _stamp, whose attributes are then cleared.
void SaveCommanderFile(GameState& _state, FileStore& _files, const FileStamp& _stamp)
{
  std::unique_ptr<OpenFile> file;
  if (_files.Create(CommanderFileName(_state), 0, _stamp, file) != DosError::None)
  {
    Fail(_state);
    return;
  }
  std::uint32_t count = 0;
  if (file->Write(CommanderFileBytes(_state), _stamp, count) != DosError::None)
  {
    Fail(_state);
    return;
  }
  file.reset();
  if (_files.SetAttribute(CommanderFileName(_state), 0) != DosError::None)
  {
    Fail(_state);
  }
}

// DeleteCommanderFile.
void DeleteCommanderFile(GameState& _state, FileStore& _files)
{
  if (_files.Delete(CommanderFileName(_state)) != DosError::None)
  {
    Fail(_state);
  }
}

} // namespace

void PerformDiskRequest(GameState& _state, Host& _host, std::uint8_t _request)
{
  switch (_request)
  {
  case DISK_LOAD:
    LoadCommanderFile(_state, _host.Files());
    return;
  case DISK_SAVE:
    SaveCommanderFile(_state, _host.Files(), _host.FileStampNow());
    return;
  case DISK_DELETE:
    DeleteCommanderFile(_state, _host.Files());
    return;
  default:
    return;
  }
}

void SaveStartupCommander(GameState& _state)
{
  // commanderBlock into startupCommander, the whole record, a byte at a time going up: the original's copy never runs downward
  // where the game calls it, as the only routine that reverses the direction, MaskOutsideTunnel, restores it before it returns
  // (change 3c).
  _state.Data().saveLoad.startupCommander = _state.Data().commander;
}

} // namespace Elite
