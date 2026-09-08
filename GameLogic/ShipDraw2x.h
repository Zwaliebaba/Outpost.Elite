#pragma once

#include "HeapOffset.h"
#include "LineHeap.h"
#include "Picture.h"

#include <cstdint>

namespace Elite
{

  /*
   * The 640x400 picture's ship lines (Design/Resolution.md section 4.1, slices RS-2 and RS-3).
   *
   * THIS IS WHERE THE RESOLUTION IS ACTUALLY SPENT, and after two slices of measuring what that can
   * honestly mean, it is one thing: THINNESS. Every projecting divide, every multiply and now the
   * line drawer's own slope turns out to be a logarithm-table lookup rather than exact arithmetic,
   * so there is no dropped bit for a twin to recover in a wireframe -- and a twin that computed one
   * would draw a different ship. What the wide surface gives instead is one hi-res pixel of line
   * where the canvas draws a two-pixel-wide one, and a vertex placed on the finer grid.
   *
   * RS-3 EMPTIED THIS FILE OF ALMOST EVERYTHING RS-2 PUT IN IT, and the reason is in `Lines2x.h`:
   * once `DrawLine2x` takes the FAITHFUL line and doubles it itself, there is nothing left for a
   * parallel wide heap to hold. `Line2x`, `LineHeap2x`, `ClipLine2x`, `Doubled` and `PushHeapLine2x`
   * are gone with it, and so are 13,824 bytes of state, a `Universe` field, a state-hash exclusion
   * and a whole class of divergence: the wide line cannot disagree with the faithful one about
   * anything, because it is computed from it.
   */

  /*
   * 2x of: DrawShipLines -- draw every line of a run, from the FAITHFUL heap.
   *
   * `LL155`'s own walk: byte 0 is the run's length, under four there is not a whole line there, and
   * everything after it is groups of four. The twin reads the same bytes and hands each group to
   * `DrawLine2x`, so a run the game considers empty draws nothing here either (rule T1).
   */
  /// 2x of: DrawShipLines
  void DrawShipLines2x(Picture& _picture, const LineHeap& _heap, HeapOffset _run) noexcept;

} // namespace Elite
