// GameLogic/TimerState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Timer.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct TimerState
{
  /// port 0x61 at start-up, restored on exit
  std::uint8_t savedSpeakerPort;
  /// incremented by TimerTick; reset by RateLimit
  std::uint16_t timerTicks;
  /// incremented every tick; bit 9 drives a blink
  std::uint16_t millisecondCounter;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("savedSpeakerPort", _self.savedSpeakerPort);
    _visitor("timerTicks", _self.timerTicks);
    _visitor("millisecondCounter", _self.millisecondCounter);
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

static_assert(std::is_trivially_copyable_v<TimerState> && std::is_standard_layout_v<TimerState>);

} // namespace Elite
