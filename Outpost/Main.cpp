#include "pch.h"

#include "Application.h"

#include <string_view>

namespace
{

// Whether the command line holds --cheat as one of its words: the game's cheat, where the original read ' cheat' (D80).
[[nodiscard]] bool AsksForCheat(std::wstring_view _commandLine) noexcept
{
  constexpr std::wstring_view CHEAT = L"--cheat";
  for (std::size_t at = _commandLine.find(CHEAT); at != std::wstring_view::npos; at = _commandLine.find(CHEAT, at + 1))
  {
    const bool startsWord = at == 0 || _commandLine[at - 1] == L' ';
    const std::size_t end = at + CHEAT.size();
    const bool endsWord = end == _commandLine.size() || _commandLine[end] == L' ';
    if (startsWord && endsWord)
    {
      return true;
    }
  }
  return false;
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE _instance, _In_opt_ HINSTANCE _previous, _In_ LPWSTR _commandLine, _In_ int _show)
{
  (void)_previous;
  (void)_show;
  Outpost::Application application;
  return application.Run(_instance, _commandLine != nullptr && AsksForCheat(_commandLine));
}
