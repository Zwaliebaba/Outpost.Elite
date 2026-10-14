// GameLogic/SceneState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Scene.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct SceneState
{
  /// written by DrawVisibleFaces (0x3B38) from faceFillPatterns, read by DrawStackedSpansFromRow (0x1CDD): low byte on even rows, high b...
  std::uint16_t triangleFillPattern;
  /// 42 vertices of three words, x, y and z, in view space, 34-41 the box's corners BuildBoxCornerVertices writes; ProjectVertices
  /// projects the first projectedVertexCount in place into points of two words, x and y
  std::uint16_t vertexBuffer[126];
  /// read by ProjectVertices
  std::uint16_t projectedVertexCount;
  /// blueprint byte +2 (RunBlueprintHandler); 15 to 200; BuildDodoVertices ignores it
  std::uint16_t boxHalfWidth;
  /// object +0x0A for the blueprint handler (0x3E5A)
  std::uint16_t drawPitchAngle;
  /// object +0x0C
  std::uint16_t drawYawAngle;
  /// object +0x0E
  std::uint16_t drawRollAngle;
  /// view-space centre of the object being drawn; ReflectVertexAboutCenter and OffsetVertexByCenter
  std::uint16_t drawCenterX;
  std::uint16_t drawCenterY;
  std::uint16_t drawCenterZ;

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
