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
   * screen's pauses and `DrawHyperspaceRing` -- and of those the ring has the frame and the other
   * two have nothing on it. And after RN-0 a site's `Picture*` may be either surface, so a wrapper
   * that guessed would clear the backdrop on every docked screen. The caller names the frame.
   *
   * Under I-4 these become the awaitables and the clear moves into `co_await`, which is why they
   * are functions now rather than a line repeated nineteen times.
   */

  /*
   * Is the clear switched on?
   *
   * RN-1's first commit puts the boundary in place with this false, so that every digest in the
   * suite proves the PLUMBING alone -- nineteen call sites rerouted through four functions, and not
   * one pixel moved. The second commit turns it on together with the six erase twins it replaces,
   * because those two changes are only equal to each other and neither is a no-op by itself.
   */
  inline constexpr bool CLEAR_THE_FRAME = false;

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
   * driver after each checkpoint, so that a checkpoint hashes the frame AS PRESENTED and not the
   * one after it. `Game::EndFrame` is this over its own universe.
   */
  void EndFrame(Picture* _frame) noexcept;

} // namespace Elite
