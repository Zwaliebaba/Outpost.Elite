// GameLogic/MathsState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Maths.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the
/// original data segment's, by their names, in their order there; the GameState holds it in GameData.
struct MathsState
{
  /// A rotation's sine and cosine.
  struct StoredSinCos
  {
    std::uint16_t sine;
    std::uint16_t cosine;

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("sine", _self.sine);
      _visitor("cosine", _self.cosine);
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
  /// filled by nine entries of the sine and cosine setter, one each, used by the rotations...
  StoredSinCos rotationSinCos[9];

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("rotationSinCos", _self.rotationSinCos);
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

static_assert(std::is_trivially_copyable_v<MathsState> && std::is_standard_layout_v<MathsState>);

} // namespace Elite
