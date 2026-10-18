#include "pch.h"

#include "StateDigest.h"

#include "Display.h"
#include "GameState.h"
#include "Sha256.h"
#include "StateWriter.h"

#include <array>

namespace Elite
{

std::string GameStateDigest(const GameState& _state, const Display& _display)
{
  Sha256 hash;
  hash.Update(StateBytes(_state.Data()));
  const std::array<std::uint8_t, 1> colorSelect = {_display.ColorSelect()};
  hash.Update(colorSelect);
  return Sha256::ToHex(hash.Finish());
}

} // namespace Elite
