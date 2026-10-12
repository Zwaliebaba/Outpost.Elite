// Engine/Shader/ScenePS.hlsl
// A scene model's pixel (ADR-033): the colour its vertices carry, flat across a face, in linear light. Alpha 1 is coverage, which
// the resolve averages with the clear's 0 at a model's edge, and the composite lays it over the under layer by.

#include "Scene.hlsli"

float4 main(SceneVertexOut _input) : SV_Target
{
  return float4(_input.color, 1.0);
}
