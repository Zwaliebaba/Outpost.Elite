#include "pch.h"

#include "SaveLoad.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Docked.h"
#include "Input.h"
#include "Text.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t DOS_NO_MORE_FILES = 0x12;
constexpr std::uint8_t IGNORE_ERROR = 0;   // what a critical-error handler answers DOS in AL
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

// Its texts, NUL-terminated. Two of them run on into a second line, which the menu prints with INC SI.
constexpr std::uint16_t RETRY_TEXT = 0x86B0;            // "ERROR: Retry (Y/N)?"
constexpr std::uint16_t BLANK_STATUS_TEXT = 0x86C4;     // 20 spaces
constexpr std::uint16_t EXIT_QUESTION_TEXT = 0x869C;    // "Are you sure (Y/N)?"
constexpr std::uint16_t DEVICE_TEXT = 0x8667;           // "DEVICE: "
constexpr std::uint16_t FILE_NAME_PROMPT_TEXT = 0x8676; // "FILENAME?"
constexpr std::uint16_t COMMANDER_EXTENSION = 0x8697;   // ".CDR"
constexpr std::uint16_t CATALOGUE_OK_TEXT = 0x8708;     // "OK"
constexpr std::uint16_t NO_MOUSE_TEXT = 0x870B;         // "ERROR: No Mouse!"
constexpr std::uint16_t MOUSE_HELP_TEXT = 0x8720;       // two lines
constexpr std::uint16_t NO_JOYSTICK_TEXT = 0x875F;      // "ERROR: No Joystick!"
constexpr std::uint16_t JOYSTICK_HELP_TEXT = 0x8774;    // two lines
constexpr std::uint16_t JOYSTICK_TITLE_TEXT = 0x87BA;   // "Joystick Detection"
constexpr std::uint16_t MOVE_JOYSTICK_TEXT = 0x87CF;
constexpr std::uint16_t CENTER_JOYSTICK_TEXT = 0x87F0;
constexpr std::uint16_t BLANK_LINE_TEXT = 0x8811;      // 36 spaces
constexpr std::uint16_t JOYSTICK_TYPE_TEXT = 0x8838;   // "I..IBM  A..Amstrad ?"
constexpr std::uint16_t FILE_NAME_ERROR_TEXT = 0x884E; // "FILENAME ERROR"

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
constexpr std::uint16_t EXTENSION_BYTES = 5; // ".CDR" and its NUL
constexpr std::uint16_t CATALOGUE_ROWS = 10;
constexpr std::uint16_t CATALOGUE_COLUMN_BYTES = 0x12; // nine cells
constexpr std::uint8_t TEXT_LAYOUT_BIT = 2;            // screenLayout: the text page shows
constexpr std::uint16_t GRAPHICS_ERROR_POSITION = 0x0C90;
constexpr std::uint16_t TEXT_ERROR_POSITION = 0x0330;
constexpr std::uint16_t DISK_ERROR_CHARACTERS = 10;
constexpr std::uint16_t ERROR_INK = 0xFFFF; // colour 3

// The common failure (0x039D): DOS's _error, which is no failure when it only ran out of files.
void Fail(GameState& _state, std::uint16_t _error)
{
  if (_error != DOS_NO_MORE_FILES)
  {
    _state.Set(DS.diskError, 1);
  }
}

// A failed read or write (0x0393): the file is closed, and the failure is DOS's _error, which the original keeps on the stack
// round the close.
void CloseAndFail(GameState& _state, Hardware& _hardware, std::uint16_t _error)
{
  static_cast<void>(_hardware.CloseFile(_state.Get(DS.fileHandle)));
  Fail(_state, _error);
}

