// GameLogic/Video.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's video routines, ported (plan §5 Phase 3, ADR-010): drawing: lines, spans, the space view and the dashboard into CGA memory. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv. Those de-assembled so far (ADR-012) follow
// the bodies: they take values and give values back, and their entries keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> VideoEntries() noexcept;

/// FinishSpaceViewFrame (CS:0570): DrawLaserSights, PresentSpaceView and ClearDrawBuffer, through their hooks.
/// It waits. Out: ES=B800h, DF=0; AX, BX, CX, DX, SI, DI, BP clobbered.
void FinishSpaceViewFrame(Guest& _guest);

/// PresentChartFrame (CS:0587): CopyChartBufferToScreen, with AL=0 whatever the caller passed, then
/// ClearDrawBuffer. It waits. Out: ES=B800h, DF=0; AX, BX, CX, DX, SI, DI, BP clobbered.
void PresentChartFrame(Guest& _guest);

/// PresentSpaceView (CS:0599): waits until msSinceFrame reaches minimumFrameMs and clears it,
/// WaitRetraceThenDelay, then copies spaceViewBuffer to the screen. In: ES=B800h, DF=0. AX, BX, CX, DX, SI, DI,
/// BP clobbered.
void PresentSpaceView(Guest& _guest);

/// DrawClippedLine (CS:1603): the line from (DX, BX) to (CX, AX), signed words, clipped to the 256x128
/// buffer, through DrawLine. Everything but DS clobbered.
void DrawClippedLine(Guest& _guest);

/// ClipLineToLowEdge (CS:1686): moves the endpoint of (CX, AX)-(DX, BX) below 0 on the CX/DX axis onto
/// 0; BP counts the endpoints still outside. CF clear when both are below, else set, with ZF once BP
/// reaches 0. SI clobbered.
void ClipLineToLowEdge(Guest& _guest);

/// ClipLineToHighEdge (CS:16C1): ClipLineToLowEdge for the edge at 255, the caller having subtracted
/// 0FFh from the clipped coordinates.
void ClipLineToHighEdge(Guest& _guest);

/// DrawDisc (CS:1826): a filled disc of radius BX at (DX, CX), signed words, with a ragged edge while
/// sunFringeMask is set. Everything but DS clobbered; ES=DS.
void DrawDisc(Guest& _guest);

/// DrawCircle (CS:1AC1): 32 chords round (CX, DX) with radius BL, through DrawClippedLine. Everything but
/// DS clobbered.
void DrawCircle(Guest& _guest);

/// FillTriangle (CS:1BFB) and FillClippedTriangle (CS:1E6E), which it runs into: the triangle (AX, DX),
/// (BX, BP), (CX, DI) filled with triangleFillPattern. Out: ES=DS unless it is wholly outside;
/// everything else but DS clobbered.
void FillTriangle(Guest& _guest);

/// FillClippedTriangle (CS:1E6E): FillTriangle's path for a triangle not wholly inside the buffer, entered with SI
/// = A's x and the rows doubled.
void FillClippedTriangle(Guest& _guest);

