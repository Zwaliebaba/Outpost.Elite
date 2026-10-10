#include "pch.h"

#include "StateDigest.h"

#include "Cga.h"
#include "GameState.h"
#include "Reference.h"
#include "Sha256.h"

namespace Elite
{

std::string GameStateDigest(const GameState& _state, const Machine::Cga& _video)
{
  const std::span<const std::uint8_t> data = _state.DataBytes();
  Machine::Sha256 hash;
  hash.Update(data.first(STACK_FIRST_OFFSET));
  hash.Update(data.subspan(STACK_END_OFFSET));
  hash.Update(_state.VideoBytes());

  std::array<std::uint8_t, 2 + Machine::Cga::CRTC_REGISTER_COUNT> registers{};
  registers[0] = _video.ModeControl();
  registers[1] = _video.ColorSelect();
  for (std::uint32_t index = 0; index < Machine::Cga::CRTC_REGISTER_COUNT; ++index)
  {
    registers[2 + index] = _video.Crtc(static_cast<Machine::CrtcRegister>(index));
  }
  hash.Update(registers);
  return Machine::Sha256::ToHex(hash.Finish());
}

} // namespace Elite