// LoadCommanderFile (0x031A): only a file with no attributes, read whole into commanderBlock.
void LoadCommanderFile(GameState& _state, Hardware& _hardware)
{
  const std::uint16_t data = _state.DataSegment();
  const DosFileAttributes attributes = _hardware.ReadFileAttributes(data, DS.commanderFileName.offset);
  if (attributes.answer.failed)
  {
    Fail(_state, attributes.answer.value);
    return;
  }
  if (attributes.attributes != 0)
  {
    _state.Set(DS.diskError, 1);
    return;
  }
  const DosAnswer opened = _hardware.OpenFile(data, DS.commanderFileName.offset, READ_ACCESS);
  if (opened.failed)
  {
    Fail(_state, opened.value);
    return;
  }
  _state.Set(DS.fileHandle, opened.value);
  const DosAnswer read = _hardware.ReadFile(_state.Get(DS.fileHandle), data, DS.commanderBlock.offset, _state.Get(DS.commanderFileBytes));
  if (read.failed)
  {
    CloseAndFail(_state, _hardware, read.value);
    return;
  }
  const DosAnswer closed = _hardware.CloseFile(_state.Get(DS.fileHandle));
  if (closed.failed)
  {
    Fail(_state, closed.value);
  }
}

// SaveCommanderFile (0x0358): commanderBlock written to a new file, whose attributes are then cleared.
void SaveCommanderFile(GameState& _state, Hardware& _hardware)
{
  const std::uint16_t data = _state.DataSegment();
  const DosAnswer created = _hardware.CreateFile(data, DS.commanderFileName.offset, NO_ATTRIBUTES);
  if (created.failed)
  {
    Fail(_state, created.value);
    return;
  }
  _state.Set(DS.fileHandle, created.value);
  const DosAnswer written =
    _hardware.WriteFile(_state.Get(DS.fileHandle), data, DS.commanderBlock.offset, _state.Get(DS.commanderFileBytes));
  if (written.failed)
  {
    CloseAndFail(_state, _hardware, written.value);
    return;
  }
  const DosAnswer closed = _hardware.CloseFile(_state.Get(DS.fileHandle));
  if (closed.failed)
  {
    Fail(_state, closed.value);
    return;
  }
  const DosAnswer cleared = _hardware.SetFileAttributes(data, DS.commanderFileName.offset, NO_ATTRIBUTES);
  if (cleared.failed)
  {
    Fail(_state, cleared.value);
  }
}

