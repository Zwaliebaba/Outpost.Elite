// GameLogic/KeyboardState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// A key going down or coming up, as the host gives it to the game (D88): the key by the number keyDown indexes it by (Keys), 01h-7Fh.
struct KeyEvent
{
  std::uint8_t key;
  bool down;
};

/// The keys the game reads in keyDown, by their scan codes: what each does.
namespace Keys
{

/// pauses the game in flight (ProcessFlightKeys 813D)
inline constexpr std::uint8_t ESC = 0x01;
/// pitch -1 (ReadKeyboardSteering)
inline constexpr std::uint8_t Q = 0x10;
/// ECM (ProcessFlightKeys 829F)
inline constexpr std::uint8_t E = 0x12;
/// primes a missile (8171)
inline constexpr std::uint8_t T = 0x14;
/// unarms the missile (81FB)
inline constexpr std::uint8_t U = 0x16;
/// ID indicator: identifies the ship in the sights (82FE)
inline constexpr std::uint8_t I = 0x17;
/// roll -1 (ReadKeyboardSteering)
inline constexpr std::uint8_t O = 0x18;
/// roll +1 (ReadKeyboardSteering)
inline constexpr std::uint8_t P = 0x19;
/// with Esc, a freeze without the pause menu (8149)
inline constexpr std::uint8_t CTRL = 0x1D;
/// pitch +1 (ReadKeyboardSteering)
inline constexpr std::uint8_t A = 0x1E;
/// arms the galactic hyperdrive (802E)
inline constexpr std::uint8_t G = 0x22;
/// hyperspace (805E)
inline constexpr std::uint8_t H = 0x23;
/// jump drive (8133)
inline constexpr std::uint8_t J = 0x24;
/// anti-ECM emulator while held (837E)
inline constexpr std::uint8_t L = 0x26;
/// makes ReadScanCode OR 80h into the buffered code
inline constexpr std::uint8_t LEFT_SHIFT = 0x2A;
/// launches the escape capsule (82DC)
inline constexpr std::uint8_t C = 0x2E;
/// energy bomb (82C6)
inline constexpr std::uint8_t B = 0x30;
/// masking device while held (8368)
inline constexpr std::uint8_t N = 0x31;
/// launches a locked missile (8222)
inline constexpr std::uint8_t M = 0x32;
/// playerSpeed -4 (84B2)
inline constexpr std::uint8_t COMMA = 0x33;
/// playerSpeed +4 (849E)
inline constexpr std::uint8_t PERIOD = 0x34;
/// makes ReadScanCode OR 80h into the buffered code
inline constexpr std::uint8_t RIGHT_SHIFT = 0x36;
/// with W the mis-jump latch
inline constexpr std::uint8_t ALT = 0x38;
/// fire on the keyboard (ReadFireButton); the pause screen clears it on resume
inline constexpr std::uint8_t SPACE = 0x39;
/// pitch -1 (ReadKeyboardSteering)
inline constexpr std::uint8_t UP = 0x48;
/// roll -1 (ReadKeyboardSteering)
inline constexpr std::uint8_t LEFT = 0x4B;
/// roll +1 (ReadKeyboardSteering)
inline constexpr std::uint8_t RIGHT = 0x4D;
/// pitch +1 (ReadKeyboardSteering)
inline constexpr std::uint8_t DOWN = 0x50;

} // namespace Keys

/// The keys held, which the keyboard's handler writes and flight clears. Its members are the 1987 data segment's, by their names, in their
/// order there; the GameState holds it in GameData.
struct KeyboardState
{
  /// 1 while the key is held, indexed by the scan code itself (Keys; entry 0 unused; the map's 'scan code - 1' misses the inc ah at
  /// 7452); written by ReadScanCode
  std::uint8_t keyDown[128];

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("keyDown", _self.keyDown);
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

static_assert(std::is_trivially_copyable_v<KeyboardState> && std::is_standard_layout_v<KeyboardState>);

} // namespace Elite
