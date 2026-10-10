#include "pch.h"

#include "StartUp.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Input.h"
#include "Maths.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t DIVIDE_OVERFLOW_INTERRUPT = 0x025E;
constexpr std::uint16_t KEYBOARD_INTERRUPT = 0x0201;
constexpr std::uint16_t WAIT_FOR_TIMER_TICK = 0x7772;

// The interrupt table's words, at 0000:vector*4.
constexpr std::uint16_t DIVIDE_VECTOR_OFFSET = 0x0000;
constexpr std::uint16_t DIVIDE_VECTOR_SEGMENT = 0x0002;
constexpr std::uint16_t TIMER_VECTOR_SEGMENT = 0x0022;
constexpr std::uint16_t KEYBOARD_VECTOR_OFFSET = 0x0024;
constexpr std::uint16_t KEYBOARD_VECTOR_SEGMENT = 0x0026;

constexpr std::uint16_t COMMAND_TAIL = 0x80; // in the PSP: its length, then the text
constexpr std::uint8_t CHEAT_KEY = 0xAA;
constexpr std::uint8_t CHEAT_ARGUMENT_BYTES = 6; // ' cheat'

constexpr std::uint8_t VIDEO_VECTOR = 0x10;
constexpr std::uint8_t DOS_VECTOR = 0x21;
constexpr std::uint8_t DOS_PRINT_STRING = 0x09;
constexpr std::uint8_t DOS_FLUSH_AND_READ = 0x0C;
constexpr std::uint8_t DOS_BUFFERED_INPUT = 0x0A;
constexpr std::uint16_t CGA_STATUS_PORT = 0x3DA;
constexpr std::uint8_t CGA_VERTICAL_RETRACE = 0x08;
constexpr std::uint8_t QUESTION_MASK = 0x3F; // 64 questions
constexpr std::uint16_t QUESTION_KEY = 0x6161;
constexpr std::uint8_t UPPER_CASE_MASK = 0xDF;
constexpr std::uint8_t NO_QUESTION = 0xFF;

constexpr std::uint16_t NEW_GAME_ENERGY = 0x3FF;
constexpr std::uint8_t FULL_SHIELD = 0xFF;
constexpr std::uint8_t NEW_GAME_CABIN_TEMPERATURE = 0x0C;
constexpr std::uint8_t NEW_GAME_ALTITUDE = 0xFF;

// The routines start-up calls, each through its hook or the original (ADR-010 item 8).
constexpr std::uint16_t INSTALL_TIMER_INTERRUPT = 0x00C6;
constexpr std::uint16_t INSTALL_DIVIDE_AND_KEYBOARD_INTERRUPTS = 0x0105;
constexpr std::uint16_t RESTORE_DIVIDE_AND_KEYBOARD_INTERRUPTS = 0x0148;
constexpr std::uint16_t RESTORE_TIMER_INTERRUPT = 0x016B;
constexpr std::uint16_t CHECK_CHEAT_ARGUMENT = 0x02A5;
constexpr std::uint16_t CRITICAL_ERROR_INTERRUPT = 0x02F0;
constexpr std::uint16_t PERFORM_DISK_REQUEST = 0x02FF;
constexpr std::uint16_t COPY_PROTECTION = 0x04A3;
constexpr std::uint16_t WIPE_PROGRAM = 0x0554;
constexpr std::uint16_t FINISH_SPACE_VIEW_FRAME = 0x0570;
constexpr std::uint16_t CLEAR_DRAW_BUFFER = 0x060D;
constexpr std::uint16_t DRAW_VIEW_STRING = 0x31EC;
constexpr std::uint16_t SAVE_STARTUP_COMMANDER = 0x4660;
constexpr std::uint16_t GAME_LOOP = 0x7D30;
constexpr std::uint16_t RUN_TITLE_AND_DOCKED = 0x7D81;
constexpr std::uint16_t RUN_FLIGHT = 0x7E9B;

