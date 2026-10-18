// GameLogic/SpaceViewLayers.h
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Elite
{

class GameState;

/// The space view: 256 pixels across and 126 lines down, buffer rows 1-126 of the drawing buffer (FinishSpaceViewFrame).
inline constexpr std::uint32_t SPACE_VIEW_WIDTH_PIXELS = 256;
inline constexpr std::uint32_t SPACE_VIEW_HEIGHT_PIXELS = 126;

/// A pixel of a layer that holds nothing: what is under it shows through. Any other value is a pixel value, 0-3.
inline constexpr std::uint8_t LAYER_CLEAR = 0xFF;

/// One layer of the space view, row by row, a pixel value 0-3 or LAYER_CLEAR each.
using SpaceViewLayer = std::array<std::uint8_t, std::size_t{SPACE_VIEW_WIDTH_PIXELS} * SPACE_VIEW_HEIGHT_PIXELS>;

/// The space view of one frame of flight split by what drew it (ADR-033, ADR-035), for a renderer that draws much of it itself:
/// - **what the renderer draws**: the blueprints' models, and the sun, the planet, the stardust, the laser beams and the sights
///   (NoteModel, NoteRendered), which it draws in their place from the flight snapshot;
/// - **under**, what each other object drew before the first the renderer draws, in the game's own pixels: a distant station's
///   disc, the dots of far ships;
/// - **over**, everything else drawn in the frame: the title's words and the message line, and what other objects drew after one
///   the renderer draws, which the game's painter's order puts nearer than it; of it, which pixels no object drew, the game's words;
/// - which slots were drawn as a blueprint's model, and which object last changed each pixel.
///
/// It watches the drawing buffer round each object TransformAndDrawObjects draws and round each thing the renderer draws
/// (BeginObject, EndObject), and reads the finished view from the buffer PresentSpaceView presented (Present, Finish), where the
/// original copied it to video memory (ADR-069). It reads the game's state and writes none of it: it is the platform's, outside the
/// game's state, as the flight snapshot is (ADR-032).
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

  /// What is being drawn is the renderer's to draw in its place, not as a model: the sun, the planet, the stardust, the beams and
  /// the sights (ADR-035).
  void NoteRendered() noexcept;

  /// The object in slot _slot is drawn: what it changed in the buffer is the renderer's when it is a model or the renderer's to
  /// draw; else the under layer's, or the over layer's once the renderer has drawn an object of the frame. NO_SLOT for what is no
  /// object's: the stardust, the beams, the sights, the title's planet.
  void EndObject(const GameState& _state, std::uint8_t _slot) noexcept;

  /// EndObject's slot for what is drawn like an object but is no slot's.
  static constexpr std::uint8_t NO_SLOT = 0xFF;

  /// The drawing buffer, _buffer, presented as the frame's view.
  void Present(std::span<const std::uint8_t> _buffer) noexcept;

  /// The game shows a screen other than the view: it is no longer shown until the next frame finishes.
  void Leave() noexcept;

  /// The frame's drawing is finished and presented: the over layer of it, and the under layer and the models as the objects left
  /// them, published for the renderer.
  void Finish() noexcept;

  /// The frame is a tunnel's, which the renderer draws from the snapshot's tunnel (ADR-069): both layers clear, and no model,
  /// nothing the renderer's and no words.
  void FinishTunnel() noexcept;

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

  /// Whether the screen still shows the view of the last finished frame: false once the game has shown a screen of its own, a
  /// chart's, a docked screen's or the cockpit's afresh (Leave), and before any frame is finished.
  [[nodiscard]] bool Shown() const noexcept;

  /// Whether a model or something the renderer draws was the last to change pixel _pixel of the last finished frame, row by row:
  /// there the renderer's drawing shows, unless the over layer covers it.
  [[nodiscard]] bool RendererOwns(std::size_t _pixel) const noexcept
  {
    return _pixel < m_publishedRendererOwned.size() && m_publishedRendererOwned[_pixel];
  }

  /// Whether pixel _pixel of the last finished frame's over layer, row by row, was drawn by no object and painted over by none: the
  /// game's words, the title's, the pause screen's, the credits', which the cockpit does not cover (D66).
  [[nodiscard]] bool HoldsWords(std::size_t _pixel) const noexcept
  {
    return _pixel < m_publishedWords.size() && m_publishedWords[_pixel];
  }

  /// The slot of the object that last changed pixel _pixel of the last finished frame, row by row, or NO_SLOT for none or for
  /// what is no slot's.
  [[nodiscard]] std::uint8_t OwnerOf(std::size_t _pixel) const noexcept
  {
    return _pixel < m_publishedOwners.size() ? m_publishedOwners[_pixel] : NO_SLOT;
  }

  /// Pixel _x of view row _row as PresentSpaceView last presented the view, 0-3.
  [[nodiscard]] std::uint8_t PresentedPixel(std::size_t _row, std::size_t _x) const noexcept;

  static constexpr std::size_t BUFFER_BYTES = 0x2000; ///< the drawing buffer's, DRAWING_BUFFER_BYTES

private:
  using Buffer = std::array<std::uint8_t, BUFFER_BYTES>;
  using PixelBits = std::bitset<std::size_t{SPACE_VIEW_WIDTH_PIXELS} * SPACE_VIEW_HEIGHT_PIXELS>;
  using PixelSlots = std::array<std::uint8_t, std::size_t{SPACE_VIEW_WIDTH_PIXELS} * SPACE_VIEW_HEIGHT_PIXELS>;

  // What the object being drawn is.
  enum class Drawing : std::uint8_t
  {
    Plain,
    Model,
    Rendered
  };

  Buffer m_before{}; // the buffer as the object being drawn found it
  Buffer m_last{};   // the buffer as the last object left it
  SpaceViewLayer m_under{};
  SpaceViewLayer m_over{}; // what was drawn between objects or after one the renderer draws, and no object painted over since
  PixelBits m_rendererOwned;
  PixelBits m_words; // over, drawn by no object
  PixelSlots m_owners{};
  std::bitset<SLOTS> m_models;
  Drawing m_drawing = Drawing::Plain;
  bool m_rendererDrew = false; // an object of the frame was the renderer's: what plain objects draw after it is over

  SpaceViewLayer m_publishedUnder{};
  SpaceViewLayer m_publishedOver{};
  PixelBits m_publishedRendererOwned;
  PixelBits m_publishedWords;
  PixelSlots m_publishedOwners{};
  std::bitset<SLOTS> m_publishedModels;
  Buffer m_presented{}; // the drawing buffer as PresentSpaceView presented it
  bool m_finished = false;
  bool m_left = false; // the game has shown another screen since the frame finished
};

} // namespace Elite
