// Engine/Shader/PresentPS.hlsl
// The picture with a sharp bilinear filter (ADR-009): each source pixel keeps a flat colour across the
// screen pixels it covers, and only the screen pixel that straddles the boundary between two source
// pixels is blended, in proportion. With a whole number of screen pixels per source pixel it is nearest
// neighbour; with a fraction (the CGA's 2.4 screen rows to a line) the rows stay even and sharp.

Texture2D<float4> g_picture : register(t0);
SamplerState g_linear : register(s0);

cbuffer Scale : register(b0)
{
  float2 g_sourcePixels;        // the picture's size in source pixels
  float2 g_screenPixelsPerPixel; // screen pixels covered by one source pixel, across and down
};

float4 main(float4 _position : SV_Position, float2 _uv : TEXCOORD0) : SV_Target
{
  const float2 texel = _uv * g_sourcePixels;
  const float2 texelFloor = floor(texel);
  const float2 fromCenter = frac(texel) - 0.5;
  const float2 halfFlat = max(0.5 - 0.5 / g_screenPixelsPerPixel, 0.0);
  const float2 blend = (fromCenter - clamp(fromCenter, -halfFlat, halfFlat)) * g_screenPixelsPerPixel + 0.5;
  const float2 uv = (texelFloor + blend) / g_sourcePixels;
  return float4(g_picture.Sample(g_linear, uv).rgb, 1.0);
}
