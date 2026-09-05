#pragma once

#include <cstdint>

namespace Outpost
{

  /*
   * A Windows key to a Commodore 64 key (slice 2e).
   *
   * The C64's keyboard hands the game a MATRIX POSITION -- 0 to 64, where 0 means nothing is
   * pressed -- and two different parts of the game want two different things from it. `TT102`
   * dispatches on the position itself, so its "8" is 37; every docked screen reads a CHARACTER
   * through the `KeySource` seam, which the game produces by indexing `TRANTABLE`. Both come from
   * one key press, so this maps a virtual key to the POSITION and lets `Elite::KEY_TRANSLATION` do
   * the rest -- rather than inventing a second mapping straight to characters that would agree
   * with the table on the keys somebody tested and not on the others.
   *
   * The virtual-key codes are given as plain integers rather than through `<windows.h>`, so the
   * mapping is testable on a machine with no Windows SDK. They are the documented `VK_*` values and
   * a comment names each one; a header that pulled in Windows here would move this file to the
   * unverifiable half of the shell for nothing.
   */

  /// 0 is what RDKEY returns when nothing is pressed, and TRANTABLE[0] is 0 -- so an unmapped key
  /// translates to a character the text system will not print, which is the behaviour to want.
  inline constexpr std::uint8_t NO_KEY = 0;

  /*
   * The virtual keys the port maps, with the C64 key each one stands for.
   *
   * ADR-005 section 4 USED TO fix the default map as the C64's own -- the number row for the
   * docked screens, F1/F3/F5/F7 for launch and the three views, and the flight controls left to
   * whatever `KYTB` turned out to hold. It no longer does, and the ADR says so. Two things made
   * the C64 layout the wrong default on a PC. The flight half of it was never mapped at all, so
   * the seven primary controls and the nine secondary ones had no key on this side of the seam;
   * and where the C64 put them -- roll on "<" and ">", pitch on "X" and "S", speed on Space and
   * "?" -- reads as arbitrary on a keyboard that has arrow keys under the hand already.
   *
   * So the default map is now the modern layout: the arrow keys steer, "," and "." set speed, the
   * six information screens are F1 to F6, the four views are F7 to F10, and every letter the game
   * names keeps its own letter.
   *
   * IT IS MANY-TO-ONE, and that is the price of moving the screens off the number row. `gnum`
   * reads a QUANTITY through this same seam, as the CHARACTER `TRANTABLE` gives the position -- so
   * unbinding "4" to free it for F1 would leave no way to type four units of food. "4" therefore
   * keeps its position AND F1 gets the same one, which means "4" still opens the Galactic Chart
   * from the top-level dispatch. That cannot be otherwise while `TT102` is the original's: the
   * dispatch compares the POSITION, so any key that can type a "4" is a key that opens the chart.
   * Splitting them would take a second table chosen by which screen is showing, which is exactly
   * the two-table drift the shape of this file exists to avoid.
   *
   * THREE CHOICES IN IT ARE THE PORT'S, and all three are named so nobody mistakes them for the
   * game's. A PC keyboard has no C64 "@" key, so the disk menu is on the key that carries "@" on a
   * UK or US layout -- VK_OEM_3 on UK, which is also where a C64 player's fingers were. RETURN,
   * DELETE and ESCAPE are mapped to the C64 positions whose translations are 13, 127 and 27,
   * because the line editor compares against those numbers and nothing else would reach them. And
   * "J" and "P" are bound although the layout above does not name them: "J" is the in-system jump
   * and "P" is the ONLY thing that turns the docking computer off, so a map without them has one
   * control the player cannot reach and one they cannot cancel once it is on.
   */
  struct KeyBinding
  {
    int virtualKey = 0;
    std::uint8_t c64Key = NO_KEY;
    const char* what = "";
  };

  /// The whole map, as data, so a test can walk it rather than trusting a switch.
  [[nodiscard]] const KeyBinding* Bindings() noexcept;
  [[nodiscard]] int BindingCount() noexcept;

  /// The C64 key a Windows virtual key stands for, or NO_KEY.
  [[nodiscard]] std::uint8_t C64KeyFor(int _virtualKey) noexcept;

  /// The character `TT217` would have returned for that C64 key -- `TRANTABLE`, and nothing else.
  [[nodiscard]] std::uint8_t CharacterFor(std::uint8_t _c64Key) noexcept;

} // namespace Outpost