// ListCommanderFiles (0x03A8): the names of up to 40 *.cdr files with no attributes, without their extension, into
// commanderFileList. _attributes, the attributes searched for, are the caller's CX.
void ListCommanderFiles(GameState& _state, Hardware& _hardware, std::uint16_t _attributes)
{
  _state.Set(DS.commanderFileCount, 0);
  _state.Set(DS.commanderFileListNext, DS.commanderFileList.offset);
  DosAnswer found = _hardware.FindFirstFile(_state.DataSegment(), DS.commanderFilePattern.offset, _attributes);
  while (!found.failed)
  {
    if (_state.Byte(DS.diskTransferArea.At(FOUND_ATTRIBUTE)) == 0)
    {
      std::uint16_t from = DS.diskTransferArea.At(FOUND_NAME);
      std::uint16_t to = _state.Get(DS.commanderFileListNext);
      for (std::uint8_t character = _state.Byte(from); character != '.'; character = _state.Byte(from))
      {
        from = Offset(from, 1);
        _state.SetByte(to, character);
        to = Offset(to, 1);
      }
      _state.SetByte(to, 0);
      _state.Set(DS.commanderFileListNext, Offset(to, 1));
      const auto count = static_cast<std::uint8_t>(_state.Get(DS.commanderFileCount) + 1);
      _state.Set(DS.commanderFileCount, count);
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
  const DosAnswer deleted = _hardware.DeleteFile(_state.DataSegment(), DS.commanderFileName.offset);
  if (deleted.failed)
  {
    Fail(_state, deleted.value);
  }
}

// ---- The disc menu ----

// MOV SI,_text / MOV DI,_position / CALL PrintTextModeString: the text at DS:_text at B800:_position.
PrintedText PrintAt(GameState& _state, std::uint16_t _text, std::uint16_t _position)
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
PrintedText BlankHelpRows(GameState& _state)
{
  PrintAt(_state, BLANK_LINE_TEXT, HELP_POSITION);
  return PrintAt(_state, BLANK_LINE_TEXT, SECOND_HELP_POSITION);
}

// LeaveGameLoopForDisk (CS:7E90): diskOperation=_operation, and resumeAtDiskMenu=1, so that the menu shows what Start's disk
// work came to when it runs again. The two POP AX between its writes, which drop the return address of the call it is in and the
// one under it, so that its RET takes GameLoop's back to Start, are the entries' (DiscMenuExitOut): they read the stack and write
// no memory, so the writes keep the original's order.
DiscMenuExit LeaveGameLoopForDisk(GameState& _state, std::uint8_t _operation)
{
  _state.Set(DS.diskOperation, _operation);
  _state.Set(DS.resumeAtDiskMenu, 1);
  return DiscMenuExit{true, 0, 0};
}

// Where ShowControlDevice reads the input device's name from, in inputDeviceNames.
[[nodiscard]] std::uint16_t InputDeviceName(const GameState& _state)
{
  return static_cast<std::uint16_t>((_state.Get(DS.inputDevice) << 1) + DS.inputDeviceNames.offset);
}

// ShowControlDevice (CS:6705): "DEVICE:" and the input device's name, and the help rows blanked. Returns where the second
// blank stops.
PrintedText ShowControlDevice(GameState& _state)
{
  const PrintedText label = PrintAt(_state, DEVICE_TEXT, STATUS_POSITION);
  PrintAt(_state, _state.Word(InputDeviceName(_state)), label.nextCell);
  return BlankHelpRows(_state);
}

// The joystick could not be read (CS:67C4), or the Amstrad's never moved: the error, and the help's two lines, or only its
// first for the Amstrad's. Returns where the last print stops.
PrintedText ShowNoJoystick(GameState& _state)
{
  PrintAt(_state, NO_JOYSTICK_TEXT, STATUS_POSITION);
  const PrintedText help = PrintAt(_state, JOYSTICK_HELP_TEXT, HELP_POSITION);
  if (_state.Get(DS.joystickIsAmstrad) == 1)
  {
    return help;
  }
  // INC SI: the second line starts after the first's NUL.
  return PrintAt(_state, Offset(help.end, 1), SECOND_HELP_POSITION);
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
    _state.Set(DS.joystickIsAmstrad, 1);
    PrintAt(_state, MOVE_JOYSTICK_TEXT, HELP_POSITION);
    _state.Set(DS.amstradJoystickMoved, 0);
    for (;;)
    {
      const std::uint8_t scan = WaitForKey(_state, _hardware, AMSTRAD_KEY_LOOP).scanCode;
      if (scan < SCAN_PAST_KEYBOARD)
      {
        break;
      }
      if (scan >= SCAN_AMSTRAD_JOYSTICK && scan < SCAN_PAST_AMSTRAD_JOYSTICK)
      {
        _state.Set(DS.amstradJoystickMoved, 1);
      }
      _hardware.LoopTurn(AMSTRAD_KEY_LOOP, {});
    }
    return _state.Get(DS.amstradJoystickMoved) == 1;
  }
  // The IBM's: its centre read once a key says the stick is let go. ReadJoystickAxes fires the stick with AL as GetKey leaves
  // it, from the print's NUL.
  _state.Set(DS.joystickIsAmstrad, 0);
  PrintAt(_state, CENTER_JOYSTICK_TEXT, HELP_POSITION);
  const KeyPress key = WaitForKey(_state, _hardware, CENTER_KEY_LOOP);
  const StickAxes center = ReadJoystickAxes(_state, _hardware, AlAfterKey(0, key));
  _state.Set(DS.joystickCenterX, center.x);
  _state.Set(DS.joystickCenterY, center.y);
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
      _state.Set(DS.inputDevice, KEYBOARD_DEVICE);
      showDevice = true;
      continue;
    }
    if (scan == SCAN_M)
    {
      if (IsMouseDriverInstalled(_state).installed)
      {
        _state.Set(DS.inputDevice, MOUSE_DEVICE);
        _hardware.LoopTurn(SHOW_CONTROL_DEVICE, {});
        showDevice = true;
        continue;
      }
      PrintAt(_state, NO_MOUSE_TEXT, STATUS_POSITION);
      const PrintedText help = PrintAt(_state, MOUSE_HELP_TEXT, HELP_POSITION);
      PrintAt(_state, Offset(help.end, 1), SECOND_HELP_POSITION); // INC SI: the second line
      _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
      al = 0;
      continue;
    }
    if (scan == SCAN_J)
    {
      if (ChooseJoystick(_state, _hardware))
      {
        _state.Set(DS.inputDevice, JOYSTICK_DEVICE);
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
      PrintAt(_state, DS.versionString.offset, HELP_POSITION);
      _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
      al = 0;
      continue;
    }
    if (scan < SCAN_F1 || scan >= SCAN_PAST_F10)
    {
      _hardware.LoopTurn(DISC_CONTROL_KEY_LOOP, {});
      continue;
    }
    _state.Set(DS.resumeAtDiskMenu, 0);
    return DiscMenuExit{false, scan, al};
  }
}

