// GameLogic/Video.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's video routines, ported (plan §5 Phase 3, ADR-010): drawing: lines, spans, the space view and the dashboard into CGA memory. All
// of them are de-assembled (ADR-012): they take values and give values back, and their entries keep the register contracts of
// Symbols.tsv for the hooks and the callers not yet de-assembled.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> VideoEntries() noexcept;

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight, and every byte it writes is written as the
// original writes it, in the same order and at the same width. A string instruction's direction is the direction flag its
// entry finds: _backward.

/// A point of the screen, x and row, signed: a vertex of vertexBuffer once ProjectVertices has projected it, what ProjectToScreen
/// gives, or a corner FillTriangle takes, which may lie off the drawing buffer.
struct ScreenPoint
{
  std::int16_t x;
  std::int16_t y;
};

/// Three projected vertices: a face's first three, which say which way it faces, or a filled triangle's corners A, B and C, as
/// the original holds them in (AX, DX), (BX, BP) and (CX, DI).
struct Triangle
{
  ScreenPoint first;
  ScreenPoint second;
  ScreenPoint third;
};

/// SaveScreenshot (CS:01B7): with the game's divide, keyboard and timer handlers taken out and CriticalErrorInterrupt on
/// int 24h, WriteScreenshotFile, then ShowDiskError if it failed; then int 24h as it was, and the game's handlers put back.
void SaveScreenshot(GameState& _state, Hardware& _hardware);

/// WriteScreenshotFile (CS:03FD): screenshotNumber stepped, and eliteNN.lo (the text page) or eliteNN.hi (both graphics
/// banks) written from B800:0000 through DOS, with its disk transfer area at diskTransferArea; diskError = 1 on a failure.
void WriteScreenshotFile(GameState& _state, Hardware& _hardware);

/// FinishSpaceViewFrame (CS:0570): DrawLaserSights, then PresentSpaceView and ClearDrawBuffer, both forwards. It waits.
void FinishSpaceViewFrame(GameState& _state, Hardware& _hardware);

/// PresentChartFrame (CS:0587): CopyChartBufferToScreen of the whole chart, with no bands skipped whatever the caller passed, then
/// ClearDrawBuffer, both forwards. It waits.
void PresentChartFrame(GameState& _state, Hardware& _hardware);

/// PresentSpaceView (CS:0599): waits until msSinceFrame reaches minimumFrameMs and clears it, WaitRetraceThenDelay, then copies
/// spaceViewBuffer's 63 line pairs to the screen at B800:01E8. Its loops turn through _hardware.
void PresentSpaceView(GameState& _state, Hardware& _hardware, bool _backward);

/// CopyChartBufferToScreen (CS:05CC): waits for a vertical retrace and a delay, then copies 64 - 4 * _bandsSkipped line pairs, as
/// a byte, of the drawing buffer from DS:_bandsSkipped * 512 to the chart's place on the screen, at B800:0648. Its loops turn
/// through _hardware.
void CopyChartBufferToScreen(GameState& _state, Hardware& _hardware, std::uint8_t _bandsSkipped, bool _backward);

/// ClearDrawBuffer (CS:060D): the drawing buffer, DS:0000-1FFF, zeroed a word at a time as REP STOSW does.
void ClearDrawBuffer(GameState& _state, bool _backward);

/// PlotPixel (CS:15E0): the pixel at _x, _row of the drawing buffer set to colorFillBytes[drawColor].
void PlotPixel(GameState& _state, std::uint8_t _x, std::uint8_t _row);

/// DrawClippedLine (CS:1603): the line from (_fromX, _fromRow) to (_toX, _toRow), signed words, clipped to the 256x128 buffer and
/// drawn (DrawLine). Returns what DrawLine returns: whether it filled a horizontal line's bytes, for which the original sets ES to
/// DS and clears the direction flag.
bool DrawClippedLine(GameState& _state, std::uint16_t _fromX, std::uint16_t _fromRow, std::uint16_t _toX, std::uint16_t _toRow);

