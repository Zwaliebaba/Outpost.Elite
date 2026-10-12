// Engine/Shader/CanvasVS.hlsl
// One shape of the 2D layer (ADR-034): two triangles over its bounds, grown by a screen pixel so that its smoothed edge is
// covered, placed by the design units' scale and origin (D48). A glyph's bounds are its texels', not grown.

#include "Canvas.hlsli"

static const uint GLYPH = 6;
static const uint CORNERS[6] = {0, 1, 2, 2, 1, 3}; // two triangles: x from bit 0, y from bit 1

CanvasVertexOut main(uint _vertex : SV_VertexID, uint _instance : SV_InstanceID)
{
  const CanvasShape shape = g_shapes[_instance];
  const float grow = shape.kind == GLYPH ? 0.0 : 1.0 / g_scale;
  const uint corner = CORNERS[_vertex];
  const float2 at = float2((corner & 1) != 0 ? shape.bounds.z + grow : shape.bounds.x - grow,
                           (corner & 2) != 0 ? shape.bounds.w + grow : shape.bounds.y - grow);
  const float2 pixel = g_origin + at * g_scale;
  CanvasVertexOut output;
  output.position = float4(pixel.x / g_targetPixels.x * 2.0 - 1.0, 1.0 - pixel.y / g_targetPixels.y * 2.0, 0.0, 1.0);
  output.design = at;
  output.shape = _instance;
  return output;
}
