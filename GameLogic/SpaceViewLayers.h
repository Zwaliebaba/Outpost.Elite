// GameLogic/SpaceViewLayers.h
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace Elite
{

class GameState;

/// The space view as the screen shows it: 256 pixels of the 320x200 screen across from x 32, and 126 lines down from line 12,
/// buffer rows 1-126 of the drawing buffer (FinishSpaceViewFrame).
inline constexpr std::uint32_t SPACE_VIEW_WIDTH_PIXELS = 256;
inline constexpr std::uint32_t SPACE_VIEW_HEIGHT_PIXELS = 126;
inline constexpr std::uint32_t SPACE_VIEW_LEFT_PIXELS = 32;
inline constexpr std::uint32_t SPACE_VIEW_TOP_LINES = 12;

/// A pixel of a layer that holds nothing: what is under it shows through. Any other value is a pixel value, 0-3.
inline constexpr std::uint8_t LAYER_CLEAR = 0xFF;

/// One layer of the space view, row by row, a pixel value 0-3 or LAYER_CLEAR each.
using SpaceViewLayer = std::array<std::uint8_t, std::size_t{SPACE_VIEW_WIDTH_PIXELS} * SPACE_VIEW_HEIGHT_PIXELS>;

/// The space view of one frame of flight split by what drew it (ADR-033), for a renderer that draws its ships itself:
/// - **under**, what each object that is not a blueprint's model drew, in the game's own pixels: the sun and the planet, a
///   distant station's disc, the dots of far ships;
/// - **over**, what was drawn after the last object: the laser beams, the stardust, the message line's pixels in the view and
///   the sights;
/// - and which slots were drawn as a blueprint's model, which the renderer draws in their place.
///
/// It watches the drawing buffer round each object TransformAndDrawObjects draws (BeginObject, EndObject), and reads the
/// finished view from video memory once FinishSpaceViewFrame has copied it (Finish). It reads the game's state and writes none
/// of it: it is the platform's, outside the counted state, as the flight snapshot is (ADR-032).
class SpaceViewLayers
{
public:
  static constexpr std::size_t SLOTS = 36; ///< shipSlots' slots

  /// Nothing finished yet: both layers clear and no model.
  SpaceViewLayers() noexcept;

  /// A frame's drawing begins: nothing drawn yet, on the buffer as the frame before left it, cleared.
  void BeginFrame() noexcept;

  /// An object is about to be drawn.
  void BeginObject(const GameState& _state) noexcept;

  /// The object being drawn is a blueprint's model, which the renderer draws in its place.
  void NoteModel() noexcept;

  /// The object in slot _slot is drawn: what it changed in the buffer is the under layer's, unless it is a model.
  void EndObject(const GameState& _state, std::uint8_t _slot) noexcept;

  /// The frame's drawing is finished and in video memory: the over layer of it, and the under layer and the models as the
  /// objects left them, published for the renderer.
  void Finish(const GameState& _state) noexcept;

  /// The last finished frame's layers and models.
  [[nodiscard]] const SpaceViewLayer& Under() const noexcept
  {
    return m_publishedUnder;
  }

  [[nodiscard]] const SpaceViewLayer& Over() const noexcept
  {
    return m_publishedOver;
  }

  [[nodiscard]] bool DrawnAsModel(std::uint8_t _slot) const noexcept
  {
    return _slot < SLOTS && m_publishedModels[_slot];
  }

  /// Whether the screen still shows the view of the last finished frame, byte for byte: false once the game has drawn anything
  /// else there, a tunnel, a chart or a screen of its own, and before any frame is finished. A frame's view reaches the screen in
  /// work, which paced time does not divide, so between two frames of flight it is true.
  [[nodiscard]] bool Shown(const GameState& _state) const noexcept;

  /// Whether a model was the last object to change pixel _pixel of the last finished frame, row by row: there the renderer's
  /// model shows, unless the over layer covers it.
  [[nodiscard]] bool ModelOwns(std::size_t _pixel) const noexcept
  {
    return _pixel < m_publishedModelOwned.size() && m_publishedModelOwned[_pixel];
  }

  static constexpr std::size_t BUFFER_BYTES = 0x2000; ///< the drawing buffer's, DRAWING_BUFFER_BYTES

private:
  using Buffer = std::array<std::uint8_t, BUFFER_BYTES>;

  Buffer m_before{}; // the buffer as the object being drawn found it
  Buffer m_last{};   // the buffer as the last object left it
  SpaceViewLayer m_under{};
  std::bitset<std::size_t{SPACE_VIEW_WIDTH_PIXELS} * SPACE_VIEW_HEIGHT_PIXELS> m_modelOwned;
  std::bitset<SLOTS> m_models;
  bool m_drawingModel = false;

  SpaceViewLayer m_publishedUnder{};
  SpaceViewLayer m_publishedOver{};
  std::bitset<std::size_t{SPACE_VIEW_WIDTH_PIXELS} * SPACE_VIEW_HEIGHT_PIXELS> m_publishedModelOwned;
  std::bitset<SLOTS> m_publishedModels;
  std::array<std::uint8_t, std::size_t{SPACE_VIEW_HEIGHT_PIXELS} * SPACE_VIEW_WIDTH_PIXELS / 4> m_publishedShown{}; // its bytes
  bool m_finished = false;
};

} // namespace Elite
