// GameLogic/Scene.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's 3d routines, ported (plan §5 Phase 3, ADR-010): the 3D scene: transforming, projecting and drawing objects. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> SceneEntries() noexcept;

/// ProjectVertices (CS:2340): projects the first projectedVertexCount 6-byte vertices of vertexBuffer in
/// place to 4-byte points, 80h + 256x/z and 40h + 256y/z rounded; a vertex nearer than nearPlaneZ gets
/// x = 8000h and keeps a stale y. An overflowing divide is saturated by the trap, as AL = 7Fh. Keeps SI;
/// AX, BX, CX, DX, DI clobbered.
void ProjectVertices(Guest& _guest);

/// ReflectVertexAboutCenter (CS:3740): [DI] = drawCenter - [SI], then [SI] += drawCenter, three words
/// each. AX, BX clobbered.
void ReflectVertexAboutCenter(Guest& _guest);

/// RunVertexProgram (CS:3A40): runs the count byte and ops at SI on vertexBuffer, with BP, BX, DX as the
/// accumulator. Out: SI past the program; AX, CX, DI clobbered.
void RunVertexProgram(Guest& _guest);

/// TriangleWindingSign (CS:3A9B): SF from (x0-x1)(y2-y1) - (y0-y1)(x2-x1), with p0 = (AX, DX), p1 =
/// (BX, BP), p2 = (CX, DI): the high words' difference, or the low words' when the high words agree.
void TriangleWindingSign(Guest& _guest);

/// DrawVisibleFaces (CS:3AB3): for each of CX faces at SI that faces the viewer, its edges
/// (DrawClippedLine) and filled triangles (FillTriangle) in order. Out: SI past the list.
void DrawVisibleFaces(Guest& _guest);

/// CheckShipInRange (CS:3BEA): CF clear when slot DI is near and within maxAxisDistance and
/// maxDistanceSquaredHigh, setting its +3Eh and byte 0 bit 6; else erases its blip and sets CF.
void CheckShipInRange(Guest& _guest);

/// TransformSunOrPlanet (CS:3C52): scales slot DI's position down, transforms it to the view, stores it
/// at +10h/+12h/+14h and sets byte 0 bit 7.
void TransformSunOrPlanet(Guest& _guest);

/// ClassifyStationPosition (CS:3C72): ClassifyViewPosition on the view position at +20h/+22h/+24h. Out:
/// CF clear when visible.
void ClassifyStationPosition(Guest& _guest);

/// TransformShip (CS:3C7E): slot DI's position to the view, scooping where it may, then
/// ClassifyViewPosition. Out: CF clear when visible.
void TransformShip(Guest& _guest);

/// RunBlueprintHandler (CS:3CDD): sets boxHalfWidth from blueprint SI, runs the handler the blueprint
/// names, then what follows it at RenderBlueprintBody (CS:3CF2): the vertex program, the projection and
/// the faces. Out: DI = the slot; every other register clobbered.
void RunBlueprintHandler(Guest& _guest);

/// TransformAndDrawObjects (CS:3D25): classifies and transforms every slot, then draws the visible ones
/// from the farthest in. Clobbers every register.
void TransformAndDrawObjects(Guest& _guest);

/// TransformToViewWithBlip (CS:3ED7): RotatePitchYawRoll, slot DI's +3Ch and scanner blip, then the view
/// direction. In and out: AX, BX, CX.
void TransformToViewWithBlip(Guest& _guest);

/// TransformToView (CS:3EE3): RotatePitchYawRoll, then the view direction. In and out: AX, BX, CX.
void TransformToView(Guest& _guest);

/// DrawSunOrPlanet (CS:3F4F): the disc of the sun or planet in slot DI, with its altitude, cabin
/// temperature, fuel scooping and death by heat.
void DrawSunOrPlanet(Guest& _guest);

/// ProjectToScreen (CS:8D2E): AX = 80h + 256x/z, BX = 40h + 256y/z for x = AX, y = BX, z = CX, an
/// overflowing divide saturated by the trap. DX, BP clobbered.
void ProjectToScreen(Guest& _guest);

} // namespace Elite
