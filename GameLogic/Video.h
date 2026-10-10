// GameLogic/Video.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's video routines, ported (plan §5 Phase 3, ADR-010): drawing: lines, spans, the space view and the dashboard into CGA memory. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> VideoEntries() noexcept;

/// ClearDrawBuffer (CS:060D): zero-fills DS:0000-1FFF, the drawing buffer. Out: ES=DS, AX=0, CX=0,
/// DI=2000h.
void ClearDrawBuffer(Guest& _guest);

/// PlotPixel (CS:15E0): sets the pixel at DL=x, DH=row to colorFillBytes[drawColor]. BX, CX clobbered.
void PlotPixel(Guest& _guest);

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

/// DrawLine (CS:16D1): Bresenham from (DL, DH) to (CL, CH) into the drawing buffer in
/// colorFillBytes[drawColor]. AX, BX, CX, DX, DI, BP and ES clobbered.
void DrawLine(Guest& _guest);

/// DrawDisc (CS:1826): a filled disc of radius BX at (DX, CX), signed words, with a ragged edge while
/// sunFringeMask is set. Everything but DS clobbered; ES=DS.
void DrawDisc(Guest& _guest);

/// FillSpan (CS:1A07): fills row CL/2 of the drawing buffer from x=DL to x=DH with discFillByte. DI
/// clobbered.
void FillSpan(Guest& _guest);

/// DrawCircle (CS:1AC1): 32 chords round (CX, DX) with radius BL, through DrawClippedLine. Everything but
/// DS clobbered.
void DrawCircle(Guest& _guest);

/// FillTriangleSpan (CS:1B7A): fills x=DL to DH of the buffer row at SI with BL, the end bytes through
/// triangleEdgeMasks at SS:0000. Out: DL=DH when the span crosses a byte, else DX the right edge's mask
/// word; AX, DI, BP clobbered.
void FillTriangleSpan(Guest& _guest);

/// FillTriangle (CS:1BFB) and FillClippedTriangle (CS:1E6E), which it runs into: the triangle (AX, DX),
/// (BX, BP), (CX, DI) filled with triangleFillPattern. Out: ES=DS unless it is wholly outside;
/// everything else but DS clobbered.
void FillTriangle(Guest& _guest);

/// ShowCockpitScreen (CS:7BC0): unless the cockpit shows already, graphics mode or a clear screen and
/// the cockpit image copied in. AX, CX, SI, DI, ES clobbered, and BX and DX when it sets the mode.
void ShowCockpitScreen(Guest& _guest);

/// ClearCgaScreen (CS:7BFB): zero-fills both banks of CGA memory. Out: ES=B800h; AX, CX, DI clobbered.
void ClearCgaScreen(Guest& _guest);

/// ClearTextScreen (CS:7C12): the 40x25 text page filled with spaces in textAttribute. Out: ES=B800h;
/// AX, CX, DI clobbered.
void ClearTextScreen(Guest& _guest);

/// DrawChartFrame (CS:7C25): unless the chart frame shows already, graphics mode or a clear screen and
/// the frame's box lines. AX, BX, CX, DX, SI, DI, BP, ES clobbered.
void DrawChartFrame(Guest& _guest);

/// SetGraphicsMode (CS:7CFE): BIOS mode 4, palette 0, bright on black. AX, BX, DX clobbered.
void SetGraphicsMode(Guest& _guest);

/// SetTextMode (CS:7D11): BIOS mode 1, the cursor off the page, blink off. AX, DX clobbered.
void SetTextMode(Guest& _guest);

/// DrawTitlePlanet (CS:7D4E): DrawDisc with sunFringeMask 1, then 0. The registers come back as DrawDisc
/// leaves them.
void DrawTitlePlanet(Guest& _guest);

} // namespace Elite
