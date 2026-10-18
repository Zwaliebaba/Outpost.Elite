// Engine/Shader/CompositePS.hlsl
// The scene's inset (ADR-033, ADR-035): the over layer's pixel where it holds one; else the resolved scene over the under layer by
// the scene's coverage, in linear light, the under layer's pixel where it holds one or else the sky, with the scene's bloom added
// over all of it. The layers lie in a rectangle of the inset (D69), all of it by default, each layer pixel its share of it,
// nearest; beside them only the scene and the sky. The scene is the inset's size on screen, a pixel each. FullScreenVS covers the
// inset.
//
// The sky is fixed in the scene's frame: a star in a cell of a cube round the camera where the seed's hash says, a dot a pixel or
// two across, and a nebula of two colours where the seed's noise rises.

#include "Noise.hlsli"

Texture2D<float4> g_scene : register(t0);
Texture2D<uint2> g_layers : register(t1); // x under, y over: a palette index, or CLEAR
Texture2D<float4> g_bloom : register(t2);
SamplerState g_linear : register(s0);

cbuffer Composite : register(b0)
{
  float4 g_palette[4]; // as the picture's pixels, display-encoded
  float2 g_layerPixels;
  float2 g_scenePixels;
  float4 g_inset;       // its width and height, and where the camera's axis meets it, x and y, in its own units
  float2 g_focal;       // its units per unit of x / z and of y / z
  float g_bloomShare;   // how much of the bloom is added
  float g_skyShown;     // 1 to draw the sky, 0 for none
  float4 g_toScene;     // a unit quaternion: the camera's frame into the scene's
  float4 g_nebulaNear;  // linear, and the nebula's density in w
  float4 g_nebulaFar;   // linear, and the stars' density in w
  float g_pixelRadians; // the angle a pixel of the scene spans at the inset's middle
  uint g_skySeed;
  float2 g_compositePadding;
  float4 g_layersPlace; // where the layers lie in the inset: left, top, width and height, each a share of it (D69)
};

static const uint CLEAR = 0xFF;
static const float STAR_CELLS = 96.0;      // cells across each face of the sky's cube
static const float STAR_LIGHT = 1.4;       // the brightest star's light, which just blooms
static const float NEBULA_LIGHT = 0.18;    // the nebula's light where it is thickest

float3 ToLinear(float3 _color)
{
  return _color <= 0.04045 ? _color / 12.92 : pow((_color + 0.055) / 1.055, 2.4);
}

float3 ToDisplay(float3 _color)
{
  const float3 color = saturate(_color);
  return color <= 0.0031308 ? color * 12.92 : 1.055 * pow(color, 1.0 / 2.4) - 0.055;
}

// The sky in _direction, a unit vector of the scene's frame.
float3 Sky(float3 _direction)
{
  // The cube's face the direction meets, and where on it, -1 to 1 each way.
  const float3 magnitude = abs(_direction);
  uint face = 0u;
  float2 onFace = _direction.yz / magnitude.x;
  if (magnitude.y >= magnitude.x && magnitude.y >= magnitude.z)
  {
    face = 2u;
    onFace = _direction.xz / magnitude.y;
  }
  else if (magnitude.z >= magnitude.x)
  {
    face = 4u;
    onFace = _direction.xy / magnitude.z;
  }
  face += _direction[face / 2u] < 0.0 ? 1u : 0u;
  const float2 grid = (onFace * 0.5 + 0.5) * STAR_CELLS;
  const int2 cell = int2(floor(grid));
  const uint hash = HashCell(int3(cell, int(face)), g_skySeed);
  float3 sky = float3(0.0, 0.0, 0.0);
  if (Unit(hash) < g_nebulaFar.w)
  {
    // The star stands within its cell, clear of its edges; its size is a pixel or two, whatever the cell's.
    const float2 place = float2(cell) + 0.2 + 0.6 * float2(Unit(Hash(hash ^ 0x9E3779B9u)), Unit(Hash(hash ^ 0x7F4A7C15u)));
    const float cellRadians = 2.0 / STAR_CELLS; // near enough, across most of a face
    const float distance = length(grid - place) * cellRadians / g_pixelRadians;
    const float brightness = pow(Unit(Hash(hash ^ 0x2545F491u)), 3.0) * STAR_LIGHT;
    const float warmth = Unit(Hash(hash ^ 0x68E31DA4u));
    const float3 tint = lerp(float3(0.75, 0.85, 1.0), float3(1.0, 0.85, 0.7), warmth);
    sky += tint * brightness * exp(-distance * distance * 1.2);
  }
  const float thickness = smoothstep(0.45, 0.8, Fractal(_direction * 2.2, g_skySeed + 401u, 5)) * g_nebulaNear.w;
  const float3 nebula = lerp(g_nebulaNear.rgb, g_nebulaFar.rgb, Fractal(_direction * 3.7, g_skySeed + 503u, 4));
  return sky + nebula * thickness * NEBULA_LIGHT;
}

float4 main(float4 _position : SV_Position, float2 _uv : TEXCOORD0) : SV_Target
{
  // The layers where they lie; beside them, both clear.
  const float2 inLayers = (_uv - g_layersPlace.xy) / g_layersPlace.zw;
  uint2 layers = uint2(CLEAR, CLEAR);
  if (all(inLayers >= 0.0) && all(inLayers < 1.0))
  {
    const uint2 layerAt = min(uint2(inLayers * g_layerPixels), uint2(g_layerPixels) - 1);
    layers = g_layers.Load(int3(layerAt, 0));
  }
  if (layers.y != CLEAR)
  {
    return float4(g_palette[layers.y].rgb, 1.0);
  }
  float3 under = ToLinear(g_palette[0].rgb);
  if (layers.x != CLEAR)
  {
    under = ToLinear(g_palette[layers.x].rgb);
  }
  else if (g_skyShown > 0.5)
  {
    const float2 at = _uv * g_inset.xy;
    const float3 ray = normalize(float3((at.x - g_inset.z) / g_focal.x, (at.y - g_inset.w) / g_focal.y, 1.0));
    under += Sky(Rotate(g_toScene, ray));
  }
  const uint2 sceneAt = min(uint2(_uv * g_scenePixels), uint2(g_scenePixels) - 1);
  const float4 scene = g_scene.Load(int3(sceneAt, 0)); // premultiplied by its coverage
  const float3 bloom = g_bloom.SampleLevel(g_linear, _uv, 0.0).rgb * g_bloomShare;
  return float4(ToDisplay(under * (1.0 - scene.a) + scene.rgb + bloom), 1.0);
}
