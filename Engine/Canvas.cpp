#include "pch.h"

#include "Canvas.h"

#include <algorithm>

namespace Engine
{

namespace
{

// A shape of _kind in _color covering _left to _right and _top to _bottom, grown by _margin on every side.
[[nodiscard]] CanvasShape ShapeOver(ShapeKind _kind, std::uint32_t _color, float _left, float _top, float _right, float _bottom,
                                    float _margin) noexcept
{
  CanvasShape shape;
  shape.bounds = {_left - _margin, _top - _margin, _right + _margin, _bottom + _margin};
  shape.kind = _kind;
  shape.color = _color;
  return shape;
}

} // namespace

void Canvas::Clear() noexcept
{
  m_shapes.clear();
  m_texts.clear();
  m_items.clear();
}

void Canvas::Add(const CanvasShape& _shape)
{
  m_items.push_back(Item{.text = false, .index = m_shapes.size()});
  m_shapes.push_back(_shape);
}

void Canvas::Box(const Rect& _rect, std::uint32_t _color, float _cornerRadius)
{
  const float right = _rect.left + _rect.width;
  const float bottom = _rect.top + _rect.height;
  CanvasShape shape = ShapeOver(ShapeKind::Box, _color, _rect.left, _rect.top, right, bottom, 0.0f);
  shape.params = {_rect.left, _rect.top, right, bottom, _cornerRadius, 0.0f, 0.0f, 0.0f};
  Add(shape);
}

void Canvas::BoxEdge(const Rect& _rect, float _width, std::uint32_t _color)
{
  const float right = _rect.left + _rect.width;
  const float bottom = _rect.top + _rect.height;
  CanvasShape shape = ShapeOver(ShapeKind::BoxEdge, _color, _rect.left, _rect.top, right, bottom, _width / 2.0f);
  shape.params = {_rect.left, _rect.top, right, bottom, _width, 0.0f, 0.0f, 0.0f};
  Add(shape);
}

void Canvas::Quad(const std::array<Point, 4>& _corners, std::uint32_t _top, std::uint32_t _bottom)
{
  float left = _corners[0].x;
  float top = _corners[0].y;
  float right = left;
  float bottom = top;
  for (const Point& corner : _corners)
  {
    left = std::min(left, corner.x);
    right = std::max(right, corner.x);
    top = std::min(top, corner.y);
    bottom = std::max(bottom, corner.y);
  }
  CanvasShape shape = ShapeOver(ShapeKind::Quad, _top, left, top, right, bottom, 0.0f);
  for (std::size_t corner = 0; corner < _corners.size(); ++corner)
  {
    shape.params[2 * corner] = _corners[corner].x;
    shape.params[2 * corner + 1] = _corners[corner].y;
  }
  shape.color2 = _bottom;
  Add(shape);
}

void Canvas::Line(Point _from, Point _to, float _width, std::uint32_t _color, float _dash, float _gap)
{
  CanvasShape shape = ShapeOver(ShapeKind::Line, _color, std::min(_from.x, _to.x), std::min(_from.y, _to.y), std::max(_from.x, _to.x),
                                std::max(_from.y, _to.y), _width / 2.0f);
  shape.params = {_from.x, _from.y, _to.x, _to.y, _width, _dash, _gap, 0.0f};
  Add(shape);
}

void Canvas::Ellipse(Point _center, float _radiusX, float _radiusY, std::uint32_t _color)
{
  CanvasShape shape =
    ShapeOver(ShapeKind::Ellipse, _color, _center.x - _radiusX, _center.y - _radiusY, _center.x + _radiusX, _center.y + _radiusY, 0.0f);
  shape.params = {_center.x, _center.y, _radiusX, _radiusY, 0.0f, 0.0f, 0.0f, 0.0f};
  Add(shape);
}

void Canvas::Ring(Point _center, float _radiusX, float _radiusY, float _width, std::uint32_t _color, float _dash, float _gap)
{
  CanvasShape shape = ShapeOver(ShapeKind::Ring, _color, _center.x - _radiusX, _center.y - _radiusY, _center.x + _radiusX,
                                _center.y + _radiusY, _width / 2.0f);
  shape.params = {_center.x, _center.y, _radiusX, _radiusY, _width, _dash, _gap, 0.0f};
  Add(shape);
}

void Canvas::Text(std::wstring_view _text, Font _font, float _size, float _spacing, Point _anchor, Align _align, std::uint32_t _color)
{
  m_items.push_back(Item{.text = true, .index = m_texts.size()});
  m_texts.push_back(CanvasText{
    .text = std::wstring(_text), .font = _font, .size = _size, .spacing = _spacing, .anchor = _anchor, .align = _align, .color = _color});
}

} // namespace Engine
