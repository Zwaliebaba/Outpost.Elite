#include "Canvas.hlsli"

// One triangle covering the viewport twice over, from the vertex index alone: no vertex buffer,
// no input layout, no index buffer. Vertex 0 is (0,0), vertex 1 is (2,0) and vertex 2 is (0,2) in
// uv, which becomes (-1,1), (3,1) and (-1,-3) in clip space -- the quad plus the waste that keeps
// it to one primitive.
Vertex VsMain(uint id : SV_VertexID)
{
  Vertex output;
  output.uv = float2((id << 1) & 2, id & 2);
  output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  return output;
}
