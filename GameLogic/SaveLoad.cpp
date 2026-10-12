#include "pch.h"

#include "SaveLoad.h"

#include "Arithmetic.h"
#include "Docked.h"
#include "Input.h"
#include "SaveLoadTables.h"
#include "SharedTables.h"
#include "Text.h"

#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace Elite
{

namespace
{

constexpr std::uint16_t DOS_NO_MORE_FILES = 0x12;
constexpr std::uint8_t READ_ACCESS = 0;    // a file opened to read
constexpr std::uint16_t NO_ATTRIBUTES = 0; // a commander's file is neither read-only, hidden nor system

constexpr std::uint16_t FOUND_ATTRIBUTE = 0x15; // in the transfer area, after find-first or find-next
constexpr std::uint16_t FOUND_NAME = 0x1E;
constexpr std::uint8_t MOST_COMMANDER_FILES = 0x28;

// ---- The disc menu ----

// Where its loops jump back to (Hardware::LoopTurn).
constexpr std::uint16_t RETRY_KEY_LOOP = 0x6625;
constexpr std::uint16_t DISC_CONTROL_KEY_LOOP = 0x665E;
constexpr std::uint16_t EXIT_KEY_LOOP = 0x66C3;
constexpr std::uint16_t SHOW_CONTROL_DEVICE = 0x6705;
constexpr std::uint16_t JOYSTICK_KEY_LOOP = 0x6752;
constexpr std::uint16_t AMSTRAD_KEY_LOOP = 0x6777;
constexpr std::uint16_t SELECT_JOYSTICK = 0x6799;
constexpr std::uint16_t CENTER_KEY_LOOP = 0x67B2;
constexpr std::uint16_t FIND_NAME_EXTENSION = 0x681C;
constexpr std::uint16_t COPY_LOADED_NAME = 0x683B;
constexpr std::uint16_t NEXT_NAME_CHARACTER = 0x688C;
constexpr std::uint16_t APPEND_EXTENSION = 0x68C4;

// ".CDR" and its NUL, which PromptCommanderFileName appends to a name: DS:8697 in 1987, a text no symbol starts and nothing
// writes, whose bytes the reference's image holds there.
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
constexpr std::uint8_t SCAN_I = 0x17;
constexpr std::uint8_t SCAN_A = 0x1E;
constexpr std::uint8_t SCAN_S = 0x1F;
constexpr std::uint8_t SCAN_D = 0x20;
constexpr std::uint8_t SCAN_J = 0x24;
constexpr std::uint8_t SCAN_K = 0x25;
constexpr std::uint8_t SCAN_L = 0x26;
constexpr std::uint8_t SCAN_C = 0x2E;
constexpr std::uint8_t SCAN_V = 0x2F;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_M = 0x32;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_PAST_F10 = 0x45;
// The Amstrad's joystick sends 77h-7Ch; a code below 54h ends the wait for it.
constexpr std::uint8_t SCAN_PAST_KEYBOARD = 0x54;
constexpr std::uint8_t SCAN_AMSTRAD_JOYSTICK = 0x77;
constexpr std::uint8_t SCAN_PAST_AMSTRAD_JOYSTICK = 0x7D;

// diskOperation, what Start does on the menu's behalf.
constexpr std::uint8_t EXIT_TO_DOS = 0;
constexpr std::uint8_t LOAD_COMMANDER = 1;
constexpr std::uint8_t SAVE_COMMANDER = 2;
constexpr std::uint8_t DELETE_COMMANDER = 3;
constexpr std::uint8_t CATALOGUE_COMMANDERS = 4;

// inputDevice.
constexpr std::uint8_t KEYBOARD_DEVICE = 0;
constexpr std::uint8_t JOYSTICK_DEVICE = 1;
constexpr std::uint8_t MOUSE_DEVICE = 2;

constexpr std::uint8_t MOST_NAME_CHARACTERS = 8;
constexpr std::uint8_t LOWER_CASE_BIT_CLEAR = 0xDF;
constexpr auto EXTENSION_BYTES = static_cast<std::uint16_t>(COMMANDER_EXTENSION.size());
constexpr std::uint16_t CATALOGUE_ROWS = 10;
constexpr std::uint16_t CATALOGUE_COLUMN_BYTES = 0x12; // nine cells
constexpr std::uint8_t TEXT_LAYOUT_BIT = 2;            // screenLayout: the text page shows
constexpr std::uint16_t GRAPHICS_ERROR_POSITION = 0x0C90;
constexpr std::uint16_t TEXT_ERROR_POSITION = 0x0330;
constexpr std::uint16_t DISK_ERROR_CHARACTERS = 10;
constexpr std::uint16_t ERROR_INK = 0xFFFF; // colour 3

// commanderFileName up to its NUL: the name the original hands DOS in DS:DX. Every writer leaves a NUL inside it: the image's
// '12345678.CDR', ReadTextLine's eight characters at most and their NUL, the NUL after the ".CDR" appended to a name
// (PromptCommanderFileName), and the NUL over its '.' (ShowDiskResult).
[[nodiscard]] std::string_view CommanderFileName(const GameState& _state)
{
  return TextIn(_state.Data().saveLoad.commanderFileName);
}

// commanderBlock's commanderFileBytes, the .cdr file's: the buffer DS:DX names to the read that loads a commander.
[[nodiscard]] std::span<Counted<std::uint8_t>> CommanderFile(GameState& _state)
{
  return BytesOf(_state.Data().commander);
}

// The same bytes as DOS writes them out: each Counted byte is its stored byte (Counted.h).
[[nodiscard]] std::span<const std::uint8_t> CommanderFileBytes(const GameState& _state)
{
  const std::span<const Counted<std::uint8_t>> bytes = BytesOf(_state.Data().commander);
  return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

// The common failure (0x039D): DOS's _error, which is no failure when it only ran out of files.
void Fail(GameState& _state, std::uint16_t _error)
{
  if (_error != DOS_NO_MORE_FILES)
  {
    _state.Data().disk.diskError = 1;
  }
}

// A failed read or write (0x0393): the file is closed, and the failure is DOS's _error, which the original keeps on the stack
// round the close.
void CloseAndFail(GameState& _state, Hardware& _hardware, std::uint16_t _error)
{
  static_cast<void>(_hardware.CloseFile(_state.Data().disk.fileHandle));
  Fail(_state, _error);
}

// LoadCommanderFile (0x031A): only a file with no attributes, read whole into commanderBlock.
void LoadCommanderFile(GameState& _state, Hardware& _hardware)
{
  const DosFileAttributes attributes = _hardware.ReadFileAttributes(CommanderFileName(_state));
  if (attributes.answer.failed)
  {
    Fail(_state, attributes.answer.value);
    return;
  }
  if (attributes.attributes != 0)
  {
    _state.Data().disk.diskError = 1;
    return;
  }
  const DosAnswer opened = _hardware.OpenFile(CommanderFileName(_state), READ_ACCESS);
  if (opened.failed)
  {
    Fail(_state, opened.value);
    return;
  }
  _state.Data().disk.fileHandle = opened.value;
  const DosAnswer read = _hardware.ReadFile(_state.Data().disk.fileHandle, CommanderFile(_state));
  if (read.failed)
  {
    CloseAndFail(_state, _hardware, read.value);
    return;
  }
  const DosAnswer closed = _hardware.CloseFile(_state.Data().disk.fileHandle);
  if (closed.failed)
  {
    Fail(_state, closed.value);
  }
}

// SaveCommanderFile (0x0358): commanderBlock written to a new file, whose attributes are then cleared.
void SaveCommanderFile(GameState& _state, Hardware& _hardware)
{
  const DosAnswer created = _hardware.CreateFile(CommanderFileName(_state), NO_ATTRIBUTES);
  if (created.failed)
  {
    Fail(_state, created.value);
    return;
  }
  _state.Data().disk.fileHandle = created.value;
  const DosAnswer written = _hardware.WriteFile(_state.Data().disk.fileHandle, CommanderFileBytes(_state));
  if (written.failed)
  {
    CloseAndFail(_state, _hardware, written.value);
    return;
  }
  const DosAnswer closed = _hardware.CloseFile(_state.Data().disk.fileHandle);
  if (closed.failed)
  {
    Fail(_state, closed.value);
    return;
  }
  const DosAnswer cleared = _hardware.SetFileAttributes(CommanderFileName(_state), NO_ATTRIBUTES);
  if (cleared.failed)
  {
    Fail(_state, cleared.value);
  }
}

// ListCommanderFiles (0x03A8): the names of up to 40 *.cdr files with no attributes, without their extension, into
// commanderFileList. _attributes, the attributes searched for, are the caller's CX.
void ListCommanderFiles(GameState& _state, Hardware& _hardware, std::uint16_t _attributes)
{
  _state.Data().saveLoad.commanderFileCount = 0;
  _state.Data().saveLoad.commanderFileListNext = 0;
  // commanderFilePattern, "*.cdr" and its NUL.
  const std::string_view pattern(reinterpret_cast<const char*>(COMMANDER_FILE_PATTERN.data()), COMMANDER_FILE_PATTERN.size());
  DosAnswer found = _hardware.FindFirstFile(pattern, _attributes);
  while (!found.failed)
  {
    if (_state.Data().saveLoad.diskTransferArea[FOUND_ATTRIBUTE] == 0)
    {
      // The name the search found, up to its '.', at the index in commanderFileList that commanderFileListNext holds.
      const std::span<const Counted<std::uint8_t>> foundName =
        std::span<const Counted<std::uint8_t>>(_state.Data().saveLoad.diskTransferArea).subspan(FOUND_NAME);
      const std::span<Counted<std::uint8_t>> list(_state.Data().saveLoad.commanderFileList);
      std::size_t to = _state.Data().saveLoad.commanderFileListNext;
      for (std::size_t from = 0; foundName[from] != '.'; ++from)
      {
        list[to] = std::uint8_t{foundName[from]};
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
    found = _hardware.FindNextFile();
  }
  Fail(_state, found.value);
}

// DeleteCommanderFile (0x03F2).
void DeleteCommanderFile(GameState& _state, Hardware& _hardware)
{
  const DosAnswer deleted = _hardware.DeleteFile(CommanderFileName(_state));
  if (deleted.failed)
  {
    Fail(_state, deleted.value);
  }
}

// ---- The disc menu ----

// MOV SI,_text / MOV DI,_position / CALL PrintTextModeString: _text at B800:_position. Returns the cell after its last.
std::uint16_t PrintAt(GameState& _state, std::string_view _text, std::uint16_t _position)
{
  return PrintTextModeString(_state, _text, _position);
}

// CALL GetKey / JZ back: GetKey until a key comes, each empty turn ending at the jump back to CS:_loop (ADR-015). The turns carry
// nothing, as WaitForKeyPress's do. Returns the key.
KeyPress WaitForKey(GameState& _state, Hardware& _hardware, std::uint16_t _loop)
{
  for (;;)
  {
    const KeyPress key = GetKey(_state, _hardware);
    if (key.scanCode != 0)
    {
      return key;
    }
    _hardware.LoopTurn(_loop, {});
  }
}

// WaitForKey at CS:_loop until one of _first and _second comes, every other key going back there too. Returns it.
std::uint8_t WaitForEitherKey(GameState& _state, Hardware& _hardware, std::uint16_t _loop, std::uint8_t _first, std::uint8_t _second)
{
  for (;;)
  {
    const std::uint8_t scan = WaitForKey(_state, _hardware, _loop).scanCode;
    if (scan == _first || scan == _second)
    {
      return scan;
    }
    _hardware.LoopTurn(_loop, {});
  }
}

// The two help rows blanked (CS:6664 and CS:671F). Returns where the second blank stops.
std::uint16_t BlankHelpRows(GameState& _state)
{
  PrintAt(_state, BLANK_LINE_TEXT, HELP_POSITION);
  return PrintAt(_state, BLANK_LINE_TEXT, SECOND_HELP_POSITION);
}

// LeaveGameLoopForDisk (CS:7E90): diskOperation=_operation, and resumeAtDiskMenu=1, so that the menu shows what Start's disk
// work came to when it runs again. The two POP AX between its writes, which drop the return address of the call it is in and the
// one under it, so that its RET takes GameLoop's back to Start, are its callers' return (DiscMenuExit::leaves): they read the stack
// and write no memory, so the writes keep the original's order.
DiscMenuExit LeaveGameLoopForDisk(GameState& _state, std::uint8_t _operation)
{
  _state.Data().saveLoad.diskOperation = _operation;
  _state.Data().saveLoad.resumeAtDiskMenu = 1;
  return DiscMenuExit{true, 0, 0};
}

// The input device's name, which ShowControlDevice reads from inputDeviceNames: the disc menu sets inputDevice to 0, 1 or 2.
[[nodiscard]] std::string_view InputDeviceName(const GameState& _state)
{
  return INPUT_DEVICE_NAMES[_state.Data().saveLoad.inputDevice];
}

// ShowControlDevice (CS:6705): "DEVICE:" and the input device's name, and the help rows blanked.
void ShowControlDevice(GameState& _state)
{
  const std::uint16_t label = PrintAt(_state, DEVICE_TEXT, STATUS_POSITION);
  PrintTextModeString(_state, InputDeviceName(_state), label);
  BlankHelpRows(_state);
}

// The joystick could not be read (CS:67C4), or the Amstrad's never moved: the error, and the help's two lines, or only its
// first for the Amstrad's.
void ShowNoJoystick(GameState& _state)
{
  PrintAt(_state, NO_JOYSTICK_TEXT, STATUS_POSITION);
  PrintAt(_state, JOYSTICK_HELP_LINES[0], HELP_POSITION);
  if (_state.Data().saveLoad.joystickIsAmstrad == 1)
  {
    return;
  }
  // INC SI: the second line starts after the first's NUL.
  PrintAt(_state, JOYSTICK_HELP_LINES[1], SECOND_HELP_POSITION);
}

// J (CS:6743): which joystick, then the Amstrad's moves or the IBM's centre. True when it is selected, and the menu goes on at
// ShowControlDevice; false when it could not be read.
[[nodiscard]] bool ChooseJoystick(GameState& _state, Hardware& _hardware)
{
  PrintAt(_state, JOYSTICK_TYPE_TEXT, STATUS_POSITION);
  // I or A, then the title over the question: the original sets SI and DI for it before the wait.
  const bool amstrad = WaitForEitherKey(_state, _hardware, JOYSTICK_KEY_LOOP, SCAN_I, SCAN_A) == SCAN_A;
  PrintAt(_state, JOYSTICK_TITLE_TEXT, STATUS_POSITION);
  if (amstrad)
  {
    // The Amstrad's: a key from it, one of 77h-7Ch, until a key from the keyboard, below 54h, ends the wait.
    _state.Data().saveLoad.joystickIsAmstrad = 1;
    PrintAt(_state, MOVE_JOYSTICK_TEXT, HELP_POSITION);
    _state.Data().saveLoad.amstradJoystickMoved = 0;
    for (;;)
    {
      const std::uint8_t scan = WaitForKey(_state, _hardware, AMSTRAD_KEY_LOOP).scanCode;
      if (scan < SCAN_PAST_KEYBOARD)
      {
        break;
      }
      if (scan >= SCAN_AMSTRAD_JOYSTICK && scan < SCAN_PAST_AMSTRAD_JOYSTICK)
      {
        _state.Data().saveLoad.amstradJoystickMoved = 1;
      }
      _hardware.LoopTurn(AMSTRAD_KEY_LOOP, {});
    }
    return _state.Data().saveLoad.amstradJoystickMoved == 1;
  }
  // The IBM's: its centre read once a key says the stick is let go.
  _state.Data().saveLoad.joystickIsAmstrad = 0;
  PrintAt(_state, CENTER_JOYSTICK_TEXT, HELP_POSITION);
  (void)WaitForKey(_state, _hardware, CENTER_KEY_LOOP);
  const StickAxes center = ReadJoystickAxes(_state, _hardware);
  _state.Data().saveLoad.joystickCenterX = center.x;
  _state.Data().saveLoad.joystickCenterY = center.y;
  if (center.timedOut)
  {
    return false;
  }
  _hardware.LoopTurn(SELECT_JOYSTICK, {});
  return true;
}

// RejectFileName (CS:68AD), after a bad name: "FILENAME ERROR", and the jump back to the menu's keys.
void RejectFileName(GameState& _state, Hardware& _hardware)
{
  PrintAt(_state, FILE_NAME_ERROR_TEXT, STATUS_POSITION);
  _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
}

// The disc menu's keys: DiscControlKeyLoop (CS:665E), with AL _al there, or ShowControlDevice before it when _showDevice; and
// everything they run until the menu is left. Each turn back to the keys carries nothing, as WaitForKey's turns do.
DiscMenuExit RunDiscControlKeys(GameState& _state, Hardware& _hardware, bool _showDevice, std::uint8_t _al)
{
  std::uint8_t al = _al;
  bool showDevice = _showDevice;
  for (;;)
  {
    if (showDevice)
    {
      ShowControlDevice(_state);
      _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
      al = 0; // the print's NUL
      showDevice = false;
    }
    const KeyPress key = WaitForKey(_state, _hardware, DISC_CONTROL_KEY_LOOP);
    al = AlAfterKey(al, key);
    // PUSH AX and POP AX round it.
    BlankHelpRows(_state);
    const std::uint8_t scan = key.scanCode;
    if (scan == SCAN_E)
    {
      PrintAt(_state, EXIT_QUESTION_TEXT, STATUS_POSITION);
      if (WaitForEitherKey(_state, _hardware, EXIT_KEY_LOOP, SCAN_N, SCAN_Y) == SCAN_N)
      {
        _hardware.LoopTurn(SHOW_CONTROL_DEVICE, {});
        showDevice = true;
        continue;
      }
      return LeaveGameLoopForDisk(_state, EXIT_TO_DOS);
    }
    if (scan == SCAN_L || scan == SCAN_S || scan == SCAN_D)
    {
      if (!PromptCommanderFileName(_state, _hardware))
      {
        // The prompt's return address dropped, and the menu's keys read on in its place.
        RejectFileName(_state, _hardware);
        al = 0;
        continue;
      }
      return LeaveGameLoopForDisk(_state, scan == SCAN_L ? LOAD_COMMANDER : scan == SCAN_S ? SAVE_COMMANDER : DELETE_COMMANDER);
    }
    if (scan == SCAN_C)
    {
      return LeaveGameLoopForDisk(_state, CATALOGUE_COMMANDERS);
    }
    if (scan == SCAN_K)
    {
      _state.Data().saveLoad.inputDevice = KEYBOARD_DEVICE;
      showDevice = true;
      continue;
    }
    if (scan == SCAN_M)
    {
      if (IsMouseDriverInstalled(_hardware).installed)
      {
        _state.Data().saveLoad.inputDevice = MOUSE_DEVICE;
        _hardware.LoopTurn(SHOW_CONTROL_DEVICE, {});
        showDevice = true;
        continue;
      }
      PrintAt(_state, NO_MOUSE_TEXT, STATUS_POSITION);
      PrintAt(_state, MOUSE_HELP_LINES[0], HELP_POSITION);
      PrintAt(_state, MOUSE_HELP_LINES[1], SECOND_HELP_POSITION); // INC SI: the second line
      _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
      al = 0;
      continue;
    }
    if (scan == SCAN_J)
    {
      if (ChooseJoystick(_state, _hardware))
      {
        _state.Data().saveLoad.inputDevice = JOYSTICK_DEVICE;
        _hardware.LoopTurn(SHOW_CONTROL_DEVICE, {});
        showDevice = true;
        continue;
      }
      ShowNoJoystick(_state);
      _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
      al = 0;
      continue;
    }
    if (scan == SCAN_V)
    {
      PrintAt(_state, VERSION_STRING, HELP_POSITION);
      _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
      al = 0;
      continue;
    }
    if (scan < SCAN_F1 || scan >= SCAN_PAST_F10)
    {
      _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
      continue;
    }
    _state.Data().saveLoad.resumeAtDiskMenu = 0;
    return DiscMenuExit{false, scan, al};
  }
}

// What the disk operation Start did came to (CS:67E7): LOADED:, SAVED:, DELETED: and the name, or CATALOGUED and the catalogue;
// a loaded name becomes the default. Returns AL as it leaves it: its last print's NUL, or once a name is copied, its last byte.
std::uint8_t ShowDiskResult(GameState& _state, Hardware& _hardware)
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
    return 0;
  }
  // The name without its extension: a NUL over its '.', which each turn of the search, carrying SI, looks for.
  const std::span<Counted<std::uint8_t>> name(_state.Data().saveLoad.commanderFileName);
  std::size_t letter = 1;
  for (; name[letter] != '.'; ++letter)
  {
    _hardware.LoopTurn(FIND_NAME_EXTENSION, {_state.PlaceOf(name[letter])});
  }
  name[letter] = 0;
  PrintAt(_state, TextIn(name), result);
  if (_state.Data().saveLoad.diskOperation != LOAD_COMMANDER)
  {
    return 0;
  }
  // Its eight bytes copied to defaultCommanderName, the LOOP's turns carrying both places and the count.
  const std::span<Counted<std::uint8_t>> commander(_state.Data().commander.defaultCommanderName);
  std::size_t from = 0;
  std::uint8_t copied = 0;
  for (std::uint16_t left = MOST_NAME_CHARACTERS;;)
  {
    copied = name[from];
    commander[from] = copied;
    ++from;
    if (--left == 0)
    {
      return copied;
    }
    _hardware.LoopTurn(COPY_LOADED_NAME, {_state.PlaceOf(name[from]), _state.PlaceOf(commander[from]), left});
  }
}

// Whether AL may stand in a commander's name after its first letter: a digit or an upper-case letter.
[[nodiscard]] constexpr bool IsNameCharacter(std::uint8_t _character) noexcept
{
  return (_character >= '0' && _character <= '9') || (_character >= 'A' && _character <= 'Z');
}

} // namespace

void PerformDiskRequest(GameState& _state, Hardware& _hardware, std::uint8_t _request, std::uint16_t _attributes)
{
  _hardware.SetDiskTransferArea(_state.Data().saveLoad.diskTransferArea);
  // DEC AL until it reaches 0: 1 load, 2 save, 3 delete.
  switch (_request)
  {
  case LOAD_COMMANDER:
    LoadCommanderFile(_state, _hardware);
    return;
  case SAVE_COMMANDER:
    SaveCommanderFile(_state, _hardware);
    return;
  case DELETE_COMMANDER:
    DeleteCommanderFile(_state, _hardware);
    return;
  default:
    ListCommanderFiles(_state, _hardware, _attributes);
    return;
  }
}

void SaveStartupCommander(GameState& _state)
{
  // REP MOVSB within the data segment, a byte at a time: commanderBlock's bytes into startupCommander's, upwards, as the direction flag is
  // clear wherever the game calls it: the only STD, MaskOutsideTunnel's, has its CLD before the routine returns (change 3c).
  const std::span<Counted<std::uint8_t>> to(_state.Data().saveLoad.startupCommander);
  const std::span<const Counted<std::uint8_t>> from = BytesOf(std::as_const(_state).Data().commander);
  for (std::size_t index = 0; index < COMMANDER_FILE_BYTES; ++index)
  {
    to[index] = std::uint8_t{from[index]};
  }
}

void ShowDiskError(GameState& _state)
{
  _state.Data().disk.diskError = 0;
  if ((_state.Data().drawing.screenLayout & TEXT_LAYOUT_BIT) == 0)
  {
    DrawScreenString(_state, DISK_ERROR_TEXT, ERROR_INK, GameState::VIDEO_SEGMENT, GRAPHICS_ERROR_POSITION);
    return;
  }
  // The characters alone, over the text page's attributes.
  static_assert(DISK_ERROR_TEXT.size() == DISK_ERROR_CHARACTERS);
  std::uint16_t to = TEXT_ERROR_POSITION;
  for (const char character : DISK_ERROR_TEXT)
  {
    _state.SetVideoByte(to, static_cast<std::uint8_t>(character));
    to = Offset(to, 2);
  }
}

DiscMenuExit ShowDiscControlScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  if (_state.Data().saveLoad.resumeAtDiskMenu == 0)
  {
    PrintAt(_state, DrawDockedFrame(_state, _hardware, DISC_CONTROL_FRAME, _backward), TITLE_POSITION);
    PrintCountedTextLines(_state, DISC_MENU_TEXT, MENU_POSITION);
    return RunDiscControlKeys(_state, _hardware, true, 0);
  }
  // Back from Start's disk work.
  if (_state.Data().disk.diskError == 0)
  {
    const std::uint8_t al = ShowDiskResult(_state, _hardware);
    _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
    return RunDiscControlKeys(_state, _hardware, false, al);
  }
  PrintAt(_state, RETRY_TEXT, STATUS_POSITION);
  if (WaitForEitherKey(_state, _hardware, RETRY_KEY_LOOP, SCAN_N, SCAN_Y) == SCAN_N)
  {
    return RunDiscControlKeys(_state, _hardware, true, 0);
  }
  PrintAt(_state, BLANK_STATUS_TEXT, STATUS_POSITION);
  return LeaveGameLoopForDisk(_state, _state.Data().saveLoad.diskOperation);
}

bool PromptCommanderFileName(GameState& _state, Hardware& _hardware)
{
  PrintAt(_state, FILE_NAME_PROMPT_TEXT, STATUS_POSITION);
  const std::span<Counted<std::uint8_t>> name(_state.Data().saveLoad.commanderFileName);
  ReadTextLine(_state, _hardware, name, MOST_NAME_CHARACTERS, GameState::VIDEO_SEGMENT, NAME_POSITION);
  // A letter, then letters and digits, folded to upper case, each turn of the check carrying its place.
  std::size_t letter = 0;
  auto character = static_cast<std::uint8_t>(name[letter] & LOWER_CASE_BIT_CLEAR);
  if (name[letter] == 0 || character < 'A' || character > 'Z')
  {
    return false;
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
      return false;
    }
    name[letter] = character;
    _hardware.LoopTurn(NEXT_NAME_CHARACTER, {_state.PlaceOf(name[letter])});
  }
  // ".CDR" and its NUL after the name, the LOOP's turns carrying both places, SI and DI, and the count. SI goes through the
  // constant, whose place the turns carry as its index (D38).
  for (std::size_t from = 0, left = EXTENSION_BYTES;;)
  {
    name[letter] = COMMANDER_EXTENSION[from];
    ++from;
    ++letter;
    if (--left == 0)
    {
      return true;
    }
    _hardware.LoopTurn(APPEND_EXTENSION,
                       {static_cast<std::uint16_t>(from), _state.PlaceOf(name[letter]), static_cast<std::uint16_t>(left)});
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
  const std::span<const Counted<std::uint8_t>> list(_state.Data().saveLoad.commanderFileList);
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
