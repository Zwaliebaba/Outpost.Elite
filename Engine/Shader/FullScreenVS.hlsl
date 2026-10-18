// Engine/Shader/FullScreenVS.hlsl
// One triangle that covers the viewport, with texture coordinates across it: (0, 0) top left, (1, 1) bottom right. The scene's
// composite, its spheres and its bloom each set the viewport to what they cover.

struct FullScreenVertex
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

FullScreenVertex main(uint _vertex : SV_VertexID)
{
  const float2 uv = float2((_vertex << 1) & 2, _vertex & 2);
  FullScreenVertex output;
  output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  output.uv = uv;
  return output;
}
