// Outpost/SpaceView.h
#pragma once

#include "Canvas.h"
#include "Cga.h"
#include "Renderer.h"
#include "SceneSnapshot.h"
#include "ShipModel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Elite
{
class Platform;
struct FlightSnapshot;
} // namespace Elite

namespace Outpost
{

/// The space view drawn as a scene in the cockpit's window (ADR-033, ADR-034, ADR-035, D62, D65): the ships the game drew as
/// its blueprints' models, the sun and the planet as spheres, the planet's surface and the sky the system's (PlanetLookOf,
/// SkyLookOf), the stardust and the beams as strokes, all drawn by the renderer, between the space view's two layers, the game's
/// own pixels of everything else. It draws while the screen shows the last frame of flight or of the title the game published, and
/// draws nothing otherwise: the tunnels, the jumps and every other screen keep the CGA picture.
class SpaceView
{
public:
  /// The ship models, into _renderer, each numbered by its type. Returns false if the renderer refuses one.
  [[nodiscard]] bool AddModels(Engine::Renderer& _renderer);

  /// The scene to draw now, as _platform's last two flight snapshots make it, a frame drawn one update late between them (D49),
  /// its view stretched over _place, a place in design units under a canvas (ADR-034). None while the screen does not show the
  /// last one. It holds until the next call.
  [[nodiscard]] const Engine::SceneFrame* Frame(const Elite::Platform& _platform, const Engine::Rect& _place);

  /// The game's words in the frame the screen shows (SpaceViewLayers::HoldsWords), into _canvas over the cockpit, which covers
  /// none of them (D66): the ink of each, in the screen's colours, at its pixels of the view stretched over _place. None while
  /// the screen does not show the last frame.
  static void DrawWords(const Elite::Platform& _platform, const Engine::Rect& _place, Engine::Canvas& _canvas);

private:
  // The scene of _snapshot: the camera at the origin, turned as the frame's camera was, each object drawn as a model, and the
  // sun, the planet and the title's planet as spheres, the sun in _palette's colour of its disc.
  [[nodiscard]] Engine::SceneSnapshot SceneOf(const Elite::FlightSnapshot& _snapshot, const std::array<std::uint32_t, 4>& _palette) const;

  // The stardust of _before moved _t of the way to _after's, and _before's beams, as strokes in _palette's colours.
  void TakeStrokes(const Elite::FlightSnapshot& _before, const Elite::FlightSnapshot& _after, float _t,
                   const std::array<std::uint32_t, 4>& _palette);

  std::vector<Elite::ShipModel> m_models; // by type
  Engine::SceneSnapshot m_before;
  Engine::SceneSnapshot m_after;
  std::uint64_t m_afterSequence = 0;
  Engine::SceneSnapshot m_drawn;
  Engine::SceneFrame m_frame;
  std::vector<Engine::SceneStroke> m_dust;
  std::vector<Engine::SceneStroke> m_beams;
};

} // namespace Outpost