// Where the original jumps back (Guest::LoopTurn).
constexpr std::uint16_t RESTART_PLAY = 0x003B;
constexpr std::uint16_t EXIT_KEY_DRAIN = 0x00B9;
constexpr std::uint16_t CREDITS_LINE = 0x8F0E;
constexpr std::uint16_t CREDITS_WAIT = 0x8F2B;

constexpr std::uint16_t CRITICAL_ERROR_VECTOR_OFFSET = 0x0090; // int 24h
constexpr std::uint16_t CRITICAL_ERROR_VECTOR_SEGMENT = 0x0092;
constexpr std::uint16_t AMSTRAD_ROM_SEGMENT = 0xFC00;
constexpr std::uint16_t AMSTRAD_ROM_SIGNATURE = 0x0016;
constexpr std::uint16_t AMSTRAD_SIGNATURE_BYTES = 7;
constexpr std::uint8_t DOS_GET_TIME = 0x2C;
constexpr std::uint8_t DOS_GET_VERSION = 0x30;
constexpr std::uint8_t STARTUP_WAIT_SECONDS = 7;
constexpr std::uint8_t SECONDS_PER_MINUTE = 60;
constexpr std::uint8_t BIOS_KEYBOARD_VECTOR = 0x16;
constexpr std::uint8_t BIOS_KEY_STATUS = 0x01;
constexpr std::uint8_t BIOS_READ_KEY = 0x00;
constexpr std::uint16_t VIDEO_MODE_TEXT_80 = 0x0002; // AH=0 set mode, AL=2: 80x25 text

// WipeProgram: the code segment from just past it to the end of the program.
constexpr std::uint16_t WIPE_FIRST = 0x0564;
constexpr std::uint16_t WIPE_END = 0x8F31;

constexpr std::uint16_t CREDITS_FIRST_LINE = 0x202;
constexpr std::uint16_t CREDITS_LINES = 9;
constexpr std::uint16_t CREDITS_LINE_STEP = 0x200;
constexpr std::uint16_t WHITE_MASK = 0xFFFF;
constexpr std::uint16_t CREDITS_TIMER_TICKS = 3000; // 3 s

// REP MOVSB from DS:SI to ES:DI, forwards or, with DF set, backwards.
void MoveBytes(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto step = static_cast<std::uint16_t>((regs.flags & Machine::FLAG_DIRECTION) != 0 ? 0xFFFF : 1);
  for (; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarByte(regs.es, regs.di, _guest.FarByte(regs.ds, regs.si));
    regs.si = static_cast<std::uint16_t>(regs.si + step);
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
}

// INT 21h AH=09h: the $-terminated text at DS:_text.
void PrintDosString(Guest& _guest, std::uint16_t _text)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = _text;
  regs.ax = WithHigh(regs.ax, DOS_PRINT_STRING);
  _guest.Interrupt(DOS_VECTOR);
}

// ExitToDos (CS:00AD): the BIOS's text mode, the farewell, and the BIOS's key buffer emptied. The RETF
// that follows, to PSP:0000 where Start pushed it, is the entry's own return.
void ExitToDos(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = VIDEO_MODE_TEXT_80;
  _guest.Interrupt(VIDEO_VECTOR);
  PrintDosString(_guest, DS.exitMessage.offset);
  for (;;)
  {
    SetHigh(regs.ax, BIOS_KEY_STATUS);
    _guest.Interrupt(BIOS_KEYBOARD_VECTOR);
    if (_guest.Flag(Machine::FLAG_ZERO))
    {
      return;
    }
    SetHigh(regs.ax, BIOS_READ_KEY);
    _guest.Interrupt(BIOS_KEYBOARD_VECTOR);
    _guest.JumpBack(EXIT_KEY_DRAIN);
  }
}

} // namespace

