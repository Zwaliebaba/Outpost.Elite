// GameLogic/SceneState.h
#pragma once

#include "Field.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Scene.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct SceneState
{
  SceneState() noexcept = default;
  explicit SceneState(const SceneState&) noexcept = default;

  /// written by DrawVisibleFaces (0x3B38) from faceFillPatterns, read by DrawStackedSpansFromRow (0x1CDD): low byte on even rows, high b...
  Field<std::uint16_t> triangleFillPattern;
  /// vertexBuffer's fields, DS:7230-732B in 1987, at their places there.
  struct VertexBuffer
  {
    VertexBuffer() noexcept = default;
    explicit VertexBuffer(const VertexBuffer&) noexcept = default;

    Field<std::uint8_t> gap7230[204];
    /// vertices 34-41 inside vertexBuffer, written by BuildBoxCornerVertices
    Field<std::uint8_t> boxCornerVertices[48];

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("gap7230", _self.gap7230);
      _visitor("boxCornerVertices", _self.boxCornerVertices);
    }

    template <typename Visitor> void Visit(Visitor& _visitor)
    {
      VisitMembers(*this, _visitor);
    }

    template <typename Visitor> void Visit(Visitor& _visitor) const
    {
      VisitMembers(*this, _visitor);
    }
  };
  /// 6-byte x,y,z words; model vertices 0-41 in view space; ProjectVertices projects the first projectedVertexCount in place into 4-byte...
  VertexBuffer vertexBuffer;
  /// read by ProjectVertices
  Field<std::uint16_t> projectedVertexCount;
  /// blueprint byte +2 (RunBlueprintHandler); 15 to 200; BuildDodoVertices ignores it
  Field<std::uint16_t> boxHalfWidth;
  /// object +0x0A for the blueprint handler (0x3E5A)
  Field<std::uint16_t> drawPitchAngle;
  /// object +0x0C
  Field<std::uint16_t> drawYawAngle;
  /// object +0x0E
  Field<std::uint16_t> drawRollAngle;
  /// view-space centre of the object being drawn; ReflectVertexAboutCenter and OffsetVertexByCenter
  Field<std::uint16_t> drawCenterX;
  Field<std::uint16_t> drawCenterY;
  Field<std::uint16_t> drawCenterZ;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("triangleFillPattern", _self.triangleFillPattern);
    _visitor("vertexBuffer", _self.vertexBuffer);
    _visitor("projectedVertexCount", _self.projectedVertexCount);
    _visitor("boxHalfWidth", _self.boxHalfWidth);
    _visitor("drawPitchAngle", _self.drawPitchAngle);
    _visitor("drawYawAngle", _self.drawYawAngle);
    _visitor("drawRollAngle", _self.drawRollAngle);
    _visitor("drawCenterX", _self.drawCenterX);
    _visitor("drawCenterY", _self.drawCenterY);
    _visitor("drawCenterZ", _self.drawCenterZ);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }
};

static_assert(std::is_trivially_copyable_v<SceneState> && std::is_standard_layout_v<SceneState>);

} // namespace Elite
