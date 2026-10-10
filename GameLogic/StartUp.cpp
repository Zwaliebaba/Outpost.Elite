#include "pch.h"

#include "StartUp.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Input.h"
#include "Maths.h"
#include "Text.h"
#include "Timer.h"
#include "Video.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t DIVIDE_OVERFLOW_INTERRUPT = 0x025E;
constexpr std::uint16_t KEYBOARD_INTERRUPT = 0x0201;

// The interrupt table's words, at 0000:vector*4.
constexpr std::uint16_t DIVIDE_VECTOR_OFFSET = 0x0000;
constexpr std::uint16_t DIVIDE_VECTOR_SEGMENT = 0x0002;
constexpr std::uint16_t TIMER_VECTOR_SEGMENT = 0x0022;
constexpr std::uint16_t KEYBOARD_VECTOR_OFFSET = 0x0024;
constexpr std::uint16_t KEYBOARD_VECTOR_SEGMENT = 0x0026;

constexpr std::uint16_t COMMAND_TAIL = 0x80; // in the PSP: its length, then the text
constexpr std::uint16_t COMMAND_TAIL_TEXT = 0x81;
constexpr std::uint8_t CHEAT_KEY = 0xAA;
constexpr std::uint8_t CHEAT_ARGUMENT_BYTES = 6; // ' cheat'

constexpr std::uint8_t DOS_VECTOR = 0x21;
constexpr std::uint8_t VIDEO_MODE_TEXT_40 = 0x00; // 40x25 text, the colour burst off
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
constexpr std::uint16_t SAVE_STARTUP_COMMANDER = 0x4660;
constexpr std::uint16_t GAME_LOOP = 0x7D30;
constexpr std::uint16_t RUN_TITLE_AND_DOCKED = 0x7D81;
constexpr std::uint16_t RUN_FLIGHT = 0x7E9B;

// Where the original jumps back (Guest::LoopTurn, Hardware::LoopTurn).
constexpr std::uint16_t RESTART_PLAY = 0x003B;
constexpr std::uint16_t EXIT_KEY_DRAIN = 0x00B9;
constexpr std::uint16_t RETRACE_POLL = 0x04C1;
constexpr std::uint16_t UPPER_CASE_LOOP = 0x0529;
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
constexpr std::uint8_t VIDEO_MODE_TEXT_80 = 0x02; // 80x25 text

// WipeProgram: the code segment from just past it to the end of the program.
constexpr std::uint16_t WIPE_FIRST = 0x0564;
constexpr std::uint16_t WIPE_END = 0x8F31;

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
  _hardware.PrintDosString(_state.DataSegment(), DS.exitMessage.offset);
  // At the jump back the original holds in AX the key int 16h took out, and the next turn reads no register before it writes
  // it but DX, the message, which does not change. Every turn takes a key, a change paced time sees, so no turn idles.
  while (_hardware.PeekBiosKey().has_value())
  {
    const std::uint16_t key = _hardware.ReadBiosKey();
    _hardware.LoopTurn(EXIT_KEY_DRAIN, {key});
  }
}

} // namespace

void InstallDivideAndKeyboardInterrupts(GameState& _state, Hardware& _hardware, std::uint16_t _vectors)
{
  _state.SetWord(DS.savedDivideVector.offset, _state.FarWord(_vectors, DIVIDE_VECTOR_OFFSET));
  _state.Set(DS.data226D, _state.FarWord(_vectors, DIVIDE_VECTOR_SEGMENT));
  _state.SetWord(DS.savedKeyboardVector.offset, _state.FarWord(_vectors, KEYBOARD_VECTOR_OFFSET));
  _state.Set(DS.data2271, _state.FarWord(_vectors, KEYBOARD_VECTOR_SEGMENT));
  _hardware.DisableInterrupts();
  _state.SetFarWord(_vectors, DIVIDE_VECTOR_OFFSET, DIVIDE_OVERFLOW_INTERRUPT);
  _state.SetFarWord(_vectors, KEYBOARD_VECTOR_OFFSET, KEYBOARD_INTERRUPT);
  const std::uint16_t code = _state.CodeSegment();
  _state.SetFarWord(_vectors, DIVIDE_VECTOR_SEGMENT, code);
  _state.SetFarWord(_vectors, TIMER_VECTOR_SEGMENT, code);
  _state.SetFarWord(_vectors, KEYBOARD_VECTOR_SEGMENT, code);
  _hardware.EnableInterrupts();
  ResetKeyboard(_state, _hardware);
}

