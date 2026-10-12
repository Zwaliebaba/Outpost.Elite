// GameLogic/ShipModel.h
#pragma once

#include "FlightSnapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Elite
{

/// A point of a ship model, in the model's own frame and the game's axes: x right, y down and z ahead, in the slot's units.
struct ModelVertex
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

/// A filled triangle of a ship model: its three vertices, and the face fill pattern it is filled with, faceFillPatterns' index.
struct ModelTriangle
{
  std::array<std::uint16_t, 3> vertices{};
  std::uint8_t pattern = 0;
  std::uint16_t face = 0; ///< the face it belongs to, ShipModel::faces' index
};

/// A line drawn over a ship model's face: its two vertices and its colour, 0-3.
struct ModelEdge
{
  std::array<std::uint16_t, 2> vertices{};
  std::uint8_t color = 0;
  std::uint16_t face = 0; ///< the face it belongs to
};

/// A face of a ship model: the triangle whose winding decides whether the game draws it (TriangleWindingSign), and that
/// triangle's unit normal, (t0 - t1) x (t2 - t1) in the game's axes, which points out of the model: the game draws the face, and
/// every triangle and edge of it, exactly when the normal points towards the viewer, its dot product with any vertex of the face
/// turned and moved to the view negative.
struct ModelFace
{
  std::array<std::uint16_t, 3> test{};
  ModelVertex normal;
};

/// A ship type's model (ADR-033): the vertices its blueprint's handler and vertex program build, with every angle 0 and its centre
/// at the origin, and its faces as DrawVisibleFaces walks them. A face's triangles need not lie in its test triangle's plane,
/// so it is the face, by its normal, that is seen or not, not each triangle. Turned by ModelRotation and then the player's rotation and moved to the object's view
/// position, its vertices are where the game puts them (ShipModelTests).
struct ShipModel
{
  std::vector<ModelVertex> vertices;
  std::vector<ModelFace> faces;
  std::vector<ModelTriangle> triangles;
  std::vector<ModelEdge> edges;
  bool rollReversed = false; ///< the Dodo's: its handler builds its rings at the roll's phase, turning it by the roll, not its negation
};

/// The ship types that have a blueprint: blueprintTable's entries, 0-29. The sun and the planet, 30 and 31, have none.
inline constexpr std::size_t SHIP_MODELS = 30;

/// The model of ship type _kind, below SHIP_MODELS.
[[nodiscard]] ShipModel ShipModelOf(std::uint8_t _kind);

/// The rotation _model's object's angles make of it: ObjectRotation, its roll negated first when the model's roll is reversed.
[[nodiscard]] Orientation ModelRotation(const ShipModel& _model, std::uint16_t _pitch, std::uint16_t _yaw, std::uint16_t _roll) noexcept;

/// What fraction of a fill pattern's pixels is each of the four CGA colours: a solid colour is 1 of one, a checkerboard half of two.
[[nodiscard]] std::array<float, 4> PatternMix(std::uint8_t _pattern);

} // namespace Elite