void InstallDivideAndKeyboardInterrupts(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetWord(DS.savedDivideVector.offset, _guest.FarWord(regs.es, DIVIDE_VECTOR_OFFSET));
  _guest.Set(DS.data226D, _guest.FarWord(regs.es, DIVIDE_VECTOR_SEGMENT));
  _guest.SetWord(DS.savedKeyboardVector.offset, _guest.FarWord(regs.es, KEYBOARD_VECTOR_OFFSET));
  _guest.Set(DS.data2271, _guest.FarWord(regs.es, KEYBOARD_VECTOR_SEGMENT));
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
  _guest.SetFarWord(regs.es, DIVIDE_VECTOR_OFFSET, DIVIDE_OVERFLOW_INTERRUPT);
  _guest.SetFarWord(regs.es, KEYBOARD_VECTOR_OFFSET, KEYBOARD_INTERRUPT);
  const std::uint16_t code = _guest.CodeSegment();
  _guest.SetFarWord(regs.es, DIVIDE_VECTOR_SEGMENT, code);
  _guest.SetFarWord(regs.es, TIMER_VECTOR_SEGMENT, code);
  _guest.SetFarWord(regs.es, KEYBOARD_VECTOR_SEGMENT, code);
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = Guest::VIDEO_SEGMENT;
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
  ResetKeyboard(_guest);
}

void RestoreDivideAndKeyboardInterrupts(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = 0;
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
  _guest.SetFarWord(regs.es, KEYBOARD_VECTOR_SEGMENT, _guest.Get(DS.data2271));
  _guest.SetFarWord(regs.es, KEYBOARD_VECTOR_OFFSET, _guest.Word(DS.savedKeyboardVector.offset));
  _guest.SetFarWord(regs.es, DIVIDE_VECTOR_SEGMENT, _guest.Get(DS.data226D));
  regs.ax = _guest.Word(DS.savedDivideVector.offset);
  _guest.SetFarWord(regs.es, DIVIDE_VECTOR_OFFSET, regs.ax);
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
}

void CheckCheatArgument(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.cheatEnabled, 0);
  regs.ax = _guest.Get(DS.pspSegment);
  const std::uint16_t psp = regs.ax;
  regs.bx = COMMAND_TAIL;
  if (_guest.FarByte(psp, regs.bx) != CHEAT_ARGUMENT_BYTES)
  {
    return;
  }
  ++regs.bx;
  regs.si = DS.cheatArgumentKey.offset;
  regs.cx = CHEAT_ARGUMENT_BYTES;
  do
  {
    regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(_guest.Byte(regs.si) ^ CHEAT_KEY));
    if (_guest.FarByte(psp, regs.bx) != Low(regs.ax))
    {
      return;
    }
    ++regs.bx;
    ++regs.si;
    --regs.cx;
  } while (regs.cx != 0);
  _guest.Set(DS.cheatEnabled, 1);
}

