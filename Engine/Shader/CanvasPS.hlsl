// Engine/Shader/CanvasPS.hlsl
// One pixel of a 2D shape (ADR-034): its coverage from the shape's signed distance, smoothed over a screen pixel, or a glyph's
// from the atlas; its colour turned to linear light and premultiplied, for an sRGB target that blends in linear light.

#include "Canvas.hlsli"

Texture2D<float> g_atlas : register(t1);

static const uint BOX = 0;
static const uint BOX_EDGE = 1;
static const uint QUAD = 2;
static const uint LINE = 3;
static const uint ELLIPSE = 4;
static const uint RING = 5;
static const uint GLYPH = 6;
static const float OUTSIDE = 1.0e6;

float4 Unpack(uint _color)
{
  const float4 encoded = float4((_color >> 16) & 0xFF, (_color >> 8) & 0xFF, _color & 0xFF, (_color >> 24) & 0xFF) / 255.0;
  const float3 linearColor = encoded.rgb <= 0.04045 ? encoded.rgb / 12.92 : pow((encoded.rgb + 0.055) / 1.055, 2.4);
  return float4(linearColor, encoded.a);
}

float BoxDistance(float2 _p, float4 _box, float _radius)
{
  const float2 center = (_box.xy + _box.zw) / 2.0;
  const float2 halfSize = (_box.zw - _box.xy) / 2.0;
  const float2 q = abs(_p - center) - (halfSize - _radius);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - _radius;
}

// Inigo Quilez's approximation of the distance to an ellipse, near its edge.
float EllipseDistance(float2 _p, float2 _radii)
{
  const float k0 = length(_p / _radii);
  const float k1 = length(_p / (_radii * _radii));
  return k1 > 0.0 ? k0 * (k0 - 1.0) / k1 : -min(_radii.x, _radii.y);
}

// Whether _along, a distance along a dashed outline, falls in a gap.
bool InGap(float _along, float _dash, float _gap)
{
  return _dash > 0.0 && fmod(_along + 1.0e4 * (_dash + _gap), _dash + _gap) > _dash;
}

float4 main(CanvasVertexOut _input) : SV_Target
{
  const CanvasShape shape = g_shapes[_input.shape];
  const float2 p = _input.design;
  const float pixel = 1.0 / g_scale; // a screen pixel, in design units
  float4 color = Unpack(shape.color);
  float coverage = 0.0;
  if (shape.kind == GLYPH)
  {
    const float2 texel = lerp(shape.params0.xy, shape.params0.zw, (p - shape.bounds.xy) / (shape.bounds.zw - shape.bounds.xy));
    coverage = g_atlas.Load(int3(int2(texel), 0));
  }
  else
  {
    float distance = OUTSIDE;
    if (shape.kind == BOX)
    {
      distance = BoxDistance(p, shape.params0, shape.params1.x);
    }
    else if (shape.kind == BOX_EDGE)
    {
      distance = abs(BoxDistance(p, shape.params0, 0.0)) - shape.params1.x / 2.0;
    }
    else if (shape.kind == QUAD)
    {
      // Corners clockwise on screen: each edge's inward normal is its direction turned a quarter clockwise, (-dy, dx).
      const float2 corners[4] = {shape.params0.xy, shape.params0.zw, shape.params1.xy, shape.params1.zw};
      distance = -OUTSIDE;
      for (uint edge = 0; edge < 4; ++edge)
      {
        const float2 from = corners[edge];
        const float2 along = corners[(edge + 1) % 4] - from;
        const float2 inward = normalize(float2(-along.y, along.x));
        distance = max(distance, -dot(p - from, inward));
      }
      const float t = saturate((p.y - shape.bounds.y) / max(shape.bounds.w - shape.bounds.y, pixel));
      color = lerp(Unpack(shape.color), Unpack(shape.color2), t);
    }
    else if (shape.kind == LINE)
    {
      const float2 from = shape.params0.xy;
      const float2 span = shape.params0.zw - from;
      const float length2 = max(dot(span, span), 1.0e-6);
      const float h = saturate(dot(p - from, span) / length2);
      distance = length(p - from - span * h) - shape.params1.x / 2.0;
      if (InGap(h * sqrt(length2), shape.params1.y, shape.params1.z))
      {
        distance = OUTSIDE;
      }
    }
    else if (shape.kind == ELLIPSE || shape.kind == RING)
    {
      const float2 offset = p - shape.params0.xy;
      distance = EllipseDistance(offset, shape.params0.zw);
      if (shape.kind == RING)
      {
        distance = abs(distance) - shape.params1.x / 2.0;
        // Along the outline by its angle, at the radii's mean: near enough for a dash.
        const float angle = atan2(offset.y / shape.params0.w, offset.x / shape.params0.z);
        if (InGap(angle * (shape.params0.z + shape.params0.w) / 2.0, shape.params1.y, shape.params1.z))
        {
          distance = OUTSIDE;
        }
      }
    }
    coverage = saturate(0.5 - distance / pixel);
  }
  const float alpha = color.a * coverage;
  return float4(color.rgb * alpha, alpha);
}
