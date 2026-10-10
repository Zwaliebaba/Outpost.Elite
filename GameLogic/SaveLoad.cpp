#include "pch.h"

#include "SaveLoad.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Maths.h"
#include "Text.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t DOS_NO_MORE_FILES = 0x12;
constexpr std::uint8_t IGNORE_ERROR = 0; // what a critical-error handler answers DOS in AL

constexpr std::uint16_t FOUND_ATTRIBUTE = 0x15; // in the transfer area, after find-first or find-next
constexpr std::uint16_t FOUND_NAME = 0x1E;
constexpr std::uint8_t MOST_COMMANDER_FILES = 0x28;

// ---- The disc menu ----

// What the disc menu calls, through the hooks.
constexpr std::uint16_t IS_MOUSE_DRIVER_INSTALLED = 0x02D4;
constexpr std::uint16_t PRINT_TEXT_MODE_STRING = 0x60D2;
constexpr std::uint16_t PRINT_COUNTED_TEXT_LINES = 0x65FA;
constexpr std::uint16_t PROMPT_COMMANDER_FILE_NAME = 0x6862;
constexpr std::uint16_t PRINT_COMMANDER_CATALOGUE = 0x68CE;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t READ_TEXT_LINE = 0x7694;
constexpr std::uint16_t READ_JOYSTICK_AXES = 0x777E;
constexpr std::uint16_t DRAW_DOCKED_FRAME = 0x7C88;

// Where its loops jump back to.
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
constexpr std::uint8_t TEXT_LAYOUT_BIT = 2; // screenLayout: the text page shows
constexpr std::uint16_t GRAPHICS_ERROR_POSITION = 0x0C90;
constexpr std::uint16_t TEXT_ERROR_POSITION = 0x0330;
constexpr std::uint16_t DISK_ERROR_CHARACTERS = 10;
constexpr std::uint16_t ERROR_INK = 0xFFFF; // colour 3

// What a DOS file service leaves in the registers, AX and CF, as the register code that calls it has them after its INT 21h.
// True when the service failed.
bool LeaveDosAnswer(Machine::Registers& _regs, DosAnswer _answer) noexcept
{
  _regs.ax = _answer.value;
  const auto carry = static_cast<std::uint16_t>(_regs.flags & ~Machine::FLAG_CARRY);
  _regs.flags = _answer.failed ? static_cast<std::uint16_t>(carry | Machine::FLAG_CARRY) : carry;
  return _answer.failed;
}

// The common failure (0x039D): DOS's _error, which is no failure when it only ran out of files.
void Fail(GameState& _state, std::uint16_t _error)
{
  if (_error != DOS_NO_MORE_FILES)
  {
    _state.Set(DS.diskError, 1);
  }
}

// A failed read or write (0x0393): the file is closed, keeping the error.
void CloseAndFail(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t error = regs.ax;
  regs.bx = _guest.Get(DS.fileHandle);
  static_cast<void>(LeaveDosAnswer(regs, _guest.Devices().CloseFile(regs.bx)));
  regs.ax = error;
  Fail(_guest.State(), regs.ax);
}

// LoadCommanderFile (0x031A): only a file with no attributes, read whole into commanderBlock.
void LoadCommanderFile(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  Hardware& dos = _guest.Devices();
  const std::uint16_t data = _guest.DataSegment();
  regs.dx = DS.commanderFileName.offset;
  regs.ax = WithLow(regs.ax, 0);
  const DosFileAttributes attributes = dos.ReadFileAttributes(data, regs.dx);
  regs.cx = attributes.attributes;
  if (LeaveDosAnswer(regs, attributes.answer))
  {
    Fail(_guest.State(), regs.ax);
    return;
  }
  if (regs.cx != 0)
  {
    _guest.Set(DS.diskError, 1);
    return;
  }
  regs.dx = DS.commanderFileName.offset;
  regs.ax = WithLow(regs.ax, 0);
  if (LeaveDosAnswer(regs, dos.OpenFile(data, regs.dx, Low(regs.ax))))
  {
    Fail(_guest.State(), regs.ax);
    return;
  }
  _guest.Set(DS.fileHandle, regs.ax);
  regs.bx = _guest.Get(DS.fileHandle);
  regs.cx = _guest.Get(DS.commanderFileBytes);
  regs.dx = DS.commanderBlock.offset;
  if (LeaveDosAnswer(regs, dos.ReadFile(regs.bx, data, regs.dx, regs.cx)))
  {
    CloseAndFail(_guest);
    return;
  }
  regs.bx = _guest.Get(DS.fileHandle);
  if (LeaveDosAnswer(regs, dos.CloseFile(regs.bx)))
  {
    Fail(_guest.State(), regs.ax);
  }
}

