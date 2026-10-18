// Outpost/SpaceView.h
#pragma once

#include "Canvas.h"
#include "Renderer.h"
#include "SceneSnapshot.h"
#include "ShipModel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Elite
{
class Host;
struct FlightSnapshot;
} // namespace Elite

namespace Outpost
{

/// The space view drawn as a scene in the cockpit's window (ADR-033, ADR-034, ADR-035, D62, D65): the ships the game drew as
/// its blueprints' models, the sun and the planet as spheres, the planet's surface and the sky the system's (PlanetLookOf,
/// SkyLookOf), the stardust and the beams as strokes, all drawn by the renderer, between the space view's two layers, the game's
/// own pixels of everything else. It draws while the screen shows the last frame of flight or of the title the game published, and
/// draws nothing otherwise. A tunnel's frame it draws from the snapshot's tunnel (ADR-069): the station's rectangles as a corridor of
/// glowing edges and rails, the hyperspace's rings as glowing loops, on black.
class SpaceView
{
public:
  /// The ship models, into _renderer, each numbered by its type. Returns false if the renderer refuses one.
  [[nodiscard]] bool AddModels(Engine::Renderer& _renderer);

  /// The scene to draw now, as _platform's last two flight snapshots make it, a frame drawn one update late between them (D49),
  /// its original view stretched over _view and the scene filling _window round it at the same scale (Hor+, D50), places in design
  /// units under a canvas (ADR-034). The game's layers lie over _view; beside it, the renderer draws by the original's own rule what the
  /// game drew nothing of (D69). None while the screen does not show the last one. It holds until the next call.
  [[nodiscard]] const Engine::SceneFrame* Frame(const Elite::Host& _platform, const Engine::Rect& _view, const Engine::Rect& _window);

  /// The game's words in the frame the screen shows (SpaceViewLayers::HoldsWords), into _canvas over the cockpit, which covers
  /// none of them (D66): each block of lines on a panel in the cockpit's style (D68), and the ink of each, in the screen's
  /// colours, at its pixels of the view stretched over _place. None while the screen does not show the last frame.
  static void DrawWords(const Elite::Host& _platform, const Engine::Rect& _place, Engine::Canvas& _canvas);

private:
  // The scene of _snapshot: the camera at the origin, turned as the frame's camera was, each object drawn as a model, and the
  // sun, the planet and the title's planet as spheres, the sun in _palette's colour of its disc.
  [[nodiscard]] Engine::SceneSnapshot SceneOf(const Elite::FlightSnapshot& _snapshot, const std::array<std::uint32_t, 4>& _palette) const;

  // _snapshot's objects out of the view's cone that the game would draw as dots (D69), as strokes in _palette's colours with the
  // stardust.
  void TakeSideDots(const Elite::FlightSnapshot& _snapshot, const std::array<std::uint32_t, 4>& _palette);

  // _latest's tunnel, as strokes in _palette's colours, in place of the beams.
  void TakeTunnel(const Elite::FlightSnapshot& _latest, const std::array<std::uint32_t, 4>& _palette);

  // _before's beams, as strokes in _palette's colours.
  void TakeBeams(const Elite::FlightSnapshot& _before, const std::array<std::uint32_t, 4>& _palette);

  // The stardust (D67), moved back by the player's velocity for the paced time since the last call and seen by the camera as it is
  // drawn now, as strokes in _palette's colour of the original's dust; none while the frame draws no dust, nor moved.
  void MoveStardust(const Elite::Host& _platform, const std::array<std::uint32_t, 4>& _palette);

  std::vector<Elite::ShipModel> m_models; // by type
  Engine::SceneSnapshot m_before;
  Engine::SceneSnapshot m_after;
  std::uint64_t m_afterSequence = 0;
  Engine::SceneSnapshot m_drawn;
  Engine::SceneFrame m_frame;
  std::vector<Engine::SceneStroke> m_dust; // the stardust's, and the dots beside the view
  std::vector<Engine::SceneStroke> m_beams;
  std::vector<Engine::Vector3> m_stardust; // the motes, in the scene's axes about the camera
  std::uint64_t m_stardustClock = 0;       // the paced time they were moved to
};

} // namespace Outpost
