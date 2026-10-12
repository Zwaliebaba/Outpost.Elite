// Engine/Shader/BloomPS.hlsl
// The scene's bloom (ADR-035, D47), at a quarter of its size: first the light past 1 of each 4 x 4 block of the resolved scene,
// four bilinear taps of it; then a Gaussian of nine texels in five bilinear taps, across and then down, g_direction's texels a step.
// PresentVS covers the target.

Texture2D<float4> g_source : register(t0);
SamplerState g_linear : register(s0);

cbuffer Bloom : register(b0)
{
  float2 g_sourceTexel; // a texel of the source, in its texture coordinates
  float2 g_direction;   // the blur's step, in texels, across or down; 0 to take the light past 1 and shrink
};

static const float THRESHOLD = 1.0;
// The weights and offsets of a Gaussian of nine texels: the middle, and two taps each side, each between two texels to weigh both.
static const float WEIGHTS[3] = {0.2270270270, 0.3162162162, 0.0702702703};
static const float OFFSETS[3] = {0.0, 1.3846153846, 3.2307692308};

float4 main(float4 _position : SV_Position, float2 _uv : TEXCOORD0) : SV_Target
{
  if (all(g_direction == float2(0.0, 0.0)))
  {
    float3 light = float3(0.0, 0.0, 0.0);
    [unroll] for (int tap = 0; tap < 4; ++tap)
    {
      const float2 offset = float2((tap & 1) != 0 ? 1.0 : -1.0, (tap & 2) != 0 ? 1.0 : -1.0) * g_sourceTexel;
      light += max(g_source.SampleLevel(g_linear, _uv + offset, 0.0).rgb - THRESHOLD, 0.0);
    }
    return float4(light * 0.25, 1.0);
  }
  float3 sum = g_source.SampleLevel(g_linear, _uv, 0.0).rgb * WEIGHTS[0];
  [unroll] for (int tap = 1; tap < 3; ++tap)
  {
    const float2 offset = g_direction * g_sourceTexel * OFFSETS[tap];
    sum += (g_source.SampleLevel(g_linear, _uv + offset, 0.0).rgb + g_source.SampleLevel(g_linear, _uv - offset, 0.0).rgb) * WEIGHTS[tap];
  }
  return float4(sum, 1.0);
}