// SaveCommanderFile (0x0358): commanderBlock written to a new file, whose attributes are then cleared.
void SaveCommanderFile(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  Hardware& dos = _guest.Devices();
  const std::uint16_t data = _guest.DataSegment();
  regs.dx = DS.commanderFileName.offset;
  regs.cx = 0;
  if (LeaveDosAnswer(regs, dos.CreateFile(data, regs.dx, regs.cx)))
  {
    Fail(_guest.State(), regs.ax);
    return;
  }
  _guest.Set(DS.fileHandle, regs.ax);
  regs.bx = _guest.Get(DS.fileHandle);
  regs.cx = _guest.Get(DS.commanderFileBytes);
  regs.dx = DS.commanderBlock.offset;
  if (LeaveDosAnswer(regs, dos.WriteFile(regs.bx, data, regs.dx, regs.cx)))
  {
    CloseAndFail(_guest);
    return;
  }
  regs.bx = _guest.Get(DS.fileHandle);
  if (LeaveDosAnswer(regs, dos.CloseFile(regs.bx)))
  {
    Fail(_guest.State(), regs.ax);
    return;
  }
  regs.dx = DS.commanderFileName.offset;
  regs.cx = 0;
  regs.ax = WithLow(regs.ax, 1);
  if (LeaveDosAnswer(regs, dos.SetFileAttributes(data, regs.dx, regs.cx)))
  {
    Fail(_guest.State(), regs.ax);
  }
}

// ListCommanderFiles (0x03A8): the names of up to 40 *.cdr files with no attributes, without their extension, into
// commanderFileList. CX, the attributes searched for, is the caller's.
void ListCommanderFiles(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  Hardware& dos = _guest.Devices();
  _guest.Set(DS.commanderFileCount, 0);
  _guest.Set(DS.commanderFileListNext, DS.commanderFileList.offset);
  regs.dx = DS.commanderFilePattern.offset;
  if (LeaveDosAnswer(regs, dos.FindFirstFile(_guest.DataSegment(), regs.dx, regs.cx)))
  {
    Fail(_guest.State(), regs.ax);
    return;
  }
  for (;;)
  {
    regs.di = DS.diskTransferArea.At(FOUND_ATTRIBUTE);
    if (_guest.Byte(regs.di) == 0)
    {
      regs.di = DS.diskTransferArea.At(FOUND_NAME);
      regs.si = _guest.Get(DS.commanderFileListNext);
      for (;;)
      {
        regs.ax = WithLow(regs.ax, _guest.Byte(regs.di));
        ++regs.di;
        if (static_cast<std::uint8_t>(regs.ax) == '.')
        {
          break;
        }
        _guest.SetByte(regs.si, static_cast<std::uint8_t>(regs.ax));
        ++regs.si;
      }
      _guest.SetByte(regs.si, 0);
      ++regs.si;
      _guest.Set(DS.commanderFileListNext, regs.si);
      const auto count = static_cast<std::uint8_t>(_guest.Get(DS.commanderFileCount) + 1);
      _guest.Set(DS.commanderFileCount, count);
      if (count == MOST_COMMANDER_FILES)
      {
        return;
      }
    }
    if (LeaveDosAnswer(regs, dos.FindNextFile()))
    {
      Fail(_guest.State(), regs.ax);
      return;
    }
  }
}