void CopyProtection(Guest& _guest)
{
  if (_guest.Get(DS.protectionShown) != 0)
  {
    return;
  }
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.protectionShown, 1);
  regs.ax = 0;
  _guest.Interrupt(VIDEO_VECTOR);
  regs.di = _guest.Get(DS.protectionDescriptor);
  PrintDosString(_guest, _guest.Word(regs.di));

  // The question depends on how long the random generator runs before a vertical retrace.
  regs.dx = CGA_STATUS_PORT;
  do
  {
    NextRandomEntry(_guest);
    regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(regs.dx) & CGA_VERTICAL_RETRACE));
  } while (Low(regs.ax) == 0);
  NextRandomEntry(_guest);
  const auto index = static_cast<std::uint8_t>(Low(regs.ax) & QUESTION_MASK);
  regs.bx = static_cast<std::uint16_t>(static_cast<std::uint8_t>(index * 3) * 2);
  const auto question = static_cast<std::uint8_t>(index + 1);
  _guest.Set(DS.protectionQuestion, question);

  // The question's three words (page, line, word), each XORed with its number and 6161h, into the prompt.
  regs.di = _guest.Get(DS.protectionDescriptor);
  regs.si = static_cast<std::uint16_t>(_guest.Word(static_cast<std::uint16_t>(regs.di + 8)) + regs.bx);
  regs.bx = static_cast<std::uint16_t>(((question << 8) | question) ^ QUESTION_KEY);
  for (std::uint16_t field = 0; field < 3; ++field)
  {
    regs.ax = static_cast<std::uint16_t>(regs.bx ^ _guest.Word(static_cast<std::uint16_t>(regs.si + field * 2)));
    _guest.SetWord(_guest.Word(static_cast<std::uint16_t>(regs.di + 2 + field * 2)), regs.ax);
  }
  PrintDosString(_guest, _guest.Word(static_cast<std::uint16_t>(regs.di + 2)));

  regs.dx = DS.protectionInput.offset;
  regs.ax = static_cast<std::uint16_t>((DOS_FLUSH_AND_READ << 8) | DOS_BUFFERED_INPUT);
  _guest.Interrupt(DOS_VECTOR);
  regs.di = DS.data25CA.offset;
  regs.cx = WithLow(regs.cx, _guest.Byte(regs.di));
  if (Low(regs.cx) != 0)
  {
    regs.cx = Low(regs.cx);
    do
    {
      ++regs.di;
      _guest.SetByte(regs.di, static_cast<std::uint8_t>(_guest.Byte(regs.di) & UPPER_CASE_MASK));
      --regs.cx;
    } while (regs.cx != 0);
  }
  _guest.Call(WAIT_FOR_TIMER_TICK);
  _guest.Set(DS.protectionAnswerCorrect, 1);
  _guest.Set(DS.protectionQuestion, NO_QUESTION);
}

void StartNewGame(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = regs.ds;
  regs.es = regs.ds;
  regs.di = DS.commanderBlock.offset;
  regs.si = DS.startupCommander.offset;
  regs.cx = _guest.Get(DS.commanderFileBytes);
  MoveBytes(_guest);
  _guest.Set(DS.witchspaceCountdown, 0);
  _guest.Set(DS.playerEnergy, NEW_GAME_ENERGY);
  _guest.Set(DS.foreShield, FULL_SHIELD);
  _guest.Set(DS.aftShield, FULL_SHIELD);
  _guest.Set(DS.cabinTemperature, NEW_GAME_CABIN_TEMPERATURE);
  _guest.Set(DS.altitude, NEW_GAME_ALTITUDE);
  _guest.Set(DS.playerDead, 0);
  _guest.Set(DS.missionJumpCount, 0);
  _guest.Set(DS.data7629, 0);
  _guest.Set(DS.missionNumber, 0);
  _guest.Set(DS.warningFrames, 0);
  _guest.Set(DS.incomingMissileAlert, 0);
  _guest.Set(DS.missionStage, 0);
  _guest.Set(DS.fuelLeakDelayFrames, 0);
  _guest.Set(DS.fuelLeakFrames, 0);
  _guest.Set(DS.supernovaFrames, 0);
  _guest.Set(DS.supernovaHeat, 0);
  _guest.Set(DS.maskShipDestroyed, 0);
  _guest.Set(DS.thargoidInvasionActive, 0);
  _guest.Set(DS.maskMissionShipsLeft, 0);
  _guest.Set(DS.invadedStationDestroyed, 0);
  _guest.Set(DS.forceMisjump, 0);
  _guest.Set(DS.messageFrames, 0);
  _guest.Set(DS.missileState, 0);
  _guest.Set(DS.fledMaskShip, 0);
  _guest.Set(DS.maskSystemJumps, 0);
}

