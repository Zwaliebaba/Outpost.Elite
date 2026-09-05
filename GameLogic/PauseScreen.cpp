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

  MusicChange NoteMusicSwitch(std::uint8_t _mutok, std::uint8_t& _mutokOld, std::uint8_t _dockingComputer) noexcept
  {
    // 6502: LDA MUTOK / CMP MUTOKOLD / BEQ P%+5 -- nothing to do unless the switch moved.
    if (_mutok == _mutokOld)
    {
      return MusicChange::None;
    }

    // 6502: .MUTOKCH STA MUTOKOLD -- and A is `MUTOK`, so the record is of the NEW setting.
    _mutokOld = _mutok;

    /*
     * 6502: EOR #&FF / AND auto / BMI april16.
     *
     * The music is ON when `MUTOK` is CLEAR, so the `EOR` is what turns "switched on" into a set
     * bit 7; the `AND` then requires the docking computer to be flying. Only that combination
     * starts it -- everything else falls into `stopbd`.
     */
    const std::uint8_t started = static_cast<std::uint8_t>(static_cast<std::uint8_t>(_mutok ^ 0xFFu) & _dockingComputer);
    return ((started & 0x80u) != 0u) ? MusicChange::StartNow : MusicChange::Stop;
  }

  PausePass PressPauseKey(const OptionBlock& _options, std::uint8_t& _soundDisabled, std::uint8_t& _mutokOld, std::uint8_t _dockingComputer,
                          std::uint8_t _key) noexcept
  {
    PausePass pass;

    /*
     * 6502: CPX #&02 / BNE DK6 / STX DNOIZ.
     *
     * `STX` and not `LDA #1 / STA`: the key code itself goes into the flag, so pressing "2" leaves
     * a TWO there. Everything that reads `DNOIZ` tests it for non-zero, so the value never matters
     * -- and the port stores the same two, because the byte is in the commander file.
     */
    if (_key == SOUND_OFF_KEY)
    {
      _soundDisabled = _key;
    }

    // 6502: .DK6 LDY #0 / DKL4 ... / nosillytog -- ten toggles, or thirteen behind `PATG`.
    pass.delayFrames = ApplyOptionKey(_options, _key);

    // 6502: LDA MUTOK / CMP MUTOKOLD / BEQ P%+5 / JSR MUTOKCH -- and `MUTOK` is the eighth byte of
    // the block, so one of the toggles above may have just moved it.
    pass.music = NoteMusicSwitch(*_options[OPTION_MUTOK], _mutokOld, _dockingComputer);

    // 6502: CPX #&33 / BNE DK7 / LDA #0 / STA DNOIZ -- and this one DOES load a value.
    if (_key == SOUND_ON_KEY)
    {
      _soundDisabled = 0u;
    }

    // 6502: .DK7 CPX #&07 / BNE P%+5 / JMP DEATH2 -- and `DEATH2` does not come back.
    if (_key == QUIT_KEY)
    {
      pass.outcome = PauseOutcome::Quit;
      return pass;
    }

    // 6502: CPX #&0D / BNE FREEZE -- anything else goes round again.
    pass.outcome = (_key == RESUME_KEY) ? PauseOutcome::Resumed : PauseOutcome::Paused;
    return pass;
  }

} // namespace Elite