// DeleteCommanderFile (0x03F2).
void DeleteCommanderFile(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = DS.commanderFileName.offset;
  if (LeaveDosAnswer(regs, _guest.Devices().DeleteFile(_guest.DataSegment(), regs.dx)))
  {
    Fail(_guest.State(), regs.ax);
  }
}

// ---- The disc menu ----

// MOV SI,_text / MOV DI,_position / CALL PrintTextModeString: the text at DS:_text at B800:_position.
PrintedText PrintAt(GameState& _state, std::uint16_t _text, std::uint16_t _position)
{
  return PrintTextModeString(_state, _text, _position);
}

// PrintAt, and the registers as the original leaves them: PrintTextModeString's SI, DI and ES, and AX the attribute
// it printed in with the NUL in AL.
void PrintAtOnRegisters(Guest& _guest, std::uint16_t _text, std::uint16_t _position)
{
  Machine::Registers& regs = _guest.Regs();
  const PrintedText printed = PrintAt(_guest.State(), _text, _position);
  regs.si = printed.end;
  regs.di = printed.nextCell;
  regs.es = Guest::VIDEO_SEGMENT;
  regs.ax = Join(_guest.Get(DS.textAttribute), 0);
}

// CALL GetKey / JZ back: the wait for a key at CS:_loop. Out: AH the key's scan code.
void WaitForKey(Guest& _guest, std::uint16_t _loop)
{
  for (;;)
  {
    _guest.Call(GET_KEY);
    if (!_guest.Flag(Machine::FLAG_ZERO))
    {
      return;
    }
    _guest.JumpBack(_loop);
  }
}

// The two help rows blanked (CS:6664 and CS:671F).
void BlankHelpRows(Guest& _guest)
{
  PrintAtOnRegisters(_guest, BLANK_LINE_TEXT, HELP_POSITION);
  PrintAtOnRegisters(_guest, BLANK_LINE_TEXT, SECOND_HELP_POSITION);
}

// LeaveGameLoopForDisk (CS:7E90): diskOperation=_operation, and resumeAtDiskMenu=1, so that the menu shows what Start's disk
// work came to when it runs again.
void LeaveGameLoopForDisk(GameState& _state, std::uint8_t _operation)
{
  _state.Set(DS.diskOperation, _operation);
  _state.Set(DS.resumeAtDiskMenu, 1);
}

// LeaveGameLoopForDisk as the disc menu's register code reaches it, by a jump with AL the operation, and the two POP AX between
// its writes, which drop the return address of the call it is in and the one under it, so that its RET takes GameLoop's back to
// Start, which does the disk work. The pops read the stack and write no memory, so the writes keep the original's order.
void LeaveGameLoopForDiskOnRegisters(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  LeaveGameLoopForDisk(_guest.State(), Low(regs.ax));
  regs.ax = _guest.Pop();
  regs.ax = _guest.Pop();
}

// ShowControlDevice (CS:6705): "DEVICE:" and the input device's name, and the help rows blanked.
void ShowControlDevice(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PrintAtOnRegisters(_guest, DEVICE_TEXT, STATUS_POSITION);
  regs.bx = Join(0, _guest.Get(DS.inputDevice));
  regs.bx = static_cast<std::uint16_t>((regs.bx << 1) + DS.inputDeviceNames.offset);
  regs.si = _guest.Word(regs.bx);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  BlankHelpRows(_guest);
}

// The joystick could not be read (CS:67C4), or the Amstrad's never moved.
void ShowNoJoystick(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PrintAtOnRegisters(_guest, NO_JOYSTICK_TEXT, STATUS_POSITION);
  PrintAtOnRegisters(_guest, JOYSTICK_HELP_TEXT, HELP_POSITION);
  if (_guest.Get(DS.joystickIsAmstrad) != 1)
  {
    ++regs.si;
    regs.di = SECOND_HELP_POSITION;
    _guest.Call(PRINT_TEXT_MODE_STRING);
  }
}

