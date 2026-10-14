// GameLogic/SoundState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Sound.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct SoundState
{
  /// 2-8 by effect
  std::uint8_t sweepStepShrink;
  /// 0-0x3FF into noiseSource, advanced by EmitNoiseSample
  std::uint16_t noiseSourceIndex;
  /// when 1, TimerTick plays the tune at musicPointer on PIT channel 2
  std::uint8_t musicPlaying;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("sweepStepShrink", _self.sweepStepShrink);
    _visitor("noiseSourceIndex", _self.noiseSourceIndex);
    _visitor("musicPlaying", _self.musicPlaying);
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

static_assert(std::is_trivially_copyable_v<SoundState> && std::is_standard_layout_v<SoundState>);

} // namespace Elite
