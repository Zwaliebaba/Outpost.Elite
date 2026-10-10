// Engine/Shader/PresentVS.hlsl
// One triangle that covers the viewport, with the picture's texture coordinates: (0, 0) top left,
// (1, 1) bottom right. Presenter sets the viewport to the picture's rectangle.

struct PresentVertex
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

PresentVertex main(uint _vertex : SV_VertexID)
{
  const float2 uv = float2((_vertex << 1) & 2, _vertex & 2);
  PresentVertex output;
  output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  output.uv = uv;
  return output;
}
