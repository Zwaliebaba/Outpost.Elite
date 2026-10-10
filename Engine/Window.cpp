#include "pch.h"

#include "Window.h"

#include <algorithm>
#include <bit>

namespace Engine
{

namespace
{

constexpr wchar_t CLASS_NAME[] = L"OutpostEliteWindow";
constexpr LPARAM SCAN_CODE_SHIFT = 16;
constexpr LPARAM SCAN_CODE_MASK = 0xFF;
constexpr LPARAM EXTENDED_BIT = LPARAM{1} << 24;
constexpr LPARAM CONTEXT_BIT = LPARAM{1} << 29; // Alt is down
constexpr LPARAM PREVIOUS_STATE_BIT = LPARAM{1} << 30;

KeyEvent EventOf(LPARAM _lParam, bool _down) noexcept
{
  KeyEvent event;
  event.scanCode = static_cast<std::uint8_t>((_lParam >> SCAN_CODE_SHIFT) & SCAN_CODE_MASK);
  event.extended = (_lParam & EXTENDED_BIT) != 0;
  event.down = _down;
  event.repeat = _down && (_lParam & PREVIOUS_STATE_BIT) != 0;
  return event;
}

bool SameKey(const KeyEvent& _a, const KeyEvent& _b) noexcept
{
  return _a.scanCode == _b.scanCode && _a.extended == _b.extended;
}

} // namespace

Window::~Window()
{
  if (m_window != nullptr)
  {
    SetWindowLongPtrW(m_window, GWLP_USERDATA, 0);
    DestroyWindow(m_window);
  }
}

bool Window::Create(HINSTANCE _instance, const wchar_t* _title, int _clientWidth, int _clientHeight)
{
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = &Window::WindowProc;
  windowClass.hInstance = _instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = CLASS_NAME;
  if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
  {
    return false;
  }

  RECT frame{0, 0, _clientWidth, _clientHeight};
  AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
  if (CreateWindowExW(0, CLASS_NAME, _title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left,
                      frame.bottom - frame.top, nullptr, nullptr, _instance, this) == nullptr)
  {
    return false;
  }
  m_open = true;
  ShowWindow(m_window, SW_SHOWNORMAL);
  UpdateWindow(m_window);
  return true;
}

bool Window::PumpMessages()
{
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
  {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return m_open;
}

std::vector<KeyEvent> Window::TakeKeyEvents()
{
  std::vector<KeyEvent> events;
  events.swap(m_events);
  return events;
}

bool Window::TakeResized() noexcept
{
  const bool resized = m_resized;
  m_resized = false;
  return resized;
}

LRESULT CALLBACK Window::WindowProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  if (_message == WM_NCCREATE)
  {
    // The first message: keep the window and its owner, so this message and every later one reach
    // HandleMessage with m_window already set.
    const auto* create = std::bit_cast<const CREATESTRUCTW*>(_lParam);
    auto* owner = static_cast<Window*>(create->lpCreateParams);
    owner->m_window = _window;
    SetWindowLongPtrW(_window, GWLP_USERDATA, std::bit_cast<LONG_PTR>(owner));
  }
  auto* self = std::bit_cast<Window*>(GetWindowLongPtrW(_window, GWLP_USERDATA));
  if (self == nullptr)
  {
    return DefWindowProcW(_window, _message, _wParam, _lParam);
  }
  return self->HandleMessage(_message, _wParam, _lParam);
}

LRESULT Window::HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  switch (_message)
  {
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
  {
    if (_wParam == VK_F4 && (_lParam & CONTEXT_BIT) != 0)
    {
      break; // Alt+F4 closes the window as everywhere else
    }
    const KeyEvent event = EventOf(_lParam, true);
    if (_wParam == VK_F11)
    {
      if (!event.repeat)
      {
        ToggleFullScreen();
      }
      return 0;
    }
    if (event.scanCode == 0)
    {
      return 0;
    }
    if (std::none_of(m_held.begin(), m_held.end(), [&](const KeyEvent& _held) { return SameKey(_held, event); }))
    {
      m_held.push_back(event);
    }
    m_events.push_back(event);
    return 0; // Alt and F10 do not open the window menu
  }
  case WM_KEYUP:
  case WM_SYSKEYUP:
  {
    const KeyEvent event = EventOf(_lParam, false);
    if (_wParam == VK_F11 || event.scanCode == 0)
    {
      return 0;
    }
    std::erase_if(m_held, [&](const KeyEvent& _held) { return SameKey(_held, event); });
    m_events.push_back(event);
    return 0;
  }
  case WM_SYSCHAR:
    return 0; // no beep for Alt with a letter
  case WM_KILLFOCUS:
    ReleaseHeldKeys();
    return 0;
  case WM_SIZE:
    m_resized = true;
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_CLOSE:
    DestroyWindow(m_window);
    return 0;
  case WM_DESTROY:
    m_open = false;
    m_window = nullptr;
    return 0;
  default:
    break;
  }
  return DefWindowProcW(m_window, _message, _wParam, _lParam);
}

void Window::ReleaseHeldKeys()
{
  for (KeyEvent event : m_held)
  {
    event.down = false;
    event.repeat = false;
    m_events.push_back(event);
  }
  m_held.clear();
}

void Window::ToggleFullScreen()
{
  if (!m_fullScreen)
  {
    m_windowedPlacement.length = sizeof(m_windowedPlacement);
    GetWindowPlacement(m_window, &m_windowedPlacement);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST), &monitor))
    {
      return;
    }
    SetWindowLongPtrW(m_window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(m_window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top, monitor.rcMonitor.right - monitor.rcMonitor.left,
                 monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    m_fullScreen = true;
  }
  else
  {
    SetWindowLongPtrW(m_window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetWindowPlacement(m_window, &m_windowedPlacement);
    SetWindowPos(m_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    m_fullScreen = false;
  }
  m_resized = true;
}

} // namespace Engine
