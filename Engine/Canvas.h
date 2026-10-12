// Engine/Canvas.h
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{

/// The 2D layer's units (D48): a screen of 1920 x 1080 design units, scaled by min(W / 1920, H / 1080) and centred.
inline constexpr float DESIGN_WIDTH = 1920.0f;
inline constexpr float DESIGN_HEIGHT = 1080.0f;

/// A point in design units, x right and y down.
struct Point
{
  float x = 0.0f;
  float y = 0.0f;
};

/// A rectangle in design units: its top-left corner and its size.
struct Rect
{
  float left = 0.0f;
  float top = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

/// The faces text is drawn in (D39, D55): Bahnschrift SemiCondensed for labels and Bahnschrift for numbers.
enum class Font : std::uint8_t
{
  Label,
  Number
};

/// Where a line of text stands against its anchor across: its left, its middle or its right. Down, the anchor is its middle.
enum class Align : std::uint8_t
{
  Left,
  Center,
  Right
};

/// What a shape is, as CanvasPS draws it, and what its parameters hold.
enum class ShapeKind : std::uint32_t
{
  Box,     ///< params[0-3] its left, top, right and bottom; params[4] its corners' radius
  BoxEdge, ///< a box's outline: params[0-3] the box, params[4] the outline's width, centred on its edge
  Quad,    ///< a convex quadrilateral: params[0-7] its corners, clockwise; color at its top shading to color2 at its bottom
  Line,    ///< params[0-1] to params[2-3], params[4] wide, dashed params[5] on and params[6] off when params[5] is not 0
  Ellipse, ///< filled: params[0-1] its centre, params[2-3] its radii
  Ring,    ///< an ellipse's outline: as Ellipse, params[4] wide, dashed by params[5] and params[6] along it as a line is
  Glyph    ///< a glyph of the atlas covering its bounds: params[0-3] its texels' left, top, right and bottom
};

/// One shape of the 2D layer, as the GPU reads it: the rectangle it covers, in design units, its parameters, and its colours,
/// 0xAARRGGBB with the colour display-encoded (sRGB), as a design gives it.
struct CanvasShape
{
  std::array<float, 4> bounds{}; ///< left, top, right and bottom of what it may cover, before CanvasVS adds a pixel for its edge
  std::array<float, 8> params{};
  ShapeKind kind = ShapeKind::Box;
  std::uint32_t color = 0;
  std::uint32_t color2 = 0;
  std::uint32_t padding = 0;
};

/// A line of text to draw: the renderer lays it out with DirectWrite at the screen's own pixels and turns it into glyphs.
struct CanvasText
{
  std::wstring text;
  Font font = Font::Label;
  float size = 16.0f;   ///< the em, in design units
  float spacing = 0.0f; ///< added after each character, in design units
  Point anchor;
  Align align = Align::Left;
  std::uint32_t color = 0xFFFFFFFF;
};

/// The 2D layer of one frame (ADR-034, D47, D48): shapes and text in design units, drawn in order over what is under them, with
/// edges smoothed over a screen pixel. The renderer draws it through an sRGB view of the back buffer, so colours blend in
/// linear light.
class Canvas
{
public:
  /// Empty again, for the next frame.
  void Clear() noexcept;

  void Box(const Rect& _rect, std::uint32_t _color, float _cornerRadius = 0.0f);
  void BoxEdge(const Rect& _rect, float _width, std::uint32_t _color);
  /// A convex quadrilateral, _corners clockwise on screen, shaded from _top at its highest point to _bottom at its lowest.
  void Quad(const std::array<Point, 4>& _corners, std::uint32_t _top, std::uint32_t _bottom);
  void Line(Point _from, Point _to, float _width, std::uint32_t _color, float _dash = 0.0f, float _gap = 0.0f);
  void Ellipse(Point _center, float _radiusX, float _radiusY, std::uint32_t _color);
  void Ring(Point _center, float _radiusX, float _radiusY, float _width, std::uint32_t _color, float _dash = 0.0f, float _gap = 0.0f);
  void Text(std::wstring_view _text, Font _font, float _size, float _spacing, Point _anchor, Align _align, std::uint32_t _color);

  /// What the canvas holds, in the order it was added. Text is drawn after the shapes added before it and before those after.
  struct Item
  {
    bool text = false;
    std::size_t index = 0; ///< into Shapes() or Texts()
  };

  [[nodiscard]] const std::vector<CanvasShape>& Shapes() const noexcept
  {
    return m_shapes;
  }

  [[nodiscard]] const std::vector<CanvasText>& Texts() const noexcept
  {
    return m_texts;
  }

  [[nodiscard]] const std::vector<Item>& Items() const noexcept
  {
    return m_items;
  }

private:
  void Add(const CanvasShape& _shape);

  std::vector<CanvasShape> m_shapes;
  std::vector<CanvasText> m_texts;
  std::vector<Item> m_items;
};

} // namespace Engine
