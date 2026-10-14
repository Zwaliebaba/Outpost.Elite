// GameLogic/DockingState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Docking.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct DockingState
{
  /// RunDockingComputer's steering, AL=roll, AH=pitch
  std::uint16_t dockingComputerSteering;
  /// RunDockingComputer's state, 0-11
  std::uint8_t dockingComputerState;
  /// angle RunDockingComputer is turning to
  std::uint16_t dockingTargetAngle;
  /// each alignment phase of RunDockingComputer runs twice
  std::uint8_t dockingAlignPasses;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("dockingComputerSteering", _self.dockingComputerSteering);
    _visitor("dockingComputerState", _self.dockingComputerState);
    _visitor("dockingTargetAngle", _self.dockingTargetAngle);
    _visitor("dockingAlignPasses", _self.dockingAlignPasses);
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

static_assert(std::is_trivially_copyable_v<DockingState> && std::is_standard_layout_v<DockingState>);

} // namespace Elite