/// A line as DrawClippedLine's clip routines hold it: its ends (CX, AX) and (DX, BX), the first coordinate of each the one an edge
/// cuts and the second the one along it, and BP, the count of ends still outside 0-255.
struct ClipLine
{
  std::uint16_t firstCut;    ///< CX
  std::uint16_t firstAlong;  ///< AX
  std::uint16_t secondCut;   ///< DX
  std::uint16_t secondAlong; ///< BX
  std::uint16_t outside;     ///< BP
};

/// What a clip routine leaves: the line, an end moved onto the edge where one was cut, and the flags DrawClippedLine reads.
struct ClipStep
{
  ClipLine line;
  bool draw;      ///< CF: some of the line may still show
  bool allInside; ///< ZF: no end is outside any more
};

/// ClipLineToLowEdge (CS:1686): of _line's ends, the one below 0 on the cut axis moved along the line onto 0, the first end, or
/// the second with the ends swapped (SwapThenCutLine); BP less one when the moved end lands inside. Both below: nothing to draw.
[[nodiscard]] ClipStep ClipLineToLowEdge(GameState& _state, ClipLine _line);

/// ClipLineToHighEdge (CS:16C1): ClipLineToLowEdge for the edge at 255, the caller having subtracted 0FFh from the cut
/// coordinates: the end at or beyond 0 is the one moved. Both inside: still to draw; both beyond: nothing to draw, ZF set when
/// the first end's high byte is 0.
[[nodiscard]] ClipStep ClipLineToHighEdge(GameState& _state, ClipLine _line);

/// DrawCircle (CS:1AC1): a circle of radius _radius about (_centerX, _centerRow) as 32 chords through DrawClippedLine, its points
/// circleOctant scaled by _radius/128 and turned through the eight octants into circlePoints. Returns whether any chord's
/// DrawLine filled a horizontal line's bytes.
bool DrawCircle(GameState& _state, std::uint8_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow);

/// DrawLine (CS:16D1): Bresenham from (_fromX, _fromRow) to (_toX, _toRow) into the drawing buffer in colorFillBytes[drawColor].
/// Returns whether it filled whole bytes of a horizontal line with REP STOSB, for which the original sets ES to DS and clears
/// the direction flag first.
bool DrawLine(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromRow, std::uint8_t _toX, std::uint8_t _toRow);

/// DrawDisc (CS:1826): a disc of radius _radius filled at (_centerX, _centerRow), signed words, in colorFillBytes[drawColor & 3],
/// which it keeps as discFillByte. Below radius 5, a sprite from smallDiscSprites clipped to the buffer; from 5, the rows of
/// circleProfile scaled to the radius, each FillSpan, with a ragged edge while sunFringeMask is set.
void DrawDisc(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow, bool _backward);

/// FillSpan (CS:1A07): row _doubledRow / 2 of the drawing buffer filled with discFillByte from x = _left to x = _right, the
/// end bytes through spanLeftMasks and spanRightMasks and the bytes between them by STOSB and REP STOSW into ES = DS.
/// Returns the offset of the span's last byte.
std::uint16_t FillSpan(GameState& _state, std::uint8_t _left, std::uint8_t _right, std::uint8_t _doubledRow, bool _backward);

/// FillTriangleSpan (CS:1B7A): x = _left to _right, _left first, of the drawing buffer's row at DS:_row filled with _fill: the
/// end bytes through triangleEdgeMasks, which the original reads through BP from SS:0000, DS:AD60, and the bytes between
/// them by STOSB and REP STOSW into ES = DS.
void FillTriangleSpan(GameState& _state, std::uint16_t _row, std::uint8_t _left, std::uint8_t _right, std::uint8_t _fill, bool _backward);

/// FillTriangle (CS:1BFB): _triangle filled with triangleFillPattern, its low byte on even rows and its high on odd ones. Wholly
/// on the drawing buffer, the rows' spans are walked down from the top on 8.8 edges into a container, as the original pushes
/// them, and drawn from the bottom up, as it pops them (DrawStackedSpansFromRow, FillTriangleSpan); otherwise FillClippedTriangle.
/// The edges' steps are the instructions it patches into its code, ADD or SUB, which it writes as the original does. Returns
/// false when the triangle is wholly off the buffer and nothing is drawn; otherwise the original sets ES to DS.
bool FillTriangle(GameState& _state, Triangle _triangle, bool _backward);

