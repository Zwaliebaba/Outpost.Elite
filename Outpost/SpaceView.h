// Outpost/SpaceView.h
#pragma once

#include "Cga.h"
#include "Renderer.h"
#include "SceneSnapshot.h"
#include "ShipModel.h"

#include <cstdint>
#include <vector>

namespace Elite
{
class Platform;
struct FlightSnapshot;
} // namespace Elite

namespace Outpost
{

/// The space view drawn as a scene over the CGA picture (ADR-033, D62): the ships the game drew as its blueprints' models, drawn
/// by the renderer, between the space view's two layers, the game's own pixels of everything else. It draws while the screen shows
/// the last frame of flight the game published, and draws nothing otherwise: the tunnels, the jumps and every other screen keep
/// the CGA picture.
class SpaceView
{
public:
  /// The ship models, into _renderer, each numbered by its type. Returns false if the renderer refuses one.
  [[nodiscard]] bool AddModels(Engine::Renderer& _renderer);

  /// The space view's place in the CGA picture's grid of 640 dots by 200 lines (FrameImage): its 256 pixels from x 32, two dots
  /// each, and its 126 lines from line 12.
  static constexpr Engine::Rect PICTURE_PLACE{.left = 64.0f, .top = 12.0f, .width = 512.0f, .height = 126.0f};

  /// The scene to draw now, as _platform's last two flight snapshots make it, a frame drawn one update late between them (D49),
  /// its view stretched over _place: PICTURE_PLACE over the picture, or a place in design units under a canvas (ADR-034). None
  /// while the screen does not show the last one. It holds until the next call.
  [[nodiscard]] const Engine::SceneFrame* Frame(const Elite::Platform& _platform, const Engine::Rect& _place);

private:
  // The scene of _snapshot: the camera at the origin, turned as the frame's camera was, and each object drawn as a model.
  [[nodiscard]] Engine::SceneSnapshot SceneOf(const Elite::FlightSnapshot& _snapshot) const;

  std::vector<Elite::ShipModel> m_models; // by type
  Engine::SceneSnapshot m_before;
  Engine::SceneSnapshot m_after;
  std::uint64_t m_afterSequence = 0;
  Engine::SceneSnapshot m_drawn;
  Engine::SceneFrame m_frame;
};

} // namespace Outpost
