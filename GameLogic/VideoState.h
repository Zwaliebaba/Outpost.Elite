// GameLogic/VideoState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Video.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the
/// original's data segment's, by their names, in their order there; the GameState holds it in GameData.
struct VideoState
{
  /// colorFillBytes[drawColor], set by DrawDisc for FillSpan
  std::uint8_t discFillByte;
  /// DrawDisc's centre x
  std::uint16_t discCenterX;
  /// DrawDisc's radius/2: the rows above and below the centre
  std::uint8_t discRadiusRows;
  /// DrawDisc's row counter
  std::uint8_t discRowsLeft;
  /// fractional step through circleProfile per row (0x20000/rows)
  std::uint16_t discProfileStepFraction;
  /// whole-byte step through circleProfile per row
  std::uint16_t discProfileStepWhole;
  /// Fibonacci generator state with fringeRandomB: (a,b) becomes (b,a+b)
  std::uint16_t fringeRandomA;
  /// its high byte AND sunFringeMask is added to each half-width
  std::uint16_t fringeRandomB;
  /// A point of DrawCircle's chords.
  struct CirclePoint
  {
    std::uint16_t x;
    std::uint16_t row;

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("x", _self.x);
      _visitor("row", _self.row);
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
  /// DrawCircle's polygon
  CirclePoint circlePoints[32];
  /// FillTriangle: apex x (flat bottom), B.x (flat top) or top x (general)
  std::uint8_t triangleEdgeStartX;
  /// FillTriangle: apex x, C.x, or top x
  std::uint8_t triangleEdgeStartX2;
  /// the lowest row, where DrawStackedSpans starts; FillClippedOneRow writes the row byte too
  std::uint8_t triangleBottomRow;
  /// rows from top to the lower vertex (flat) or to the middle vertex (general)
  std::uint8_t triangleUpperRows;
  /// general triangle: rows from top to bottom
  std::uint8_t triangleTotalRows;
  /// general triangle: middle vertex x
  std::uint8_t triangleMiddleX;
  /// general triangle: bottom vertex x
  std::uint8_t triangleBottomX;
  /// FillClippedTriangle: first edge start x (apex, B.x or top)
  std::uint16_t clippedEdgeStartX;
  /// FillClippedTriangle: second edge start x (apex, C.x or top)
  std::uint16_t clippedEdgeStartX2;
  /// rows to the lower (flat) or middle (general) vertex
  std::uint16_t clippedUpperRows;
  /// general: rows top to bottom
  std::uint16_t clippedTotalRows;
  /// first drawn row; 0x8000 = none yet
  std::uint16_t clippedFirstRow;
  /// 16.16 slope of edge A or the long edge, fraction word (second div)
  std::uint16_t clippedSlopeAFraction;
  /// its integer word (first div)
  std::uint16_t clippedSlopeAWhole;
  /// 16.16 slope of edge B or the short edge, fraction word
  std::uint16_t clippedSlopeBFraction;
  /// its integer word
  std::uint16_t clippedSlopeBWhole;
  /// general: middle vertex x
  std::uint16_t clippedMiddleX;
  /// general: bottom vertex x
  std::uint16_t clippedBottomX;
  /// general: rows drawn, the span count for DrawStackedSpansFromRow
  std::uint8_t clippedRowCount;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("discFillByte", _self.discFillByte);
    _visitor("discCenterX", _self.discCenterX);
    _visitor("discRadiusRows", _self.discRadiusRows);
    _visitor("discRowsLeft", _self.discRowsLeft);
    _visitor("discProfileStepFraction", _self.discProfileStepFraction);
    _visitor("discProfileStepWhole", _self.discProfileStepWhole);
    _visitor("fringeRandomA", _self.fringeRandomA);
    _visitor("fringeRandomB", _self.fringeRandomB);
    _visitor("circlePoints", _self.circlePoints);
    _visitor("triangleEdgeStartX", _self.triangleEdgeStartX);
    _visitor("triangleEdgeStartX2", _self.triangleEdgeStartX2);
    _visitor("triangleBottomRow", _self.triangleBottomRow);
    _visitor("triangleUpperRows", _self.triangleUpperRows);
    _visitor("triangleTotalRows", _self.triangleTotalRows);
    _visitor("triangleMiddleX", _self.triangleMiddleX);
    _visitor("triangleBottomX", _self.triangleBottomX);
    _visitor("clippedEdgeStartX", _self.clippedEdgeStartX);
    _visitor("clippedEdgeStartX2", _self.clippedEdgeStartX2);
    _visitor("clippedUpperRows", _self.clippedUpperRows);
    _visitor("clippedTotalRows", _self.clippedTotalRows);
    _visitor("clippedFirstRow", _self.clippedFirstRow);
    _visitor("clippedSlopeAFraction", _self.clippedSlopeAFraction);
    _visitor("clippedSlopeAWhole", _self.clippedSlopeAWhole);
    _visitor("clippedSlopeBFraction", _self.clippedSlopeBFraction);
    _visitor("clippedSlopeBWhole", _self.clippedSlopeBWhole);
    _visitor("clippedMiddleX", _self.clippedMiddleX);
    _visitor("clippedBottomX", _self.clippedBottomX);
    _visitor("clippedRowCount", _self.clippedRowCount);
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

static_assert(std::is_trivially_copyable_v<VideoState> && std::is_standard_layout_v<VideoState>);

} // namespace Elite
