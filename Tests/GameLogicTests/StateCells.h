#pragma once

#include "pch.h"

#include "Universe.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/*
 * Every byte of game state, one cell each, so that a fold over it can be held to a list.
 *
 * WHAT THIS WAS. `UniverseImage` mapped each of these bytes to the address the 6502 kept it at, and
 * four walks over the table did the work every oracle test needed: `Materialise` wrote the port's
 * state into the interpreter's memory, `Compare` read it back, `Absorb` reversed it, and `Hash`
 * folded it into the digest the replay recorded. The first three went with the interpreter
 * (Modernize.md M6-b-7) and the fourth with the labels, which is what M6-f always said would
 * happen to it.
 *
 * WHAT IT IS NOW. `Elite::HashState` is the fold that outlived them -- library-native, over
 * `Universe`'s own declaration order -- and the risk with a hand-written fold is a field it
 * forgot. This table is the list it is held to: `StateHashTests` walks it, changes one byte
 * through each cell, and requires the hash to move. A cell whose byte the fold walks past is a
 * gap, and it is named rather than counted.
 *
 * The names are the original's, and stay for the reason §1 R-b gives: they are the port's own
 * vocabulary, and `QQ17` says which byte is meant where "the case flags" would not.
 */
namespace GameLogicTests
{

  enum class CellScope : std::uint8_t
  {
    Seeded,   ///< a constant the port does not hold as a field
    Image,    ///< an ordinary byte of state
    Compared, ///< the same, and one an oracle test used to compare after every call
  };

  /// One byte of game state, read and written through the port's own types.
  struct Cell
  {
    std::wstring name;
    CellScope scope = CellScope::Image;

    /// Which bits of the byte are the port's. Only the shared sprite-x-high register is partial:
    /// the Trumbles own bits 2 to 7 and the laser sights own the other two.
    std::uint8_t mask = 0xFFu;

    /// The random number generator, which one caller used to leave uncompared.
    bool generator = false;

    std::function<std::uint8_t()> get;
    std::function<void(std::uint8_t)> set;
  };

  /*
   * The table. Non-const because the setters write the universe; it takes the universe by
   * reference for the same reason.
   *
   * `Universe::picture` HAS NO CELLS HERE, and that is the whole of its treatment (Resolution.md
   * section 3.4). Every cell was a byte the original held, and the 640x400 picture is not one: the
   * original had no such surface. `Elite::HashState` walks past it too, deliberately, and
   * `StateHash.cpp` carries the reason; `ThePicture::TheStateHashDeliberatelyDoesNotSeeIt` is what
   * fails if somebody folds it in.
   */
  [[nodiscard]] std::vector<Cell> StateCells(Elite::Universe& _universe);

} // namespace GameLogicTests
