// Engine/Shader/SpherePS.hlsl
// A sphere of a scene (ADR-035), traced through each pixel of the inset: where the ray meets it, its surface lit by the scene's
// light, and its depth, so that what is nearer covers it and what is further is covered. A star glows, white at its middle and its
// colour at its rim; terrain is sea under its level and land above, under cloud; bands run across its latitudes. The surface is
// fixed in the scene's frame, and the noise that lays it out is the seed's. Where the ray misses, nothing is drawn.

#include "Sphere.hlsli"

struct SphereOut
{
  float4 color : SV_Target;
  float depth : SV_Depth;
};

static const float STAR_CORE = 6.0;  // a star's light at its middle, which blooms
static const float STAR_RIM = 2.0;   // and at its rim
static const float AMBIENT = 0.04;   // the light on the night side
static const float3 CLOUD = float3(0.92, 0.93, 0.95);
static const float CLOUD_OPACITY = 0.75;
static const float NOISE_MIDDLE = 0.49;
static const float NOISE_SPREAD = 0.26;

// The noise's value below which a share _share of the sphere lies: Fractal's values on a sphere spread about their middle,
// NOISE_MIDDLE, with NOISE_SPREAD the slope of their share near it (ADR-035 measures both).
float NoiseBelow(float _share)
{
  return NOISE_MIDDLE + NOISE_SPREAD * (_share - 0.5);
}

float3 Terrain(float3 _at)
{
  const float sea = NoiseBelow(g_shape.x);
  const float cloudFloor = NoiseBelow(1.0 - g_shape.y);
  const float height = Fractal(_at * 1.8, g_seed, 6);
  float3 color;
  if (height < sea)
  {
    const float depth = saturate((sea - height) * 12.0);
    color = lerp(g_colors[1].rgb, g_colors[0].rgb, depth);
  }
  else
  {
    const float rise = saturate((height - sea) * 6.0);
    const float detail = Fractal(_at * 9.0, g_seed + 101u, 3);
    color = lerp(g_colors[2].rgb, g_colors[3].rgb, saturate(rise + (detail - 0.5) * 0.3));
  }
  // Cloud where its noise rises past the floor its cover sets, the noise itself warped so that it streams, and thinned by a finer
  // noise so that it is wisps rather than blots.
  const float3 warp = float3(Fractal(_at * 1.3, g_seed + 223u, 3), Fractal(_at * 1.3, g_seed + 227u, 3), 0.0) - 0.5;
  const float clouds = Fractal(_at * float3(2.6, 5.2, 2.6) + warp * 1.5 + float3(5.0, 9.0, 13.0), g_seed + 211u, 5);
  const float wisps = 0.45 + 0.55 * Fractal(_at * 11.0, g_seed + 229u, 3);
  const float cloud = smoothstep(cloudFloor, cloudFloor + 0.12, clouds) * wisps * step(0.001, g_shape.y);
  return lerp(color, CLOUD, cloud * CLOUD_OPACITY);
}

float3 Bands(float3 _at)
{
  const float turbulence = g_shape.w;
  const float stir = (Fractal(_at * 3.0, g_seed, 5) - 0.5) * turbulence * 0.5;
  const float across = (_at.y + stir) * g_shape.z;
  const float band = floor(across);
  const float within = across - band;
  const uint first = uint(band + 64.0) % 4u;
  const float3 color = lerp(g_colors[first].rgb, g_colors[(first + 1u) % 4u].rgb, smoothstep(0.35, 0.65, within));
  const float detail = Fractal(_at * float3(2.0, 24.0, 2.0), g_seed + 307u, 3);
  return color * (0.85 + 0.3 * detail);
}

SphereOut main(float4 _position : SV_Position, float2 _uv : TEXCOORD0)
{
  const float3 ray = RayOf(_uv);
  const float hit = FirstHit(ray);
  if (hit <= 0.0)
  {
    discard;
  }
  const float3 at = ray * hit;
  const float3 normal = normalize(at - g_center);
  const float3 toEye = normalize(-ray);
  const float facing = saturate(dot(normal, toEye));

  float3 color;
  if (g_surface == SURFACE_STAR)
  {
    const float3 surface = lerp(g_colors[0].rgb, float3(1.0, 1.0, 1.0), facing * facing);
    const float granules = Fractal(Rotate(g_toScene, normal) * 12.0, g_seed, 3);
    color = surface * lerp(STAR_RIM, STAR_CORE, facing) * (0.9 + 0.2 * granules);
  }
  else
  {
    const float3 inScene = Rotate(g_toScene, normal);
    const float3 surface = g_surface == SURFACE_TERRAIN ? Terrain(inScene) : Bands(inScene);
    const float lit = dot(normal, g_light);
    const float daylight = AMBIENT + (1.0 - AMBIENT) * saturate(lit);
    const float rim = pow(1.0 - facing, 3.0) * g_atmosphere.w * smoothstep(-0.2, 0.3, lit);
    color = surface * daylight + g_atmosphere.rgb * rim;
  }

  SphereOut output;
  output.color = float4(color, 1.0);
  output.depth = saturate(g_nearUnits / (at.z * g_radius));
  return output;
}
