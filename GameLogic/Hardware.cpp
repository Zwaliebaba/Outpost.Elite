#include "pch.h"

#include "Hardware.h"

#include "Arithmetic.h"
#include "Pc.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>

namespace Elite
{

namespace
{

constexpr std::uint16_t PIC_COMMAND_PORT = 0x20;
constexpr std::uint8_t END_OF_INTERRUPT = 0x20;
constexpr std::uint16_t PIT_CHANNEL_0_PORT = 0x40;
constexpr std::uint16_t PIT_CHANNEL_2_PORT = 0x42;
constexpr std::uint16_t PIT_COMMAND_PORT = 0x43;
constexpr std::uint8_t PIT_CHANNEL_2_SQUARE_WAVE = 0xB6; // channel 2, low then high byte, mode 3
constexpr std::uint16_t SYSTEM_CONTROL_PORT = 0x61;
constexpr std::uint16_t KEYBOARD_DATA_PORT = 0x60;
constexpr std::uint8_t KEYBOARD_ACKNOWLEDGE = 0x80; // port 61h bit 7, the XT's PB7
constexpr std::uint16_t GAME_PORT = 0x201;

constexpr std::uint8_t VIDEO_VECTOR = 0x10;
constexpr std::uint8_t BIOS_KEYBOARD_VECTOR = 0x16;
constexpr std::uint8_t DOS_VECTOR = 0x21;
constexpr std::uint8_t MOUSE_VECTOR = 0x33;
constexpr std::uint8_t VIDEO_SET_MODE = 0x00;
constexpr std::uint8_t BIOS_READ_KEY = 0x00;
constexpr std::uint8_t BIOS_KEY_STATUS = 0x01;
constexpr std::uint8_t DOS_PRINT_STRING = 0x09;
constexpr std::uint16_t MOUSE_RESET = 0x0000;
constexpr std::uint16_t MOUSE_BUTTON_PRESSES = 0x0005;
constexpr std::uint16_t MOUSE_MOTION = 0x000B;

// A service operation: INT _vector with the registers _setInputs leaves (from the processor's own), and the registers the
// service leaves returned. The processor's registers are then put back as they were, so the operation has no register effect
// of its own.
template <typename SetInputs> Machine::Registers CallService(Machine::Pc& _pc, std::uint8_t _vector, SetInputs _setInputs)
{
  Machine::Registers& regs = _pc.Processor().Regs();
  const Machine::Registers kept = regs;
  _setInputs(regs);
  _pc.CallInterrupt(_vector);
  const Machine::Registers left = regs;
  regs = kept;
  return left;
}

} // namespace

std::uint8_t Hardware::SystemControl()
{
  return m_pc.Ports().In8(SYSTEM_CONTROL_PORT);
}

void Hardware::SetSystemControl(std::uint8_t _value)
{
  m_pc.Ports().Out8(SYSTEM_CONTROL_PORT, _value);
}

void Hardware::SetToneDivisor(std::uint16_t _divisor)
{
  m_pc.Ports().Out8(PIT_COMMAND_PORT, PIT_CHANNEL_2_SQUARE_WAVE);
  m_pc.Ports().Out8(PIT_CHANNEL_2_PORT, Low(_divisor));
  m_pc.Ports().Out8(PIT_CHANNEL_2_PORT, High(_divisor));
}

void Hardware::SetTickDivisor(std::uint8_t _mode, std::uint16_t _divisor)
{
  m_pc.Ports().Out8(PIT_COMMAND_PORT, _mode);
  m_pc.Ports().Out8(PIT_CHANNEL_0_PORT, Low(_divisor));
  m_pc.Ports().Out8(PIT_CHANNEL_0_PORT, High(_divisor));
}

void Hardware::EndOfInterrupt()
{
  m_pc.Ports().Out8(PIC_COMMAND_PORT, END_OF_INTERRUPT);
}

void Hardware::EnableInterrupts() noexcept
{
  m_pc.Processor().Regs().flags |= Machine::FLAG_INTERRUPT;
}

void Hardware::LoopTurn(std::uint16_t _loop, std::initializer_list<std::uint16_t> _carried)
{
  std::array<std::uint16_t, Machine::Pc::MOST_TURN_WORDS> signature{};
  if (_carried.size() >= signature.size())
  {
    throw std::logic_error("Hardware::LoopTurn: more carried values than a turn signature holds");
  }
  signature[0] = _loop;
  std::ranges::copy(_carried, signature.begin() + 1);
  m_pc.LoopTurn(std::span<const std::uint16_t>(signature.data(), _carried.size() + 1));
}

void Hardware::DisableInterrupts() noexcept
{
  m_pc.Processor().Regs().flags = static_cast<std::uint16_t>(m_pc.Processor().Regs().flags & ~Machine::FLAG_INTERRUPT);
}

std::uint8_t Hardware::KeyboardData()
{
  return m_pc.Ports().In8(KEYBOARD_DATA_PORT);
}

void Hardware::AcknowledgeKeyboard()
{
  const auto pulse = static_cast<std::uint8_t>(m_pc.Ports().In8(SYSTEM_CONTROL_PORT) | KEYBOARD_ACKNOWLEDGE);
  m_pc.Ports().Out8(SYSTEM_CONTROL_PORT, pulse);
  m_pc.Ports().Out8(SYSTEM_CONTROL_PORT, static_cast<std::uint8_t>(pulse & ~KEYBOARD_ACKNOWLEDGE));
}

std::uint8_t Hardware::GamePortButtons()
{
  return m_pc.Ports().In8(GAME_PORT);
}

MouseReset Hardware::ResetMouse()
{
  const Machine::Registers left = CallService(m_pc, MOUSE_VECTOR, [](Machine::Registers& _regs) { _regs.ax = MOUSE_RESET; });
  return MouseReset{left.ax, left.bx};
}

MousePresses Hardware::ReadMousePresses(std::uint16_t _button)
{
  const Machine::Registers left = CallService(m_pc, MOUSE_VECTOR,
                                              [_button](Machine::Registers& _regs)
                                              {
                                                _regs.ax = MOUSE_BUTTON_PRESSES;
                                                _regs.bx = _button;
                                              });
  return MousePresses{left.ax, left.bx, left.cx, left.dx};
}

MouseMotion Hardware::ReadMouseMotion()
{
  const Machine::Registers left = CallService(m_pc, MOUSE_VECTOR, [](Machine::Registers& _regs) { _regs.ax = MOUSE_MOTION; });
  return MouseMotion{left.cx, left.dx};
}

void Hardware::SetVideoMode(std::uint8_t _mode)
{
  CallService(m_pc, VIDEO_VECTOR, [_mode](Machine::Registers& _regs) { _regs.ax = Join(VIDEO_SET_MODE, _mode); });
}

void Hardware::PrintDosString(std::uint16_t _segment, std::uint16_t _offset)
{
  CallService(m_pc, DOS_VECTOR,
              [_segment, _offset](Machine::Registers& _regs)
              {
                SetHigh(_regs.ax, DOS_PRINT_STRING);
                _regs.ds = _segment;
                _regs.dx = _offset;
              });
}

std::optional<std::uint16_t> Hardware::PeekBiosKey()
{
  const Machine::Registers left =
    CallService(m_pc, BIOS_KEYBOARD_VECTOR, [](Machine::Registers& _regs) { SetHigh(_regs.ax, BIOS_KEY_STATUS); });
  if ((left.flags & Machine::FLAG_ZERO) != 0)
  {
    return std::nullopt;
  }
  return left.ax;
}

std::uint16_t Hardware::ReadBiosKey()
{
  return CallService(m_pc, BIOS_KEYBOARD_VECTOR, [](Machine::Registers& _regs) { SetHigh(_regs.ax, BIOS_READ_KEY); }).ax;
}

// ── SaveLoad, StartUp, Timer and Input (level 4, group C) ──

namespace
{

constexpr std::uint16_t CGA_STATUS_PORT = 0x3DA;
constexpr std::uint8_t TIMER_VECTOR = 0x08;
constexpr std::uint16_t TIMER_VECTOR_OFFSET = TIMER_VECTOR * 4; // in the interrupt table at 0000:0000
constexpr std::uint16_t TIMER_VECTOR_SEGMENT = TIMER_VECTOR_OFFSET + 2;
constexpr std::uint8_t TIME_OF_DAY_VECTOR = 0x1A;
constexpr std::uint8_t READ_CLOCK = 0x00;
constexpr std::uint8_t SET_CLOCK = 0x01;
constexpr std::uint16_t DOS_FLUSH_AND_READ_LINE = 0x0C0A; // AH=0Ch, flush the buffer, then AL=0Ah, buffered input
constexpr std::uint8_t DOS_SET_TRANSFER_AREA = 0x1A;
constexpr std::uint8_t DOS_CREATE = 0x3C;
constexpr std::uint8_t DOS_OPEN = 0x3D;
constexpr std::uint8_t DOS_CLOSE = 0x3E;
constexpr std::uint8_t DOS_READ = 0x3F;
constexpr std::uint8_t DOS_WRITE = 0x40;
constexpr std::uint8_t DOS_DELETE = 0x41;
constexpr std::uint8_t DOS_ATTRIBUTES = 0x43;
constexpr std::uint8_t GET_ATTRIBUTES = 0x00; // AL
constexpr std::uint8_t SET_ATTRIBUTES = 0x01;
constexpr std::uint8_t DOS_FIND_FIRST = 0x4E;
constexpr std::uint8_t DOS_FIND_NEXT = 0x4F;

// CF and AX, as a DOS file service leaves them.
[[nodiscard]] DosAnswer Answer(const Machine::Registers& _left) noexcept
{
  return DosAnswer{(_left.flags & Machine::FLAG_CARRY) != 0, _left.ax};
}

// A DOS service on the file named, or the pattern given, at _segment:_name: AH=_function and DS:DX the name, then what
// _setMore sets, the rest as the processor holds it.
template <typename SetMore>
[[nodiscard]] Machine::Registers CallDosOnName(Machine::Pc& _pc, std::uint8_t _function, std::uint16_t _segment, std::uint16_t _name,
                                               SetMore _setMore)
{
  return CallService(_pc, DOS_VECTOR,
                     [&](Machine::Registers& _regs)
                     {
                       SetHigh(_regs.ax, _function);
                       _regs.ds = _segment;
                       _regs.dx = _name;
                       _setMore(_regs);
                     });
}

// For a DOS service on a name that reads no other register.
void SetNothingMore(Machine::Registers&) noexcept {}

// Int 21h AH=_function on the file _handle, with _bytes at _segment:_buffer: a read or a write.
[[nodiscard]] DosAnswer TransferFile(Machine::Pc& _pc, std::uint8_t _function, std::uint16_t _handle, std::uint16_t _segment,
                                     std::uint16_t _buffer, std::uint16_t _bytes)
{
  return Answer(CallService(_pc, DOS_VECTOR,
                            [=](Machine::Registers& _regs)
                            {
                              SetHigh(_regs.ax, _function);
                              _regs.bx = _handle;
                              _regs.cx = _bytes;
                              _regs.ds = _segment;
                              _regs.dx = _buffer;
                            }));
}

} // namespace

std::uint8_t Hardware::CgaStatus()
{
  return m_pc.Ports().In8(CGA_STATUS_PORT);
}

void Hardware::FireGamePort(std::uint8_t _value)
{
  m_pc.Ports().Out8(GAME_PORT, _value);
}

std::uint8_t Hardware::GamePortOneShots()
{
  return m_pc.Ports().In8(GAME_PORT);
}

void Hardware::CountInstructionCycles(Machine::Cycles _cycles) noexcept
{
  m_pc.CountInstructionCycles(_cycles);
}

BiosClock Hardware::ReadBiosClock()
{
  const Machine::Registers left = CallService(m_pc, TIME_OF_DAY_VECTOR, [](Machine::Registers& _regs) { SetHigh(_regs.ax, READ_CLOCK); });
  return BiosClock{(std::uint32_t{left.cx} << 16) | left.dx, Low(left.ax)};
}

void Hardware::SetBiosClock(std::uint32_t _ticks)
{
  CallService(m_pc, TIME_OF_DAY_VECTOR,
              [_ticks](Machine::Registers& _regs)
              {
                SetHigh(_regs.ax, SET_CLOCK);
                _regs.cx = static_cast<std::uint16_t>(_ticks >> 16);
                _regs.dx = static_cast<std::uint16_t>(_ticks);
              });
}

void Hardware::RunBiosTimerTick(std::uint16_t _segment, std::uint16_t _offset)
{
  Machine::Memory& memory = m_pc.Ram();
  const std::uint16_t offset = memory.Read16(0, TIMER_VECTOR_OFFSET);
  const std::uint16_t segment = memory.Read16(0, TIMER_VECTOR_SEGMENT);
  memory.Write16(0, TIMER_VECTOR_OFFSET, _offset);
  memory.Write16(0, TIMER_VECTOR_SEGMENT, _segment);
  m_pc.CallInterrupt(TIMER_VECTOR);
  memory.Write16(0, TIMER_VECTOR_OFFSET, offset);
  memory.Write16(0, TIMER_VECTOR_SEGMENT, segment);
}

void Hardware::ReadDosLine(std::uint16_t _segment, std::uint16_t _offset)
{
  CallService(m_pc, DOS_VECTOR,
              [_segment, _offset](Machine::Registers& _regs)
              {
                _regs.ax = DOS_FLUSH_AND_READ_LINE;
                _regs.ds = _segment;
                _regs.dx = _offset;
              });
}

void Hardware::SetDiskTransferArea(std::uint16_t _segment, std::uint16_t _offset)
{
  CallService(m_pc, DOS_VECTOR,
              [_segment, _offset](Machine::Registers& _regs)
              {
                SetHigh(_regs.ax, DOS_SET_TRANSFER_AREA);
                _regs.ds = _segment;
                _regs.dx = _offset;
              });
}

DosFileAttributes Hardware::ReadFileAttributes(std::uint16_t _segment, std::uint16_t _name)
{
  const Machine::Registers left =
    CallDosOnName(m_pc, DOS_ATTRIBUTES, _segment, _name, [](Machine::Registers& _regs) { SetLow(_regs.ax, GET_ATTRIBUTES); });
  return DosFileAttributes{Answer(left), left.cx};
}

DosAnswer Hardware::SetFileAttributes(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes)
{
  return Answer(CallDosOnName(m_pc, DOS_ATTRIBUTES, _segment, _name,
                              [_attributes](Machine::Registers& _regs)
                              {
                                SetLow(_regs.ax, SET_ATTRIBUTES);
                                _regs.cx = _attributes;
                              }));
}

DosAnswer Hardware::CreateFile(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes)
{
  return Answer(CallDosOnName(m_pc, DOS_CREATE, _segment, _name, [_attributes](Machine::Registers& _regs) { _regs.cx = _attributes; }));
}

DosAnswer Hardware::OpenFile(std::uint16_t _segment, std::uint16_t _name, std::uint8_t _access)
{
  return Answer(CallDosOnName(m_pc, DOS_OPEN, _segment, _name, [_access](Machine::Registers& _regs) { SetLow(_regs.ax, _access); }));
}

DosAnswer Hardware::ReadFile(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes)
{
  return TransferFile(m_pc, DOS_READ, _handle, _segment, _buffer, _bytes);
}

DosAnswer Hardware::WriteFile(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes)
{
  return TransferFile(m_pc, DOS_WRITE, _handle, _segment, _buffer, _bytes);
}

DosAnswer Hardware::CloseFile(std::uint16_t _handle)
{
  return Answer(CallService(m_pc, DOS_VECTOR,
                            [_handle](Machine::Registers& _regs)
                            {
                              SetHigh(_regs.ax, DOS_CLOSE);
                              _regs.bx = _handle;
                            }));
}

DosAnswer Hardware::DeleteFile(std::uint16_t _segment, std::uint16_t _name)
{
  return Answer(CallDosOnName(m_pc, DOS_DELETE, _segment, _name, &SetNothingMore));
}

DosAnswer Hardware::FindFirstFile(std::uint16_t _segment, std::uint16_t _pattern, std::uint16_t _attributes)
{
  return Answer(
    CallDosOnName(m_pc, DOS_FIND_FIRST, _segment, _pattern, [_attributes](Machine::Registers& _regs) { _regs.cx = _attributes; }));
}

DosAnswer Hardware::FindNextFile()
{
  return Answer(CallService(m_pc, DOS_VECTOR, [](Machine::Registers& _regs) { SetHigh(_regs.ax, DOS_FIND_NEXT); }));
}

} // namespace Elite
