#pragma once

#include <cstdint>

/*
 * Constants the game is built with (Source-Inventory section 6).
 *
 * These come from the master file's preamble rather than from any routine, and they carry the
 * original names so that a reader with the commentary open can find them. Build options live
 * here too, which is how the plan keeps a variant a switch rather than a fork (ADR-001 section 2).
 */

namespace Elite
{

  /// Which release this port reproduces. Only one variant is built today; the enum exists so that
  /// a second one is a value rather than a second code path.
  enum class Variant : std::uint8_t
  {
    Gma85Ntsc
  };

  inline constexpr Variant VARIANT = Variant::Gma85Ntsc;

  /// Whether the game starts with a maxed-out commander. False is the shipping setting.
  inline constexpr bool MAX_COMMANDER = false;

  // ---- text ------------------------------------------------------------------------------

  /// 6502: RE -- the byte the recursive token table is hidden behind. The table is stored with
  /// every character exclusive-ored against this, purely to stop the text being readable in a
  /// dump of the binary. The port keeps the obfuscation rather than storing plain text, because
  /// the table is extracted verbatim and the routine that reads it is being verified against the
  /// original.
  inline constexpr std::uint8_t RECURSIVE_TOKEN_KEY = 0x23;

  /// 6502: VE -- the same trick, for the extended token table.
  inline constexpr std::uint8_t EXTENDED_TOKEN_KEY = 0x57;

  /// 6502: LL -- the width justified text wraps at, in characters.
  inline constexpr std::uint8_t JUSTIFIED_LINE_WIDTH = 30;

} // namespace Elite
