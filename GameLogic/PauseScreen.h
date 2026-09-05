#pragma once

#include "LookupTables.h"

#include <array>
#include <cstdint>

namespace Elite
{

  /*
   * The pause screen -- `DK4`, `FREEZE` and `DKS3` (slice 4e).
   *
   * `DOKEY` falls into `DK4` and the port has never followed it, which is recorded in
   * `Controls.cpp` as a comment and is what this slice answers: pressing the pause key stops the
   * game, and every configuration toggle in the game is reachable only from there.
   */

  /*
   * 6502: DAMP through MUSILLY -- the THIRTEEN configuration bytes, as `DKS3` addresses them.
   *
   * `DKS3` is `CMP TGINT,Y / LDA DAMP,Y / EOR #&FF / STA DAMP,Y`: entry Y of the key table and the
   * byte Y after `DAMP` are one pair, and the ASSEMBLER'S LAYOUT IS THE WHOLE RELATIONSHIP. There
   * is no other statement anywhere in the game of which key toggles which option.
   *
   * THE PORT DOES NOT HAVE THEM CONTIGUOUS, and moving them would touch eighty-seven call sites
   * across six headers to buy an invariant a sweep can establish instead. So this is thirteen
   * POINTERS in the assembler's order, built once, and the order is verified the only way it can
   * be: `TheTogglesMatchDKS3` presses all 256 key codes at every one of the thirteen positions and
   * compares the whole run against the shipped routine's. A pointer in the wrong slot fails on the
   * first key that hits it.
   *
   * &1D06 to &1D12 is thirteen bytes and `TGINT` is thirteen entries; `LookupTables.h` records
   * that as the check on the count.
   */
  inline constexpr std::size_t OPTION_COUNT = 13;

  /// 6502: CPY #(MUFOR-DAMP) -- the first loop stops here, so ten toggles are always live.
  inline constexpr std::size_t OPTION_COUNT_ALWAYS = 10;

  /*
   * 6502: BIT PATG / BPL nosillytog -- and the three music options behind it.
   *
   * `PATG` is `DAMP+2`, so the option that decides whether the last three can be toggled is itself
   * one of the toggles, two places into the same run. Switching the author names off hides three
   * other switches, which is the third unrelated thing that byte does (§6.121 has the other two).
   */
  inline constexpr std::size_t OPTION_PATG = 2;

  /// 6502: MUTOK is `DAMP+7` -- the eighth of the thirteen, and the one `MUTOKCH` watches.
  inline constexpr std::size_t OPTION_MUTOK = 7;

  /// The thirteen bytes `DKS3` walks, in the order `TGINT` names them. Never reordered.
  using OptionBlock = std::array<std::uint8_t*, OPTION_COUNT>;

  /*
   * 6502: DKS3 -- one toggle, for the key in X and the position in Y.
   *
   * `EOR #&FF` and not `EOR #1`: the bytes are 0 or 255 and every reader tests them with `BIT` or
   * `BMI` or `AND`, so a port that stored 1 would satisfy `BNE` and fail `BPL`. Returns whether it
   * flipped, because the caller rings the bell and waits twenty frames only when it did.
   */
  [[nodiscard]] bool ToggleOption(const OptionBlock& _options, std::uint8_t _key, std::size_t _at) noexcept;

  /*
   * 6502: DKL4 and DKL42 -- every toggle the key might be, which is ten or thirteen.
   *
   * Returns how many frames the pass owes `DELAY`, which is twenty per toggle that flipped. It is
   * counted rather than slept for the reason §6.138 gives: `check_gamelogic.py` forbids this
   * library a clock, and the platform decides how to spend the time.
   */
  [[nodiscard]] std::uint8_t ApplyOptionKey(const OptionBlock& _options, std::uint8_t _key) noexcept;

  /// 6502: LDY #20 / JSR DELAY -- what one toggle costs, and it is per toggle and not per pass.
  inline constexpr std::uint8_t TOGGLE_DELAY_FRAMES = 20;

  /*
   * 6502: MUTOKCH -- what happens when the docking-music switch is the one that just moved.
   *
   * `STA MUTOKOLD / EOR #&FF / AND auto / BMI april16`, and then it FALLS INTO `stopbd`. So
   * switching the music ON (`MUTOK` clear) while the docking computer is flying starts it
   * immediately, and everything else stops it -- and "stops it" goes through `stopbd`, which for
   * a FORCED setting starts it again instead. Two of the thirteen toggles have an effect the
   * moment they are pressed rather than the next time something reads them.
   */
  enum class MusicChange : std::uint8_t
  {
    None,     ///< `MUTOK` did not move, so `MUTOKCH` was not called
    StartNow, ///< 6502: BMI april16 -- switched on with the computer flying
    Stop,     ///< 6502: the fall-through into `stopbd`, which may start it again if `MUFOR` is set
  };

  /*
   * 6502: LDA MUTOK / CMP MUTOKOLD / BEQ P%+5 / JSR MUTOKCH.
   *
   * `MUTOKOLD` is how the routine notices, and it is written by `MUTOKCH` itself -- so the
   * comparison is against what the LAST pass saw and not against what the option was before the
   * key. Returns what the caller should do to the player, because the music belongs to phase 5.
   */
  [[nodiscard]] MusicChange NoteMusicSwitch(std::uint8_t _mutok, std::uint8_t& _mutokOld, std::uint8_t _dockingComputer) noexcept;

  /// What the pause screen decided about the key just pressed.
  enum class PauseOutcome : std::uint8_t
  {
    Paused,  ///< 6502: BNE FREEZE -- still frozen, go round again
    Resumed, ///< 6502: CPX #&0D -- CLR/HOME, and `DK2`'s `RTS`
    Quit,    ///< 6502: CPX #&07 / JMP DEATH2 -- and it does not come back
  };

  /// 6502: CPX #&40 -- the key `DK4` freezes on, which is what `DOKEY` falls into and the port has
  /// never followed (`Controls.cpp` says so in a comment).
  inline constexpr std::uint8_t PAUSE_KEY = 0x40;

  /// 6502: CPX #&0D, CPX #&07, CPX #&02 and CPX #&33 -- resume, quit, and the two sound keys.
  inline constexpr std::uint8_t RESUME_KEY = 0x0D;
  inline constexpr std::uint8_t QUIT_KEY = 0x07;
  inline constexpr std::uint8_t SOUND_OFF_KEY = 0x02;
  inline constexpr std::uint8_t SOUND_ON_KEY = 0x33;

  /// What one pass round `FREEZE` did, which is a toggle or two and then a decision.
  struct PausePass
  {
    PauseOutcome outcome = PauseOutcome::Paused;
    MusicChange music = MusicChange::None;
    std::uint8_t delayFrames = 0; ///< twenty per toggle that flipped
  };

  /*
   * 6502: FREEZE -- one pass round the pause loop, for the key that was read.
   *
   * The original blocks: `JSR WSCAN / JSR RDKEY` and then round again until CLR/HOME. A port
   * cannot block, so this is ONE pass and the executable owns the loop -- the same shape as
   * `RunLoopTail`'s frames and for the same reason.
   *
   * `STX DNOIZ` with X still holding the key is the joke worth keeping: pressing "2" stores TWO in
   * the sound flag rather than a true, because the routine never loads a value. Any non-zero
   * disables sound, so the byte the game leaves there is the key code itself.
   */
  [[nodiscard]] PausePass PressPauseKey(const OptionBlock& _options, std::uint8_t& _soundDisabled, std::uint8_t& _mutokOld,
                                        std::uint8_t _dockingComputer, std::uint8_t _key) noexcept;

} // namespace Elite