// J (CS:6743): which joystick, then the Amstrad's moves or the IBM's centre. True when it is selected, and
// the menu goes on at ShowControlDevice; false when it could not be read.
[[nodiscard]] bool ChooseJoystick(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PrintAtOnRegisters(_guest, JOYSTICK_TYPE_TEXT, STATUS_POSITION);
  regs.di = STATUS_POSITION;
  regs.si = JOYSTICK_TITLE_TEXT;
  bool amstrad = false;
  for (;;)
  {
    WaitForKey(_guest, JOYSTICK_KEY_LOOP);
    if (High(regs.ax) == SCAN_I)
    {
      break;
    }
    if (High(regs.ax) == SCAN_A)
    {
      amstrad = true;
      break;
    }
    _guest.JumpBack(JOYSTICK_KEY_LOOP);
  }
  _guest.Call(PRINT_TEXT_MODE_STRING);
  if (amstrad)
  {
    _guest.Set(DS.joystickIsAmstrad, 1);
    PrintAtOnRegisters(_guest, MOVE_JOYSTICK_TEXT, HELP_POSITION);
    _guest.Set(DS.amstradJoystickMoved, 0);
    for (;;)
    {
      WaitForKey(_guest, AMSTRAD_KEY_LOOP);
      const std::uint8_t scan = High(regs.ax);
      if (scan < SCAN_PAST_KEYBOARD)
      {
        break;
      }
      if (scan >= SCAN_AMSTRAD_JOYSTICK && scan < SCAN_PAST_AMSTRAD_JOYSTICK)
      {
        _guest.Set(DS.amstradJoystickMoved, 1);
      }
      _guest.JumpBack(AMSTRAD_KEY_LOOP);
    }
    return _guest.Get(DS.amstradJoystickMoved) == 1;
  }
  _guest.Set(DS.joystickIsAmstrad, 0);
  PrintAtOnRegisters(_guest, CENTER_JOYSTICK_TEXT, HELP_POSITION);
  WaitForKey(_guest, CENTER_KEY_LOOP);
  _guest.Call(READ_JOYSTICK_AXES);
  _guest.Set(DS.joystickCenterX, regs.bx);
  _guest.Set(DS.joystickCenterY, regs.cx);
  if (_guest.Flag(Machine::FLAG_CARRY))
  {
    return false;
  }
  _guest.JumpBack(SELECT_JOYSTICK);
  return true;
}

