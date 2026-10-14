// GameLogic/FlowState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// Where the game is, at the title, docked or in flight, and the frame's clock, which several subsystems write. Its members are the
/// original data segment's, by their names, in their order there; the GameState holds it in GameData.
struct FlowState
{
  /// incremented every tick; compared with minimumFrameMs and cleared by PresentSpaceView
  std::uint8_t msSinceFrame;
  /// 1 while RunFlight's loop owns the screen (GameLoop), 0 on the title and docked screens and while HandleFlightFunctionKeys runs a sc...
  std::uint8_t inFlight;
  /// 1 once the title has run; cleared on death (GameLoop) and by Abort on the pause screen, so the title runs again
  std::uint8_t titleShown;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("msSinceFrame", _self.msSinceFrame);
    _visitor("inFlight", _self.inFlight);
    _visitor("titleShown", _self.titleShown);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }
};

static_assert(std::is_trivially_copyable_v<FlowState> && std::is_standard_layout_v<FlowState>);

} // namespace Elite