// What the disk operation Start did came to (CS:67E7): LOADED:, SAVED:, DELETED: and the name, or CATALOGUED and the catalogue;
// a loaded name becomes the default. Returns AL as it leaves it: its last print's NUL, or once a name is copied, its last byte.
std::uint8_t ShowDiskResult(GameState& _state, Hardware& _hardware)
{
  PrintAt(_state, BLANK_STATUS_TEXT, STATUS_POSITION);
  // BL = diskOperation - 1, BH = 0, doubled: the operation's name in diskOperationNames.
  const auto name =
    static_cast<std::uint16_t>((static_cast<std::uint8_t>(_state.Get(DS.diskOperation) - 1) << 1) + DS.diskOperationNames.offset);
  const PrintedText result = PrintAt(_state, _state.Word(name), STATUS_POSITION);
  if (_state.Get(DS.diskOperation) == CATALOGUE_COMMANDERS)
  {
    PrintAt(_state, CATALOGUE_OK_TEXT, result.nextCell);
    PrintCommanderCatalogue(_state);
    return 0;
  }
  // The name without its extension: a NUL over its '.', which each turn of the search, carrying SI, looks for.
  std::uint16_t letter = DS.commanderFileName.offset;
  for (;;)
  {
    letter = Offset(letter, 1);
    if (_state.Byte(letter) == '.')
    {
      break;
    }
    _hardware.LoopTurn(FIND_NAME_EXTENSION, {letter});
  }
  _state.SetByte(letter, 0);
  PrintAt(_state, DS.commanderFileName.offset, result.nextCell);
  if (_state.Get(DS.diskOperation) != LOAD_COMMANDER)
  {
    return 0;
  }
  // Its eight bytes copied to defaultCommanderName, the LOOP's turns carrying both places and the count.
  std::uint16_t from = DS.commanderFileName.offset;
  std::uint16_t to = DS.defaultCommanderName.offset;
  std::uint8_t copied = 0;
  for (std::uint16_t left = MOST_NAME_CHARACTERS;;)
  {
    copied = _state.Byte(from);
    _state.SetByte(to, copied);
    from = Offset(from, 1);
    to = Offset(to, 1);
    if (--left == 0)
    {
      return copied;
    }
    _hardware.LoopTurn(COPY_LOADED_NAME, {from, to, left});
  }
}

// Whether AL may stand in a commander's name after its first letter: a digit or an upper-case letter.
[[nodiscard]] constexpr bool IsNameCharacter(std::uint8_t _character) noexcept
{
  return (_character >= '0' && _character <= '9') || (_character >= 'A' && _character <= 'Z');
}

// What the disc menu leaves when it is done, as its entries hand it back: AX the F-key it returns with, and AL as its GetKey left
// it; or, once it leaves for the disk, the second of the two return addresses LeaveGameLoopForDisk pops; and ES on the text page,
// from its prints.
void DiscMenuExitOut(Guest& _guest, const DiscMenuExit& _exit)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = Guest::VIDEO_SEGMENT;
  if (_exit.leaves)
  {
    regs.ax = _guest.Pop();
    regs.ax = _guest.Pop();
    return;
  }
  regs.ax = Join(_exit.scanCode, _exit.al);
}

} // namespace

