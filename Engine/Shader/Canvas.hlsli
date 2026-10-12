// Engine/Shader/Canvas.hlsli
// What CanvasVS and CanvasPS share (ADR-034): a shape as Canvas.h's CanvasShape lays it out, the root constants Renderer sets,
// and what a vertex hands its pixels.

struct CanvasShape
{
  float4 bounds; // left, top, right, bottom, in design units
  float4 params0;
  float4 params1;
  uint kind;
  uint color;  // 0xAARRGGBB, display-encoded
  uint color2;
  uint padding;
};

StructuredBuffer<CanvasShape> g_shapes : register(t0);

cbuffer Placement : register(b0)
{
  float g_scale;          // screen pixels per design unit
  float2 g_origin;        // where design (0, 0) is on the target, in pixels
  float g_placementPadding;
  float2 g_targetPixels;  // the target's size
  float2 g_atlasPixels;   // the glyph atlas's size
};

struct CanvasVertexOut
{
  float4 position : SV_Position;
  float2 design : TEXCOORD0; // the pixel's place in design units
  nointerpolation uint shape : SHAPE;
};
