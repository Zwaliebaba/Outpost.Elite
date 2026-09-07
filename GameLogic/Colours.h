#pragma once

#include <cstdint>

namespace Elite
{

  /*
   * The sixteen VIC-II colours (slice 5a).
   *
   * A COLOUR IS AN INDEX AND NOTHING ELSE. The chip has a fixed palette and every colour it can be
   * told about is a number from 0 to 15; what that number looks like is the hardware's business,
   * which is why the RGB table is in `Outpost/Presentation.h` and not here. The order is the
   * VIC-II's own, which is why yellow is 7 and orange is 8 rather than anything an artist would
   * choose.
   *
   * WHAT THIS TYPE IS NOT FOR, and the reason it needs saying is that the original uses the word
   * "colour" for three unrelated bytes:
   *
   *   - `RED`, `YELLOW`, `GREEN` and `WHITE` are %01010101, %10101010, %11111111 and %01011010 --
   *     FOUR MULTICOLOUR PIXELS packed in a byte, which `COL` holds and `CPIX2` ANDs with a mask
   *     from `CTWOS2`. They are pixel patterns, not colours; `BLUE`, `CYAN` and `MAG` are all
   *     defined as `YELLOW`, because `scacol` holds names from the BBC build that this machine
   *     cannot honour. Nothing in that family is a `Colour`.
   *   - `RED2` (&27), `GREEN2` (&57), `YELLOW2` (&87), `BLACK2` (&B7), `MAG2` (&40) and `BULBCOL`
   *     (&E0) are SCREEN RAM palette bytes, each holding TWO of these indices: the high nibble is
   *     what %01 draws in and the low nibble what %10 draws in. Their names describe the high
   *     nibble and two of them describe it wrongly -- `YELLOW2` is orange (8) over yellow (7) and
   *     `BLACK2` is dark grey (&B) over yellow. A byte from that family is a pair and not a
   *     `Colour`.
   *   - Colour RAM's nibble, the background register, and each sprite's own colour register ARE
   *     one index, and those are what this type is for.
   *
   * AND THE REGISTERS TAKE FOUR BITS, WHICH IS WHY `ColourOf` EXISTS. `STA VIC+&21` writes eight
   * bits and the chip latches the low four; the game relies on that -- `COMIRQ1` increments
   * `welcome` while the energy bomb burns and stores the running count straight into the register,
   * so the byte on its way there is a counter and only becomes a colour when the hardware masks it.
   * Every conversion from a byte to a `Colour` goes through `ColourOf`, and that is the port's
   * version of the latch.
   */
  enum class Colour : std::uint8_t
  {
    Black = 0,
    White = 1,
    Red = 2,
    Cyan = 3,
    Purple = 4,
    Green = 5,
    Blue = 6,
    Yellow = 7,
    Orange = 8,
    Brown = 9,
    LightRed = 10,
    DarkGrey = 11,
    Grey = 12,
    LightGreen = 13,
    LightBlue = 14,
    LightGrey = 15,
  };

  /// The number a `Colour` is, for the places that must hand one to a byte -- the canvas the
  /// presenter uploads, and the memory image the oracle compares.
  [[nodiscard]] constexpr std::uint8_t ColourIndex(Colour _colour) noexcept
  {
    return static_cast<std::uint8_t>(_colour);
  }

  /// 6502: what the VIC-II does to every byte stored in a colour register -- it keeps four bits.
  [[nodiscard]] constexpr Colour ColourOf(std::uint8_t _byte) noexcept
  {
    return static_cast<Colour>(_byte & 0x0Fu);
  }

} // namespace Elite
