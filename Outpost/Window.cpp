#include "pch.h"

#include "Window.h"

#include "KeyMap.h"
#include "Presentation.h"

#include "Canvas.h"

namespace Outpost
{

namespace
{
constexpr wchar_t WINDOW_CLASS[] = L"OutpostEliteWindow";
constexpr wchar_t WINDOW_TITLE[] = L"Elite";
} // namespace

Window::~Window()
{
  if (m_window != nullptr)
  {
    DestroyWindow(m_window);
    m_window = nullptr;
  }
  if (m_instance != nullptr)
  {
    UnregisterClassW(WINDOW_CLASS, m_instance);
  }
}

void Window::Create(HINSTANCE _instance, int _scale)
{
  m_instance = _instance;

  WNDCLASSEXW description{};
  description.cbSize = sizeof(description);
  description.style = CS_HREDRAW | CS_VREDRAW;
  description.lpfnWndProc = &Window::Dispatch;
  description.hInstance = _instance;
  description.hCursor = LoadCursorW(nullptr, IDC_ARROW);

  // No background brush. Every pixel of the client area is painted by the presenter, and letting
  // the system erase it first is the classic source of a white flash on resize.
  description.hbrBackground = nullptr;
  description.lpszClassName = WINDOW_CLASS;

  if (RegisterClassExW(&description) == 0)
  {
    winrt::check_hresult(HRESULT_FROM_WIN32(GetLastError()));
  }

  // The client area is the canvas at an integer scale; AdjustWindowRect turns that into the outer
  // size, which is what CreateWindowEx takes. Asking for the outer size directly would give a
  // client area smaller than the canvas by the frame, and the first thing the player would see is
  // a letterboxed picture in a window sized for an unletterboxed one.
  RECT wanted{ 0, 0, Elite::Canvas::WIDTH * _scale, Elite::Canvas::HEIGHT * _scale };
  const DWORD style = WS_OVERLAPPEDWINDOW;
  if (AdjustWindowRectEx(&wanted, style, FALSE, 0) == 0)
  {
    winrt::check_hresult(HRESULT_FROM_WIN32(GetLastError()));
  }

  m_window = CreateWindowExW(0, WINDOW_CLASS, WINDOW_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             wanted.right - wanted.left, wanted.bottom - wanted.top, nullptr, nullptr,
                             _instance, this);
  if (m_window == nullptr)
  {
    winrt::check_hresult(HRESULT_FROM_WIN32(GetLastError()));
  }

  ShowWindow(m_window, SW_SHOW);
}

LRESULT CALLBACK Window::Dispatch(HWND _window, UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept
{
  /*
   * WM_NCCREATE is where the instance pointer arrives, and it is the FIRST message a window gets
   * -- so every message after it finds a pointer and the ones before it (there are none for a
   * window created this way) would not.
   */
  if (_message == WM_NCCREATE)
  {
    const CREATESTRUCTW* creation = reinterpret_cast<const CREATESTRUCTW*>(_lparam);
    SetWindowLongPtrW(_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));

    // And the handle itself, so that `Handle()` and `GetClientRect` work from the first message
    // rather than from whenever CreateWindowEx gets round to returning.
    Window* creating = reinterpret_cast<Window*>(creation->lpCreateParams);
    if (creating != nullptr)
    {
      creating->m_window = _window;
    }
  }

  Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(_window, GWLP_USERDATA));
  if (self != nullptr)
  {
    return self->OnMessage(_window, _message, _wparam, _lparam);
  }
  return DefWindowProcW(_window, _message, _wparam, _lparam);
}

LRESULT Window::OnMessage(HWND _window, UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept
{
  switch (_message)
  {
    case WM_CLOSE:
      m_closed = true;
      PostQuitMessage(0);
      return 0;

    case WM_DESTROY:
      m_closed = true;
      m_window = nullptr;
      PostQuitMessage(0);
      return 0;

    case WM_SIZE:
      m_resized = true;
      return 0;

    /*
     * WM_SYSKEYDOWN as well as WM_KEYDOWN, because the function keys reach it when Alt is held
     * and F10 reaches it always. Falling through to DefWindowProc afterwards is deliberate for
     * the SYS pair: Alt+F4 has to keep working.
     */
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      PressKey(_wparam, true);
      if (_message == WM_KEYDOWN)
      {
        return 0;
      }
      break;

    case WM_KEYUP:
    case WM_SYSKEYUP:
      PressKey(_wparam, false);
      if (_message == WM_KEYUP)
      {
        return 0;
      }
      break;

    /*
     * The system menu on F10 and on a bare Alt press, which would otherwise swallow the next key
     * press and leave the game looking wedged. Everything else the system menu does is left
     * alone.
     */
    case WM_SYSCOMMAND:
      if ((_wparam & 0xFFF0u) == SC_KEYMENU)
      {
        return 0;
      }
      break;

    default:
      break;
  }

  return DefWindowProcW(_window, _message, _wparam, _lparam);
}

void Window::PressKey(WPARAM _virtualKey, bool _down) noexcept
{
  const std::uint8_t key = C64KeyFor(static_cast<int>(_virtualKey));
  if (key == NO_KEY || key >= KEY_COUNT)
  {
    return;
  }

  m_held[key] = _down;
  if (!_down)
  {
    return;
  }

  /*
   * AUTO-REPEAT IS KEPT, and that is a choice rather than an oversight. Windows sends repeated
   * WM_KEYDOWNs while a key is held, and the game's own keyboard scan repeats too -- holding a
   * cursor key is how the crosshairs are moved across the chart. So a repeat is a key press.
   */
  if (m_pressed.size() >= MAX_QUEUED_KEYS)
  {
    m_pressed.pop_front();
  }
  m_pressed.push_back(key);
}

bool Window::Pump() noexcept
{
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0)
  {
    if (message.message == WM_QUIT)
    {
      m_closed = true;
      return false;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return !m_closed;
}

void Window::ClientSize(int& _outWidth, int& _outHeight) const noexcept
{
  _outWidth = 0;
  _outHeight = 0;
  RECT client{};
  if (m_window != nullptr && GetClientRect(m_window, &client) != 0)
  {
    _outWidth = static_cast<int>(client.right - client.left);
    _outHeight = static_cast<int>(client.bottom - client.top);
  }
}

bool Window::TakeKey(std::uint8_t& _outKey) noexcept
{
  if (m_pressed.empty())
  {
    return false;
  }
  _outKey = m_pressed.front();
  m_pressed.pop_front();
  return true;
}

bool Window::Held(std::uint8_t _c64Key) const noexcept
{
  return (_c64Key < KEY_COUNT) && m_held[_c64Key];
}

void Window::FlushKeys() noexcept
{
  m_pressed.clear();
}

bool Window::TakeResize() noexcept
{
  const bool resized = m_resized;
  m_resized = false;
  return resized;
}

} // namespace Outpost
