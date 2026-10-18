#include "pch.h"

#include "Application.h"

#include "AudioStream.h"
#include "Canvas.h"
#include "Cockpit.h"
#include "DirectoryFileStore.h"
#include "DockedBriefing.h"
#include "DockedChart.h"
#include "DockedDisc.h"
#include "DockedEquip.h"
#include "DockedFrame.h"
#include "DockedMarket.h"
#include "DockedStatus.h"
#include "Game.h"
#include "Host.h"
#include "Renderer.h"
#include "Replay.h"
#include "SpaceView.h"
#include "Window.h"

#include <objbase.h>
#include <shlobj.h>

#include <format>
#include <optional>
#include <system_error>

namespace Outpost
{

namespace
{

constexpr wchar_t TITLE[] = L"Outpost.Elite";
constexpr std::uint32_t LOGICAL_WIDTH = 640; // the window's first size, 4:3 (D8), START_SCALE times over
constexpr std::uint32_t LOGICAL_HEIGHT = 480;
constexpr int START_SCALE = 2;
constexpr bool START_FULL_SCREEN = true; // borderless over the whole screen, F11 returning to a window START_SCALE times (D42)
constexpr std::uint32_t SAMPLE_RATE = 44'100;
constexpr std::uint64_t LONGEST_CATCH_UP_MILLISECONDS = 250;
constexpr std::uint32_t COCKPIT_BACKGROUND = 0x000000; // round the view in the frame's window, and past the design at other shapes
constexpr std::uint32_t DOCKED_BACKGROUND = 0x070605;  // past the docked screens' design at other shapes (D82)

// The scan code a click on the docked key bar sends (D83): the key the bar names, which the docked host acts on (D96).
[[nodiscard]] std::optional<std::uint8_t> ScanCodeOf(DockedKey _key) noexcept
{
  switch (_key)
  {
  case DockedKey::Disc:
    return Elite::DockedKeyOf(Elite::DockedScreen::Disc);
  case DockedKey::Launch:
    return std::uint8_t{0x3B};
  case DockedKey::Market:
    return Elite::DockedKeyOf(Elite::DockedScreen::Market);
  case DockedKey::Equip:
    return Elite::DockedKeyOf(Elite::DockedScreen::Equip);
  case DockedKey::Galaxy:
    return Elite::DockedKeyOf(Elite::DockedScreen::Galaxy);
  case DockedKey::Local:
    return Elite::DockedKeyOf(Elite::DockedScreen::Local);
  case DockedKey::Status:
    return Elite::DockedKeyOf(Elite::DockedScreen::Status);
  }
  return std::nullopt;
}

// A click at _point on a screen the shell draws in the docked frame (D83): a key of the bar pressed and released, the flight's own
// on a chart in flight (D110), or the control under it named to the game. On a chart, the point is sent as the chart's (D99, D109);
// returns whether it was, so that a drag from it goes on moving the cursor.
bool ClickDocked(Elite::Host& _host, Engine::Point _point)
{
  if (const Elite::BriefingView* const briefing = _host.ShownBriefing())
  {
    // D09 waits for its key, which the bar's would answer as any other: only its own buttons send one (ADR-068).
    if (const std::optional<std::uint8_t> code = DockedBriefing::ScanCodeAt(*briefing, _point))
    {
      _host.Inject(Elite::KeyEvent{.key = *code, .down = true});
      _host.Inject(Elite::KeyEvent{.key = *code, .down = false});
    }
    return false;
  }
  const Elite::ChartView* const chart = _host.ShownChart();
  const Elite::MarketView* const market = _host.ShownMarket();
  const Elite::StatusView* const status = _host.ShownStatus();
  // In flight a chart, the market's prices and the status (ADR-069) map the bar to the flight's keys (D110).
  const bool flight = (chart != nullptr && chart->flight) || (market != nullptr && market->flight) || (status != nullptr && status->flight);
  if (const std::optional<DockedKey> key = DockedFrame::KeyAt(_point))
  {
    if (*key == DockedKey::Disc && _host.ShownDisc() != nullptr)
    {
      // D10's own key, lit: Esc would close it.
      return false;
    }
    const std::optional<std::uint8_t> code = flight ? DockedChart::FlightScanCodeOf(*key) : ScanCodeOf(*key);
    if (code)
    {
      _host.Inject(Elite::KeyEvent{.key = *code, .down = true});
      _host.Inject(Elite::KeyEvent{.key = *code, .down = false});
    }
    return false;
  }
  if (chart != nullptr)
  {
    const std::optional<Elite::ChartClick> click = DockedChart::ChartAt(chart->kind, _point);
    if (click)
    {
      _host.Click(*click);
    }
    return click.has_value();
  }
  if (market != nullptr)
  {
    if (market->flight)
    {
      // Nothing is traded in flight.
      return false;
    }
    if (const std::optional<Elite::MarketClick> click = DockedMarket::ControlAt(_point))
    {
      _host.Click(*click);
    }
  }
  else if (const Elite::EquipView* const equip = _host.ShownEquip())
  {
    if (const std::optional<Elite::EquipClick> click = DockedEquip::ControlAt(*equip, _point))
    {
      _host.Click(*click);
    }
  }
  else if (const Elite::DiscView* const disc = _host.ShownDisc())
  {
    if (const std::optional<Elite::DiscClick> click = DockedDisc::ControlAt(*disc, _point))
    {
      _host.Click(*click);
    }
  }
  return false;
}

// Whether a screen the shell draws in the docked frame is shown, which a click acts on.
[[nodiscard]] bool DockedFrameShown(const Elite::Host& _host) noexcept
{
  return _host.ShownMarket() != nullptr || _host.ShownEquip() != nullptr || _host.ShownStatus() != nullptr ||
         _host.ShownChart() != nullptr || _host.ShownDisc() != nullptr || _host.ShownBriefing() != nullptr;
}

// Initialises COM on this thread for XAudio2, and undoes it.
class ComScope
{
public:
  ComScope() noexcept
    : m_initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
  {
  }

  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

