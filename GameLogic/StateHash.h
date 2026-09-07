#pragma once

#include <cstdint>

namespace Elite
{
  struct Universe;

  /*
   * FNV-1a over every byte of game state, folded in `Universe`'s declaration order, through the
   * codecs where a struct has one (`Ship`, `Commander`) and byte by byte where it is bytes.
   *
   * LIBRARY-NATIVE, which is the point (M5-e-3): no 6502 label, no oracle, nothing from the test
   * tree. `UniverseImage::Hash` is the same idea over the label table and is the one the replay
   * record was taken with; this is what `Game::StateHash` answers, and the replay records BOTH until
   * M6-f retires the labels (owner ruling, 2026-09-07). Any stable hash would do; FNV-1a 64 is what
   * the image and `Canvas::Hash` already use, so a reader learns one.
   *
   * The definition is the fold order in StateHash.cpp and nothing else: a field that moves between
   * structs moves in the fold, and the hash moves with it -- which is a RECORD move under
   * Modernize.md rule 1 like any other, taken deliberately. `StateHashTests` holds the walk to the
   * image: every cell the label table names must move this hash when its byte changes.
   */
  [[nodiscard]] std::uint64_t HashState(const Universe& _universe) noexcept;
}
