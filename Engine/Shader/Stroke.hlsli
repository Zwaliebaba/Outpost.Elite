// Engine/Shader/Stroke.hlsli
// What StrokeVS and StrokePS share (ADR-035): a stroke's root constants, and what a vertex hands its pixels. A stroke is a capsule
// between two points of the inset's layer grid, tapering from one width to the other, measured in the scene target's pixels.

cbuffer StrokeFrame : register(b0)
{
  float2 g_targetPixels; // the scene's target, width and height
  float2 g_layerPixels;  // the layers' grid over it
};

cbuffer Stroke : register(b1)
{
  float2 g_from; // in the layers' pixels
  float2 g_to;
  float g_fromWidth; // in the layers' rows
  float g_toWidth;
  float g_intensity;
  float g_strokePadding;
  float4 g_color; // linear
};

struct StrokeVertex
{
  float4 position : SV_Position;
  float2 at : TEXCOORD0; // in the target's pixels
};

// A point of the layer grid in the target's pixels, and a width in its rows.
float2 TargetOf(float2 _layer)
{
  return _layer * g_targetPixels / g_layerPixels;
}

float TargetWidth(float _width)
{
  return _width * g_targetPixels.y / g_layerPixels.y;
}
