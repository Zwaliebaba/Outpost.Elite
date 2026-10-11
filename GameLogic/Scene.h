// GameLogic/Scene.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "Maths.h"
#include "SpaceObject.h"
#include "Ships.h"
#include "Video.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

// The reference's 3d routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the 3D scene: transforming, projecting and
// drawing objects. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// A vertex is the offset in the data segment of its three words, x, y and z, as the original holds it in SI or DI. A string
// instruction's direction is the direction flag the routine finds: _backward.

/// The vertices a blueprint's program starts from, by the handler its first word names.
enum class BlueprintShape : std::uint8_t
{
  Box, ///< BuildBoxCornerVertices (CS:377A): a box from its extents and half-width
  Dodo ///< BuildDodoVertices (CS:38BF): the Dodo's two rings and its slot
};

/// A ship type's blueprint (RunBlueprintHandler, RenderBlueprint): what its handler builds, and the parts of its body, each
/// as the original reads it, a byte at a time. Scene.cpp holds each type's (BLUEPRINTS, SceneTables.h).
struct Blueprint
{
  BlueprintShape shape;
  std::uint8_t halfWidth;                      ///< boxHalfWidth
  std::uint16_t extents;                       ///< a box's: its half-height in the low byte, its half-length in the high
  std::span<const std::uint8_t> vertexProgram; ///< RunVertexProgram's operations
  std::uint8_t projectedVertices;              ///< the vertices ProjectVertices projects
  std::span<const std::uint8_t> fixedEdges;    ///< two bytes an edge, drawn by no routine
  std::span<const std::uint8_t> faceEdges;     ///< two bytes an edge: its points in the low bits, its colour in the high
  std::uint8_t faces;                          ///< the faces DrawVisibleFaces walks
  std::span<const std::uint8_t> faceList;      ///< each face: its three vertex bytes, a count, and its items
};

/// ScaleDodoRadii's two radii, in the ratio of a dodecahedron's two rings, about 1.62.
struct DodoRadii
{
  std::int16_t inner; ///< 2a + a/4 + a/8 - a/32, about 2.34a
  std::int16_t outer; ///< 3a + a/2 + a/4 + a/16 - a/64, about 3.80a
};

/// How far CheckShipInRange's test went with a ship IsObjectNear calls near: it stops at the first bound the ship exceeds.
struct ShipRange
{
  bool within;
  std::array<std::uint16_t, 3> magnitudes; ///< |x|, |y|, |z|
  std::uint16_t squaredHigh;               ///< the sum of the high words of their squares
};

