#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>

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

    [[nodiscard]] HWND Handle() const noexcept
    {
      return m_window;
    }

    /// Dispatches everything waiting and returns false once the window has closed. It does NOT
    /// block: the vsync wait belongs to `ScreenPresenter::Present`, so that a frame is what paces
    /// the loop rather than a timer.
    [[nodiscard]] bool Pump() noexcept;

    [[nodiscard]] bool Closed() const noexcept
    {
      return m_closed;
    }

    void ClientSize(int& _outWidth, int& _outHeight) const noexcept;

    /*
     * `thiskey` for `TT102` -- the C64 internal key NUMBER pressed since the last time this
     * was asked, or `NO_KEY`, and it is an EDGE: one answer per press of a key, however long it is
     * held, and never an auto-repeat (InputTimer.md I-1).
     *
     * A number and not a character, because that is what the game's own keyboard scan produces and
     * what `TT102` compares against; `Outpost::CharacterFor` is the other half, and `KeyMap.h` says
     * why there are two. The QUEUE that stood here until I-1 -- sixteen deep, one entry per
     * `WM_KEYDOWN` including the repeats -- is what delivered a held RETURN to the next prompt and
     * typed a held digit twice; the blocking read is `Elite::ReadKey` over `Held` now, and this is
     * the one press the dispatch takes per step. Two presses between two steps keep the later one,
     * which is what a matrix scanned once a pass would have seen.
     */
    [[nodiscard]] std::uint8_t TakePressed() noexcept;

    /// Whether a key is held right now, for the polling idiom `DOKEY` uses.
    [[nodiscard]] bool Held(std::uint8_t _c64Key) const noexcept;

    /// True once, if the client area has changed since the last time this was asked. The presenter
    /// resizes its buffers on it.
    [[nodiscard]] bool TakeResize() noexcept;

    /*
     * Does this window have the player's attention? (Design/Platform.md §3.2, slice T-1.)
     *
     * THE SCHEDULER'S AUTO-PAUSE READS THIS, and it is the whole of what a windowed player means
     * by pause: an inactive window plans no steps and BANKS no time, so alt-tabbing away for a
     * minute leaves the game where it was rather than owing it a minute. The keys are already
     * released on the same messages (I-6), so nothing is held across the gap either.
     *
     * Minimised counts as inactive on its own message rather than by asking for the client size,
     * because the size is asked for further down the turn than the clock is read.
     */
    [[nodiscard]] bool Active() const noexcept
    {
      return m_active;
    }

    /// A diagnostic the player should read before the game goes on -- a settings line it could not
    /// use, for instance -- as a box owned by this window. An empty text shows nothing.
    void Warn(const std::string& _text) const noexcept;

    /*
     * How many backlogs the scheduler has dropped, in the title bar (ADR-005 §3, Platform.md T-1).
     *
     * A DEBUG BUILD'S ONLY, and the composition root is what guards it. ADR-005 §3 has asked for a
     * stall to be logged since the ADR was written, and the honest note in `Main.cpp` was that a
     * windowed build had nowhere to report one to; the debugger's output is one place and this is
     * the other, and this is the one somebody PLAYING can see.
     */
    void ShowStallCount(std::uint32_t _stalls) const noexcept;

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

    /// `_repeat` is lParam's bit 30 -- the key was already down -- which Windows sets on every
    /// auto-repeat; a repeat moves nothing here.
    void PressKey(WPARAM _virtualKey, bool _down, bool _repeat) noexcept;

    /// Every key up and the pending press forgotten: what focus loss means, because no WM_KEYUP
    /// follows it.
    void ReleaseAllKeys() noexcept;

    /// KEYLOOK is 65 bytes, one per internal key number, which is also TRANTABLE's extent.
    static constexpr std::uint8_t KEY_COUNT = 65;

    HWND m_window = nullptr;
    HINSTANCE m_instance = nullptr;
    bool m_closed = false;
    bool m_resized = false;

    /// See `Active`. True until a message says otherwise: a window that has just been created has
    /// had no focus message yet and is not paused.
    bool m_active = true;

    std::uint8_t m_pressed = 0; ///< the last press since `TakePressed`, or 0
    bool m_held[KEY_COUNT] = {};
  };

} // namespace Outpost