// The disc menu's keys: DiscControlKeyLoop (CS:665E), or ShowControlDevice before it when _showDevice, and
// everything they run until the menu is left. A function key returns from the call it is in, with AH its scan
// code; a disk operation, or E then Y, leaves through LeaveGameLoopForDisk. ShowDiscControlScreen runs on into
// it, and PromptCommanderFileName jumps into it after a bad name, its own return address dropped.
void RunDiscControlKeys(Guest& _guest, bool _showDevice)
{
  Machine::Registers& regs = _guest.Regs();
  bool showDevice = _showDevice;
  for (;;)
  {
    if (showDevice)
    {
      ShowControlDevice(_guest);
      _guest.JumpBack(DISC_CONTROL_KEY_LOOP);
      showDevice = false;
    }
    WaitForKey(_guest, DISC_CONTROL_KEY_LOOP);
    _guest.Push(regs.ax);
    BlankHelpRows(_guest);
    regs.ax = _guest.Pop();
    const std::uint8_t scan = High(regs.ax);
    if (scan == SCAN_E)
    {
      PrintAtOnRegisters(_guest, EXIT_QUESTION_TEXT, STATUS_POSITION);
      for (;;)
      {
        WaitForKey(_guest, EXIT_KEY_LOOP);
        if (High(regs.ax) == SCAN_N || High(regs.ax) == SCAN_Y)
        {
          break;
        }
        _guest.JumpBack(EXIT_KEY_LOOP);
      }
      if (High(regs.ax) == SCAN_N)
      {
        showDevice = true;
        continue;
      }
      SetLow(regs.ax, EXIT_TO_DOS);
      LeaveGameLoopForDiskOnRegisters(_guest);
      return;
    }
    if (scan == SCAN_L || scan == SCAN_S || scan == SCAN_D)
    {
      // A bad name does not come back here.
      _guest.Call(PROMPT_COMMANDER_FILE_NAME);
      SetLow(regs.ax, scan == SCAN_L ? LOAD_COMMANDER : scan == SCAN_S ? SAVE_COMMANDER : DELETE_COMMANDER);
      LeaveGameLoopForDiskOnRegisters(_guest);
      return;
    }
    if (scan == SCAN_C)
    {
      SetLow(regs.ax, CATALOGUE_COMMANDERS);
      LeaveGameLoopForDiskOnRegisters(_guest);
      return;
    }
    if (scan == SCAN_K)
    {
      _guest.Set(DS.inputDevice, KEYBOARD_DEVICE);
      showDevice = true;
      continue;
    }
    if (scan == SCAN_M)
    {
      _guest.Call(IS_MOUSE_DRIVER_INSTALLED);
      if (!_guest.Flag(Machine::FLAG_ZERO))
      {
        _guest.Set(DS.inputDevice, MOUSE_DEVICE);
        _guest.JumpBack(SHOW_CONTROL_DEVICE);
        showDevice = true;
        continue;
      }
      PrintAtOnRegisters(_guest, NO_MOUSE_TEXT, STATUS_POSITION);
      PrintAtOnRegisters(_guest, MOUSE_HELP_TEXT, HELP_POSITION);
      ++regs.si;
      regs.di = SECOND_HELP_POSITION;
      _guest.Call(PRINT_TEXT_MODE_STRING);
      _guest.JumpBack(DISC_CONTROL_KEY_LOOP);
      continue;
    }
    if (scan == SCAN_J)
    {
      if (ChooseJoystick(_guest))
      {
        _guest.Set(DS.inputDevice, JOYSTICK_DEVICE);
        _guest.JumpBack(SHOW_CONTROL_DEVICE);
        showDevice = true;
        continue;
      }
      ShowNoJoystick(_guest);
      _guest.JumpBack(DISC_CONTROL_KEY_LOOP);
      continue;
    }
    if (scan == SCAN_V)
    {
      PrintAtOnRegisters(_guest, DS.versionString.offset, HELP_POSITION);
      _guest.JumpBack(DISC_CONTROL_KEY_LOOP);
      continue;
    }
    if (scan < SCAN_F1 || scan >= SCAN_PAST_F10)
    {
      _guest.JumpBack(DISC_CONTROL_KEY_LOOP);
      continue;
    }
    _guest.Set(DS.resumeAtDiskMenu, 0);
    return;
  }
}

// What the disk operation Start did came to (CS:67E7): LOADED:, SAVED:, DELETED: and the name, or CATALOGUED
// and the catalogue; a loaded name becomes the default.
void ShowDiskResult(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PrintAtOnRegisters(_guest, BLANK_STATUS_TEXT, STATUS_POSITION);
  regs.bx = Join(0, static_cast<std::uint8_t>(_guest.Get(DS.diskOperation) - 1));
  regs.bx = static_cast<std::uint16_t>((regs.bx << 1) + DS.diskOperationNames.offset);
  regs.si = _guest.Word(regs.bx);
  regs.di = STATUS_POSITION;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  if (_guest.Get(DS.diskOperation) == CATALOGUE_COMMANDERS)
  {
    regs.si = CATALOGUE_OK_TEXT;
    _guest.Call(PRINT_TEXT_MODE_STRING);
    _guest.Call(PRINT_COMMANDER_CATALOGUE);
    return;
  }
  // The name without its extension.
  regs.si = DS.commanderFileName.offset;
  for (;;)
  {
    ++regs.si;
    if (_guest.Byte(regs.si) == '.')
    {
      break;
    }
    _guest.JumpBack(FIND_NAME_EXTENSION);
  }
  _guest.SetByte(regs.si, 0);
  regs.si = DS.commanderFileName.offset;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  if (_guest.Get(DS.diskOperation) != LOAD_COMMANDER)
  {
    return;
  }
  regs.si = DS.commanderFileName.offset;
  regs.di = DS.defaultCommanderName.offset;
  regs.cx = MOST_NAME_CHARACTERS;
  for (;;)
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.si;
    ++regs.di;
    if (--regs.cx == 0)
    {
      return;
    }
    _guest.JumpBack(COPY_LOADED_NAME);
  }
}

