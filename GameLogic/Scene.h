// GameLogic/Scene.h
#pragma once

#include "GameState.h"
#include "Maths.h"
#include "NativeEntry.h"
#include "ObjectSlot.h"
#include "Ships.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace Elite
{

// The reference's 3d routines, ported (plan §5 Phase 3, ADR-010): the 3D scene: transforming, projecting and drawing objects. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv; those de-assembled (ADR-012) take values and give values
// back, below the register bodies, and their entries keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> SceneEntries() noexcept;

/// ProjectVertices (CS:2340): projects the first projectedVertexCount 6-byte vertices of vertexBuffer in
/// place to 4-byte points, 80h + 256x/z and 40h + 256y/z rounded; a vertex nearer than nearPlaneZ gets
/// x = 8000h and keeps a stale y. An overflowing divide is saturated by the trap, as AL = 7Fh. Keeps SI;
/// AX, BX, CX, DX, DI clobbered.
void ProjectVertices(Guest& _guest);

/// DrawVisibleFaces (CS:3AB3): for each of CX faces at SI that faces the viewer, its edges
/// (DrawClippedLine) and filled triangles (FillTriangle) in order. Out: SI past the list.
void DrawVisibleFaces(Guest& _guest);

/// TransformSunOrPlanet (CS:3C52): scales slot DI's position down, transforms it to the view, stores it
/// at +10h/+12h/+14h and sets byte 0 bit 7.
void TransformSunOrPlanet(Guest& _guest);

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

/// DrawSunOrPlanet (CS:3F4F): the disc of the sun or planet in slot DI, with its altitude, cabin
/// temperature, fuel scooping and death by heat.
void DrawSunOrPlanet(Guest& _guest);

/// DrawDistantStation (CS:45C6): the station in slot DI as a disc in colour 3 at its projected compass position,
/// radius 7 rows when its depth byte +25h is below 14h and smaller further off, or nothing.
void DrawDistantStation(Guest& _guest);

/// ProjectToScreen (CS:8D2E): AX = 80h + 256x/z, BX = 40h + 256y/z for x = AX, y = BX, z = CX, an
/// overflowing divide saturated by the trap. DX, BP clobbered.
void ProjectToScreen(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// A vertex is the offset in the data segment of its three words, x, y and z, as the original holds it in SI or DI.

/// A vertex of vertexBuffer once ProjectVertices has projected it: its screen x and y.
struct ScreenPoint
{
  std::int16_t x;
  std::int16_t y;
};

/// Three projected vertices: a face's first three, which say which way it faces, or a filled triangle's corners.
struct Triangle
{
  ScreenPoint first;
  ScreenPoint second;
  ScreenPoint third;
};

/// ScaleDodoRadii's two radii, in the ratio of a dodecahedron's two rings, about 1.62.
struct DodoRadii
{
  std::int16_t inner; ///< 2a + a/4 + a/8 - a/32, about 2.34a
  std::int16_t outer; ///< 3a + a/2 + a/4 + a/16 - a/64, about 3.80a
};

/// Where RunVertexProgram leaves the program and its accumulator.
struct VertexProgramEnd
{
  std::uint16_t next; ///< the offset past the program
  Vector accumulator;
};

/// How far CheckShipInRange's test went with a ship IsObjectNear calls near: it stops at the first bound the ship exceeds.
struct ShipRange
{
  bool within;
  std::uint8_t axes;                       ///< the magnitudes it took, 1-3
  std::array<std::uint16_t, 3> magnitudes; ///< |x|, |y|, |z|
  std::uint8_t squares;                    ///< the squares it summed, 0, 2 or 3: none until every axis is within maxAxisDistance
  std::uint16_t squaredHigh;               ///< the sum of their high words
};

/// What CheckShipInRange finds of a ship.
struct ShipRangeCheck
{
  NearTest near;                            ///< IsObjectNear's test
  ShipRange range;                          ///< once the ship is near, how far the test went
  std::optional<DashboardPixel> erasedBlip; ///< out of range, the last pixel of the blip the EraseScannerBlip after it erased

  /// Whether the ship is in range: near, and within both bounds.
  [[nodiscard]] bool InRange() const noexcept
  {
    return near.nearby && range.within;
  }
};

/// How far ClassifyViewPosition (CS:3CA1), the shared tail of TransformShip and ClassifyStationPosition, got with a view position.
enum class ViewTest : std::uint8_t
{
  TooNear, ///< z negative or below nearClipZ: nothing stored
  WideX,   ///< stored, but twice |x| is beyond z
  WideY,   ///< twice |y| is beyond z
  Visible, ///< and byte 0 bit 7 set
};

/// What TransformToViewWithBlip gives: the position in the view, and the last pixel of the scanner blip UpdateScannerBlip drew on
/// the way, when it drew one.
struct ViewWithBlip
{
  Vector view;
  std::optional<DashboardPixel> blip;
};

/// ReflectVertexAboutCenter (CS:3740): for x, y and z in turn, the vertex at _reflection = drawCenter - the vertex at _vertex,
/// then the vertex at _vertex += drawCenter. Returns drawCenter, each coordinate as it read it.
Vector ReflectVertexAboutCenter(GameState& _state, std::uint16_t _vertex, std::uint16_t _reflection);

/// OffsetVertexByCenter (CS:3768): the vertex at _vertex plus drawCenter, x, y and z in turn. Returns drawCenter, each
/// coordinate as it read it.
Vector OffsetVertexByCenter(GameState& _state, std::uint16_t _vertex);

/// BuildBoxCornerVertices (CS:377A): the blueprint handler of types 1-29, from the blueprint's two extent bytes at DS:_extents,
/// blueprint+3. The eight corners (+-boxHalfWidth, +-the first, +-the second) about drawCenter, rotated by the drawn angles, the
/// player's pitch and the view direction, as vertices 34-41. Returns the offset past the extents.
std::uint16_t BuildBoxCornerVertices(GameState& _state, std::uint16_t _extents);

/// BuildDodoVertices (CS:38BF): the blueprint handler of type 0, the Dodo station: two rings of five from the sine table in
/// 72-degree steps from the roll angle, a slot of four, rotated as BuildBoxCornerVertices rotates, and the rings reflected
/// through drawCenter, as vertices 0-23. Returns drawCenter as the last OffsetVertexByCenter read it.
Vector BuildDodoVertices(GameState& _state);

/// ScaleDodoRadii (CS:3A13): the signed byte _value scaled to the Dodo's two ring radii by arithmetic shifts and adds.
[[nodiscard]] DodoRadii ScaleDodoRadii(std::int8_t _value);

/// RunVertexProgram (CS:3A40): runs the count byte and ops at _program on vertexBuffer, from _accumulator. An op's bits 0-5
/// are a vertex; bits 6-7 load it into the accumulator, store the accumulator there, average the two (the 16-bit sum
/// halved arithmetically), or add it.
[[nodiscard]] VertexProgramEnd RunVertexProgram(GameState& _state, std::uint16_t _program, Vector _accumulator);

/// TriangleWindingSign (CS:3A9B): whether (x0-x1)(y2-y1) - (y0-y1)(x2-x1) is negative, by the sign of the high words'
/// difference, or of the low words' when the high words agree: true when _triangle faces the viewer.
[[nodiscard]] bool TriangleWindingSign(Triangle _triangle);

/// CheckShipInRange (CS:3BEA): IsObjectNear, then |x|, |y| and |z| of _slot's position below maxAxisDistance and the high words
/// of their squares, summed, below maxDistanceSquaredHigh after the second and the third. In range, _slot's size (+3Eh) is that
/// sum shifted right 6 and its in-range bit (byte 0 bit 6) is set; out of range, EraseScannerBlip.
ShipRangeCheck CheckShipInRange(GameState& _state, ObjectSlot _slot);

/// ClassifyStationPosition (CS:3C72): ClassifyViewPosition on _slot's compass position, +20h/+22h/+24h: visible, with byte 0 bit 7
/// set, when z is at least nearClipZ and twice |x| and twice |y| are at most z; the position stored at +10h/+12h/+14h and as
/// drawCenter once z passes.
ViewTest ClassifyStationPosition(GameState& _state, ObjectSlot _slot);

/// TransformToViewWithBlip (CS:3ED7): _position turned to the camera's frame (RotatePitchYawRoll), the high byte of its z into
/// _slot's +3Ch, _slot's scanner blip moved there (UpdateScannerBlip), then the frame turned to the view (TransformToView's tail).
ViewWithBlip TransformToViewWithBlip(GameState& _state, ObjectSlot _slot, Vector _position);

/// TransformToView (CS:3EE3): _position turned to the camera's frame (RotatePitchYawRoll), then, off the front view, (x, z) turned
/// by -viewAngle through rotationSinCos[8], which it sets.
Vector TransformToView(GameState& _state, Vector _position);

/// LoadPlayerAngles (CS:8A16): the player's pitch, yaw and roll into rotation pairs 0-2. Returns the roll's sine and cosine.
SinCos LoadPlayerAngles(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void ReflectVertexAboutCenterEntry(Guest& _guest); ///< SI = the vertex, DI = its reflection. Out: BX = drawCenterZ; AX clobbered.
void OffsetVertexByCenterEntry(Guest& _guest);     ///< SI = the vertex. Out: AX = drawCenterZ.
void BuildBoxCornerVerticesEntry(Guest& _guest);   ///< SI = blueprint+3. Out: SI = blueprint+5; AX, BX, CX, DX, BP, DI clobbered.
void BuildDodoVerticesEntry(Guest& _guest);        ///< Out: AX = BX = drawCenterZ, CX = 0, DI past the reflections; SI kept.
void ScaleDodoRadiiEntry(Guest& _guest);           ///< AL = the value. Out: CX inner, DX outer; AX clobbered.
void RunVertexProgramEntry(Guest& _guest); ///< SI = the program, BP, BX, DX the accumulator, in and out. Out: CX = 0; AX, DI clobbered.
/// p0 = (AX, DX), p1 = (BX, BP), p2 = (CX, DI). Out: SF; DX:AX = (y0-y1)(x2-x1), BX = the low word of (x0-x1)(y2-y1), less AX when the
/// high words agree; CX, DI clobbered.
void TriangleWindingSignEntry(Guest& _guest);
void CheckShipInRangeEntry(Guest& _guest); ///< DI = the slot. Out: CF clear in range; every register as the original leaves it.
/// DI = the slot. Out: CF clear when visible; AX, BX and CX the compass position, AX doubled |x| once z passes and BX doubled |y|
/// once x does, as the original leaves them.
void ClassifyStationPositionEntry(Guest& _guest);
/// DI = the slot, AX, BX, CX the position, in and out. Out: ES = B800h once UpdateScannerBlip draws a blip; DX clobbered.
void TransformToViewWithBlipEntry(Guest& _guest);
void TransformToViewEntry(Guest& _guest);  ///< AX, BX, CX the position, in and out. DX clobbered.
void LoadPlayerAnglesEntry(Guest& _guest); ///< Out: AX, BX the roll's sine and cosine.

} // namespace Elite
