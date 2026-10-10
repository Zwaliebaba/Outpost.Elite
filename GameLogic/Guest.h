// GameLogic/Guest.h
#pragma once

#include "DataField.h"
#include "Pc.h"

#include <cstdint>

namespace Elite
{

/// The machine as a native routine sees it (ADR-010): the registers, the reference's data segment by
/// name (DataOverlay.h) or by offset, its code segment, video memory, the stack, the ports, and the way
/// back into the original's code. One is made for each call of a native routine, around the Pc that
/// runs the program; everything it reads and writes goes through that Pc, so the replays, the journals
/// and paced time's wait detection see it.
class Guest
{
public:
  /// The CGA's video memory, which the reference writes directly.
  static constexpr std::uint16_t VIDEO_SEGMENT = 0xB800;

  Guest(Machine::Pc& _pc, std::uint16_t _codeSegment, std::uint16_t _dataSegment) noexcept
    : m_pc(_pc),
      m_codeSegment(_codeSegment),
      m_dataSegment(_dataSegment)
  {
  }

  [[nodiscard]] std::uint16_t CodeSegment() const noexcept
  {
    return m_codeSegment;
  }

  [[nodiscard]] std::uint16_t DataSegment() const noexcept
  {
    return m_dataSegment;
  }

  [[nodiscard]] Machine::Registers& Regs() noexcept
  {
    return m_pc.Processor().Regs();
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_pc;
  }

  [[nodiscard]] std::uint8_t Get(DataField<std::uint8_t> _field) const noexcept
  {
    return Byte(_field.offset);
  }

  [[nodiscard]] std::uint16_t Get(DataField<std::uint16_t> _field) const noexcept
  {
    return Word(_field.offset);
  }

  void Set(DataField<std::uint8_t> _field, std::uint8_t _value) noexcept
  {
    SetByte(_field.offset, _value);
  }

  void Set(DataField<std::uint16_t> _field, std::uint16_t _value) noexcept
  {
    SetWord(_field.offset, _value);
  }

  /// The byte at DS:_offset, where DS is the reference's data segment.
  [[nodiscard]] std::uint8_t Byte(std::uint16_t _offset) const noexcept
  {
    return m_pc.Ram().Read8(m_dataSegment, _offset);
  }

  /// The word at DS:_offset; at offset FFFFh its high byte is at offset 0, as on the 8088.
  [[nodiscard]] std::uint16_t Word(std::uint16_t _offset) const noexcept
  {
    return m_pc.Ram().Read16(m_dataSegment, _offset);
  }

  void SetByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_pc.Ram().Write8(m_dataSegment, _offset, _value);
  }