// Whether AL may stand in a commander's name after its first letter: a digit or an upper-case letter.
[[nodiscard]] constexpr bool IsNameCharacter(std::uint8_t _character) noexcept
{
  return (_character >= '0' && _character <= '9') || (_character >= 'A' && _character <= 'Z');
}

} // namespace

void PerformDiskRequest(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // PUSH AX / POP AX round the transfer area: what DOS leaves in AH goes no further.
  const std::uint16_t request = regs.ax;
  regs.dx = DS.diskTransferArea.offset;
  _guest.Devices().SetDiskTransferArea(_guest.DataSegment(), regs.dx);
  regs.ax = request;
  // DEC AL until it reaches 0: 1 load, 2 save, 3 delete.
  for (std::uint8_t step = 1; step <= 3; ++step)
  {
    regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(static_cast<std::uint8_t>(regs.ax) - 1));
    if (static_cast<std::uint8_t>(regs.ax) == 0)
    {
      if (step == 1)
      {
        LoadCommanderFile(_guest);
      }
      else if (step == 2)
      {
        SaveCommanderFile(_guest);
      }
      else
      {
        DeleteCommanderFile(_guest);
      }
      return;
    }
  }
  ListCommanderFiles(_guest);
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

void ShowDiscControlScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.resumeAtDiskMenu) == 0)
  {
    regs.si = DS.discControlFrame.offset;
    _guest.Call(DRAW_DOCKED_FRAME);
    regs.di = TITLE_POSITION;
    _guest.Call(PRINT_TEXT_MODE_STRING);
    regs.si = DS.discMenuText.offset;
    regs.di = MENU_POSITION;
    _guest.Call(PRINT_COUNTED_TEXT_LINES);
    RunDiscControlKeys(_guest, true);
    return;
  }
  // Back from Start's disk work.
  if (_guest.Get(DS.diskError) == 0)
  {
    ShowDiskResult(_guest);
    _guest.JumpBack(DISC_CONTROL_KEY_LOOP);
    RunDiscControlKeys(_guest, false);
    return;
  }
  PrintAtOnRegisters(_guest, RETRY_TEXT, STATUS_POSITION);
  for (;;)
  {
    WaitForKey(_guest, RETRY_KEY_LOOP);
    if (High(regs.ax) == SCAN_N)
    {
      RunDiscControlKeys(_guest, true);
      return;
    }
    if (High(regs.ax) == SCAN_Y)
    {
      break;
    }
    _guest.JumpBack(RETRY_KEY_LOOP);
  }
  PrintAtOnRegisters(_guest, BLANK_STATUS_TEXT, STATUS_POSITION);
  SetLow(regs.ax, _guest.Get(DS.diskOperation));
  LeaveGameLoopForDiskOnRegisters(_guest);
}

void PromptCommanderFileName(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PrintAtOnRegisters(_guest, FILE_NAME_PROMPT_TEXT, STATUS_POSITION);
  regs.si = DS.commanderFileName.offset;
  regs.di = NAME_POSITION;
  regs.cx = MOST_NAME_CHARACTERS;
  _guest.Call(READ_TEXT_LINE);
  // A letter, then letters and digits, folded to upper case.
  regs.si = DS.commanderFileName.offset;
  SetLow(regs.ax, _guest.Byte(regs.si));
  bool named = false;
  if (Low(regs.ax) != 0)
  {
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & LOWER_CASE_BIT_CLEAR));
    if (Low(regs.ax) >= 'A' && Low(regs.ax) <= 'Z')
    {
      _guest.SetByte(regs.si, Low(regs.ax));
      for (;;)
      {
        ++regs.si;
        SetLow(regs.ax, _guest.Byte(regs.si));
        if (Low(regs.ax) == 0)
        {
          named = true;
          break;
        }
        if (Low(regs.ax) >= 'a' && Low(regs.ax) <= 'z')
        {
          SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & LOWER_CASE_BIT_CLEAR));
        }
        if (!IsNameCharacter(Low(regs.ax)))
        {
          break;
        }
        _guest.SetByte(regs.si, Low(regs.ax));
        _guest.JumpBack(NEXT_NAME_CHARACTER);
      }
    }
  }
  if (!named)
  {
    // RejectFileName (CS:68AD): this call's return address dropped, and the menu's keys read on in its place.
    regs.ax = _guest.Pop();
    PrintAtOnRegisters(_guest, FILE_NAME_ERROR_TEXT, STATUS_POSITION);
    _guest.JumpBack(DISC_CONTROL_KEY_LOOP);
    RunDiscControlKeys(_guest, false);
    return;
  }
  regs.di = COMMANDER_EXTENSION;
  regs.cx = EXTENSION_BYTES;
  for (;;)
  {
    SetLow(regs.ax, _guest.Byte(regs.di));
    ++regs.di;
    _guest.SetByte(regs.si, Low(regs.ax));
    ++regs.si;
    if (--regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(APPEND_EXTENSION);
  }
  _guest.SetFlag(Machine::FLAG_CARRY, false);
}

