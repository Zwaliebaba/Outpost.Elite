#include "Canvas.hlsli"

float4 PsMain(Vertex input) : SV_Target
{
  // An integer Load rather than a Sample, which is why there is no sampler and no filtering state
  // anywhere in the presenter: at an integer scale this is the C64's picture with each pixel
  // repeated, and point sampling is a consequence of the lookup rather than a state to set.
  int2 texel = int2(input.uv * float2(320.0, 200.0));
  texel = clamp(texel, int2(0, 0), int2(319, 199));

  uint index = CanvasTexture.Load(int3(texel, 0)) & 15u;

  // The colour of an index is one component of one of the four uint4s. The component is selected
  // with comparisons rather than with `row[index & 3]`: dynamic indexing of a VECTOR is a corner
  // of HLSL worth not depending on, and this compiles to the same select instructions.
  uint4 row = gPalette[index >> 2];
  uint lane = index & 3;
  uint packed = (lane == 0) ? row.x : ((lane == 1) ? row.y : ((lane == 2) ? row.z : row.w));

  return float4(float(packed & 255u), float((packed >> 8) & 255u), float((packed >> 16) & 255u), 255.0) / 255.0;
}