  void SetWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_pc.Ram().Write16(m_dataSegment, _offset, _value);
  }

  /// The byte at _segment:_offset, in any segment.
  [[nodiscard]] std::uint8_t FarByte(std::uint16_t _segment, std::uint16_t _offset) const noexcept
  {
    return m_pc.Ram().Read8(_segment, _offset);
  }

  [[nodiscard]] std::uint16_t FarWord(std::uint16_t _segment, std::uint16_t _offset) const noexcept
  {
    return m_pc.Ram().Read16(_segment, _offset);
  }

  void SetFarByte(std::uint16_t _segment, std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_pc.Ram().Write8(_segment, _offset, _value);
  }

  void SetFarWord(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_pc.Ram().Write16(_segment, _offset, _value);
  }

  /// The byte at CS:_offset: data the original keeps in its code segment, and the code it patches.
  [[nodiscard]] std::uint8_t CodeByte(std::uint16_t _offset) const noexcept
  {
    return FarByte(m_codeSegment, _offset);
  }

  [[nodiscard]] std::uint16_t CodeWord(std::uint16_t _offset) const noexcept
  {
    return FarWord(m_codeSegment, _offset);
  }

  void SetCodeByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    SetFarByte(m_codeSegment, _offset, _value);
  }

  void SetCodeWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    SetFarWord(m_codeSegment, _offset, _value);
  }

  /// The byte at B800:_offset, in the CGA's video memory.
  [[nodiscard]] std::uint8_t VideoByte(std::uint16_t _offset) const noexcept
  {
    return FarByte(VIDEO_SEGMENT, _offset);
  }

  [[nodiscard]] std::uint16_t VideoWord(std::uint16_t _offset) const noexcept
  {
    return FarWord(VIDEO_SEGMENT, _offset);
  }

  void SetVideoByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    SetFarByte(VIDEO_SEGMENT, _offset, _value);
  }

  void SetVideoWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    SetFarWord(VIDEO_SEGMENT, _offset, _value);
  }

  /// Pushes _value on the program's stack, as PUSH does.
  void Push(std::uint16_t _value) noexcept
  {
    Machine::Registers& regs = Regs();
    regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
    SetFarWord(regs.ss, regs.sp, _value);
  }

  /// Pops a word from the program's stack, as POP does.
  [[nodiscard]] std::uint16_t Pop() noexcept
  {
    Machine::Registers& regs = Regs();
    const std::uint16_t value = FarWord(regs.ss, regs.sp);
    regs.sp = static_cast<std::uint16_t>(regs.sp + 2);
    return value;
  }

  /// The word at SS:SP+_bytes: an argument a caller pushed, or, at a hooked entry, the return address
  /// (at 0) and what lies above it.
  [[nodiscard]] std::uint16_t StackWord(std::uint16_t _bytes) noexcept
  {
    const Machine::Registers& regs = Regs();
    return FarWord(regs.ss, static_cast<std::uint16_t>(regs.sp + _bytes));
  }

  void SetStackWord(std::uint16_t _bytes, std::uint16_t _value) noexcept
  {
    const Machine::Registers& regs = Regs();
    SetFarWord(regs.ss, static_cast<std::uint16_t>(regs.sp + _bytes), _value);
  }

  /// IN AL, _port.
  [[nodiscard]] std::uint8_t In8(std::uint16_t _port)
  {
    return m_pc.Ports().In8(_port);
  }

  /// OUT _port, AL.
  void Out8(std::uint16_t _port, std::uint8_t _value)
  {
    m_pc.Ports().Out8(_port, _value);
  }

  /// INT _vector (Pc::CallInterrupt): the BIOS, DOS and mouse calls, and the game's own handlers.
  void Interrupt(std::uint8_t _vector)
  {
    m_pc.CallInterrupt(_vector);
  }

  /// Whether _flag (FLAG_* in Registers.h) is set: what a routine reads of a flag a call left it.
  [[nodiscard]] bool Flag(std::uint16_t _flag) const noexcept
  {
    return (m_pc.Processor().Regs().flags & _flag) != 0;
  }

  /// Sets or clears _flag (FLAG_* in Registers.h): what a routine does for a flag its callers read.
  void SetFlag(std::uint16_t _flag, bool _set) noexcept
  {
    std::uint16_t& flags = Regs().flags;
    flags = static_cast<std::uint16_t>(_set ? (flags | _flag) : (flags & ~_flag));
  }

  /// Calls the original routine at CS:_offset and runs it to its return (Pc::CallNear).
  void Call(std::uint16_t _offset)
  {
    m_pc.CallNear(_offset);
  }

  /// What the original's instructions here would have taken, in 8088 cycles, for a device timed by the
  /// instructions executed (Pc::CountInstructionCycles): the game port, which a stick read counts.
  void CountCycles(Machine::Cycles _cycles) noexcept
  {
    m_pc.CountInstructionCycles(_cycles);
  }

  /// One turn of a waiting loop that found nothing to do (Pc::Wait): the clock moves to the next device
  /// event, the run ends there if that is its end, and an interrupt now due is taken. Only a routine
  /// hooked as one that waits may call it (NativeEntry::wait).
  void Wait()
  {
    m_pc.Wait();
  }

  /// The end of one turn of a loop, where the original jumps back (Pc::LoopTurn): paced time idles
  /// here when the turn changed nothing, as it would at the original's jump. Call it at every backward
  /// jump the original takes in a loop that can wait, with the registers the original has there. Only a
  /// routine hooked as one that waits may call it.
  void LoopTurn()
  {
    m_pc.LoopTurn();
  }

private:
  Machine::Pc& m_pc;
  std::uint16_t m_codeSegment;
  std::uint16_t m_dataSegment;
};

} // namespace Elite
