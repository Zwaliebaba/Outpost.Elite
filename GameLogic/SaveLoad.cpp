#include "pch.h"

#include "SaveLoad.h"

#include "Arithmetic.h"
#include "Docked.h"
#include "Input.h"
#include "SaveLoadTables.h"
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

constexpr std::uint8_t MOST_COMMANDER_FILES = 0x28;
constexpr std::string_view COMMANDER_FILE_EXTENSION = ".CDR"; // as the store keys names, upper case

// ---- The disc menu ----

// ".CDR" and its NUL, which PromptCommanderFileName appends to a name: DS:8697 in 1987, a text no symbol starts and nothing
// writes, whose bytes the 1987 image held there.
constexpr std::array<std::uint8_t, 5> COMMANDER_EXTENSION = {'.', 'C', 'D', 'R', 0};

// Where it prints on the 40-column text page, 80 bytes a row.
constexpr std::uint16_t TITLE_POSITION = 0x0054;
constexpr std::uint16_t STATUS_POSITION = 0x0076;
constexpr std::uint16_t NAME_POSITION = 0x008A;
constexpr std::uint16_t MENU_POSITION = 0x00F4;
constexpr std::uint16_t HELP_POSITION = 0x0324;
constexpr std::uint16_t SECOND_HELP_POSITION = 0x0374;
constexpr std::uint16_t CATALOGUE_POSITION = 0x0414;
constexpr std::uint16_t TEXT_ROW_BYTES = 0x50;

// The keys it reads, as scan codes.
constexpr std::uint8_t SCAN_E = 0x12;
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_S = 0x1F;
constexpr std::uint8_t SCAN_D = 0x20;
constexpr std::uint8_t SCAN_K = 0x25;
constexpr std::uint8_t SCAN_L = 0x26;
constexpr std::uint8_t SCAN_C = 0x2E;
constexpr std::uint8_t SCAN_V = 0x2F;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_M = 0x32;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_PAST_F10 = 0x45;

// diskOperation, what Start does on the menu's behalf.
constexpr std::uint8_t EXIT_TO_DOS = 0;
constexpr std::uint8_t LOAD_COMMANDER = 1;
constexpr std::uint8_t SAVE_COMMANDER = 2;
constexpr std::uint8_t DELETE_COMMANDER = 3;
constexpr std::uint8_t CATALOGUE_COMMANDERS = 4;

// inputDevice: the keyboard or the mouse, as 1987 numbered them; its 1, the joystick, went with D81.
constexpr std::uint8_t KEYBOARD_DEVICE = 0;
constexpr std::uint8_t MOUSE_DEVICE = 2;

constexpr std::uint8_t MOST_NAME_CHARACTERS = 8;
constexpr std::uint8_t LOWER_CASE_BIT_CLEAR = 0xDF;
constexpr auto EXTENSION_BYTES = static_cast<std::uint16_t>(COMMANDER_EXTENSION.size());
constexpr std::uint16_t CATALOGUE_ROWS = 10;
constexpr std::uint16_t CATALOGUE_COLUMN_BYTES = 0x12; // nine cells
constexpr std::uint8_t TEXT_LAYOUT_BIT = 2;            // screenLayout: the text page shows

// commanderFileName up to its NUL: the name the original hands DOS in DS:DX. Every writer leaves a NUL inside it: the image's
// '12345678.CDR', ReadTextLine's eight characters at most and their NUL, the NUL after the ".CDR" appended to a name
// (PromptCommanderFileName), and the NUL over its '.' (ShowDiskResult).
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

// A failure of a disk request: diskError, which the disc menu shows as DISC ERROR and offers to retry.
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

// LoadCommanderFile (0x031A): only a file with no attributes, read whole into commanderBlock, and then held to what the game
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

// SaveCommanderFile (0x0358): commanderBlock written to a new file, stamped _stamp, whose attributes are then cleared.
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

// ListCommanderFiles (0x03A8): the names of up to 40 .CDR files with no attributes, without their extension, into
// commanderFileList, in the order of their names.
void ListCommanderFiles(GameState& _state, FileStore& _files)
{
  _state.Data().saveLoad.commanderFileCount = 0;
  _state.Data().saveLoad.commanderFileListNext = 0;
  const std::span<std::uint8_t> list(_state.Data().saveLoad.commanderFileList);
  for (const FileEntry& entry : _files.List())
  {
    const std::size_t dot = entry.name.find('.');
    if (entry.attribute != 0 || dot == std::string::npos || std::string_view(entry.name).substr(dot) != COMMANDER_FILE_EXTENSION)
    {
      continue;
    }
    // The name up to its '.', at the index in commanderFileList that commanderFileListNext holds.
    std::size_t to = _state.Data().saveLoad.commanderFileListNext;
    for (std::size_t from = 0; from < dot; ++from)
    {
      list[to] = static_cast<std::uint8_t>(entry.name[from]);
      ++to;
    }
    list[to] = 0;
    _state.Data().saveLoad.commanderFileListNext = static_cast<std::uint16_t>(to + 1);
    const auto count = static_cast<std::uint8_t>(_state.Data().saveLoad.commanderFileCount + 1);
    _state.Data().saveLoad.commanderFileCount = count;
    if (count == MOST_COMMANDER_FILES)
    {
      return;
    }
  }
}

