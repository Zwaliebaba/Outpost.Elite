// Engine/GlyphAtlas.h
#pragma once

#include "Canvas.h"
#include "Win32.h"

#include <dwrite_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <span>
#include <tuple>
#include <vector>

namespace Engine
{

/// A glyph laid out on the screen (GlyphAtlas::Lay): the rectangle its texels cover, in screen pixels, and where they are in the
/// atlas.
struct PlacedGlyph
{
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
  std::array<float, 4> texels{}; ///< left, top, right, bottom in the atlas
};

/// Text for the 2D layer (ADR-034, D39, D55): DirectWrite lays a line out in Bahnschrift with tabular figures (tnum), at the
/// screen's own pixel size, and rasterises each glyph once, in grayscale, into an 8-bit atlas the renderer samples. A glyph
/// stands on a whole pixel, so that it is as sharp as DirectWrite drew it.
class GlyphAtlas
{
public:
  static constexpr std::uint32_t SIZE_PIXELS = 1024; ///< the atlas is this square

  /// DirectWrite's factory and the faces. Returns false if DirectWrite is not available.
  [[nodiscard]] bool Create();

  /// Whether Bahnschrift is installed: it ships with Windows 10 1709 and later. Without it DirectWrite falls back to another face.
  [[nodiscard]] bool HasBahnschrift() const noexcept
  {
    return m_hasBahnschrift;
  }

  /// _text laid out at _scale screen pixels per design unit, design (0, 0) at _origin on the screen: its glyphs, added to
  /// _glyphs, each new one rasterised into the atlas. Returns false if DirectWrite fails.
  [[nodiscard]] bool Lay(const CanvasText& _text, float _scale, Point _origin, std::vector<PlacedGlyph>& _glyphs);

  /// The width of _text laid out, in design units: its advances, less the spacing after its last character.
  [[nodiscard]] float Measure(const CanvasText& _text);

  /// The atlas, row by row, a byte of coverage a texel.
  [[nodiscard]] std::span<const std::uint8_t> Pixels() const noexcept
  {
    return m_pixels;
  }

  /// Whether the atlas changed since the last call, which clears it.
  [[nodiscard]] bool TakeChanged() noexcept;

  /// A glyph as the atlas keeps it: its texels, and where they stand from the pen, in whole pixels.
  struct Entry
  {
    std::array<float, 4> texels{};
    int left = 0; ///< from the pen's pixel to the texels' left
    int top = 0;  ///< from the baseline down to their top, negative above it
    int width = 0;
    int height = 0;
  };

private:
  // The layout of _text at _sizePixels, the em in screen pixels.
  [[nodiscard]] Microsoft::WRL::ComPtr<IDWriteTextLayout> LayoutOf(const CanvasText& _text, float _sizePixels, float _spacingPixels);
  // _glyph of _face at _emSize, rasterised into the atlas the first time.
  [[nodiscard]] const Entry* EntryOf(IDWriteFontFace* _face, float _emSize, std::uint16_t _glyph);

  Microsoft::WRL::ComPtr<IDWriteFactory2> m_factory;
  Microsoft::WRL::ComPtr<IDWriteTypography> m_tabular;
  bool m_hasBahnschrift = false;
  std::vector<std::uint8_t> m_pixels;
  std::map<std::tuple<IDWriteFontFace*, float, std::uint16_t>, Entry> m_entries;
  std::vector<Microsoft::WRL::ComPtr<IDWriteFontFace>> m_faces; // every face an entry names, held while it does
  std::uint32_t m_shelfX = 0;
  std::uint32_t m_shelfY = 0;
  std::uint32_t m_shelfHeight = 0;
  bool m_changed = false;
};

} // namespace Engine
