#include "pch.h"

#include "SpaceViewLayers.h"

#include "GameState.h"
#include "Video.h"

#include <algorithm>
#include <span>

namespace Elite
{

namespace
{

constexpr std::size_t BUFFER_ROW_BYTES = 64;   // 256 pixels of 2 bits
constexpr std::size_t FIRST_VIEW_ROW = 1;      // the buffer row the view's first line is
constexpr std::size_t VIDEO_LINE_BYTES = 80;   // a line of the 320x200 screen
constexpr std::size_t VIDEO_ODD_BANK = 0x2000; // where the odd lines are
constexpr std::size_t PIXELS_PER_BYTE = 4;
constexpr std::size_t ROW_BYTES = SPACE_VIEW_WIDTH_PIXELS / PIXELS_PER_BYTE; // a row of the view, on the screen
constexpr std::uint8_t PIXEL_BITS = 3;

static_assert(sizeof(DrawingState::drawingBuffer) == DRAWING_BUFFER_BYTES && SpaceViewLayers::BUFFER_BYTES == DRAWING_BUFFER_BYTES);

// The drawing buffer, as the game's state holds it now.
[[nodiscard]] std::span<const std::uint8_t> BufferOf(const GameState& _state) noexcept
{
  return _state.DataBytes().subspan(GAME_DATA_OFFSET(drawing.drawingBuffer), DRAWING_BUFFER_BYTES);
}

// Pixel _x of a row whose first byte is _row: the leftmost the byte's top two bits, as the CGA shows them.
[[nodiscard]] std::uint8_t PixelOf(const std::uint8_t* _row, std::size_t _x) noexcept
{
  const auto shift = static_cast<unsigned>(2 * (PIXELS_PER_BYTE - 1 - _x % PIXELS_PER_BYTE));
  return static_cast<std::uint8_t>((_row[_x / PIXELS_PER_BYTE] >> shift) & PIXEL_BITS);
}

// The first byte of the view's row _row in the drawing buffer, and on the screen.
[[nodiscard]] std::size_t BufferRow(std::size_t _row) noexcept
{
  return (_row + FIRST_VIEW_ROW) * BUFFER_ROW_BYTES;
}

[[nodiscard]] std::size_t VideoRow(std::size_t _row) noexcept
{
  const std::size_t line = _row + SPACE_VIEW_TOP_LINES;
  return (line % 2) * VIDEO_ODD_BANK + (line / 2) * VIDEO_LINE_BYTES + SPACE_VIEW_LEFT_PIXELS / PIXELS_PER_BYTE;
}

// _visit(pixel, value) for each pixel of the view whose value in _after, a drawing buffer, differs from _before's.
template <typename Visit> void ForEachChanged(std::span<const std::uint8_t> _before, std::span<const std::uint8_t> _after, Visit _visit)
{
  for (std::size_t row = 0; row < SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    const std::size_t start = BufferRow(row);
    if (std::equal(_after.begin() + static_cast<std::ptrdiff_t>(start),
                   _after.begin() + static_cast<std::ptrdiff_t>(start + BUFFER_ROW_BYTES),
                   _before.begin() + static_cast<std::ptrdiff_t>(start)))
    {
      continue;
    }
    for (std::size_t x = 0; x < SPACE_VIEW_WIDTH_PIXELS; ++x)
    {
      const std::uint8_t now = PixelOf(&_after[start], x);
      if (now != PixelOf(&_before[start], x))
      {
        _visit(row * SPACE_VIEW_WIDTH_PIXELS + x, now);
      }
    }
  }
}

} // namespace

SpaceViewLayers::SpaceViewLayers() noexcept
{
  m_under.fill(LAYER_CLEAR);
  m_publishedUnder.fill(LAYER_CLEAR);
  m_publishedOver.fill(LAYER_CLEAR);
  m_over.fill(LAYER_CLEAR);
}

void SpaceViewLayers::BeginFrame() noexcept
{
  // The buffer as FinishSpaceViewFrame's ClearDrawBuffer left it, all 0: anything drawn in it since, before the objects, is over.
  m_last.fill(0);
  m_under.fill(LAYER_CLEAR);
  m_over.fill(LAYER_CLEAR);
  m_modelOwned.reset();
  m_models.reset();
  m_drawingModel = false;
}

void SpaceViewLayers::BeginObject(const GameState& _state) noexcept
{
  // What was drawn since the last object, the title's words or anything between two objects, is over, unless an object paints
  // over it later.
  const std::span<const std::uint8_t> buffer = BufferOf(_state);
  ForEachChanged(m_last, buffer, [this](std::size_t _pixel, std::uint8_t _value) { m_over[_pixel] = _value; });
  std::ranges::copy(buffer, m_before.begin());
  m_drawingModel = false;
}

void SpaceViewLayers::NoteModel() noexcept
{
  m_drawingModel = true;
}

void SpaceViewLayers::EndObject(const GameState& _state, std::uint8_t _slot) noexcept
{
  const std::span<const std::uint8_t> buffer = BufferOf(_state);
  if (m_drawingModel && _slot < SLOTS)
  {
    m_models.set(_slot);
  }
  ForEachChanged(m_before, buffer,
                 [this](std::size_t _pixel, std::uint8_t _value)
                 {
                   m_modelOwned[_pixel] = m_drawingModel;
                   m_over[_pixel] = LAYER_CLEAR;
                   if (!m_drawingModel)
                   {
                     m_under[_pixel] = _value;
                   }
                 });
  std::ranges::copy(buffer, m_last.begin());
  m_drawingModel = false;
}

void SpaceViewLayers::Finish(const GameState& _state) noexcept
{
  const Cga::VideoMemory video = _state.VideoBytes();
  for (std::size_t row = 0; row < SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    const std::uint8_t* shown = &video[VideoRow(row)];
    const std::uint8_t* last = &m_last[BufferRow(row)];
    for (std::size_t x = 0; x < SPACE_VIEW_WIDTH_PIXELS; ++x)
    {
      const std::uint8_t now = PixelOf(shown, x);
      const std::size_t pixel = row * SPACE_VIEW_WIDTH_PIXELS + x;
      m_publishedOver[pixel] = now == PixelOf(last, x) ? m_over[pixel] : now;
    }
  }
  m_publishedUnder = m_under;
  m_publishedModelOwned = m_modelOwned;
  m_publishedModels = m_models;
  for (std::size_t row = 0; row < SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    const auto shown = video.subspan(VideoRow(row), ROW_BYTES);
    std::ranges::copy(shown, m_publishedShown.begin() + static_cast<std::ptrdiff_t>(row * ROW_BYTES));
  }
  m_finished = true;
}

bool SpaceViewLayers::Shown(const GameState& _state) const noexcept
{
  if (!m_finished)
  {
    return false;
  }
  const Cga::VideoMemory video = _state.VideoBytes();
  for (std::size_t row = 0; row < SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    const auto shown = video.subspan(VideoRow(row), ROW_BYTES);
    if (!std::ranges::equal(shown, std::span(m_publishedShown).subspan(row * ROW_BYTES, ROW_BYTES)))
    {
      return false;
    }
  }
  return true;
}

} // namespace Elite
