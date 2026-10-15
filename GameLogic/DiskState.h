// GameLogic/DiskState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// The disk operation's error, which saving and loading and the start-up share. Its member is the original data segment's, by its name;
/// the GameState holds it in GameData.
struct DiskState
{
  /// set by CriticalErrorInterrupt and PerformDiskRequest's error path, cleared by Start before each operation
  std::uint8_t diskError;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("diskError", _self.diskError);
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

static_assert(std::is_trivially_copyable_v<DiskState> && std::is_standard_layout_v<DiskState>);

} // namespace Elite
