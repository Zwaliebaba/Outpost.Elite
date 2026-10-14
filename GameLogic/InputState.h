// GameLogic/InputState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Input.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct InputState
{
  /// ring buffer of make codes, written by ReadScanCode
  std::uint8_t keyBuffer[16];
  /// next write address in keyBuffer
  std::uint16_t keyBufferWrite;
  /// next read address in keyBuffer; GetKey advances it, ResetKeyboard resets it
  std::uint16_t keyBufferRead;
  /// keys waiting in keyBuffer; ReadScanCode drops a key when it reaches 16
  std::uint8_t keyBufferCount;
  /// last frame's roll direction (-1, 0, +1; 7Fh in the file); ReadKeyboardSteering
  std::uint8_t keyboardLastRollKey;
  /// last frame's pitch direction (7Fh in the file)
  std::uint8_t keyboardLastPitchKey;
  /// grows by 1 a frame while the same roll key is held, inside +-23; reset to 0 on any change
  std::uint8_t keyboardRollRamp;
  /// as keyboardRollRamp, for pitch
  std::uint8_t keyboardPitchRamp;
  /// integrated roll for the keyboard, +-23; ReadSteering stores AX here (word with keyboardPitchRate)
  std::uint8_t keyboardRollRate;
  /// integrated pitch, as keyboardRollRate
  std::uint8_t keyboardPitchRate;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("keyBuffer", _self.keyBuffer);
    _visitor("keyBufferWrite", _self.keyBufferWrite);
    _visitor("keyBufferRead", _self.keyBufferRead);
    _visitor("keyBufferCount", _self.keyBufferCount);
    _visitor("keyboardLastRollKey", _self.keyboardLastRollKey);
    _visitor("keyboardLastPitchKey", _self.keyboardLastPitchKey);
    _visitor("keyboardRollRamp", _self.keyboardRollRamp);
    _visitor("keyboardPitchRamp", _self.keyboardPitchRamp);
    _visitor("keyboardRollRate", _self.keyboardRollRate);
    _visitor("keyboardPitchRate", _self.keyboardPitchRate);
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

static_assert(std::is_trivially_copyable_v<InputState> && std::is_standard_layout_v<InputState>);

} // namespace Elite
