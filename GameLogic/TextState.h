// GameLogic/TextState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Text.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the original
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct TextState
{
  /// 'BOUNTY:000000 Cr'; ShowBountyMessage writes the digits
  std::uint8_t bountyText[17];
  /// 'CLASS: 1234567 TYPE: 12345678901'; ShowShipIdentity fills the fields of 7 and 11 characters
  std::uint8_t shipIdentityText[33];
  /// cursor glyph, a space and a zero: printed after the text by RedrawInputLine; ToggleInputCursor flips the first byte
  std::uint8_t inputCursorText[3];
  /// ReadTextLine's countdown, 300 timer ticks per cursor toggle
  std::uint16_t cursorBlinkTicks;
  /// characters ReadTextLine may still accept
  std::uint8_t inputCharsLeft;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("bountyText", _self.bountyText);
    _visitor("shipIdentityText", _self.shipIdentityText);
    _visitor("inputCursorText", _self.inputCursorText);
    _visitor("cursorBlinkTicks", _self.cursorBlinkTicks);
    _visitor("inputCharsLeft", _self.inputCharsLeft);
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

static_assert(std::is_trivially_copyable_v<TextState> && std::is_standard_layout_v<TextState>);

} // namespace Elite
