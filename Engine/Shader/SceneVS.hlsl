// Engine/Shader/SceneVS.hlsl
// A scene model's vertex (ADR-033): turned and moved into the camera's frame, x right, y down and z ahead, and projected onto the
// inset, z into reversed depth with no far plane. A vertex of a face whose normal points away from the camera is put behind the
// near plane, so that the whole face, which carries one normal and one point of its plane, is clipped. Its colour is its mix of
// the palette, lit by the light as the face's normal meets it, or as it is for a line.

#include "Scene.hlsli"

struct SceneVertexIn
{
  float3 position : POSITION;
  float3 normal : NORMAL;
  float3 facePoint : TEXCOORD0;
  float4 paletteWeights : COLOR0;
};

static const float AMBIENT = 0.35; // the light on a face turned from the sun

float3 Rotate(float4 _rotation, float3 _vector)
{
  const float3 twice = 2.0 * cross(_rotation.xyz, _vector);
  return _vector + _rotation.w * twice + cross(_rotation.xyz, twice);
}

SceneVertexOut main(SceneVertexIn _input)
{
  const float3 at = Rotate(g_rotation, _input.position) + g_translation;
  const float3 normal = Rotate(g_rotation, _input.normal);
  const float3 facePoint = Rotate(g_rotation, _input.facePoint) + g_translation;
  const float3 color = mul(_input.paletteWeights, float4x3(g_palette[0].rgb, g_palette[1].rgb, g_palette[2].rgb, g_palette[3].rgb));
  const float light = AMBIENT + (1.0 - AMBIENT) * saturate(dot(_input.normal, g_towardsLight));

  SceneVertexOut output;
  output.color = color * lerp(1.0, light, g_lit);
  if (dot(normal, facePoint) >= 0.0)
  {
    output.position = float4(0.0, 0.0, -1.0, 1.0);
    return output;
  }
  output.position = float4(g_projection.x * at.x + g_projection.y * at.z, g_projection.w * at.z - g_projection.z * at.y,
                           g_nearUnits * g_depthLift, at.z);
  return output;
}