void RestoreDivideAndKeyboardInterrupts(GameState& _state, Hardware& _hardware)
{
  // The interrupt table at 0000:0000.
  _hardware.DisableInterrupts();
  _state.SetFarWord(0, KEYBOARD_VECTOR_SEGMENT, _state.Get(DS.data2271));
  _state.SetFarWord(0, KEYBOARD_VECTOR_OFFSET, _state.Word(DS.savedKeyboardVector.offset));
  _state.SetFarWord(0, DIVIDE_VECTOR_SEGMENT, _state.Get(DS.data226D));
  _state.SetFarWord(0, DIVIDE_VECTOR_OFFSET, _state.Word(DS.savedDivideVector.offset));
  _hardware.EnableInterrupts();
}

void CheckCheatArgument(GameState& _state)
{
  _state.Set(DS.cheatEnabled, 0);
  // The command tail in the PSP: its length, then the text, which cheatArgumentKey holds XORed with AAh.
  const std::uint16_t psp = _state.Get(DS.pspSegment);
  if (_state.FarByte(psp, COMMAND_TAIL) != CHEAT_ARGUMENT_BYTES)
  {
    return;
  }
  for (std::uint16_t at = 0; at < CHEAT_ARGUMENT_BYTES; ++at)
  {
    const auto expected = static_cast<std::uint8_t>(_state.Byte(Offset(DS.cheatArgumentKey.offset, at)) ^ CHEAT_KEY);
    if (_state.FarByte(psp, Offset(COMMAND_TAIL_TEXT, at)) != expected)
    {
      return;
    }
  }
  _state.Set(DS.cheatEnabled, 1);
}

void CopyProtection(GameState& _state, Hardware& _hardware)
{
  if (_state.Get(DS.protectionShown) != 0)
  {
    return;
  }
  _state.Set(DS.protectionShown, 1);
  _hardware.SetVideoMode(VIDEO_MODE_TEXT_40);
  const std::uint16_t descriptor = _state.Get(DS.protectionDescriptor);
  _hardware.PrintDosString(_state.DataSegment(), _state.Word(descriptor));

  // The question depends on how long the random generator runs before a vertical retrace. At the jump back the original
  // holds the status it read, masked, in AL; the next turn reads no register before it writes it but DX, the port.
  for (;;)
  {
    static_cast<void>(NextRandom(_state));
    const auto retrace = static_cast<std::uint8_t>(_hardware.CgaStatus() & CGA_VERTICAL_RETRACE);
    if (retrace != 0)
    {
      break;
    }
    _hardware.LoopTurn(RETRACE_POLL, {retrace});
  }
  const auto index = static_cast<std::uint8_t>(Low(NextRandom(_state)) & QUESTION_MASK);
  const auto question = static_cast<std::uint8_t>(index + 1);
  _state.Set(DS.protectionQuestion, question);

  // The question's three words (page, line, word), each XORed with its number in both bytes and 6161h, into the prompt.
  const auto words = static_cast<std::uint16_t>(_state.Word(Offset(descriptor, 8)) + static_cast<std::uint8_t>(index * 3) * 2);
  const auto key = static_cast<std::uint16_t>(Join(question, question) ^ QUESTION_KEY);
  for (std::uint16_t field = 0; field < 3; ++field)
  {
    const auto word = static_cast<std::uint16_t>(key ^ _state.Word(Offset(words, static_cast<std::uint16_t>(field * 2))));
    _state.SetWord(_state.Word(Offset(descriptor, static_cast<std::uint16_t>(2 + field * 2))), word);
  }
  _hardware.PrintDosString(_state.DataSegment(), _state.Word(Offset(descriptor, 2)));

  // The answer typed, upper-cased in place: LOOP back with DI at the character done and CX the count left, which the next
  // turn reads.
  _hardware.ReadDosLine(_state.DataSegment(), DS.protectionInput.offset);
  std::uint16_t at = DS.data25CA.offset;
  const std::uint8_t typed = _state.Byte(at);
  if (typed != 0)
  {
    for (std::uint16_t left = typed;;)
    {
      at = Offset(at, 1);
      _state.SetByte(at, static_cast<std::uint8_t>(_state.Byte(at) & UPPER_CASE_MASK));
      if (--left == 0)
      {
        break;
      }
      _hardware.LoopTurn(UPPER_CASE_LOOP, {at, left});
    }
  }
  WaitForTimerTick(_state, _hardware);
  _state.Set(DS.protectionAnswerCorrect, 1);
  _state.Set(DS.protectionQuestion, NO_QUESTION);
}

