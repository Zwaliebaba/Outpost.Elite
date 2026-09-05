#include "pch.h"

#include "PauseScreen.h"

namespace Elite
{

  bool ToggleOption(const OptionBlock& _options, std::uint8_t _key, std::size_t _at) noexcept
  {
    // 6502: TXA / CMP TGINT,Y / BNE Dk3.
    if (_at >= OPTION_COUNT || _key != OPTION_KEY_TABLE[_at])
    {
      return false;
    }

    // 6502: LDA DAMP,Y / EOR #&FF / STA DAMP,Y -- all eight bits, so the byte is 0 or 255.
    std::uint8_t* const option = _options[_at];
    *option = static_cast<std::uint8_t>(*option ^ 0xFFu);
    return true;
  }

  std::uint8_t ApplyOptionKey(const OptionBlock& _options, std::uint8_t _key) noexcept
  {
    /*
     * 6502: LDY #0 / .DKL4 JSR DKS3 / INY / CPY #(MUFOR-DAMP) / BNE DKL4, and then `BIT PATG /
     * BPL nosillytog` before the same loop again to `MUSILLY+1`.
     *
     * EVERY POSITION IS TRIED, not just the first match. Two toggles could in principle share a
     * key and both would flip; none do, and the loop is transcribed rather than short-circuited
     * because what makes that true is the contents of a table and not the shape of the code.
     */
    std::uint8_t frames = 0;

    for (std::size_t at = 0; at < OPTION_COUNT_ALWAYS; ++at)
    {
      if (ToggleOption(_options, _key, at))
      {
        frames = static_cast<std::uint8_t>(frames + TOGGLE_DELAY_FRAMES);
      }
    }

    // 6502: BIT PATG / BPL nosillytog -- BIT tests bit 7, so a `PATG` of 1 would NOT open the
    // second loop. That is why `DKS3` flips with `EOR #&FF` and not with an increment.
    if ((*_options[OPTION_PATG] & 0x80u) == 0u)
    {
      return frames;
    }

    for (std::size_t at = OPTION_COUNT_ALWAYS; at < OPTION_COUNT; ++at)
    {
      if (ToggleOption(_options, _key, at))
      {
        frames = static_cast<std::uint8_t>(frames + TOGGLE_DELAY_FRAMES);
      }
    }

    return frames;
  }

} // namespace Elite
