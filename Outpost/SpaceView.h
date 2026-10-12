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

  /// The scene to draw over the picture now, as _platform's last two flight snapshots make it, a frame drawn one update late
  /// between them (D49); none while the screen does not show the last one. It holds until the next call.
  [[nodiscard]] const Engine::SceneFrame* Frame(const Elite::Platform& _platform);

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
