#include "pch.h"

#include "Game.h"

#include "Input.h"
#include "StartUp.h"
#include "Timer.h"

namespace Elite
{

Game::Game(FileStore& _files, const Host::Desc& _desc)
  : m_host(m_state, _files, _desc)
{
}

void Game::Start()
{
  m_state.Start();
  GameState* const state = &m_state;
  Elite::Host* const host = &m_host;
  m_host.SetEntry([state, host]() -> Task<void> { co_await Elite::Start(*state, *host); });
  m_host.SetTickHandler([state, host] { TimerInterrupt(*state, *host); });
  m_host.SetKeyHandler([state](const KeyEvent& _event) { KeyboardInterrupt(*state, _event); });
}

} // namespace Elite
