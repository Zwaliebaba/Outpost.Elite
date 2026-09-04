#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <deque>

namespace Outpost
{

/*
 * The window, the message pump and the keyboard (slice 2e, ADR-005 sections 1 and 4).
 *
 * ONE THREAD, and that is the decision this file exists to record. `TextPrint.h` leaves the
 * choice open -- "a pumped thread, a coroutine, or rewriting the docked screens as state machines
 * fed by `InputFrame`" -- because `Elite::KeySource::NextKey` BLOCKS, which is what `TT217` does,
 * and ADR-004's `InputFrame` is a poll. The three options are not equal:
 *
 *   * A GAME THREAD needs the canvas double-buffered under a mutex, needs the presenter to be
 *     told which snapshot to upload, and needs an answer for what happens to a thread parked
 *     inside `NextKey` when the window closes. Every one of those is a place to get it wrong,
 *     and none of them buys anything, because the thing the game is blocked ON is the player.
 *   * A STATE-MACHINE REWRITE stops the docked screens being a line-by-line port, which is the
 *     cost `TextPrint.h` names and ADR-001 exists to avoid paying.
 *   * A NESTED PUMP -- this -- runs the game on the main thread and pumps messages from inside
 *     `NextKey`. No mutex, no snapshot, no second thread. The window stays responsive while the
 *     game waits for a key, which is all the time that matters; it is unresponsive only while the
 *     game is computing BETWEEN keys, which for a docked screen is microseconds.
 *
 * It has a real consequence and it is better stated than discovered: the canvas is never uploaded
 * mid-mutation, because the only code that mutates it is the code that is not running while the
 * pump is. A threaded presenter would have had to arrange that deliberately.
 *
 * Phase 3's flight loop does not change this. `MLOOP` POLLS the keyboard (`TT17`) rather than
 * blocking on it, so it runs as an ordinary loop with a pump turn per iteration, and the level
 * table below is what it reads.
 */
class Window
{
public:
  Window() = default;
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  /// Creates a window whose CLIENT area is the canvas at `_scale`, and shows it. Throws through
  /// `winrt::check_hresult` on failure, which the composition root catches.
  void Create(HINSTANCE _instance, int _scale);

  [[nodiscard]] HWND Handle() const noexcept { return m_window; }

  /// Dispatches everything waiting and returns false once the window has closed. It does NOT
  /// block: the vsync wait belongs to `CanvasPresenter::Present`, so that a frame is what paces
  /// the loop rather than a timer.
  [[nodiscard]] bool Pump() noexcept;

  [[nodiscard]] bool Closed() const noexcept { return m_closed; }

  void ClientSize(int& _outWidth, int& _outHeight) const noexcept;

  /*
   * 6502: RDKEY -- the next key press as a C64 internal key NUMBER, or false if none is waiting.
   *
   * A number and not a character, because that is what the game's own keyboard scan produces and
   * what `TT102` compares against; `Outpost::CharacterFor` is the other half, and `KeyMap.h` says
   * why there are two.
   */
  [[nodiscard]] bool TakeKey(std::uint8_t& _outKey) noexcept;

  /// 6502: KEYLOOK -- whether a key is held right now, for the polling idiom `DOKEY` uses.
  [[nodiscard]] bool Held(std::uint8_t _c64Key) const noexcept;

  /// 6502: FLKB -- empty the keyboard buffer, so a key pressed before a prompt is discarded.
  void FlushKeys() noexcept;

  /// True once, if the client area has changed since the last time this was asked. The presenter
  /// resizes its buffers on it.
  [[nodiscard]] bool TakeResize() noexcept;

private:
  static LRESULT CALLBACK Dispatch(HWND, UINT, WPARAM, LPARAM) noexcept;

  /*
   * The HWND is a PARAMETER and not read from `m_window`, because the window procedure runs
   * before `CreateWindowEx` has returned and therefore before `m_window` has been assigned. The
   * first message a window gets is WM_NCCREATE, and `DefWindowProc` returning FALSE for that one
   * -- which is what it does when handed a null window -- makes CreateWindowEx fail with no
   * error worth reading.
   */
  LRESULT OnMessage(HWND _window, UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept;

  void PressKey(WPARAM _virtualKey, bool _down) noexcept;

  /*
   * How many key presses are remembered. The game consumes one per blocking read and the pump
   * produces one per press, so the queue is normally empty or has one thing in it; the cap is
   * here so that holding a key down while a screen is drawing cannot grow it without bound. When
   * it is full the OLDEST is dropped, because the newest press is the one the player means.
   */
  static constexpr std::size_t MAX_QUEUED_KEYS = 16;

  /// 6502: KEYLOOK is 65 bytes, one per internal key number, which is also TRANTABLE's extent.
  static constexpr std::uint8_t KEY_COUNT = 65;

  HWND m_window = nullptr;
  HINSTANCE m_instance = nullptr;
  bool m_closed = false;
  bool m_resized = false;

  std::deque<std::uint8_t> m_pressed;
  bool m_held[KEY_COUNT] = {};
};

} // namespace Outpost
