// Engine/Window.h
#pragma once

#include "Win32.h"

#include <cstdint>
#include <vector>

namespace Engine
{

/// A key pressed or released, as the keyboard reported it: the scan code set 1 make code, and whether
/// the key sends the E0 prefix (the cursor block, the right-hand Ctrl and Alt).
struct KeyEvent
{
  std::uint8_t scanCode = 0;
  bool extended = false;
  bool down = false;
  bool repeat = false; ///< a make the keyboard repeated while the key was held
};

/// The mouse over the client area (D83): moved, its left button pressed, or gone from the window. x and y are physical pixels
/// from the client area's top-left corner.
struct PointerEvent
{
  enum class Kind : std::uint8_t
  {
    Move,
    LeftDown,
    LeftUp, ///< the left button released, or its press ended by Windows taking the mouse away; then at 0, 0
    Leave,
  };

  Kind kind = Kind::Move;
  int x = 0;
  int y = 0;
};

/// The game's window: a resizable top-level window whose client area the Renderer draws into. It reports
/// keys, including the ones Windows keeps for itself (Alt, F10), and releases every held key when it loses
/// the focus, so nothing sticks down. F11 switches between a window and the whole screen, borderless.
///
/// It expects a process that is per-monitor DPI aware (version 2), as the executable's manifest declares, so
/// every size is in physical pixels. Moved to a screen of another scale, the window takes the size Windows
/// suggests; full screen, it covers the whole of the screen it is on.
class Window
{
public:
  Window() noexcept = default;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window();

  /// Creates and shows the window with a client area of the given size in physical pixels, or, with _fullScreen, shows it
  /// over the whole of the screen it would have opened on, borderless, F11 returning it to that size. Returns false if
  /// Windows refuses.
  [[nodiscard]] bool Create(HINSTANCE _instance, const wchar_t* _title, int _clientWidth, int _clientHeight, bool _fullScreen);

  /// Handles every message waiting. Returns false once the window has been closed.
  [[nodiscard]] bool PumpMessages();

  /// The keys reported since the last call, oldest first.
  [[nodiscard]] std::vector<KeyEvent> TakeKeyEvents();

  /// The mouse's events since the last call, oldest first.
  [[nodiscard]] std::vector<PointerEvent> TakePointerEvents();

  /// Whether the client area changed size since the last call.
  [[nodiscard]] bool TakeResized() noexcept;

  [[nodiscard]] HWND Handle() const noexcept
  {
    return m_window;
  }

private:
  static LRESULT CALLBACK WindowProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam);
  LRESULT HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam);
  void ReleaseHeldKeys();
  void ApplyDpiChange(const RECT& _suggested);
  void ToggleFullScreen();

  HWND m_window = nullptr;
  bool m_open = false;
  bool m_resized = false;
  bool m_fullScreen = false;
  WINDOWPLACEMENT m_windowedPlacement{};
  std::vector<KeyEvent> m_events;
  std::vector<KeyEvent> m_held;
  std::vector<PointerEvent> m_pointerEvents;
  bool m_trackingLeave = false; // TrackMouseEvent asked for the WM_MOUSELEAVE of the mouse now over the window
  bool m_pressed = false;       // the left button went down in the window, which holds the capture until it comes up
};

} // namespace Engine