// DeleteCommanderFile (0x03F2).
void DeleteCommanderFile(GameState& _state, FileStore& _files)
{
  if (_files.Delete(CommanderFileName(_state)) != DosError::None)
  {
    Fail(_state);
  }
}

// ---- The disc menu ----

// MOV SI,_text / MOV DI,_position / CALL PrintTextModeString: _text at B800:_position. Returns the cell after its last.
std::uint16_t PrintAt(GameState& _state, std::string_view _text, std::uint16_t _position)
{
  return PrintTextModeString(_state, _text, _position);
}

// CALL GetKey / JZ back: GetKey until a key comes (ADR-039). Returns the key.
[[nodiscard]] Task<KeyPress> WaitForKey(GameState& _state, Host& _host)
{
  for (;;)
  {
    const KeyPress key = GetKey(_state);
    if (key.scanCode != 0)
    {
      co_return key;
    }
    co_await AwaitNextKey(_host, key);
  }
}

// WaitForKey until one of _first and _second comes, every other key going back to it too. Returns it.
[[nodiscard]] Task<std::uint8_t> WaitForEitherKey(GameState& _state, Host& _host, std::uint8_t _first, std::uint8_t _second)
{
  for (;;)
  {
    const std::uint8_t scan = (co_await WaitForKey(_state, _host)).scanCode;
    if (scan == _first || scan == _second)
    {
      co_return scan;
    }
  }
}

// The two help rows blanked (CS:6664 and CS:671F).
void BlankHelpRows(GameState& _state)
{
  PrintAt(_state, BLANK_LINE_TEXT, HELP_POSITION);
  PrintAt(_state, BLANK_LINE_TEXT, SECOND_HELP_POSITION);
}

// LeaveGameLoopForDisk (CS:7E90): diskOperation=_operation, and resumeAtDiskMenu=1, so that the menu shows what Start's disk
// work came to when it runs again. The two POP AX between its writes, which drop the return address of the call it is in and the
// one under it, so that its RET takes GameLoop's back to Start, are its callers' return (DiscMenuExit::leaves): they read the stack
// and write no memory, so the writes keep the original's order.
DiscMenuExit LeaveGameLoopForDisk(GameState& _state, std::uint8_t _operation)
{
  _state.Data().saveLoad.diskOperation = _operation;
  _state.Data().saveLoad.resumeAtDiskMenu = 1;
  return DiscMenuExit{true, 0};
}

// The input device's name, which ShowControlDevice reads from inputDeviceNames: the disc menu sets inputDevice to 0 or 2.
[[nodiscard]] std::string_view InputDeviceName(const GameState& _state)
{
  return _state.Data().saveLoad.inputDevice == MOUSE_DEVICE ? MOUSE_DEVICE_NAME : KEYBOARD_DEVICE_NAME;
}

// ShowControlDevice (CS:6705): "DEVICE:" and the input device's name, and the help rows blanked.
void ShowControlDevice(GameState& _state)
{
  const std::uint16_t label = PrintAt(_state, DEVICE_TEXT, STATUS_POSITION);
  PrintTextModeString(_state, InputDeviceName(_state), label);
  BlankHelpRows(_state);
}

// RejectFileName (CS:68AD), after a bad name: "FILENAME ERROR", and the jump back to the menu's keys.
void RejectFileName(GameState& _state)
{
  PrintAt(_state, FILE_NAME_ERROR_TEXT, STATUS_POSITION);
}