void Start(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The far return to PSP:0000, where INT 20h ends the program.
  regs.ax = regs.ds;
  _guest.Push(regs.ax);
  regs.bx = 0;
  _guest.Push(regs.bx);
  regs.bx = _guest.DataSegment();
  regs.ds = regs.bx;
  _guest.Set(DS.pspSegment, regs.ax);
  SetHigh(regs.ax, DOS_GET_VERSION);
  _guest.Interrupt(DOS_VECTOR);
  if (Low(regs.ax) == 0)
  {
    // DOS 1 reports no major version, and is not enough.
    PrintDosString(_guest, DS.dosVersionMessage.offset);
    ExitToDos(_guest);
    return;
  }
  _guest.Call(SAVE_STARTUP_COMMANDER);

  // REPE CMPSB of amstradSignature against the ROM at FC00:0016: not an Amstrad if a byte before the last
  // differs.
  regs.ax = AMSTRAD_ROM_SEGMENT;
  regs.es = AMSTRAD_ROM_SEGMENT;
  regs.di = AMSTRAD_ROM_SIGNATURE;
  regs.si = DS.amstradSignature.offset;
  regs.cx = AMSTRAD_SIGNATURE_BYTES;
  _guest.SetFlag(Machine::FLAG_DIRECTION, false);
  while (regs.cx != 0)
  {
    const bool same = _guest.Byte(regs.si) == _guest.FarByte(regs.es, regs.di);
    ++regs.si;
    ++regs.di;
    --regs.cx;
    if (!same)
    {
      break;
    }
  }
  if (regs.cx != 0)
  {
    _guest.Set(DS.amstradPresent, 0);
  }

  for (;;)
  {
    // RestartPlay (CS:003B): the DOS clock read twice for a 7-second wait whose closing branch the NOPs at
    // StartupWaitPatched replaced, so only the reads remain.
    SetHigh(regs.ax, DOS_GET_TIME);
    _guest.Interrupt(DOS_VECTOR);
    SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) + STARTUP_WAIT_SECONDS));
    if (High(regs.dx) >= SECONDS_PER_MINUTE)
    {
      SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) - SECONDS_PER_MINUTE));
    }
    _guest.Push(regs.dx);
    SetHigh(regs.ax, DOS_GET_TIME);
    _guest.Interrupt(DOS_VECTOR);
    SetHigh(regs.cx, High(regs.dx));
    regs.dx = _guest.Pop();
    _guest.Call(INSTALL_TIMER_INTERRUPT);
    _guest.Call(COPY_PROTECTION);
    _guest.Call(INSTALL_DIVIDE_AND_KEYBOARD_INTERRUPTS);
    _guest.Call(CHECK_CHEAT_ARGUMENT);
    _guest.Call(GAME_LOOP);

    // HandleDiskRequest (CS:0065): GameLoop comes back only for diskOperation, 0 to leave.
    _guest.Call(RESTORE_DIVIDE_AND_KEYBOARD_INTERRUPTS);
    _guest.Call(RESTORE_TIMER_INTERRUPT);
    SetLow(regs.ax, _guest.Get(DS.diskOperation));
    if (Low(regs.ax) == 0)
    {
      _guest.Call(WIPE_PROGRAM);
      ExitToDos(_guest);
      return;
    }
    // The request, with the game's own critical-error handler on int 24h.
    _guest.Set(DS.diskError, 0);
    regs.bx = 0;
    regs.es = 0;
    _guest.Push(_guest.FarWord(regs.es, CRITICAL_ERROR_VECTOR_OFFSET));
    _guest.Push(_guest.FarWord(regs.es, CRITICAL_ERROR_VECTOR_SEGMENT));
    regs.bx = CRITICAL_ERROR_INTERRUPT;
    _guest.SetFarWord(regs.es, CRITICAL_ERROR_VECTOR_OFFSET, regs.bx);
    regs.bx = _guest.CodeSegment();
    _guest.SetFarWord(regs.es, CRITICAL_ERROR_VECTOR_SEGMENT, regs.bx);
    _guest.Call(PERFORM_DISK_REQUEST);
    regs.ax = 0;
    regs.es = 0;
    _guest.SetFarWord(regs.es, CRITICAL_ERROR_VECTOR_SEGMENT, _guest.Pop());
    _guest.SetFarWord(regs.es, CRITICAL_ERROR_VECTOR_OFFSET, _guest.Pop());
    _guest.JumpBack(RESTART_PLAY);
  }
}

