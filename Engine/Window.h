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

/// The game's window: a resizable top-level window whose client area the Presenter draws into. It reports
/// keys, including the ones Windows keeps for itself (Alt, F10), and releases every held key when it loses
/// the focus, so nothing sticks down. F11 switches between a window and the whole screen.
class Window
{
public:
  Window() noexcept = default;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window();

  /// Creates and shows the window with a client area of the given size. Returns false if Windows refuses.
  [[nodiscard]] bool Create(HINSTANCE _instance, const wchar_t* _title, int _clientWidth, int _clientHeight);

  /// Handles every message waiting. Returns false once the window has been closed.
  [[nodiscard]] bool PumpMessages();

  /// The keys reported since the last call, oldest first.
  [[nodiscard]] std::vector<KeyEvent> TakeKeyEvents();

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
  void ToggleFullScreen();

  HWND m_window = nullptr;
  bool m_open = false;
  bool m_resized = false;
  bool m_fullScreen = false;
  WINDOWPLACEMENT m_windowedPlacement{};
  std::vector<KeyEvent> m_events;
  std::vector<KeyEvent> m_held;
};

} // namespace Engine