/// What CheckShipInRange finds of a ship.
struct ShipRangeCheck
{
  NearTest near;   ///< IsObjectNear's test
  ShipRange range; ///< once the ship is near, how far the test went

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

/// What DrawVisibleFaces leaves: the offset in the face list past the faces, and the direction flag, clear once an edge's DrawLine
/// has filled.
struct FacesEnd
{
  std::uint16_t next;
  bool backward;
};

/// ProjectVertices (CS:2340): the first projectedVertexCount 6-byte vertices of vertexBuffer projected in place to 4-byte points,
/// 80h + 256x/z and 40h + 256y/z rounded; a vertex nearer than nearPlaneZ gets x = 8000h and keeps a stale y. Each divide takes a
/// memory operand, so the trap saturates only AL when it overflows.
void ProjectVertices(GameState& _state);

/// ReflectVertexAboutCenter (CS:3740): for x, y and z in turn, the vertex at _reflection = drawCenter - the vertex at _vertex,
/// then the vertex at _vertex += drawCenter, each the byte of vertexBuffer it starts at.
void ReflectVertexAboutCenter(GameState& _state, std::size_t _vertex, std::size_t _reflection);

/// OffsetVertexByCenter (CS:3768): the vertex at byte _vertex of vertexBuffer plus drawCenter, x, y and z in turn. Returns
/// drawCenter, each coordinate as it read it.
Vector OffsetVertexByCenter(GameState& _state, std::size_t _vertex);

/// BuildBoxCornerVertices (CS:377A): the blueprint handler of types 1-29, from the blueprint's two extent bytes _extents, the
/// first low. The eight corners (+-boxHalfWidth, +-the first, +-the second) about drawCenter, rotated by the drawn angles, the
/// player's pitch and the view direction, as vertices 34-41.
void BuildBoxCornerVertices(GameState& _state, std::uint16_t _extents);

/// BuildDodoVertices (CS:38BF): the blueprint handler of type 0, the Dodo station: two rings of five from the sine table in
/// 72-degree steps from the roll angle, a slot of four, rotated as BuildBoxCornerVertices rotates, and the rings reflected
/// through drawCenter, as vertices 0-23.
void BuildDodoVertices(GameState& _state);

/// ScaleDodoRadii (CS:3A13): the signed byte _value scaled to the Dodo's two ring radii by arithmetic shifts and adds.
[[nodiscard]] DodoRadii ScaleDodoRadii(std::int8_t _value);

/// RunVertexProgram (CS:3A40): runs a blueprint's ops, _program, on vertexBuffer, from _accumulator. An op's bits 0-5 are a
/// vertex; bits 6-7 load it into the accumulator, store the accumulator there, average the two (the 16-bit sum halved
/// arithmetically), or add it. Returns the accumulator.
[[nodiscard]] Vector RunVertexProgram(GameState& _state, std::span<const std::uint8_t> _program, Vector _accumulator);

/// TriangleWindingSign (CS:3A9B): whether (x0-x1)(y2-y1) - (y0-y1)(x2-x1) is negative, by the sign of the high words'
/// difference, or of the low words' when the high words agree: true when _triangle faces the viewer.
[[nodiscard]] bool TriangleWindingSign(Triangle _triangle);

/// DrawVisibleFaces (CS:3AB3): for each of _faces faces of _blueprint's face list that faces the viewer (TriangleWindingSign on
/// its first three vertices), its items in order: an edge (DrawClippedLine) of its face edges in its colour unless an end is off
/// screen, or a triangle (FillTriangle) in its pattern unless a corner is. A face turned away is skipped.
FacesEnd DrawVisibleFaces(GameState& _state, const Blueprint& _blueprint, std::uint16_t _faces, bool _backward);

/// CheckShipInRange (CS:3BEA): IsObjectNear, then |x|, |y| and |z| of _slot's position below maxAxisDistance and the high words
/// of their squares, summed, below maxDistanceSquaredHigh after the second and the third. In range, _slot's size (+3Eh) is that
/// sum shifted right 6 and its in-range bit (byte 0 bit 6) is set; out of range, EraseScannerBlip.
ShipRangeCheck CheckShipInRange(GameState& _state, SpaceObject& _slot);

/// TransformSunOrPlanet (CS:3C52): _slot's position scaled down by its GetPositionScaleShift, which becomes its disc scale
/// (+0Ah) and depth (+3Dh), turned to the view (TransformToViewWithBlip) and stored at +10h/+12h/+14h, and byte 0 bit 7 set.
void TransformSunOrPlanet(GameState& _state, SpaceObject& _slot);

/// TransformShip (CS:3C7E): _slot's position, the low words, turned to the view (TransformToViewWithBlip); offered to the hold
/// (TryScoopObject) while fuel scoops are fitted and the game is not over; _slot's depth (+3Dh) cleared; then ClassifyViewPosition.
void TransformShip(GameState& _state, SpaceObject& _slot);

/// RunBlueprintHandler (CS:3CDD): boxHalfWidth from _blueprint, the handler it names run (BuildBoxCornerVertices or
/// BuildDodoVertices), then what the handler returns into at RenderBlueprintBody (CS:3CF2): the vertex program, the projection
/// and the faces. Returns the direction flag as they leave it.
bool RunBlueprintHandler(GameState& _state, const Blueprint& _blueprint, bool _backward);

/// ClassifyStationPosition (CS:3C72): ClassifyViewPosition on _slot's compass position, +20h/+22h/+24h: visible, with byte 0 bit 7
/// set, when z is at least nearClipZ and twice |x| and twice |y| are at most z; the position stored at +10h/+12h/+14h and as
/// drawCenter once z passes.
void ClassifyStationPosition(GameState& _state, SpaceObject& _slot);

/// TransformToViewWithBlip (CS:3ED7): _position turned to the camera's frame (RotatePitchYawRoll), the high byte of its z into
/// _slot's +3Ch, _slot's scanner blip moved there (UpdateScannerBlip), then the frame turned to the view (TransformToView's tail).
/// Returns the position in the view.
Vector TransformToViewWithBlip(GameState& _state, SpaceObject& _slot, Vector _position);

/// TransformToView (CS:3EE3): _position turned to the camera's frame (RotatePitchYawRoll), then, off the front view, (x, z) turned
/// by -viewAngle through rotationSinCos[8], which it sets.
Vector TransformToView(GameState& _state, Vector _position);

/// TransformAndDrawObjects (CS:3D25): the player's pitch, yaw and roll into rotation pairs 0-2; every one of shipSlotCount slots
/// classified and transformed (ClassifyObject), from the slot each leaves DI on; then, from the farthest in, the visible objects
/// drawn (DrawFarthestObject): the sun or the planet, the station as a disc, a dot, or a blueprint. _backward is the direction flag;
/// returns it as the drawing leaves it.
bool TransformAndDrawObjects(GameState& _state, Hardware& _hardware, bool _backward);

/// DrawSunOrPlanet (CS:3F4F): the planet in _slot sized (SizePlanet), with its altitude, or the sun, with the cabin temperature, its
/// fringe, fuel scooping, death by heat and a supernova's heat; then, in front of the view, its disc. _backward is the direction
/// flag, which the disc and a supernova's energy bomb go by.
void DrawSunOrPlanet(GameState& _state, Hardware& _hardware, SpaceObject& _slot, bool _backward);

/// DrawDistantStation (CS:45C6): the station in _slot as a disc in colour 3 at its compass position, +20h/+22h/+24h, projected
/// (ProjectToScreen): radius 7 rows when its depth byte +25h is below 14h, smaller further off, and none from 21h to A5h.
void DrawDistantStation(GameState& _state, SpaceObject& _slot, bool _backward);

/// LoadPlayerAngles (CS:8A16): the player's pitch, yaw and roll into rotation pairs 0-2.
void LoadPlayerAngles(GameState& _state);

/// ProjectToScreen (CS:8D2E): _view on the screen, 80h + 256x/z and 40h + 256y/z, each divide saturated by the trap when it
/// overflows.
[[nodiscard]] ScreenPoint ProjectToScreen(Vector _view);

} // namespace Elite