void StartNewGame(GameState& _state, bool _backward)
{
  // REP MOVSB from DS to ES = DS.
  const std::uint16_t data = _state.DataSegment();
  MoveBytes(_state, data, DS.startupCommander.offset, data, DS.commanderBlock.offset, _state.Get(DS.commanderFileBytes), _backward);
  _state.Set(DS.witchspaceCountdown, 0);
  _state.Set(DS.playerEnergy, NEW_GAME_ENERGY);
  _state.Set(DS.foreShield, FULL_SHIELD);
  _state.Set(DS.aftShield, FULL_SHIELD);
  _state.Set(DS.cabinTemperature, NEW_GAME_CABIN_TEMPERATURE);
  _state.Set(DS.altitude, NEW_GAME_ALTITUDE);
  _state.Set(DS.playerDead, 0);
  _state.Set(DS.missionJumpCount, 0);
  _state.Set(DS.data7629, 0);
  _state.Set(DS.missionNumber, 0);
  _state.Set(DS.warningFrames, 0);
  _state.Set(DS.incomingMissileAlert, 0);
  _state.Set(DS.missionStage, 0);
  _state.Set(DS.fuelLeakDelayFrames, 0);
  _state.Set(DS.fuelLeakFrames, 0);
  _state.Set(DS.supernovaFrames, 0);
  _state.Set(DS.supernovaHeat, 0);
  _state.Set(DS.maskShipDestroyed, 0);
  _state.Set(DS.thargoidInvasionActive, 0);
  _state.Set(DS.maskMissionShipsLeft, 0);
  _state.Set(DS.invadedStationDestroyed, 0);
  _state.Set(DS.forceMisjump, 0);
  _state.Set(DS.messageFrames, 0);
  _state.Set(DS.missileState, 0);
  _state.Set(DS.fledMaskShip, 0);
  _state.Set(DS.maskSystemJumps, 0);
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
    // DOS 1 reports no major version, and is not enough. What the print leaves ExitToDos overwrites.
    _guest.Devices().PrintDosString(_guest.DataSegment(), DS.dosVersionMessage.offset);
    ExitToDos(_guest.State(), _guest.Devices());
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
      ExitToDos(_guest.State(), _guest.Devices());
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

void WipeProgram(GameState& _state, Hardware& _hardware, bool _backward)
{
  _hardware.DisableInterrupts();
  // REP STOSB of AL, the code segment's low byte, CX = 8F31h - 0564h times from DI = 0564h.
  const std::uint8_t fill = Low(_state.CodeSegment());
  const auto step = static_cast<std::uint16_t>(_backward ? 0xFFFF : 1);
  std::uint16_t at = WIPE_FIRST;
  for (auto left = static_cast<std::uint16_t>(WIPE_END - WIPE_FIRST); left != 0; --left)
  {
    _state.SetCodeByte(at, fill);
    at = Offset(at, step);
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

void ShowCredits(GameState& _state, Hardware& _hardware, bool _backward)
{
  ClearDrawBuffer(_state, _backward);
  // Each line from SI, one past the last line's NUL, at DI, a row of characters below the last line's start. The original pushes
  // and pops the count and the place round the print; its LOOP's turns carry the count, the text and the place.
  std::uint16_t text = DS.creditsScreenText.offset;
  std::uint16_t place = CREDITS_FIRST_LINE;
  for (std::uint16_t linesLeft = CREDITS_LINES;;)
  {
    _state.Set(DS.textPaperPattern, 0);
    text = Offset(DrawViewString(_state, text, WHITE_MASK, place).end, 1);
    place = Offset(place, CREDITS_LINE_STEP);
    if (--linesLeft == 0)
    {
      break;
    }
    _hardware.LoopTurn(CREDITS_LINE, {linesLeft, text, place});
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

// ── The entries of the de-assembled routines ──

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
// CopyProtection never writes ES, and Start goes on with it into InstallDivideAndKeyboardInterrupts, which takes the
// interrupt table's segment there: compared.
constexpr Machine::NativeContract PROTECTION{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_SI_DI{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI_ES_INTERRUPTS_OFF{REGISTER_AX | REGISTER_CX | REGISTER_DI | REGISTER_ES,
                                                                      Machine::FLAG_INTERRUPT};
// ShowCredits': all but DS, and BP, which the status screen after the title reads (ShowCreditsEntry).
constexpr Machine::NativeContract SHOWS_CREDITS{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_ES, 0};
// Start sets DS to the data segment, and never returns.
constexpr Machine::NativeContract CLOBBERS_EVERY_REGISTER{REGISTER_ALL, 0};

constexpr Machine::NativeWait ALWAYS = Machine::NativeWait::Always;
constexpr Machine::NativeWait SOMETIMES = Machine::NativeWait::Sometimes;

} // namespace

void InstallDivideAndKeyboardInterruptsEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  InstallDivideAndKeyboardInterrupts(_guest.State(), _guest.Devices(), regs.es);
  // ES on the CGA's memory through AX.
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = Guest::VIDEO_SEGMENT;
  _guest.Clobber(CLOBBERS_AX);
}

void RestoreDivideAndKeyboardInterruptsEntry(Guest& _guest)
{
  RestoreDivideAndKeyboardInterrupts(_guest.State(), _guest.Devices());
  _guest.Regs().es = 0;
  _guest.Clobber(CLOBBERS_AX);
}

void CheckCheatArgumentEntry(Guest& _guest)
{
  CheckCheatArgument(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX_CX_SI);
}

void WipeProgramEntry(Guest& _guest)
{
  WipeProgram(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_AX_CX_DI_ES_INTERRUPTS_OFF);
}

void CopyProtectionEntry(Guest& _guest)
{
  CopyProtection(_guest.State(), _guest.Devices());
  _guest.Clobber(PROTECTION);
}

void ShowCreditsEntry(Guest& _guest)
{
  ShowCredits(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  // FinishSpaceViewFrame's CLD, which StartNewGame's copy goes by after it, and PresentSpaceView's MOV BP,20h, which the status
  // screen after the title hands SelectSystemAtCursor as the count when no system is on the chart; WaitForTimerTick keeps both.
  _guest.SetFlag(Machine::FLAG_DIRECTION, false);
  _guest.Regs().bp = PRESENT_SPACE_VIEW_BP;
  _guest.Clobber(SHOWS_CREDITS);
}

void StartNewGameEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  StartNewGame(_guest.State(), _guest.Flag(Machine::FLAG_DIRECTION));
  // MOV AX,DS / MOV ES,AX for the copy.
  regs.ax = regs.ds;
  regs.es = regs.ds;
  _guest.Clobber(CLOBBERS_AX_CX_SI_DI);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x0000, "Start", &Start, CLOBBERS_EVERY_REGISTER, Machine::NativeReturn::Far, 0, ALWAYS},
  NativeEntry{0x0105, "InstallDivideAndKeyboardInterrupts", &InstallDivideAndKeyboardInterruptsEntry, CLOBBERS_AX},
  NativeEntry{0x0148, "RestoreDivideAndKeyboardInterrupts", &RestoreDivideAndKeyboardInterruptsEntry, CLOBBERS_AX},
  NativeEntry{0x02A5, "CheckCheatArgument", &CheckCheatArgumentEntry, CLOBBERS_AX_BX_CX_SI},
  // CopyProtection waits on the question's path, in its loops and WaitForTimerTick, which the D5 byte closes.
  NativeEntry{0x04A3, "CopyProtection", &CopyProtectionEntry, PROTECTION, Machine::NativeReturn::Near, 0, SOMETIMES},
  NativeEntry{0x0554, "WipeProgram", &WipeProgramEntry, CLOBBERS_AX_CX_DI_ES_INTERRUPTS_OFF},
  NativeEntry{0x4671, "StartNewGame", &StartNewGameEntry, CLOBBERS_AX_CX_SI_DI},
  NativeEntry{0x7D30, "GameLoop", &GameLoop, CLOBBERS_ALL, Machine::NativeReturn::Near, 0, ALWAYS},
  NativeEntry{0x8F02, "ShowCredits", &ShowCreditsEntry, SHOWS_CREDITS, Machine::NativeReturn::Near, 0, ALWAYS},
};

} // namespace

std::span<const NativeEntry> StartUpEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
