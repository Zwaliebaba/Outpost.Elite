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

} // namespace Elite