/// FillClippedTriangle (CS:1E6E): FillTriangle's path for a triangle not wholly on the buffer, _doubled's rows twice the rows: one
/// wholly left, above, right or below the buffer is not drawn; otherwise its rows are walked on 16.16 edges, only those on the
/// buffer kept and each span held to 0-255. Returns false when nothing is drawn for being wholly off the buffer.
bool FillClippedTriangle(GameState& _state, Triangle _doubled, bool _backward);

/// WaitRetraceThenDelay (CS:45FF): waits for a vertical retrace on the CGA's status port, then spins 2000 turns, a delay the
/// 8088's speed made. Its loops turn through _hardware.
void WaitRetraceThenDelay(Hardware& _hardware);

/// How ShowCockpitScreen and DrawChartFrame made the screen ready for what they draw.
enum class ScreenChange : std::uint8_t
{
  None,    ///< their screen showed already, and they drew nothing
  Cleared, ///< a graphics screen, cleared (ClearCgaScreen)
  ModeSet, ///< a text screen, put into graphics mode (SetGraphicsMode)
};

/// ShowCockpitScreen (CS:7BC0): unless the cockpit shows already (screenLayout 0), graphics mode from text or a clear screen from
/// graphics, screenLayout 0, and cockpitImage copied into both banks of the screen with the direction flag clear.
ScreenChange ShowCockpitScreen(GameState& _state, Hardware& _hardware, bool _backward);

/// ClearCgaScreen (CS:7BFB): both banks of CGA memory, B800:0000-1F3F and B800:2000-3F3F, zeroed a word at a time.
void ClearCgaScreen(GameState& _state, bool _backward);

/// ClearTextScreen (CS:7C12): the 40x25 text page filled with spaces in textAttribute, a cell at a time.
void ClearTextScreen(GameState& _state, bool _backward);

/// DrawChartFrame (CS:7C25): unless the chart frame shows already (screenLayout 1), graphics mode from text or a clear screen from
/// graphics, screenLayout 1, and the frame's box in colour 3: four horizontal lines by REP STOSW in the direction it finds, then
/// the two verticals a byte at a time.
ScreenChange DrawChartFrame(GameState& _state, Hardware& _hardware, bool _backward);

/// SetGraphicsMode (CS:7CFE): BIOS mode 4, palette 0, and the colour select register bright on black.
void SetGraphicsMode(Hardware& _hardware);

/// SetTextMode (CS:7D11): BIOS mode 1, the cursor's address off the page, and the mode control register with blinking off,
/// so that attribute bit 7 is a bright background.
void SetTextMode(Hardware& _hardware);

/// DrawTitlePlanet (CS:7D4E): DrawDisc with sunFringeMask 1, then sunFringeMask 0.
void DrawTitlePlanet(GameState& _state, std::uint16_t _radius, std::uint16_t _centerX, std::uint16_t _centerRow, bool _backward);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its results back
// there. The registers the contract leaves to the routine it hands to Guest::Clobber, unless a caller reads what the original
// leaves in one: then the entry leaves that, and the contract compares it (FillSpanEntry's DI, DrawLineEntry's ES,
// FillTriangleSpanEntry's AX and BP, FinishSpaceViewFrameEntry's DX, ShowCockpitScreenEntry's SI and ES, DrawChartFrameEntry's
// ES).

