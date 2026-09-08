// The presenter's shared shader declarations (slice 2e, ADR-005 section 1).
//
// Two entry points in two files, because FXC compiles one entry point per invocation and the
// project compiles each one to its own header. What they share is here so it cannot drift.
//
// The FILES still say "canvas" and the picture is `Elite::Picture`'s since Resolution.md RS-0. The
// name is the blit's rather than the surface's -- one texture, one quad, one palette lookup, and
// none of it knows which surface filled the texture. Renaming three files with custom FXC build
// steps that no Linux leg compiles would buy that sentence and risk the one leg that reads them.

// The image, one COLOUR INDEX per pixel. `uint` and not `float`, because a palette index is not a
// brightness and interpolating between two of them is meaningless.
Texture2D<uint> CanvasTexture : register(t0);

// The sixteen VIC-II colours, packed R,G,B,A one byte each, and then the image's size -- EIGHTEEN
// ROOT CONSTANTS. Root constants and not a constant buffer: a fraction of the root signature's
// budget buys the whole palette with no resource, no descriptor and no upload.
//
// THE SIZE IS A CONSTANT AND NOT A LITERAL because a resolution written into a shader is a number
// nothing checks: it renders as a smear rather than as an error, and it is duplicated from a C++
// constant no compiler relates it to. `gImageSize` is `Elite::Picture`'s own, passed once per frame.
cbuffer PaletteConstants : register(b0)
{
  uint4 gPalette[4];
  uint2 gImageSize;
};

struct Vertex
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};
