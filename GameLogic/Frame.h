#pragma once

#include <cstdint>

namespace Elite
{

  class Picture;
  class Presenter;

  /*
   * THE FRAME BOUNDARY (Design/Platform.md §3.4, slice RN-1).
   *
   * A present is the end of a frame. That sentence is the finding the whole platform track was
   * built on, and until this header nothing in the port acted on it: the two 640x400 surfaces
   * accumulated for ever and every transient was rubbed out by drawing it a second time.
   *
   * These four wrap the four ways the game waits and clear the FRAME afterwards, so that what the
   * next pass draws onto is empty. The BACKDROP is not touched -- that is the whole point of the
   * split: the glyphs, the charts, the borders and the dials stay, and the ships, the stardust and
   * the lasers are drawn fresh.
   *
   * WHY FREE FUNCTIONS IN THE LIBRARY, and not a clear in `Shell::Turn`. The replay never runs the
   * executable's loop, so a boundary in `Outpost/` would be invisible to every gate in the suite --
   * `ThePictureIsAsRecorded` and the two RN-0 tables would all be measuring a frame that never got
   * cleared. It has to be reachable from `GameLogic` or it is not tested.
   *
   * They take the PRESENTER AND THE FRAME rather than a `Universe&`, for two reasons. Three of the
   * nineteen present sites have no universe in scope -- `ReadLine`'s prompt, two of the market
   * screen's pauses and `DrawHyperspaceRing` -- and of those the ring is handed the frame as a
   * parameter of its own and the other two have nothing on it. And after RN-0 a site's `Picture*`
   * may be either surface, so a wrapper that guessed would clear the backdrop on every docked
   * screen. The caller names the frame -- the tunnel names one surface to draw on and a different
   * one to end, which is the case that made the rule.
   *
   * Under I-4 these become the awaitables and the clear moves into `co_await`, which is why they
   * are functions now rather than a line repeated nineteen times.
   */

  /*
   * `CLEAR_THE_FRAME` WAS HERE AND IS NOT ANY MORE.
   *
   * RN-1's first commit put the boundary in place with it false, so that every digest in the suite
   * proved the PLUMBING alone -- nineteen call sites rerouted through four functions, and not one
   * pixel moved. The second commit turned it on together with the seven erase twins it replaces,
   * because those two changes are only equal to each other and neither is a no-op by itself. A
   * `constexpr bool` that is now always true is scaffolding, and scaffolding left up is read as
   * structure (Design/Platform-Build.md C-1).
   */

  /// `DELAY` -- wait `_frames` of the machine's vertical syncs, then end the frame.
  void WaitFrames(Presenter& _present, Picture* _frame, std::uint8_t _frames) noexcept;

  /// One vertical sync: the tunnels' per-circle wait, and `HYPNOISE`'s own.
  void PresentFrame(Presenter& _present, Picture* _frame) noexcept;

  /// The flight loop's hold, paced by what a frame costs with this many ships in the bubble.
  void HoldFlightFrame(Presenter& _present, Picture* _frame, std::uint8_t _ships) noexcept;

  /// The title ship's hold, paced by how much of the ship there is to draw.
  void HoldTitleFrame(Presenter& _present, Picture* _frame, std::uint8_t _distance) noexcept;

  /*
   * End the frame without waiting for one.
   *
   * The executable's loop presents for itself and then wants the boundary; so does the replay
   * driver, after every step, which is where the executable does it. `Game::EndFrame` is this over
   * its own universe.
   *
   * It MARKS rather than blanks (`Picture::EndFrame`), so a checkpoint or a presenter reading the
   * surface between two passes still sees the frame that was just finished.
   */
  void EndFrame(Picture* _frame) noexcept;

} // namespace Elite