void WipeProgram(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
  regs.di = WIPE_FIRST;
  regs.cx = static_cast<std::uint16_t>(WIPE_END - regs.di);
  regs.ax = _guest.CodeSegment();
  regs.es = regs.ax;
  // REP STOSB of AL, the code segment's low byte, forwards or, with DF set, backwards.
  const auto step = static_cast<std::uint16_t>((regs.flags & Machine::FLAG_DIRECTION) != 0 ? 0xFFFF : 1);
  for (; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarByte(regs.es, regs.di, Low(regs.ax));
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
}

void GameLoop(Guest& _guest)
{
  for (;;)
  {
    _guest.Set(DS.inFlight, 0);
    _guest.Call(RUN_TITLE_AND_DOCKED);
    _guest.Set(DS.inFlight, 1);
    _guest.Call(RUN_FLIGHT);
    // After a death the title runs again.
    if (_guest.Get(DS.playerDead) == 1)
    {
      _guest.Set(DS.titleShown, 0);
    }
    _guest.JumpBack(GAME_LOOP);
  }
}

void ShowCredits(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(CLEAR_DRAW_BUFFER);
  regs.si = DS.creditsScreenText.offset;
  regs.di = CREDITS_FIRST_LINE;
  regs.cx = CREDITS_LINES;
  for (;;)
  {
    _guest.Push(regs.cx);
    _guest.Push(regs.di);
    _guest.Set(DS.textPaperPattern, 0);
    regs.bx = WHITE_MASK;
    _guest.Call(DRAW_VIEW_STRING);
    ++regs.si;
    regs.di = static_cast<std::uint16_t>(_guest.Pop() + CREDITS_LINE_STEP);
    regs.cx = _guest.Pop();
    if (--regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(CREDITS_LINE);
  }
  _guest.Call(FINISH_SPACE_VIEW_FRAME);
  regs.cx = CREDITS_TIMER_TICKS;
  for (;;)
  {
    _guest.Call(WAIT_FOR_TIMER_TICK);
    if (--regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(CREDITS_WAIT);
  }
}

namespace
{

using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_SI{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI, 0};
constexpr Machine::NativeContract CLOBBERS_ALL{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_SI_DI{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI_ES_INTERRUPTS_OFF{REGISTER_AX | REGISTER_CX | REGISTER_DI | REGISTER_ES,
                                                                      Machine::FLAG_INTERRUPT};
// Start sets DS to the data segment, and never returns.
constexpr Machine::NativeContract CLOBBERS_EVERY_REGISTER{REGISTER_ALL, 0};

constexpr Machine::NativeWait ALWAYS = Machine::NativeWait::Always;

constexpr std::array ENTRIES = {
  NativeEntry{0x0000, "Start", &Start, CLOBBERS_EVERY_REGISTER, Machine::NativeReturn::Far, 0, ALWAYS},
  NativeEntry{0x0105, "InstallDivideAndKeyboardInterrupts", &InstallDivideAndKeyboardInterrupts, CLOBBERS_AX},
  NativeEntry{0x0148, "RestoreDivideAndKeyboardInterrupts", &RestoreDivideAndKeyboardInterrupts, CLOBBERS_AX},
  NativeEntry{0x02A5, "CheckCheatArgument", &CheckCheatArgument, CLOBBERS_AX_BX_CX_SI},
  NativeEntry{0x04A3, "CopyProtection", &CopyProtection, CLOBBERS_ALL},
  NativeEntry{0x0554, "WipeProgram", &WipeProgram, CLOBBERS_AX_CX_DI_ES_INTERRUPTS_OFF},
  NativeEntry{0x4671, "StartNewGame", &StartNewGame, CLOBBERS_AX_CX_SI_DI},
  NativeEntry{0x7D30, "GameLoop", &GameLoop, CLOBBERS_ALL, Machine::NativeReturn::Near, 0, ALWAYS},
  NativeEntry{0x8F02, "ShowCredits", &ShowCredits, CLOBBERS_ALL, Machine::NativeReturn::Near, 0, ALWAYS},
};

} // namespace

std::span<const NativeEntry> StartUpEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
