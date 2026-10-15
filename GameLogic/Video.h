// GameLogic/Video.h
#pragma once

#include "GameState.h"
#include "Host.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Elite
{

// The reference's video routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): drawing: lines, spans, the space view and
// the dashboard into CGA memory. Each takes values and gives values back, on the GameState and, where it drives a device, the devices
// (ADR-014).

/// The drawing buffer's bytes: 128 rows of 64, 256 pixels of 2 bits each.
inline constexpr std::size_t DRAWING_BUFFER_BYTES = 0x2000;

/// The drawing buffer, which the space view, the charts and their texts are drawn into before they are copied
/// to the screen: drawing.drawingBuffer. A byte of it is numbered as the original addresses it, by its offset in the data segment.
[[nodiscard]] std::span<std::uint8_t, DRAWING_BUFFER_BYTES> DrawingBuffer(GameState& _state) noexcept;

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the original routine of its name computes, with no register in sight, and every byte it writes is written as the
// original writes it, in the same order and at the same width. A string instruction runs forwards (ADR-046).

/// A point of the screen, x and row, signed: a vertex of vertexBuffer once ProjectVertices has projected it, what ProjectToScreen
/// gives, or a corner FillTriangle takes, which may lie off the drawing buffer.
struct ScreenPoint
{
  std::int16_t x;
  std::int16_t y;
};

/// Three projected vertices: a face's first three, which say which way it faces, or a filled triangle's corners A, B and C.
struct Triangle
{
  ScreenPoint first;
  ScreenPoint second;
  ScreenPoint third;
};

/// FinishSpaceViewFrame: DrawLaserSights, then PresentSpaceView and ClearDrawBuffer, both forwards. It waits.
[[nodiscard]] Task<void> FinishSpaceViewFrame(GameState& _state, Host& _host);

/// PresentChartFrame: CopyChartBufferToScreen of the whole chart, with no bands skipped whatever the caller passed, then
/// ClearDrawBuffer, both forwards. It waits.
[[nodiscard]] Task<void> PresentChartFrame(GameState& _state, Host& _host);

/// PresentSpaceView: waits until msSinceFrame reaches minimumFrameMs and clears it, WaitRetraceThenDelay, then copies
/// spaceViewBuffer's 63 line pairs to the screen at B800:01E8. Its loops turn through _host.
[[nodiscard]] Task<void> PresentSpaceView(GameState& _state, Host& _host);

/// CopyChartBufferToScreen: waits for a vertical retrace and a delay, then copies 64 - 4 * _bandsSkipped line pairs, as
/// a byte, of the drawing buffer from its byte _bandsSkipped * 512 to the chart's place on the screen, at B800:0648. Its loops turn
/// through _host.
[[nodiscard]] Task<void> CopyChartBufferToScreen(GameState& _state, Host& _host, std::uint8_t _bandsSkipped);

/// ClearDrawBuffer: the drawing buffer (DrawingBuffer) zeroed a word at a time, forwards.
void ClearDrawBuffer(GameState& _state);

/// PlotPixel: the pixel at _x, _row of the drawing buffer set to colorFillBytes[drawColor].
void PlotPixel(GameState& _state, std::uint8_t _x, std::uint8_t _row);

/// DrawClippedLine: the line from (_fromX, _fromRow) to (_toX, _toRow), signed words, clipped to the 256x128 buffer and
/// drawn (DrawLine).
void DrawClippedLine(GameState& _state, std::uint16_t _fromX, std::uint16_t _fromRow, std::uint16_t _toX, std::uint16_t _toRow);

/// A line as DrawClippedLine's clip routines hold it: its two ends, the first coordinate of each the one an edge cuts and the
/// second the one along it, and the count of ends still outside 0-255.
struct ClipLine
{
  std::uint16_t firstCut;    ///< the first end, across the edge
  std::uint16_t firstAlong;  ///< the first end, along it
  std::uint16_t secondCut;   ///< the second end, across the edge
  std::uint16_t secondAlong; ///< the second end, along it
  std::uint16_t outside;     ///< the ends with a coordinate outside 0-255
};

/// What a clip routine leaves: the line, an end moved onto the edge where one was cut, and the flags DrawClippedLine reads.
struct ClipStep
{
  ClipLine line;
  bool draw;      ///< some of the line may still show
  bool allInside; ///< no end is outside any more
};

/// ClipLineToLowEdge: of _line's ends, the one below 0 on the cut axis moved along the line onto 0, the first end, or
/// the second with the ends swapped (SwapThenCutLine); outside less one when the moved end lands inside. Both below: nothing to draw.
[[nodiscard]] ClipStep ClipLineToLowEdge(ClipLine _line);

/// ClipLineToHighEdge: ClipLineToLowEdge for the edge at 255, the caller having subtracted 0FFh from the cut
/// coordinates: the end at or beyond 0 is the one moved. Both inside: still to draw; both beyond: nothing to draw, allInside set when
/// the first end's high byte is 0.
[[nodiscard]] ClipStep ClipLineToHighEdge(ClipLine _line);

/// DrawCircle: a circle of radius _radius about (_centerX, _centerRow) as 32 chords through DrawClippedLine, its points
/// circleOctant scaled by _radius/128 and turned through the eight octants into circlePoints.
void DrawCircle(GameState& _state, std::uint8_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow);

/// DrawLine: Bresenham from (_fromX, _fromRow) to (_toX, _toRow) into the drawing buffer in colorFillBytes[drawColor];
/// a horizontal line's whole bytes are filled a byte at a time, forwards.
void DrawLine(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromRow, std::uint8_t _toX, std::uint8_t _toRow);

/// DrawDisc: a disc of radius _radius filled at (_centerX, _centerRow), signed words, in colorFillBytes[drawColor & 3],
/// which it keeps as discFillByte. Below radius 5, a sprite from smallDiscSprites clipped to the buffer; from 5, the rows of
/// circleProfile scaled to the radius, each FillSpan, with a ragged edge while sunFringeMask is set.
void DrawDisc(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow);

/// FillSpan: row _doubledRow / 2 of the drawing buffer filled with discFillByte from x = _left to x = _right, the
/// end bytes through spanLeftMasks and spanRightMasks and the bytes between them by a byte store and word stores.
void FillSpan(GameState& _state, std::uint8_t _left, std::uint8_t _right, std::uint8_t _doubledRow);

/// FillTriangleSpan: x = _left to _right, _left first, of the drawing buffer's row at its byte _row filled with _fill: the
/// end bytes through triangleEdgeMasks, which the original reads in the stack segment, and the bytes between
/// them by byte and word stores.
void FillTriangleSpan(GameState& _state, std::uint16_t _row, std::uint8_t _left, std::uint8_t _right, std::uint8_t _fill);

/// FillTriangle: _triangle filled with triangleFillPattern, its low byte on even rows and its high on odd ones. Wholly
/// on the drawing buffer, the rows' spans are walked down from the top on 8.8 edges into a container, as the original pushes
/// them, and drawn from the bottom up, as it pops them (DrawStackedSpansFromRow, FillTriangleSpan); otherwise FillClippedTriangle.
/// The edges' steps are the instructions it patches into its code, an add or a subtract, which it writes as the original does.
void FillTriangle(GameState& _state, Triangle _triangle);

/// FillClippedTriangle: FillTriangle's path for a triangle not wholly on the buffer, _doubled's rows twice the rows: one
/// wholly left, above, right or below the buffer is not drawn; otherwise its rows are walked on 16.16 edges, only those on the
/// buffer kept and each span held to 0-255.
void FillClippedTriangle(GameState& _state, Triangle _doubled);

/// WaitRetraceThenDelay: waits for a vertical retrace on the CGA's status port, then spins 2000 turns, a delay the
/// 8088's speed made. Its loops turn through _host.
[[nodiscard]] Task<void> WaitRetraceThenDelay(Host& _host);

/// How ShowCockpitScreen and DrawChartFrame made the screen ready for what they draw.
enum class ScreenChange : std::uint8_t
{
  None,    ///< their screen showed already, and they drew nothing
  Cleared, ///< a graphics screen, cleared (ClearCgaScreen)
  ModeSet, ///< a text screen, put into graphics mode (SetGraphicsMode)
};

/// ShowCockpitScreen: unless the cockpit shows already (screenLayout 0), graphics mode from text or a clear screen from
/// graphics, screenLayout 0, and cockpitImage copied into both banks of the screen with the direction flag clear.
ScreenChange ShowCockpitScreen(GameState& _state, Host& _host);

/// ClearCgaScreen: both banks of CGA memory, B800:0000-1F3F and B800:2000-3F3F, zeroed a word at a time.
void ClearCgaScreen(GameState& _state);

/// ClearTextScreen: the 40x25 text page filled with spaces in textAttribute, a cell at a time.
void ClearTextScreen(GameState& _state);

/// DrawChartFrame: unless the chart frame shows already (screenLayout 1), graphics mode from text or a clear screen from
/// graphics, screenLayout 1, and the frame's box in colour 3: four horizontal lines a word at a time, forwards, then
/// the two verticals a byte at a time.
ScreenChange DrawChartFrame(GameState& _state, Host& _host);

/// SetGraphicsMode: the CGA in 320x200 graphics, video memory cleared, and the colour select register bright on
/// black.
void SetGraphicsMode(GameState& _state, Host& _host);

/// SetTextMode: the CGA in 40x25 text, video memory blank, the cursor's address off the page, and the mode control
/// register with blinking off, so that attribute bit 7 is a bright background.
void SetTextMode(GameState& _state, Host& _host);

/// DrawTitlePlanet: DrawDisc with sunFringeMask 1, then sunFringeMask 0.
void DrawTitlePlanet(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow);

} // namespace Elite
