// Engine/Shader/StrokePS.hlsl
// A stroke's pixel (ADR-035): its coverage, from its distance to the capsule's edge smoothed over one pixel, the capsule as wide
// along its length as its two ends taper; and its colour times its intensity, premultiplied by its coverage.

#include "Stroke.hlsli"

float4 main(StrokeVertex _input) : SV_Target
{
  const float2 from = TargetOf(g_from);
  const float2 to = TargetOf(g_to);
  const float2 span = to - from;
  const float lengthSquared = max(dot(span, span), 1e-6);
  const float t = saturate(dot(_input.at - from, span) / lengthSquared);
  const float distance = length(_input.at - (from + span * t));
  const float halfWidth = 0.5 * TargetWidth(lerp(g_fromWidth, g_toWidth, t));
  const float coverage = saturate(halfWidth - distance + 0.5);
  if (coverage <= 0.0)
  {
    discard;
  }
  return float4(g_color.rgb * g_intensity * coverage, coverage);
}
