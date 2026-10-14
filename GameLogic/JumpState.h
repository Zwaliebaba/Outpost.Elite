// GameLogic/JumpState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// The hyperspace and galactic jumps under way, which flight starts and Hyperspace counts down. Its members are the 1987 data segment's, by
/// their names, in their order there; the GameState holds it in GameData.
struct JumpState
{
  /// set by ReadScanCode when W (11h) is pressed while Alt is held; on hyperspace arrival (478C) it forces EnterWitchSpace, which puts t...
  std::uint8_t forceMisjump;
  /// set when H is pressed inside the galactic drive window; CompleteHyperspaceJump then makes a galactic jump
  std::uint8_t galacticJumpPending;
  /// 40 when G arms the galactic drive; H within that window starts a galactic jump
  std::uint8_t galacticDriveReadyFrames;
  /// 10 when the countdown starts, -1 every 10 frames; arrival at 0
  std::uint8_t hyperspaceCountdown;
  /// frames left in the current countdown step (10)
  std::uint8_t hyperspaceCountdownFrames;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("forceMisjump", _self.forceMisjump);
    _visitor("galacticJumpPending", _self.galacticJumpPending);
    _visitor("galacticDriveReadyFrames", _self.galacticDriveReadyFrames);
    _visitor("hyperspaceCountdown", _self.hyperspaceCountdown);
    _visitor("hyperspaceCountdownFrames", _self.hyperspaceCountdownFrames);
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

static_assert(std::is_trivially_copyable_v<JumpState> && std::is_standard_layout_v<JumpState>);

} // namespace Elite