  ~ComScope()
  {
    if (m_initialized)
    {
      CoUninitialize();
    }
  }

private:
  bool m_initialized;
};

// Saved Games\Outpost.Elite\<_leaf>, made if it is missing; empty if Windows has no Saved Games folder.
std::filesystem::path UserFolder(const wchar_t* _leaf)
{
  PWSTR savedGames = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &savedGames)))
  {
    CoTaskMemFree(savedGames);
    return {};
  }
  std::filesystem::path folder = std::filesystem::path(savedGames) / L"Outpost.Elite" / _leaf;
  CoTaskMemFree(savedGames);
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  return error ? std::filesystem::path{} : folder;
}

std::wstring Describe(Elite::StopReason _reason, const Elite::Host& _platform)
{
  switch (_reason)
  {
  case Elite::StopReason::Spinning:
    return std::format(L"The game ran {} turns without waiting.", _platform.SpinLimit());
  case Elite::StopReason::Terminated:
  case Elite::StopReason::Reached:
    break;
  }
  return {};
}

} // namespace

void Application::Fail(const std::wstring& _message) const
{
  MessageBoxW(m_window, _message.c_str(), TITLE, MB_OK | MB_ICONERROR);
}

int Application::Run(HINSTANCE _instance, bool _cheat)
{
  const ComScope com;
  Engine::Window window;
  if (!window.Create(_instance, TITLE, static_cast<int>(LOGICAL_WIDTH) * START_SCALE, static_cast<int>(LOGICAL_HEIGHT) * START_SCALE,
                     START_FULL_SCREEN))
  {
    Fail(L"Windows would not create the game's window.");
    return 1;
  }
  m_window = window.Handle();
  Engine::Renderer renderer;
  if (!renderer.Create(Engine::Renderer::Desc{.warp = false}) || !renderer.AttachWindow(window.Handle()))
  {
    Fail(L"Direct3D 12 is not available on this machine.");
    return 1;
  }
  SpaceView spaceView;
  Engine::Canvas canvas;
  if (!spaceView.AddModels(renderer))
  {
    Fail(L"The display device would not take the ships' models.");
    return 1;
  }
  Engine::AudioStream audio;
  (void)audio.Create(SAMPLE_RATE); // without a sound device the game runs silent

  const std::filesystem::path commanders = UserFolder(L"Commanders");
  Elite::DirectoryFileStore files(commanders.empty() ? std::filesystem::current_path() : commanders);
  Elite::Host::Desc desc;
  desc.startMoment = Elite::START_MOMENT; // fixed, as on a PC/XT without a clock card
  desc.cheat = _cheat;
  // Every routine is native, in place of the original (ADR-010): the native platform runs them over the memory the GameState
  // owns (D19, D33).
  const auto game = std::make_unique<Elite::Game>(files, desc);
  game->Start();
  Elite::Host* const platform = &game->Host();

  const Elite::Milliseconds start = platform->Clock();
  std::uint64_t elapsedMilliseconds = 0;
  Elite::Milliseconds audioTime = start;
  auto wallStart = std::chrono::steady_clock::now();
  std::vector<std::int16_t> samples;

  const TextMeasure measure = [&renderer](const Engine::CanvasText& _text) { return renderer.MeasureText(_text); };
  std::optional<Engine::Point> pointer;     // the mouse, in design units, while it is over the window
  std::optional<Elite::ChartClick> dragged; // while the left button, pressed on a chart, is held: the last point it sent (D109)

  Elite::StopReason reason = Elite::StopReason::Reached;
  while (window.PumpMessages())
  {
    for (const Engine::KeyEvent& key : window.TakeKeyEvents())
    {
      // The XT has no E0 prefix: the cursor block and the right-hand Ctrl and Alt send the codes of the
      // keys they copy. A key the XT does not have is not sent.
      if (Elite::KeyNameOf(key.scanCode))
      {
        platform->Inject(Elite::KeyEvent{.key = key.scanCode, .down = key.down});
      }
    }
    for (const Engine::PointerEvent& event : window.TakePointerEvents())
    {
      if (event.kind == Engine::PointerEvent::Kind::Leave)
      {
        pointer.reset();
        continue;
      }
      if (event.kind == Engine::PointerEvent::Kind::LeftUp)
      {
        dragged.reset();
        continue;
      }
      RECT client{};
      GetClientRect(window.Handle(), &client);
      const Engine::DesignPlacement place =
        Engine::PlaceDesign(static_cast<std::uint32_t>(client.right - client.left), static_cast<std::uint32_t>(client.bottom - client.top));
      pointer = Engine::Point{.x = (static_cast<float>(event.x) - place.left) / place.scale,
                              .y = (static_cast<float>(event.y) - place.top) / place.scale};
      if (event.kind == Engine::PointerEvent::Kind::LeftDown && DockedFrameShown(*platform))
      {
        dragged = ClickDocked(*platform, *pointer) && platform->ShownChart() != nullptr
                    ? DockedChart::ChartAt(platform->ShownChart()->kind, *pointer)
                    : std::nullopt;
      }
      else if (event.kind == Engine::PointerEvent::Kind::Move && dragged)
      {
        // A drag moves the cursor with it, held to the chart's units, each new point sent once (D109).
        const Elite::ChartView* const chart = platform->ShownChart();
        const std::optional<Elite::ChartClick> at = chart != nullptr ? DockedChart::ChartAt(chart->kind, *pointer) : std::nullopt;
        if (chart == nullptr)
        {
          dragged.reset();
        }
        else if (at && (at->x != dragged->x || at->row != dragged->row))
        {
          platform->Click(*at);
          dragged = at;
        }
      }
    }
    if (window.TakeResized() && !renderer.Resize())
    {
      Fail(L"The display device was lost.");
      return 1;
    }

    // Paced time follows the wall clock (ADR-008), but never tries to make up more than a moment: a
    // window dragged for a second loses that second rather than racing through it.
    auto target = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - wallStart).count());
    if (target > elapsedMilliseconds + LONGEST_CATCH_UP_MILLISECONDS)
    {
      wallStart += std::chrono::milliseconds(target - elapsedMilliseconds - LONGEST_CATCH_UP_MILLISECONDS);
      target = elapsedMilliseconds + LONGEST_CATCH_UP_MILLISECONDS;
    }
    reason = platform->RunUntil(Elite::ReplayTime(start, target));
    elapsedMilliseconds = target;
    if (reason != Elite::StopReason::Reached)
    {
      break;
    }

    const std::size_t count = Elite::Speaker::SampleCount(audioTime, platform->Clock(), SAMPLE_RATE);
    samples.resize(count);
    samples.resize(platform->Sound().RenderSamples(audioTime, platform->Clock(), SAMPLE_RATE, samples));
    audio.Submit(samples);
    audioTime = platform->Clock();
    platform->Sound().DiscardBefore(Elite::Speaker::FirstSampleStart(audioTime, SAMPLE_RATE));

    // On the market, D01 (ADR-058); on the shipyard, D02 and D03 (ADR-064); on the status screen, D08 (ADR-063); on a chart, docked
    // or in flight, D04 to D07 (ADR-065); a briefing over D08, D09 (ADR-068); on Disc access, D10 (ADR-067); in flight and on the title, the cockpit with the space view
    // in its window (ADR-034, D63, D64), a tunnel's frame too (ADR-069); between them, while the game shows none of these, the last
    // canvas drawn again, with no scene.
    bool presented = false;
    const Elite::FlightSnapshot& latest = platform->LatestFlightSnapshot();
    if (const Elite::MarketView* const market = platform->ShownMarket())
    {
      DockedMarket::Draw(*market, pointer, measure, canvas);
      presented = renderer.Present(DOCKED_BACKGROUND, nullptr, &canvas);
    }
    else if (const Elite::BriefingView* const briefing = platform->ShownBriefing())
    {
      DockedBriefing::Draw(*briefing, pointer, measure, canvas);
      presented = renderer.Present(DOCKED_BACKGROUND, nullptr, &canvas);
    }
    else if (const Elite::EquipView* const equip = platform->ShownEquip())
    {
      DockedEquip::Draw(*equip, pointer, measure, canvas);
      presented = renderer.Present(DOCKED_BACKGROUND, nullptr, &canvas);
    }
    else if (const Elite::StatusView* const status = platform->ShownStatus())
    {
      DockedStatus::Draw(*status, pointer, measure, canvas);
      presented = renderer.Present(DOCKED_BACKGROUND, nullptr, &canvas);
    }
    else if (const Elite::ChartView* const chart = platform->ShownChart())
    {
      DockedChart::Draw(*chart, pointer, measure, canvas);
      presented = renderer.Present(DOCKED_BACKGROUND, nullptr, &canvas);
    }
    else if (const Elite::DiscView* const disc = platform->ShownDisc())
    {
      DockedDisc::Draw(*disc, pointer, measure, canvas);
      presented = renderer.Present(DOCKED_BACKGROUND, nullptr, &canvas);
    }
    else if (latest.sequence != 0 && platform->SpaceViewShown())
    {
      Cockpit::Draw(latest.dashboard, latest.sight, elapsedMilliseconds, renderer, canvas);
      SpaceView::DrawWords(*platform, Cockpit::VIEW, canvas);
      presented = renderer.Present(COCKPIT_BACKGROUND, spaceView.Frame(*platform, Cockpit::VIEW, Cockpit::WINDOW), &canvas);
    }
    else
    {
      presented = renderer.Present(COCKPIT_BACKGROUND, nullptr, &canvas);
    }
    if (!presented)
    {
      Fail(L"The display device was lost.");
      return 1;
    }
  }

  if (reason != Elite::StopReason::Reached && reason != Elite::StopReason::Terminated)
  {
    Fail(Describe(reason, *platform));
    return 1;
  }
  return 0;
}

} // namespace Outpost
