// Machine/CgaFont.h
#pragma once

#include <cstdint>

namespace Machine
{

/// The 8x8 code-page 437 font the CGA's character generator draws text modes with.
///
/// Ruling D14 (Reverse-Engineering-Plan.md §8): the reference does not carry the CGA's font, which
/// lived in the card's own ROM, so this project draws its own, near-identical in look and not
/// byte-exact. Every glyph in CgaFont.cpp was drawn by hand for this project, dot by dot. No byte
/// of it is copied from IBM, from a BIOS or ROM dump, from an emulator, from a font file or from
/// the web.
///
/// Drawn: 0x00 and 0xFF (blank, as on the CGA), the printable ASCII range 0x20-0x7E, and the
/// shades, box-drawing pieces and blocks 0xB0-0xDF. That covers every character the reference
/// can put in text mode: the docked screens' strings, which are printable ASCII only; the frame
/// DrawDockedFrame (0x7CA8) draws from 0xBA, 0xCD and the corners and tees at DS:0xA403 (0xB9,
/// 0xBB, 0xBC, 0xC8, 0xC9, 0xCC); and the input cursor 0xDB that ToggleInputCursor (0x7747)
/// makes in text mode. Every other code point, 0x01-0x1F, 0x7F-0xAF and 0xE0-0xFE, is not drawn
/// and renders as a hollow box, so that a gap shows on the screen instead of passing for text.
///
/// A glyph is 8 rows of 8 dots. In a row's byte, bit 7 is the leftmost dot and a set bit is lit.
class CgaFont
{
public:
  static constexpr std::uint32_t GLYPH_COUNT = 256;
  static constexpr std::uint32_t GLYPH_ROWS = 8;
  static constexpr std::uint32_t GLYPH_COLUMNS = 8;

  CgaFont() = delete;

  /// Row _row of the glyph for _code, 0 being the top row; _row is taken modulo GLYPH_ROWS.
  [[nodiscard]] static std::uint8_t Row(std::uint8_t _code, std::uint32_t _row) noexcept;

  /// Whether _code has a glyph of its own. One that has not renders as the hollow box.
  [[nodiscard]] static bool IsDrawn(std::uint8_t _code) noexcept;
};

} // namespace Machine
