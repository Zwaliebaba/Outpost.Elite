// Engine/Shader/StrokeVS.hlsl
// A stroke's rectangle (ADR-035): two triangles round its capsule, along it and across it, a pixel wider each way than its widest
// end so that its smoothed edge fits. A stroke of no length, a dot, gets a square.

#include "Stroke.hlsli"

StrokeVertex main(uint _vertex : SV_VertexID)
{
  const float2 from = TargetOf(g_from);
  const float2 to = TargetOf(g_to);
  const float2 span = to - from;
  const float length = sqrt(dot(span, span));
  const float2 along = length > 1e-3 ? span / length : float2(1.0, 0.0); // a dot has no way along: any will do
  const float2 across = float2(-along.y, along.x);
  const float reach = 0.5 * TargetWidth(max(g_fromWidth, g_toWidth)) + 1.0;
  // Corners 0-3 of the rectangle, two triangles of them: 0 1 2 and 2 1 3.
  static const uint CORNERS[6] = {0u, 1u, 2u, 2u, 1u, 3u};
  const uint corner = CORNERS[_vertex];
  const float2 end = (corner & 1u) != 0u ? to + along * reach : from - along * reach;
  const float2 at = end + across * ((corner & 2u) != 0u ? reach : -reach);

  StrokeVertex output;
  output.position = float4(at.x / g_targetPixels.x * 2.0 - 1.0, 1.0 - at.y / g_targetPixels.y * 2.0, 0.0, 1.0);
  output.at = at;
  return output;
}