/// DrawTitlePlanet (CS:7D4E): DrawDisc with sunFringeMask 1, then 0. The registers come back as DrawDisc
/// leaves them.
void DrawTitlePlanet(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight, and every byte it writes is written as the
// original writes it, in the same order and at the same width. A string instruction's direction is the direction flag its
// entry finds: _backward.

/// SaveScreenshot (CS:01B7): with the game's divide, keyboard and timer handlers taken out and CriticalErrorInterrupt on
/// int 24h, WriteScreenshotFile, then ShowDiskError if it failed; then int 24h as it was, and the game's handlers put back.
void SaveScreenshot(GameState& _state, Hardware& _hardware);

/// WriteScreenshotFile (CS:03FD): screenshotNumber stepped, and eliteNN.lo (the text page) or eliteNN.hi (both graphics
/// banks) written from B800:0000 through DOS, with its disk transfer area at diskTransferArea; diskError = 1 on a failure.
void WriteScreenshotFile(GameState& _state, Hardware& _hardware);

/// CopyChartBufferToScreen (CS:05CC): waits for a vertical retrace and a delay, then copies 64 - 4 * _bandsSkipped line pairs, as
/// a byte, of the drawing buffer from DS:_bandsSkipped * 512 to the chart's place on the screen, at B800:0648. Its loops turn
/// through _hardware.
void CopyChartBufferToScreen(GameState& _state, Hardware& _hardware, std::uint8_t _bandsSkipped, bool _backward);

/// ClearDrawBuffer (CS:060D): the drawing buffer, DS:0000-1FFF, zeroed a word at a time as REP STOSW does.
void ClearDrawBuffer(GameState& _state, bool _backward);

/// PlotPixel (CS:15E0): the pixel at _x, _row of the drawing buffer set to colorFillBytes[drawColor].
void PlotPixel(GameState& _state, std::uint8_t _x, std::uint8_t _row);

/// DrawLine (CS:16D1): Bresenham from (_fromX, _fromRow) to (_toX, _toRow) into the drawing buffer in colorFillBytes[drawColor].
/// Returns whether it filled whole bytes of a horizontal line with REP STOSB, for which the original sets ES to DS and clears
/// the direction flag first.
bool DrawLine(GameState& _state, std::uint8_t _fromX, std::uint8_t _fromRow, std::uint8_t _toX, std::uint8_t _toRow);

/// FillSpan (CS:1A07): row _doubledRow / 2 of the drawing buffer filled with discFillByte from x = _left to x = _right, the
/// end bytes through spanLeftMasks and spanRightMasks and the bytes between them by STOSB and REP STOSW into ES = DS.
/// Returns the offset of the span's last byte.
std::uint16_t FillSpan(GameState& _state, std::uint8_t _left, std::uint8_t _right, std::uint8_t _doubledRow, bool _backward);

/// FillTriangleSpan (CS:1B7A): x = _left to _right, _left first, of the drawing buffer's row at DS:_row filled with _fill: the
/// end bytes through triangleEdgeMasks, which the original reads through BP from SS:0000, DS:AD60, and the bytes between
/// them by STOSB and REP STOSW into ES = DS.
void FillTriangleSpan(GameState& _state, std::uint16_t _row, std::uint8_t _left, std::uint8_t _right, std::uint8_t _fill, bool _backward);

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

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its results back
// there. The registers the contract leaves to the routine it hands to Guest::Clobber, unless a caller reads what the original
// leaves in one: then the entry leaves that, and the contract compares it (FillSpanEntry's DI, DrawLineEntry's ES,
// FillTriangleSpanEntry's AX and BP, ShowCockpitScreenEntry's SI and ES, DrawChartFrameEntry's ES).

void SaveScreenshotEntry(Guest& _guest); ///< Out: ES=B800h; AX, BX, CX, DX, SI, DI clobbered.
/// AX, BX, CX, DX clobbered.
void WriteScreenshotFileEntry(Guest& _guest);
/// AL = the bands to skip, ES = B800h, DF clear. AX, BX, CX, DX, SI, DI, BP clobbered.
void CopyChartBufferToScreenEntry(Guest& _guest);
void ClearDrawBufferEntry(Guest& _guest); ///< Out: ES = DS, AX = 0, CX = 0, DI past the buffer.
void PlotPixelEntry(Guest& _guest);       ///< DL = x, DH = row. BX, CX clobbered.
void DrawLineEntry(Guest& _guest);        ///< DL, DH to CL, CH. Out: ES = DS and DF clear after REP STOSB.
/// What DrawLine leaves of the machine when it returns _filled: ES = DS and the direction flag clear, which it sets before a
/// horizontal line's REP STOSB. For the entries and the register code of the routines that call it, whose originals go on
/// with them.
void DrawLineOut(Guest& _guest, bool _filled);
void FillSpanEntry(Guest& _guest);        ///< DL = left x, DH = right x, CL = 2 * row, ES = DS. Out: DI = the last byte.
void ClearCgaScreenEntry(Guest& _guest);  ///< Out: ES = B800h. AX, CX, DI clobbered.
void ClearTextScreenEntry(Guest& _guest); ///< Out: ES = B800h. AX, CX, DI clobbered.
/// DL = left x, DH = right x, SI = the row, BL = the fill, ES = DS. Out: DL = DH when the span crosses a byte, else DX the
/// right end's mask word; AX the right end's AND and OR, BP its mask's index; DI clobbered.
void FillTriangleSpanEntry(Guest& _guest);
void WaitRetraceThenDelayEntry(Guest& _guest); ///< AX, DX clobbered.
/// Out, once it draws: DF clear, SI past the image and ES = B800h; and BX = 0100h and DX = 03D9h, as SetGraphicsMode leaves them,
/// once it sets the mode. AX, CX, DI clobbered.
void ShowCockpitScreenEntry(Guest& _guest);
void DrawChartFrameEntry(Guest& _guest);  ///< Out: ES = B800h once it draws. Every other register but DS clobbered.
void SetGraphicsModeEntry(Guest& _guest); ///< AX, BX, DX clobbered.
void SetTextModeEntry(Guest& _guest);     ///< AX, DX clobbered.

} // namespace Elite