void PrintCommanderCatalogue(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = CATALOGUE_POSITION;
  regs.cx = CATALOGUE_ROWS;
  do
  {
    _guest.Push(regs.cx);
    _guest.Push(regs.di);
    regs.si = BLANK_LINE_TEXT;
    PrintTextModeStringEntry(_guest);
    regs.di = _guest.Pop();
    regs.di = static_cast<std::uint16_t>(regs.di + TEXT_ROW_BYTES);
    regs.cx = _guest.Pop();
  } while (--regs.cx != 0);
  SetLow(regs.cx, _guest.Get(DS.commanderFileCount));
  if (Low(regs.cx) == 0)
  {
    return;
  }
  // Name DH at row DH mod 10, column DH / 10, 9 cells apart.
  regs.si = DS.commanderFileList.offset;
  SetHigh(regs.cx, 0);
  regs.dx = CATALOGUE_ROWS;
  do
  {
    regs.ax = SignExtend(High(regs.dx));
    DivideByte(_guest, Low(regs.dx));
    SetHigh(regs.bx, High(regs.ax));
    regs.ax = SignExtend(Low(regs.ax));
    regs.di = regs.ax;
    regs.ax = static_cast<std::uint16_t>(regs.ax << 3);
    regs.di = static_cast<std::uint16_t>(regs.di + regs.ax);
    regs.di = static_cast<std::uint16_t>(regs.di << 1);
    SetLow(regs.bx, 0);
    regs.bx = static_cast<std::uint16_t>(regs.bx >> 2);
    regs.di = static_cast<std::uint16_t>(regs.di + regs.bx);
    regs.bx = static_cast<std::uint16_t>(regs.bx >> 2);
    regs.di = static_cast<std::uint16_t>(regs.di + regs.bx);
    regs.di = static_cast<std::uint16_t>(regs.di + CATALOGUE_POSITION);
    PrintTextModeStringEntry(_guest);
    ++regs.si;
    SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) + 1));
  } while (--regs.cx != 0);
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

void ShowDiskErrorEntry(Guest& _guest)
{
  ShowDiskError(_guest.State());
  _guest.Regs().es = Guest::VIDEO_SEGMENT;
  _guest.Clobber(SHOWS_DISK_ERROR);
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
  NativeEntry{0x02FF, "PerformDiskRequest", &PerformDiskRequest, DISK_REQUEST},
  NativeEntry{0x0470, "ShowDiskError", &ShowDiskErrorEntry, SHOWS_DISK_ERROR},
  NativeEntry{0x4660, "SaveStartupCommander", &SaveStartupCommanderEntry, COPY},
  NativeEntry{0x660B, "ShowDiscControlScreen", &ShowDiscControlScreen, CLOBBERS_GENERAL, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x6862, "PromptCommanderFileName", &PromptCommanderFileName, PROMPTS_FOR_NAME, Machine::NativeReturn::Near, 0, WAITS},
  NativeEntry{0x68CE, "PrintCommanderCatalogue", &PrintCommanderCatalogue, CLOBBERS_GENERAL},
};

} // namespace

std::span<const NativeEntry> SaveLoadEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
