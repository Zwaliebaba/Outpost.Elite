#include "pch.h"

#include "StateDigest.h"

#include "Pc.h"
#include "Reference.h"
#include "Sha256.h"

namespace Elite
{

std::string GameStateDigest(const Machine::Pc& _pc, const Machine::LoadedProgram& _program)
{
  const std::span<const std::uint8_t> memory = _pc.Ram().Bytes();
  const std::uint32_t data = Machine::Memory::Linear(DataSegment(_program), 0);
  Machine::Sha256 hash;
  // The data segment never wraps past 1 MiB here: it sits in the low 640 KiB.
  hash.Update(memory.subspan(data, STACK_FIRST_OFFSET));
  hash.Update(memory.subspan(data + STACK_END_OFFSET, 0x10000u - STACK_END_OFFSET));
  hash.Update(memory.subspan(Machine::Cga::VIDEO_MEMORY_LINEAR, Machine::Cga::VIDEO_MEMORY_BYTES));

  const Machine::Cga& video = _pc.Video();
  std::array<std::uint8_t, 2 + Machine::Cga::CRTC_REGISTER_COUNT> registers{};
  registers[0] = video.ModeControl();
  registers[1] = video.ColorSelect();
  for (std::uint32_t index = 0; index < Machine::Cga::CRTC_REGISTER_COUNT; ++index)
  {
    registers[2 + index] = video.Crtc(static_cast<Machine::CrtcRegister>(index));
  }
  hash.Update(registers);
  return Machine::Sha256::ToHex(hash.Finish());
}

} // namespace Elite
