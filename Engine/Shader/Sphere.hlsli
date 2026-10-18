// Engine/Shader/Sphere.hlsli
// What SpherePS and SphereGlowPS share (ADR-035): the root constants Renderer sets for a sphere, and the ray through a pixel of
// the inset that meets it. Both draw over the whole inset, FullScreenVS's triangle, and find the sphere by the ray.

#include "Noise.hlsli"

cbuffer SphereFrame : register(b0)
{
  float4 g_inset;     // the inset's width and height, and where the camera's axis meets it, x and y, in its own units
  float2 g_focal;     // its units per unit of x / z and of y / z
  float g_nearUnits;  // reversed depth: g_nearUnits / z, 1 at the near plane
  float g_pixelUnits; // the inset's units across a pixel of the scene's target
};

cbuffer Sphere : register(b1)
{
  float3 g_center;     // in the camera's frame, x right, y down and z ahead, in the sphere's radii
  float g_radius;      // in the scene's units
  float4 g_toScene;    // a unit quaternion: the camera's frame into the scene's, in which the surface is fixed
  float3 g_light;      // towards the light, in the camera's frame
  uint g_surface;      // 0 a star, 1 terrain, 2 bands
  float4 g_colors[4];  // linear
  float4 g_atmosphere; // linear, and its strength in w
  float4 g_shape;      // the sea's level, the cloud's cover, the bands, the turbulence
  uint g_seed;
  float3 g_spherePadding;
};

static const uint SURFACE_STAR = 0;
static const uint SURFACE_TERRAIN = 1;
static const uint SURFACE_BANDS = 2;

// The ray through the inset at _uv, 0 to 1 across it: its direction in the camera's frame, z 1.
float3 RayOf(float2 _uv)
{
  const float2 at = _uv * g_inset.xy;
  return float3((at.x - g_inset.z) / g_focal.x, (at.y - g_inset.w) / g_focal.y, 1.0);
}

// Where _ray first meets the sphere, as a multiple of it, or a negative number where it does not meet it in front of the camera.
float FirstHit(float3 _ray)
{
  const float a = dot(_ray, _ray);
  const float b = dot(_ray, g_center);
  const float c = dot(g_center, g_center) - 1.0;
  const float discriminant = b * b - a * c;
  // One return, which FXC can see is always reached with its value set.
  float hit = -1.0;
  if (discriminant >= 0.0)
  {
    hit = (b - sqrt(discriminant)) / a;
  }
  return hit;
}