// The disc menu's keys: DiscControlKeyLoop (CS:665E), or ShowControlDevice before it when _showDevice; and everything they run
// until the menu is left.
[[nodiscard]] Task<DiscMenuExit> RunDiscControlKeys(GameState& _state, Host& _host, bool _showDevice)
{
  bool showDevice = _showDevice;
  for (;;)
  {
    if (showDevice)
    {
      ShowControlDevice(_state);
      showDevice = false;
    }
    const KeyPress key = co_await WaitForKey(_state, _host);
    // PUSH AX and POP AX round it.
    BlankHelpRows(_state);
    const std::uint8_t scan = key.scanCode;
    if (scan == SCAN_E)
    {
      PrintAt(_state, EXIT_QUESTION_TEXT, STATUS_POSITION);
      if (co_await WaitForEitherKey(_state, _host, SCAN_N, SCAN_Y) == SCAN_N)
      {
        showDevice = true;
        continue;
      }
      co_return LeaveGameLoopForDisk(_state, EXIT_TO_DOS);
    }
    if (scan == SCAN_L || scan == SCAN_S || scan == SCAN_D)
    {
      if (!co_await PromptCommanderFileName(_state, _host))
      {
        // The prompt's return address dropped, and the menu's keys read on in its place.
        RejectFileName(_state);
        continue;
      }
      co_return LeaveGameLoopForDisk(_state, scan == SCAN_L ? LOAD_COMMANDER : scan == SCAN_S ? SAVE_COMMANDER : DELETE_COMMANDER);
    }
    if (scan == SCAN_C)
    {
      co_return LeaveGameLoopForDisk(_state, CATALOGUE_COMMANDERS);
    }
    if (scan == SCAN_K)
    {
      _state.Data().saveLoad.inputDevice = KEYBOARD_DEVICE;
      showDevice = true;
      continue;
    }
    if (scan == SCAN_M)
    {
      if (IsMouseDriverInstalled(_host))
      {
        _state.Data().saveLoad.inputDevice = MOUSE_DEVICE;
        showDevice = true;
        continue;
      }
      PrintAt(_state, NO_MOUSE_TEXT, STATUS_POSITION);
      PrintAt(_state, MOUSE_HELP_LINES[0], HELP_POSITION);
      PrintAt(_state, MOUSE_HELP_LINES[1], SECOND_HELP_POSITION); // INC SI: the second line
      continue;
    }
    if (scan == SCAN_V)
    {
      PrintAt(_state, VERSION_STRING, HELP_POSITION);
      continue;
    }
    if (scan < SCAN_F1 || scan >= SCAN_PAST_F10)
    {
      continue;
    }
    _state.Data().saveLoad.resumeAtDiskMenu = 0;
    co_return DiscMenuExit{false, scan};
  }
}

// What the disk operation Start did came to (CS:67E7): LOADED:, SAVED:, DELETED: and the name, or CATALOGUED and the catalogue;
// a loaded name becomes the default.
void ShowDiskResult(GameState& _state)
{
  PrintAt(_state, BLANK_STATUS_TEXT, STATUS_POSITION);
  // BL = diskOperation - 1, BH = 0, doubled: the operation's name in diskOperationNames. Only operations 1-4 come back to the
  // disc menu: 0 leaves the game.
  const std::uint16_t result =
    PrintTextModeString(_state, DISK_OPERATION_NAMES[static_cast<std::uint8_t>(_state.Data().saveLoad.diskOperation - 1)], STATUS_POSITION);
  if (_state.Data().saveLoad.diskOperation == CATALOGUE_COMMANDERS)
  {
    PrintAt(_state, CATALOGUE_OK_TEXT, result);
    PrintCommanderCatalogue(_state);
    return;
  }
  // The name without its extension: a NUL over its '.', which the search, SI going up, looks for.
  const std::span<std::uint8_t> name(_state.Data().saveLoad.commanderFileName);
  std::size_t letter = 1;
  for (; name[letter] != '.'; ++letter)
  {
  }
  name[letter] = 0;
  PrintAt(_state, TextIn(name), result);
  if (_state.Data().saveLoad.diskOperation != LOAD_COMMANDER)
  {
    return;
  }
  // Its eight bytes copied to defaultCommanderName by a LOOP.
  const std::span<std::uint8_t> commander(_state.Data().commander.defaultCommanderName);
  for (std::size_t from = 0; from < MOST_NAME_CHARACTERS; ++from)
  {
    commander[from] = name[from];
  }
}

// Whether AL may stand in a commander's name after its first letter: a digit or an upper-case letter.
[[nodiscard]] constexpr bool IsNameCharacter(std::uint8_t _character) noexcept
{
  return (_character >= '0' && _character <= '9') || (_character >= 'A' && _character <= 'Z');
}

} // namespace

