// The canvas presenter's shared shader declarations (slice 2e, ADR-005 section 1).
//
// Two entry points in two files, because FXC compiles one entry point per invocation and the
// project compiles each one to its own header. What they share is here so it cannot drift.

// The 320x200 canvas, one COLOUR INDEX per pixel. `uint` and not `float`, because a palette index
// is not a brightness and interpolating between two of them is meaningless.
Texture2D<uint> CanvasTexture : register(t0);

// The sixteen VIC-II colours, packed R,G,B,A one byte each, arriving as SIXTEEN ROOT CONSTANTS.
// Root constants and not a constant buffer: a quarter of the root signature's budget buys the
// whole palette with no resource, no descriptor and no upload.
cbuffer PaletteConstants : register(b0)
{
  uint4 gPalette[4];
};

struct Vertex
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};
