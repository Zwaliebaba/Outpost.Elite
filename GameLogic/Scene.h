// GameLogic/Scene.h
#pragma once

#include "GameState.h"
#include "Maths.h"
#include "NativeEntry.h"
#include "ObjectSlot.h"
#include "Ships.h"
#include "Video.h"

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

/// TransformShip (CS:3C7E): slot DI's position to the view, scooping where it may, then
/// ClassifyViewPosition. Out: CF clear when visible.
void TransformShip(Guest& _guest);

/// TransformAndDrawObjects (CS:3D25): classifies and transforms every slot, then draws the visible ones
/// from the farthest in. Clobbers every register.
void TransformAndDrawObjects(Guest& _guest);

/// DrawSunOrPlanet (CS:3F4F): the disc of the sun or planet in slot DI, with its altitude, cabin
/// temperature, fuel scooping and death by heat. Every register but DS clobbered.
void DrawSunOrPlanet(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// A vertex is the offset in the data segment of its three words, x, y and z, as the original holds it in SI or DI. A string
// instruction's direction is the direction flag the routine finds: _backward.

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

/// What DrawVisibleFaces leaves: the offset past the faces, and the direction flag, clear once an edge's DrawLine has filled.
struct FacesEnd
{
  std::uint16_t next;
  bool backward;
};

/// What TransformToViewWithBlip gives: the position in the view, and the last pixel of the scanner blip UpdateScannerBlip drew on
/// the way, when it drew one.
struct ViewWithBlip
{
  Vector view;
  std::optional<DashboardPixel> blip;
};

/// ProjectVertices (CS:2340): the first projectedVertexCount 6-byte vertices of vertexBuffer projected in place to 4-byte points,
/// 80h + 256x/z and 40h + 256y/z rounded; a vertex nearer than nearPlaneZ gets x = 8000h and keeps a stale y. Each divide takes a
/// memory operand, so the trap saturates only AL when it overflows; it saves BX, _trapHigh over the coordinate's sign, _trapHigh
/// being the high byte of the BX its caller leaves.
void ProjectVertices(GameState& _state, std::uint8_t _trapHigh);

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

/// DrawVisibleFaces (CS:3AB3): for each of _faces faces at _list that faces the viewer (TriangleWindingSign on its first three
/// vertices), its items in order: an edge (DrawClippedLine) in its colour unless an end is off screen, or a triangle
/// (FillTriangle) in its pattern unless a corner is. A face turned away is skipped.
FacesEnd DrawVisibleFaces(GameState& _state, std::uint16_t _list, std::uint16_t _faces, bool _backward);

/// CheckShipInRange (CS:3BEA): IsObjectNear, then |x|, |y| and |z| of _slot's position below maxAxisDistance and the high words
/// of their squares, summed, below maxDistanceSquaredHigh after the second and the third. In range, _slot's size (+3Eh) is that
/// sum shifted right 6 and its in-range bit (byte 0 bit 6) is set; out of range, EraseScannerBlip.
ShipRangeCheck CheckShipInRange(GameState& _state, ObjectSlot _slot);

/// TransformSunOrPlanet (CS:3C52): _slot's position scaled down by its GetPositionScaleShift, which becomes its disc scale
/// (+0Ah) and depth (+3Dh), turned to the view (TransformToViewWithBlip) and stored at +10h/+12h/+14h, and byte 0 bit 7 set.
ViewWithBlip TransformSunOrPlanet(GameState& _state, ObjectSlot _slot);

/// RunBlueprintHandler (CS:3CDD): boxHalfWidth from the blueprint at _blueprint, the handler it names run (BuildBoxCornerVertices
/// or BuildDodoVertices), then what the handler returns into at RenderBlueprintBody (CS:3CF2): the vertex program, the projection
/// and the faces. Returns the direction flag as they leave it.
bool RunBlueprintHandler(GameState& _state, std::uint16_t _blueprint, bool _backward);

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

/// DrawDistantStation (CS:45C6): the station in _slot as a disc in colour 3 at its compass position, +20h/+22h/+24h, projected
/// (ProjectToScreen): radius 7 rows when its depth byte +25h is below 14h, smaller further off, and none from 21h to A5h.
void DrawDistantStation(GameState& _state, ObjectSlot _slot, bool _backward);

/// LoadPlayerAngles (CS:8A16): the player's pitch, yaw and roll into rotation pairs 0-2. Returns the roll's sine and cosine.
SinCos LoadPlayerAngles(GameState& _state);

/// ProjectToScreen (CS:8D2E): _view on the screen, 80h + 256x/z and 40h + 256y/z, each divide saturated by the trap when it
/// overflows.
[[nodiscard]] ScreenPoint ProjectToScreen(GameState& _state, Vector _view);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void ProjectVerticesEntry(Guest& _guest);          ///< BH saved by the trap. SI, BP kept; AX, BX, CX, DX, DI clobbered.
void ReflectVertexAboutCenterEntry(Guest& _guest); ///< SI = the vertex, DI = its reflection. Out: BX = drawCenterZ; AX clobbered.
void OffsetVertexByCenterEntry(Guest& _guest);     ///< SI = the vertex. Out: AX = drawCenterZ.
void BuildBoxCornerVerticesEntry(Guest& _guest);   ///< SI = blueprint+3. Out: SI = blueprint+5; AX, BX, CX, DX, BP, DI clobbered.
void BuildDodoVerticesEntry(Guest& _guest);        ///< Out: AX = BX = drawCenterZ, CX = 0, DI past the reflections; SI kept.
void ScaleDodoRadiiEntry(Guest& _guest);           ///< AL = the value. Out: CX inner, DX outer; AX clobbered.
void RunVertexProgramEntry(Guest& _guest); ///< SI = the program, BP, BX, DX the accumulator, in and out. Out: CX = 0; AX, DI clobbered.
/// p0 = (AX, DX), p1 = (BX, BP), p2 = (CX, DI). Out: SF; DX:AX = (y0-y1)(x2-x1), BX = the low word of (x0-x1)(y2-y1), less AX when the
/// high words agree; CX, DI clobbered.
void TriangleWindingSignEntry(Guest& _guest);
/// SI = the faces, CX their count. Out: SI past them; DF clear once an edge's DrawLine fills. AX, BX, CX, DX, DI, BP, ES clobbered.
void DrawVisibleFacesEntry(Guest& _guest);
void CheckShipInRangeEntry(Guest& _guest); ///< DI = the slot. Out: CF clear in range; every register as the original leaves it.
/// DI = the slot. Out: AX, BX, CX the view position; ES = B800h once UpdateScannerBlip draws a blip. DX, BP clobbered.
void TransformSunOrPlanetEntry(Guest& _guest);
/// DI = the slot. Out: CF clear when visible; AX, BX and CX the compass position, AX doubled |x| once z passes and BX doubled |y|
/// once x does, as the original leaves them.
void ClassifyStationPositionEntry(Guest& _guest);
/// SI = the blueprint, DI = the slot. Out: DI kept; DF as the faces leave it. Every other register but DS clobbered.
void RunBlueprintHandlerEntry(Guest& _guest);
/// SI = the blueprint past its handler's bytes, BP, BX, DX the accumulator, and the slot under the return address. Out: DI = the
/// slot, popped; DF as the faces leave it. Every other register but DS clobbered.
void RenderBlueprintBodyEntry(Guest& _guest);
/// DI = the slot, AX, BX, CX the position, in and out. Out: ES = B800h once UpdateScannerBlip draws a blip; DX clobbered.
void TransformToViewWithBlipEntry(Guest& _guest);
void TransformToViewEntry(Guest& _guest);    ///< AX, BX, CX the position, in and out. DX clobbered.
void DrawDistantStationEntry(Guest& _guest); ///< DI = the slot. Every register but DS clobbered.
void LoadPlayerAnglesEntry(Guest& _guest);   ///< Out: AX, BX the roll's sine and cosine.
void ProjectToScreenEntry(Guest& _guest);    ///< AX, BX, CX = x, y, z. Out: AX, BX the point. DX, BP clobbered.

} // namespace Elite