void PerformDiskRequest(GameState& _state, Hardware& _hardware, std::uint8_t _request, std::uint16_t _attributes)
{
  _hardware.SetDiskTransferArea(_state.DataSegment(), DS.diskTransferArea.offset);
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

void SaveStartupCommander(GameState& _state, bool _backward)
{
  // REP MOVSB within the data segment, a byte at a time.
  const auto step = static_cast<std::uint16_t>(_backward ? 0xFFFF : 1);
  std::uint16_t from = DS.commanderBlock.offset;
  std::uint16_t to = DS.startupCommander.offset;
  for (std::uint16_t left = _state.Get(DS.commanderFileBytes); left != 0; --left)
  {
    _state.SetByte(to, _state.Byte(from));
    from = Offset(from, step);
    to = Offset(to, step);
  }
}

std::uint8_t CriticalErrorInterrupt(GameState& _state)
{
  _state.Set(DS.diskError, 1);
  return IGNORE_ERROR;
}

void ShowDiskError(GameState& _state)
{
  _state.Set(DS.diskError, 0);
  if ((_state.Get(DS.screenLayout) & TEXT_LAYOUT_BIT) == 0)
  {
    DrawScreenString(_state, DS.diskErrorText.offset, ERROR_INK, GameState::VIDEO_SEGMENT, GRAPHICS_ERROR_POSITION);
    return;
  }
  // The characters alone, over the text page's attributes.
  std::uint16_t from = DS.diskErrorText.offset;
  std::uint16_t to = TEXT_ERROR_POSITION;
  for (std::uint16_t left = DISK_ERROR_CHARACTERS; left != 0; --left)
  {
    _state.SetVideoByte(to, _state.Byte(from));
    from = Offset(from, 1);
    to = Offset(to, 2);
  }
}

DiscMenuExit ShowDiscControlScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  if (_state.Get(DS.resumeAtDiskMenu) == 0)
  {
    PrintAt(_state, DrawDockedFrame(_state, _hardware, DS.discControlFrame.offset, _backward), TITLE_POSITION);
    PrintCountedTextLines(_state, DS.discMenuText.offset, MENU_POSITION);
    return RunDiscControlKeys(_state, _hardware, true, 0);
  }
  // Back from Start's disk work.
  if (_state.Get(DS.diskError) == 0)
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
  return LeaveGameLoopForDisk(_state, _state.Get(DS.diskOperation));
}

bool PromptCommanderFileName(GameState& _state, Hardware& _hardware)
{
  PrintAt(_state, FILE_NAME_PROMPT_TEXT, STATUS_POSITION);
  (void)ReadTextLine(_state, _hardware, DS.commanderFileName.offset, MOST_NAME_CHARACTERS, GameState::VIDEO_SEGMENT, NAME_POSITION);
  // A letter, then letters and digits, folded to upper case, each turn of the check carrying its place.
  std::uint16_t letter = DS.commanderFileName.offset;
  auto character = static_cast<std::uint8_t>(_state.Byte(letter) & LOWER_CASE_BIT_CLEAR);
  if (_state.Byte(letter) == 0 || character < 'A' || character > 'Z')
  {
    return false;
  }
  _state.SetByte(letter, character);
  for (;;)
  {
    letter = Offset(letter, 1);
    character = _state.Byte(letter);
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
    _state.SetByte(letter, character);
    _hardware.LoopTurn(NEXT_NAME_CHARACTER, {letter});
  }
  // ".CDR" and its NUL after the name, the LOOP's turns carrying both places and the count.
  std::uint16_t extension = COMMANDER_EXTENSION;
  for (std::uint16_t left = EXTENSION_BYTES;;)
  {
    const std::uint8_t byte = _state.Byte(extension);
    extension = Offset(extension, 1);
    _state.SetByte(letter, byte);
    letter = Offset(letter, 1);
    if (--left == 0)
    {
      return true;
    }
    _hardware.LoopTurn(APPEND_EXTENSION, {extension, letter, left});
  }
}

void PrintCommanderCatalogue(GameState& _state)
{
  for (std::uint16_t row = 0; row < CATALOGUE_ROWS; ++row)
  {
    PrintAt(_state, BLANK_LINE_TEXT, static_cast<std::uint16_t>(CATALOGUE_POSITION + row * TEXT_ROW_BYTES));
  }
  // Name n at row n mod 10, column n / 10, 9 cells apart: CBW and DIV DL by 10, which cannot overflow, as ListCommanderFiles
  // lists at most 40.
  std::uint16_t name = DS.commanderFileList.offset;
  const std::uint8_t count = _state.Get(DS.commanderFileCount);
  for (std::uint8_t index = 0; index < count; ++index)
  {
    const auto column = static_cast<std::uint16_t>(index / CATALOGUE_ROWS);
    const auto row = static_cast<std::uint16_t>(index % CATALOGUE_ROWS);
    const auto cell = static_cast<std::uint16_t>(CATALOGUE_POSITION + column * CATALOGUE_COLUMN_BYTES + row * TEXT_ROW_BYTES);
    name = Offset(PrintAt(_state, name, cell).end, 1);
  }
}

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract DISK_REQUEST{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI, 0};
constexpr Machine::NativeContract COPY{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI | REGISTER_ES, 0};