void PerformDiskRequest(GameState& _state, Host& _host, std::uint8_t _request)
{
  // DEC AL until it reaches 0: 1 load, 2 save, 3 delete.
  switch (_request)
  {
  case LOAD_COMMANDER:
    LoadCommanderFile(_state, _host.Files());
    return;
  case SAVE_COMMANDER:
    SaveCommanderFile(_state, _host.Files(), _host.FileStampNow());
    return;
  case DELETE_COMMANDER:
    DeleteCommanderFile(_state, _host.Files());
    return;
  default:
    ListCommanderFiles(_state, _host.Files());
    return;
  }
}
void SaveStartupCommander(GameState& _state)
{
  // REP MOVSB within the data segment, a byte at a time: commanderBlock into startupCommander, the whole record, as the direction flag
  // is clear wherever the game calls it: the only STD, MaskOutsideTunnel's, has its CLD before the routine returns (change 3c).
  _state.Data().saveLoad.startupCommander = _state.Data().commander;
}

[[nodiscard]] Task<DiscMenuExit> ShowDiscControlScreen(GameState& _state, Host& _host)
{
  if (_state.Data().saveLoad.resumeAtDiskMenu == 0)
  {
    PrintAt(_state, DrawDockedFrame(_state, _host, DISC_CONTROL_FRAME), TITLE_POSITION);
    PrintCountedTextLines(_state, DISC_MENU_TEXT, MENU_POSITION);
    co_return co_await RunDiscControlKeys(_state, _host, true);
  }
  // Back from Start's disk work.
  if (_state.Data().disk.diskError == 0)
  {
    ShowDiskResult(_state);
    co_return co_await RunDiscControlKeys(_state, _host, false);
  }
  PrintAt(_state, RETRY_TEXT, STATUS_POSITION);
  if (co_await WaitForEitherKey(_state, _host, SCAN_N, SCAN_Y) == SCAN_N)
  {
    co_return co_await RunDiscControlKeys(_state, _host, true);
  }
  PrintAt(_state, BLANK_STATUS_TEXT, STATUS_POSITION);
  co_return LeaveGameLoopForDisk(_state, _state.Data().saveLoad.diskOperation);
}

[[nodiscard]] Task<bool> PromptCommanderFileName(GameState& _state, Host& _host)
{
  PrintAt(_state, FILE_NAME_PROMPT_TEXT, STATUS_POSITION);
  const std::span<std::uint8_t> name(_state.Data().saveLoad.commanderFileName);
  co_await ReadTextLine(_state, _host, name, MOST_NAME_CHARACTERS, NAME_POSITION);
  // A letter, then letters and digits, folded to upper case.
  std::size_t letter = 0;
  auto character = static_cast<std::uint8_t>(name[letter] & LOWER_CASE_BIT_CLEAR);
  if (name[letter] == 0 || character < 'A' || character > 'Z')
  {
    co_return false;
  }
  name[letter] = character;
  for (;;)
  {
    ++letter;
    character = name[letter];
    if (character == 0)
    {
      break;
    }
    if (character >= 'a' && character <= 'z')
    {
      character = static_cast<std::uint8_t>(character & LOWER_CASE_BIT_CLEAR);
    }
    if (!IsNameCharacter(character))
    {
      co_return false;
    }
    name[letter] = character;
  }
  // ".CDR" and its NUL after the name by a LOOP, SI through the constant and DI through the name.
  for (std::size_t from = 0, left = EXTENSION_BYTES;;)
  {
    name[letter] = COMMANDER_EXTENSION[from];
    ++from;
    ++letter;
    if (--left == 0)
    {
      co_return true;
    }
  }
}

void PrintCommanderCatalogue(GameState& _state)
{
  for (std::uint16_t row = 0; row < CATALOGUE_ROWS; ++row)
  {
    PrintAt(_state, BLANK_LINE_TEXT, static_cast<std::uint16_t>(CATALOGUE_POSITION + row * TEXT_ROW_BYTES));
  }
  // Name n at row n mod 10, column n / 10, 9 cells apart: CBW and DIV DL by 10, which cannot overflow, as ListCommanderFiles
  // lists at most 40. Each name follows the one before's NUL in commanderFileList, which holds the 40 names of eight characters
  // at most and their NULs.
  const std::span<const std::uint8_t> list(_state.Data().saveLoad.commanderFileList);
  std::size_t at = 0;
  const std::uint8_t count = _state.Data().saveLoad.commanderFileCount;
  for (std::uint8_t index = 0; index < count; ++index)
  {
    const auto column = static_cast<std::uint16_t>(index / CATALOGUE_ROWS);
    const auto row = static_cast<std::uint16_t>(index % CATALOGUE_ROWS);
    const auto cell = static_cast<std::uint16_t>(CATALOGUE_POSITION + column * CATALOGUE_COLUMN_BYTES + row * TEXT_ROW_BYTES);
    const std::string_view name = TextIn(list.subspan(at));
    PrintAt(_state, name, cell);
    at += name.size() + 1;
  }
}

} // namespace Elite
