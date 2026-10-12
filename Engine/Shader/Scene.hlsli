// Engine/Shader/Scene.hlsli
// What SceneVS and ScenePS share (ADR-033): the root constants Renderer sets, and what a vertex hands its pixels.

cbuffer Frame : register(b0)
{
  float4 g_projection; // inset NDC = (x * g_projection.x + z * g_projection.y, z * g_projection.w - y * g_projection.z) / z
  float g_nearUnits;   // reversed depth: g_nearUnits / z, 1 at the near plane
  float g_depthLift;   // 1 for faces; a little over for lines, which sit on their faces, over every layer
  float2 g_framePadding;
};

cbuffer Object : register(b1)
{
  float4 g_rotation;    // the model's frame into the camera's, a unit quaternion
  float3 g_translation; // the model's origin in the camera's frame
  float g_lit;          // 1 for faces, lit; 0 for lines, as they are
  float3 g_towardsLight; // in the model's own frame
  float g_objectPadding;
};

cbuffer Palette : register(b2)
{
  float4 g_palette[4]; // linear
};

struct SceneVertexOut
{
  float4 position : SV_Position;
  float3 color : COLOR0;
};
