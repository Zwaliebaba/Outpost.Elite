#include "pch.h"

#include "StateDigest.h"

#include "Cga.h"
#include "GameState.h"
#include "Sha256.h"

namespace Elite
{

namespace
{

// What the player sees, the end of both digests: video memory, then the mode control, colour select and CRTC registers.
void HashPicture(Sha256& _hash, const GameState& _state, const Cga& _video)
{
  _hash.Update(_state.VideoBytes());

  std::array<std::uint8_t, 2 + Cga::CRTC_REGISTER_COUNT> registers{};
  registers[0] = _video.ModeControl();
  registers[1] = _video.ColorSelect();
  for (std::uint32_t index = 0; index < Cga::CRTC_REGISTER_COUNT; ++index)
  {
    registers[2 + index] = _video.Crtc(static_cast<CrtcRegister>(index));
  }
  _hash.Update(registers);
}

} // namespace

std::string GameStateDigest(const GameState& _state, const Cga& _video)
{
  Sha256 hash;
  hash.Update(_state.DataBytes());
  HashPicture(hash, _state, _video);
  return Sha256::ToHex(hash.Finish());
}

std::string PictureDigest(const GameState& _state, const Cga& _video)
{
  Sha256 hash;
  HashPicture(hash, _state, _video);
  return Sha256::ToHex(hash.Finish());
}

} // namespace Elite
