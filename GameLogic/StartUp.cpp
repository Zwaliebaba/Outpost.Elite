#include "pch.h"

#include "StartUp.h"

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

[[nodiscard]] std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word);
}

[[nodiscard]] std::uint16_t WithLow(std::uint16_t _word, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_word & 0xFF00) | _low);
}

[[nodiscard]] std::uint16_t WithHigh(std::uint16_t _word, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>((_word & 0x00FF) | (_high << 8));
}

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
    NextRandom(_guest);
    regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(regs.dx) & CGA_VERTICAL_RETRACE));
  } while (Low(regs.ax) == 0);
  NextRandom(_guest);
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

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_SI{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI, 0};
constexpr Machine::NativeContract CLOBBERS_ALL{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_SI_DI{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0};

constexpr std::array ENTRIES = {
  NativeEntry{0x0105, "InstallDivideAndKeyboardInterrupts", &InstallDivideAndKeyboardInterrupts, CLOBBERS_AX},
  NativeEntry{0x0148, "RestoreDivideAndKeyboardInterrupts", &RestoreDivideAndKeyboardInterrupts, CLOBBERS_AX},
  NativeEntry{0x02A5, "CheckCheatArgument", &CheckCheatArgument, CLOBBERS_AX_BX_CX_SI},
  NativeEntry{0x04A3, "CopyProtection", &CopyProtection, CLOBBERS_ALL},
  NativeEntry{0x4671, "StartNewGame", &StartNewGame, CLOBBERS_AX_CX_SI_DI},
};

} // namespace

std::span<const NativeEntry> StartUpEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