void SaveScreenshotEntry(Guest& _guest); ///< Out: ES=B800h; AX, BX, CX, DX, SI, DI clobbered.
/// AX, BX, CX, DX clobbered.
void WriteScreenshotFileEntry(Guest& _guest);
/// Out: ES = B800h, DF clear, DX = 1FF0h as PresentSpaceView leaves it. AX, BX, CX, SI, DI, BP clobbered.
void FinishSpaceViewFrameEntry(Guest& _guest);
void PresentChartFrameEntry(Guest& _guest); ///< Out: ES = B800h, DF clear. AX, BX, CX, DX, SI, DI, BP clobbered.
void PresentSpaceViewEntry(Guest& _guest);  ///< ES = B800h. AX, BX, CX, DX, SI, DI, BP clobbered.
/// AL = the bands to skip, ES = B800h, DF clear. AX, BX, CX, DX, SI, DI, BP clobbered.
void CopyChartBufferToScreenEntry(Guest& _guest);
void ClearDrawBufferEntry(Guest& _guest); ///< Out: ES = DS, AX = 0, CX = 0, DI past the buffer.
void PlotPixelEntry(Guest& _guest);       ///< DL = x, DH = row. BX, CX clobbered.
/// DX, BX to CX, AX. Out: ES = DS and DF clear after DrawLine's REP STOSB. AX, BX, CX, DX, SI, DI, BP clobbered.
void DrawClippedLineEntry(Guest& _guest);
/// CX, AX and DX, BX the ends, BP the count outside, in and out. Out: CF, ZF. SI clobbered.
void ClipLineToLowEdgeEntry(Guest& _guest);
void ClipLineToHighEdgeEntry(Guest& _guest); ///< As ClipLineToLowEdgeEntry.
void DrawLineEntry(Guest& _guest);           ///< DL, DH to CL, CH. Out: ES = DS and DF clear after REP STOSB.
/// What DrawLine leaves of the machine when it returns _filled: ES = DS and the direction flag clear, which it sets before a
/// horizontal line's REP STOSB. For the entries and the register code of the routines that call it, whose originals go on
/// with them.
void DrawLineOut(Guest& _guest, bool _filled);
void DrawDiscEntry(Guest& _guest); ///< BX = the radius, DX, CX the centre. Out: ES = DS. AX, BX, CX, DX, SI, DI, BP clobbered.
/// BL = the radius, CX, DX the centre. Out: ES = DS and DF clear once a chord's DrawLine fills. AX, BX, CX, DX, SI, DI, BP clobbered.
void DrawCircleEntry(Guest& _guest);
void FillSpanEntry(Guest& _guest);        ///< DL = left x, DH = right x, CL = 2 * row, ES = DS. Out: DI = the last byte.
void ClearCgaScreenEntry(Guest& _guest);  ///< Out: ES = B800h. AX, CX, DI clobbered.
void ClearTextScreenEntry(Guest& _guest); ///< Out: ES = B800h. AX, CX, DI clobbered.
/// DL = left x, DH = right x, SI = the row, BL = the fill, ES = DS. Out: DL = DH when the span crosses a byte, else DX the
/// right end's mask word; AX the right end's AND and OR, BP its mask's index; DI clobbered.
void FillTriangleSpanEntry(Guest& _guest);
/// (AX, DX), (BX, BP), (CX, DI) the corners. Out: ES = DS unless the triangle is wholly off the buffer. AX, BX, CX, DX, SI, DI, BP
/// clobbered.
void FillTriangleEntry(Guest& _guest);
/// (SI, DX), (BX, BP), (CX, DI) the corners, the rows doubled. Out as FillTriangleEntry.
void FillClippedTriangleEntry(Guest& _guest);
void WaitRetraceThenDelayEntry(Guest& _guest); ///< AX, DX clobbered.
/// Out, once it draws: DF clear, SI past the image and ES = B800h; and BX = 0100h and DX = 03D9h, as SetGraphicsMode leaves them,
/// once it sets the mode. AX, CX, DI clobbered.
void ShowCockpitScreenEntry(Guest& _guest);
void DrawChartFrameEntry(Guest& _guest);  ///< Out: ES = B800h once it draws. Every other register but DS clobbered.
void SetGraphicsModeEntry(Guest& _guest); ///< AX, BX, DX clobbered.
void SetTextModeEntry(Guest& _guest);     ///< AX, DX clobbered.
void DrawTitlePlanetEntry(Guest& _guest); ///< BX = the radius, DX, CX the centre. Out: ES = DS. AX, BX, CX, DX, SI, DI, BP clobbered.

} // namespace Elite
