// Engine/Shader/CompositePS.hlsl
// The scene's inset (ADR-033): the over layer's pixel where it holds one; else the resolved scene over the under layer by the
// scene's coverage, in linear light, the under layer's pixel or palette colour 0 where it is clear. Each layer pixel covers its
// share of the inset, nearest, and the scene is the inset's size on screen, a pixel each. PresentVS covers the inset.

Texture2D<float4> g_scene : register(t0);
Texture2D<uint2> g_layers : register(t1); // x under, y over: a palette index, or CLEAR

cbuffer Composite : register(b0)
{
  float4 g_palette[4]; // as the picture's pixels, display-encoded
  float2 g_layerPixels;
  float2 g_scenePixels;
};

static const uint CLEAR = 0xFF;

float3 ToLinear(float3 _color)
{
  return _color <= 0.04045 ? _color / 12.92 : pow((_color + 0.055) / 1.055, 2.4);
}

float3 ToDisplay(float3 _color)
{
  const float3 color = saturate(_color);
  return color <= 0.0031308 ? color * 12.92 : 1.055 * pow(color, 1.0 / 2.4) - 0.055;
}

float4 main(float4 _position : SV_Position, float2 _uv : TEXCOORD0) : SV_Target
{
  const uint2 layerAt = min(uint2(_uv * g_layerPixels), uint2(g_layerPixels) - 1);
  const uint2 layers = g_layers.Load(int3(layerAt, 0));
  if (layers.y != CLEAR)
  {
    return float4(g_palette[layers.y].rgb, 1.0);
  }
  const float3 under = ToLinear(g_palette[layers.x != CLEAR ? layers.x : 0].rgb);
  const uint2 sceneAt = min(uint2(_uv * g_scenePixels), uint2(g_scenePixels) - 1);
  const float4 scene = g_scene.Load(int3(sceneAt, 0)); // premultiplied by its coverage
  return float4(ToDisplay(under * (1.0 - scene.a) + scene.rgb), 1.0);
}
