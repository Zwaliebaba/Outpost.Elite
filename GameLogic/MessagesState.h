// GameLogic/MessagesState.h
#pragma once

#include "Message.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// The message line, which several subsystems post to: what it shows and for how many frames, and the warning's frames. Its members are the
/// 1987 data segment's, by their names, in their order there; the GameState holds it in GameData.
struct MessagesState
{
  /// pointer of the text on the message line now; UpdateMessageLine redraws only when messagePointer differs
  Message messageShown;
  /// text the message line should show; written with messageFrames by about 18 routines
  Message messagePointer;
  /// frames left; posters write it as a word, which also zeroes messageDrawn
  std::uint16_t messageFrames;
  /// 20 when a warning is raised; counts down
  std::uint8_t warningFrames;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("messageShown", _self.messageShown);
    _visitor("messagePointer", _self.messagePointer);
    _visitor("messageFrames", _self.messageFrames);
    _visitor("warningFrames", _self.warningFrames);
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

static_assert(std::is_trivially_copyable_v<MessagesState> && std::is_standard_layout_v<MessagesState>);

} // namespace Elite
