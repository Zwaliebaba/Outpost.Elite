#include "pch.h"

#include "GlyphAtlas.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Engine
{

namespace
{

constexpr wchar_t FAMILY[] = L"Bahnschrift";
constexpr wchar_t LOCALE[] = L"en-us";
constexpr float LAYOUT_EXTENT = 100000.0f; // a line's box, wide and tall enough for any line
constexpr std::uint32_t PADDING = 1;       // texels between glyphs, so that none bleeds into the next

// One glyph of a laid-out line: its face, its size, its index, and where its pen and baseline are, in screen pixels.
struct LaidGlyph
{
  Microsoft::WRL::ComPtr<IDWriteFontFace> face;
  float emSize = 0.0f;
  std::uint16_t index = 0;
  float penX = 0.0f;
  float baselineY = 0.0f;
};

// What IDWriteTextLayout::Draw hands a renderer: each glyph run, kept glyph by glyph. It lives on the stack for one Draw, so it
// counts no references.
class GlyphCollector final : public IDWriteTextRenderer
{
public:
  std::vector<LaidGlyph> glyphs;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID _id, void** _object) override
  {
    if (_id == __uuidof(IUnknown) || _id == __uuidof(IDWritePixelSnapping) || _id == __uuidof(IDWriteTextRenderer))
    {
      *_object = this;
      return S_OK;
    }
    *_object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override
  {
    return 1;
  }

  ULONG STDMETHODCALLTYPE Release() override
  {
    return 1;
  }

  HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* _disabled) override
  {
    *_disabled = FALSE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* _transform) override
  {
    *_transform = DWRITE_MATRIX{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* _pixels) override
  {
    *_pixels = 1.0f;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT _baselineX, FLOAT _baselineY, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* _run,
                                         const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override
  {
    float pen = _baselineX;
    for (UINT32 glyph = 0; glyph < _run->glyphCount; ++glyph)
    {
      const float offset = _run->glyphOffsets != nullptr ? _run->glyphOffsets[glyph].advanceOffset : 0.0f;
      glyphs.push_back(LaidGlyph{.face = _run->fontFace,
                                 .emSize = _run->fontEmSize,
                                 .index = _run->glyphIndices[glyph],
                                 .penX = pen + offset,
                                 .baselineY = _baselineY});
      pen += _run->glyphAdvances[glyph];
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override
  {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override
  {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override
  {
    return S_OK;
  }
};

// The cap height of _face at _emSize, in pixels: a line of capitals and figures is centred on it.
[[nodiscard]] float CapHeight(IDWriteFontFace* _face, float _emSize)
{
  DWRITE_FONT_METRICS metrics{};
  _face->GetMetrics(&metrics);
  return metrics.designUnitsPerEm == 0 ? 0.0f : static_cast<float>(metrics.capHeight) * _emSize / metrics.designUnitsPerEm;
}

} // namespace

bool GlyphAtlas::Create()
{
  Microsoft::WRL::ComPtr<IUnknown> factory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), &factory)) || FAILED(factory.As(&m_factory)))
  {
    return false;
  }
  if (FAILED(m_factory->CreateTypography(&m_tabular)))
  {
    return false;
  }
  const DWRITE_FONT_FEATURE tabular{DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1};
  if (FAILED(m_tabular->AddFontFeature(tabular)))
  {
    return false;
  }
  Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
  UINT32 index = 0;
  BOOL exists = FALSE;
  m_hasBahnschrift =
    SUCCEEDED(m_factory->GetSystemFontCollection(&fonts)) && SUCCEEDED(fonts->FindFamilyName(FAMILY, &index, &exists)) && exists == TRUE;
  m_pixels.assign(std::size_t{SIZE_PIXELS} * SIZE_PIXELS, 0);
  m_changed = true;
  return true;
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> GlyphAtlas::LayoutOf(const CanvasText& _text, float _sizePixels, float _spacingPixels)
{
  // Labels in the semi-condensed width, numbers in the normal one, both semi-bold, as the canvas draws them.
  const DWRITE_FONT_STRETCH stretch = _text.font == Font::Label ? DWRITE_FONT_STRETCH_SEMI_CONDENSED : DWRITE_FONT_STRETCH_NORMAL;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  Microsoft::WRL::ComPtr<IDWriteTextLayout1> spaced;
  const DWRITE_TEXT_RANGE all{0, static_cast<UINT32>(_text.text.size())};
  if (FAILED(m_factory->CreateTextFormat(FAMILY, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, stretch,
                                         std::max(_sizePixels, 1.0f), LOCALE, &format)) ||
      FAILED(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP)) ||
      FAILED(m_factory->CreateTextLayout(_text.text.data(), static_cast<UINT32>(_text.text.size()), format.Get(), LAYOUT_EXTENT,
                                         LAYOUT_EXTENT, &layout)) ||
      FAILED(layout->SetTypography(m_tabular.Get(), all)) || FAILED(layout.As(&spaced)) ||
      FAILED(spaced->SetCharacterSpacing(0.0f, _spacingPixels, 0.0f, all)))
  {
    return nullptr;
  }
  return layout;
}

float GlyphAtlas::Measure(const CanvasText& _text)
{
  const Microsoft::WRL::ComPtr<IDWriteTextLayout> layout = LayoutOf(_text, _text.size, _text.spacing);
  DWRITE_TEXT_METRICS metrics{};
  if (!layout || FAILED(layout->GetMetrics(&metrics)))
  {
    return 0.0f;
  }
  return std::max(metrics.widthIncludingTrailingWhitespace - _text.spacing, 0.0f);
}

bool GlyphAtlas::Lay(const CanvasText& _text, float _scale, Point _origin, std::vector<PlacedGlyph>& _glyphs)
{
  if (_text.text.empty())
  {
    return true;
  }
  const float spacing = _text.spacing * _scale;
  const Microsoft::WRL::ComPtr<IDWriteTextLayout> layout = LayoutOf(_text, _text.size * _scale, spacing);
  DWRITE_TEXT_METRICS metrics{};
  GlyphCollector collector;
  if (!layout || FAILED(layout->GetMetrics(&metrics)) || FAILED(layout->Draw(nullptr, &collector, 0.0f, 0.0f)))
  {
    return false;
  }
  if (collector.glyphs.empty())
  {
    return true;
  }
  // Across, by the anchor and the alignment; down, the capitals' middle on the anchor.
  const float width = std::max(metrics.widthIncludingTrailingWhitespace - spacing, 0.0f);
  const float anchorX = _origin.x + _text.anchor.x * _scale;
  const float left = _text.align == Align::Left ? anchorX : _text.align == Align::Center ? anchorX - width / 2.0f : anchorX - width;
  const LaidGlyph& first = collector.glyphs.front();
  const float baseline = std::round(_origin.y + _text.anchor.y * _scale + CapHeight(first.face.Get(), first.emSize) / 2.0f);
  for (const LaidGlyph& glyph : collector.glyphs)
  {
    const Entry* entry = EntryOf(glyph.face.Get(), glyph.emSize, glyph.index);
    if (entry == nullptr || entry->width == 0)
    {
      continue;
    }
    const float x = std::round(left + glyph.penX) + static_cast<float>(entry->left);
    const float y = baseline + (glyph.baselineY - first.baselineY) + static_cast<float>(entry->top);
    _glyphs.push_back(PlacedGlyph{.left = x,
                                  .top = y,
                                  .right = x + static_cast<float>(entry->width),
                                  .bottom = y + static_cast<float>(entry->height),
                                  .texels = entry->texels});
  }
  return true;
}

const GlyphAtlas::Entry* GlyphAtlas::EntryOf(IDWriteFontFace* _face, float _emSize, std::uint16_t _glyph)
{
  const auto key = std::make_tuple(_face, _emSize, _glyph);
  if (const auto found = m_entries.find(key); found != m_entries.end())
  {
    return &found->second;
  }
  // The glyph alone, its pen on a whole pixel at the origin, in grayscale.
  const FLOAT advance = 0.0f;
  const DWRITE_GLYPH_OFFSET offset{};
  const DWRITE_GLYPH_RUN run{_face, _emSize, 1, &_glyph, &advance, &offset, FALSE, 0};
  Microsoft::WRL::ComPtr<IDWriteGlyphRunAnalysis> analysis;
  RECT bounds{};
  if (FAILED(m_factory->CreateGlyphRunAnalysis(&run, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, DWRITE_MEASURING_MODE_NATURAL,
                                               DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.0f, 0.0f,
                                               &analysis)) ||
      FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds)))
  {
    return nullptr;
  }
  Entry entry;
  const auto width = static_cast<std::uint32_t>(std::max<LONG>(bounds.right - bounds.left, 0));
  const auto height = static_cast<std::uint32_t>(std::max<LONG>(bounds.bottom - bounds.top, 0));
  if (width != 0 && height != 0)
  {
    // A shelf at a time, left to right; a full atlas starts again empty, and glyphs are rasterised again as they come.
    if (m_shelfX + width + PADDING > SIZE_PIXELS)
    {
      m_shelfX = 0;
      m_shelfY += m_shelfHeight + PADDING;
      m_shelfHeight = 0;
    }
    if (m_shelfY + height + PADDING > SIZE_PIXELS || width + PADDING > SIZE_PIXELS)
    {
      m_entries.clear();
      m_faces.clear();
      std::ranges::fill(m_pixels, std::uint8_t{0});
      m_shelfX = 0;
      m_shelfY = 0;
      m_shelfHeight = 0;
    }
    std::vector<std::uint8_t> coverage(std::size_t{width} * height);
    if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, coverage.data(), static_cast<UINT32>(coverage.size()))))
    {
      return nullptr;
    }
    for (std::uint32_t row = 0; row < height; ++row)
    {
      std::ranges::copy_n(coverage.begin() + static_cast<std::ptrdiff_t>(std::size_t{row} * width), width,
                          m_pixels.begin() + static_cast<std::ptrdiff_t>(std::size_t{m_shelfY + row} * SIZE_PIXELS + m_shelfX));
    }
    entry.texels = {static_cast<float>(m_shelfX), static_cast<float>(m_shelfY), static_cast<float>(m_shelfX + width),
                    static_cast<float>(m_shelfY + height)};
    entry.left = bounds.left;
    entry.top = bounds.top;
    entry.width = static_cast<int>(width);
    entry.height = static_cast<int>(height);
    m_shelfX += width + PADDING;
    m_shelfHeight = std::max(m_shelfHeight, height);
    m_changed = true;
  }
  m_faces.emplace_back(_face);
  return &m_entries.emplace(key, entry).first->second;
}

bool GlyphAtlas::TakeChanged() noexcept
{
  return std::exchange(m_changed, false);
}

} // namespace Engine
