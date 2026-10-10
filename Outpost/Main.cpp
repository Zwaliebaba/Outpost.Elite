#include "pch.h"

#include "Application.h"

int WINAPI wWinMain(_In_ HINSTANCE _instance, _In_opt_ HINSTANCE _previous, _In_ LPWSTR _commandLine, _In_ int _show)
{
  (void)_previous;
  (void)_commandLine;
  (void)_show;
  Outpost::Application application;
  return application.Run(_instance);
}
