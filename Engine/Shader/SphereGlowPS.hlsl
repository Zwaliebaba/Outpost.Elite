// Engine/Shader/SphereGlowPS.hlsl
// The light round a sphere (ADR-035), added over the scene beside it: a star's corona, which falls off from its rim over a few
// of its own radii, and a planet's haze in its atmosphere's colour just past its rim, on its lit side. It stands at the sphere's
// depth, so that nearer things cover it; over the sphere's own face it adds nothing, which SpherePS draws.

#include "Sphere.hlsli"

struct GlowOut
{
  float4 color : SV_Target;
  float depth : SV_Depth;
};

static const float CORONA_NEAR = 1.6; // the corona's bright inner part at the rim, which blooms
static const float CORONA_FAR = 0.25; // and its faint outer part
static const float HAZE_FALLOFF = 30.0; // per radius past the rim

GlowOut main(float4 _position : SV_Position, float2 _uv : TEXCOORD0)
{
  const float3 ray = normalize(RayOf(_uv));
  const float along = dot(ray, g_center);
  if (along <= 0.0)
  {
    discard;
  }
  const float distance = length(g_center);
  const float closest = sqrt(max(dot(g_center, g_center) - along * along, 0.0));
  const float past = closest - 1.0; // radii past the rim
  if (past <= 0.0)
  {
    discard;
  }
  // The scene's pixels the sphere's radius spans, and how far past its rim this one is.
  const float radiusPixels = g_focal.y / (distance * g_pixelUnits);
  const float pastPixels = past * radiusPixels;
  float3 color;
  if (g_surface == SURFACE_STAR)
  {
    color = g_colors[0].rgb * (CORONA_NEAR * exp(-pastPixels / (1.5 + 0.3 * radiusPixels)) +
                               CORONA_FAR * exp(-pastPixels / (4.0 + 0.6 * radiusPixels)));
  }
  else
  {
    const float3 limb = normalize(ray * along - g_center);
    const float lit = smoothstep(-0.2, 0.4, dot(limb, g_light));
    color = g_atmosphere.rgb * g_atmosphere.w * exp(-past * HAZE_FALLOFF) * lit;
  }
  GlowOut output;
  output.color = float4(color, 0.0);
  output.depth = saturate(g_nearUnits / (ray.z * along * g_radius));
  return output;
}
