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

/// OffsetVertexByCenter (CS:3768): the vertex at SI plus drawCenter, three words. AX clobbered.
void OffsetVertexByCenter(Guest& _guest);

/// BuildBoxCornerVertices (CS:377A): the blueprint handler of types 1-29, from SI = blueprint+3. The eight corners
/// (+-boxHalfWidth, +-byte +3, +-byte +4) about drawCenter, rotated by the drawn angles, the player's pitch and the
/// view direction, as vertices 34-41. Out: SI = blueprint+5; AX, BX, CX, DX, BP, DI clobbered.
void BuildBoxCornerVertices(Guest& _guest);

/// BuildDodoVertices (CS:38BF): the blueprint handler of type 0, the Dodo station: two rings of five from the sine
/// table in 72-degree steps from the roll angle, a slot of four, rotated as BuildBoxCornerVertices rotates, and the
/// rings reflected through drawCenter, as vertices 0-23. Keeps SI.
void BuildDodoVertices(Guest& _guest);

/// ScaleDodoRadii (CS:3A13): the signed byte AL by shifts and adds. Out: CX = AL * 2.34, DX = AL * 3.80; AX
/// clobbered.
void ScaleDodoRadii(Guest& _guest);

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

/// RenderBlueprintBody (CS:3CF2): what a blueprint handler returns into: RunBlueprintHandler's rendering of the
/// blueprint at SI, then the slot it pushed popped into DI.
void RenderBlueprintBody(Guest& _guest);

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

/// DrawDistantStation (CS:45C6): the station in slot DI as a disc in colour 3 at its projected compass position,
/// radius 7 rows when its depth byte +25h is below 14h and smaller further off, or nothing.
void DrawDistantStation(Guest& _guest);

/// LoadPlayerAngles (CS:8A16): the player's pitch, yaw and roll into rotation pairs 0-2. Out: AX, BX the roll's sine
/// and cosine.
void LoadPlayerAngles(Guest& _guest);

/// ProjectToScreen (CS:8D2E): AX = 80h + 256x/z, BX = 40h + 256y/z for x = AX, y = BX, z = CX, an
/// overflowing divide saturated by the trap. DX, BP clobbered.
void ProjectToScreen(Guest& _guest);

} // namespace Elite
