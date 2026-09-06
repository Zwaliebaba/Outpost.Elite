#pragma once

#include "pch.h"

#include "Cpu6502.h"
#include "FlightUniverse.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

/*
 * The universe as the 6502 sees it -- one image, four uses (Design/Modernize.md slice M0-b).
 *
 * Every oracle test that reaches the flight universe does the same two things: copy the port's
 * state into the interpreter's memory at the labels the game uses, run both sides, and compare the
 * bytes back. `FlightUniverse.h` did that with two hand-written functions, `Mirror` and
 * `CompareState`, each naming its fields in its own order -- which meant the map from a port field
 * to a 6502 label was written twice and that any change to the port's layout had to be made in
 * both. The modernisation changes the port's layout on purpose, slice after slice, so the map is
 * written ONCE here as a table of cells, and the four things anybody wants from it are walks over
 * that table:
 *
 *   Materialise   port -> 6502 memory        what `Mirror` was
 *   Compare       6502 memory vs port        what `CompareState` was, as data rather than assertions
 *   Absorb        6502 memory -> port        new: the replay of M0-c reads a state back
 *   Hash          FNV-1a over the image      new: the replay hash, layout-independent by construction
 *
 * THE HASH IS OVER THE IMAGE AND NOT OVER THE STRUCTS. That is the whole point: a slice that turns
 * thirty-seven bytes into a typed `Ship` changes every struct and no byte of the image, so the
 * stored hashes of a replay survive the refactor and a changed hash means a changed game.
 *
 * THE ASYMMETRY IS KEPT ON PURPOSE. `Mirror` wrote a superset of what `CompareState` checked, and
 * each cell below says which it is: `Compared` cells are both written and compared, `Image` cells
 * are written and hashed but not compared, `Seeded` cells are written and nothing else (a constant
 * the oracle needs and the port has no field for). Widening the compared set is a deliberate change
 * with a finding behind it, never a side effect of moving a field.
 */
namespace GameLogicTests
{

  enum class CellScope : std::uint8_t
  {
    Seeded,   ///< written into the oracle only -- a constant the port does not hold
    Image,    ///< written, absorbed and hashed, but not compared (what `Mirror` set and `CompareState` did not check)
    Compared, ///< all of the above, and compared back after a run
  };

  /// One byte of the 6502 image and the port state it mirrors.
  struct Cell
  {
    std::wstring name;
    std::uint16_t address = 0;
    CellScope scope = CellScope::Image;

    /// Which bits of the byte are the port's. Only the shared sprite-x-high register is partial:
    /// the Trumbles own bits 2 to 7 and the laser sights own the other two.
    std::uint8_t mask = 0xFFu;

    /// The random number generator, which one caller asks to leave uncompared (`CompareState`'s
    /// `_compareRng`, and the reason is in `FlightUniverse.h`).
    bool generator = false;

    std::function<std::uint8_t()> get;
    std::function<void(std::uint8_t)> set;
  };

  /*
   * The table. Non-const because the setters write the universe; the const entry points below
   * cast that away and call only the getters, which is the one `const_cast` in the test tree and
   * is here so that the table is written once rather than twice.
   */
  [[nodiscard]] std::vector<Cell> ImageCells(Universe& _universe, const Where& _at);

  /// Port -> 6502 memory, every cell.
  void Materialise(const Universe& _universe, Cpu6502& _cpu, const Where& _at);

  /// 6502 memory -> port, every cell but the seeded ones.
  void Absorb(const Cpu6502& _cpu, Universe& _universe, const Where& _at);

  struct Difference
  {
    std::wstring name;
    std::uint16_t address = 0;
    std::uint8_t theirs = 0; ///< what the game left in memory
    std::uint8_t ours = 0;   ///< what the port holds
  };

  /// Every compared cell that disagrees, in table order. Empty means the two agree.
  [[nodiscard]] std::vector<Difference> Compare(const Cpu6502& _cpu, const Universe& _universe, const Where& _at, bool _compareRng = true);

  /// FNV-1a over the image's bytes in table order, seeded cells excluded.
  [[nodiscard]] std::uint64_t Hash(const Universe& _universe, const Where& _at);

  /// The same, with no oracle to resolve the addresses: the cells are read in table order and
  /// their addresses are never used. This is the hash a replay stores (slice M0-c).
  [[nodiscard]] std::uint64_t Hash(const Universe& _universe);

  /*
   * The FNV-1a offset basis and prime, 64-bit. Any stable hash would do; this one is
   * constexpr-friendly, has no dependency, and is the same choice `GoldenCanvas` would make if it
   * hashed bytes rather than pixels. `FoldBytes` is the step, exposed so that a replay can widen
   * the image's hash with bytes the image does not carry.
   */
  inline constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ull;
  inline constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

  [[nodiscard]] inline std::uint64_t FoldBytes(std::uint64_t _hash, std::span<const std::uint8_t> _bytes) noexcept
  {
    for (const std::uint8_t byte : _bytes)
    {
      _hash ^= byte;
      _hash *= FNV_PRIME;
    }
    return _hash;
  }

} // namespace GameLogicTests
