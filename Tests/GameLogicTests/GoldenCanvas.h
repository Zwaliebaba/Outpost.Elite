#pragma once

#include "Canvas.h"

#include <cstdint>
#include <span>
#include <string>

/*
 * The golden-canvas harness (slice 1d-c, ADR-003 section 2).
 *
 * A golden here is NOT a screenshot somebody accepted by eye. The emulator run that would have
 * provided one was cancelled (plan section 6.5), and the replacement is stronger: the shipped
 * routines draw the same scene into the oracle's own memory, that memory is decoded into a
 * Canvas, and the two are compared pixel for pixel. The committed hash is a second check on top
 * of that -- it catches a change that moved both sides together, which a self-comparison cannot.
 *
 * On a mismatch both images are written out as PNGs beside each other so that
 * tools/golden_diff.py has something to diff. That is the whole reason this writes files: a
 * golden that fails with a number and no picture is a golden nobody will re-record correctly.
 */
namespace Elite::Testing
{

  /// Decodes the oracle's screen memory into a Canvas, so the two can be compared as pictures
  /// rather than as bytes. Colour RAM is not in the 64 KB image the oracle loads, so the caller
  /// supplies it; every scene here uses one colour throughout.
  void LoadScreenFromOracle(const std::array<std::uint8_t, 65536>& _memory, std::uint16_t _base, std::uint8_t _colourRam,
                            Canvas& _outCanvas);

  /// Writes the resolved 320x200 image as an indexed PNG. Returns the path, or empty on failure.
  std::string WriteCanvasPng(const Canvas& _canvas, const std::string& _name);

  /// Compares two canvases as pictures. On a mismatch writes both PNGs and returns a message
  /// naming the first differing pixel and the files; on a match returns an empty string.
  std::string CompareCanvasImages(const Canvas& _expected, const Canvas& _actual, const std::string& _name);

} // namespace Elite::Testing