constexpr Machine::NativeContract SHOWS_DISK_ERROR{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_GENERAL{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0};
constexpr Machine::NativeContract PROMPTS_FOR_NAME{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_ES, Machine::FLAG_CARRY};
// The disc menu's: the general registers but AX, the key it returns with in AH.
constexpr Machine::NativeContract DISC_MENU{REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0};

// The disc menu and its file name prompt wait for keys as a rule: they run on the native thread, and the digests
// accept them (ADR-010 items 5 and 8).
constexpr Machine::NativeWait WAITS = Machine::NativeWait::Always;

} // namespace

// ── The entries of the de-assembled routines ──

void CriticalErrorInterruptEntry(Guest& _guest)
{
  // DS pushed, MOV AX,data / MOV DS,AX, and XOR AL,AL after the write, then DS popped: AH is left the data segment's high
  // byte.
  _guest.Regs().ax = Join(High(_guest.DataSegment()), CriticalErrorInterrupt(_guest.State()));
  _guest.Clobber(PRESERVES_ALL);
}

void PerformDiskRequestEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  PerformDiskRequest(_guest.State(), _guest.Devices(), Low(regs.ax), regs.cx);
  _guest.Clobber(DISK_REQUEST);
}

void ShowDiskErrorEntry(Guest& _guest)
{
  ShowDiskError(_guest.State());
  _guest.Regs().es = Guest::VIDEO_SEGMENT;
  _guest.Clobber(SHOWS_DISK_ERROR);
}

void ShowDiscControlScreenEntry(Guest& _guest)
{
  DiscMenuExitOut(_guest, ShowDiscControlScreen(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION)));
  _guest.Clobber(DISC_MENU);
}

void PromptCommanderFileNameEntry(Guest& _guest)
{
  if (PromptCommanderFileName(_guest.State(), _guest.Devices()))
  {
    _guest.SetFlag(Machine::FLAG_CARRY, false);
    _guest.Clobber(PROMPTS_FOR_NAME);
    return;
  }
  // A bad name: RejectFileName drops this call's return address, and the disc menu's keys read on in its place, to leave as
  // ShowDiscControlScreen does.
  _guest.Regs().ax = _guest.Pop();
  RejectFileName(_guest.State(), _guest.Devices());
  DiscMenuExitOut(_guest, RunDiscControlKeys(_guest.State(), _guest.Devices(), false, 0));
  _guest.Clobber(DISC_MENU);
}

void PrintCommanderCatalogueEntry(Guest& _guest)
{
  PrintCommanderCatalogue(_guest.State());
  // PrintTextModeString's ES, on the text page: the contract keeps ES.
  _guest.Regs().es = Guest::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_GENERAL);
}

void SaveStartupCommanderEntry(Guest& _guest)
{
  SaveStartupCommander(_guest.State(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(COPY);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x02F0, "CriticalErrorInterrupt", &CriticalErrorInterruptEntry, PRESERVES_ALL, Machine::NativeReturn::Interrupt},
  NativeEntry{0x02FF, "PerformDiskRequest", &PerformDiskRequestEntry, DISK_REQUEST},
  NativeEntry{0x0470, "ShowDiskError", &ShowDiskErrorEntry, SHOWS_DISK_ERROR},
  NativeEntry{0x4660, "SaveStartupCommander", &SaveStartupCommanderEntry, COPY},
  NativeEntry{0x660B, "ShowDiscControlScreen", &ShowDiscControlScreenEntry, DISC_MENU, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x6862, "PromptCommanderFileName", &PromptCommanderFileNameEntry, PROMPTS_FOR_NAME, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x68CE, "PrintCommanderCatalogue", &PrintCommanderCatalogueEntry, CLOBBERS_GENERAL},
};

} // namespace

std::span<const NativeEntry> SaveLoadEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
