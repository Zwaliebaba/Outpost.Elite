#include "pch.h"

#include "Application.h"

#include "AudioStream.h"
#include "DirectoryFileStore.h"
#include "Game.h"
#include "Platform.h"
#include "Presenter.h"
#include "Reference.h"
#include "Replay.h"
#include "StateDigest.h"
#include "Window.h"

#include <objbase.h>
#include <shlobj.h>

#include <format>
#include <fstream>
#include <system_error>

namespace Outpost
{

namespace
{

constexpr wchar_t TITLE[] = L"Outpost.Elite";
constexpr std::uint32_t LOGICAL_WIDTH = 640;  // the CGA's 640 dots across...
constexpr std::uint32_t LOGICAL_HEIGHT = 480; // ...shown at 4:3 (D8)
constexpr int START_SCALE = 2;
constexpr bool START_FULL_SCREEN = true; // borderless over the whole screen, F11 returning to a window START_SCALE times (D42)
constexpr std::uint32_t SAMPLE_RATE = 44'100;
constexpr std::uint64_t DIGEST_INTERVAL_MILLISECONDS = 10'000;
constexpr std::uint64_t LONGEST_CATCH_UP_MILLISECONDS = 250;
constexpr std::uint8_t BREAK_BIT = 0x80;

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

std::string SessionName()
{
  SYSTEMTIME now{};
  GetLocalTime(&now);
  return std::format("session-{:04}{:02}{:02}-{:02}{:02}{:02}", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
}

std::wstring Describe(Elite::StopReason _reason, const Elite::Platform& _platform)
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

int Application::Run(HINSTANCE _instance)
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
  Engine::Presenter presenter;
  if (!presenter.Create(window.Handle(), Elite::FrameImage::WIDTH_PIXELS, Elite::FrameImage::HEIGHT_PIXELS, LOGICAL_WIDTH, LOGICAL_HEIGHT))
  {
    Fail(L"Direct3D 12 is not available on this machine.");
    return 1;
  }
  Engine::AudioStream audio;
  (void)audio.Create(SAMPLE_RATE); // without a sound device the game runs silent

  const std::filesystem::path commanders = UserFolder(L"Commanders");
  const std::filesystem::path replays = UserFolder(L"Replays");
  Elite::DirectoryFileStore files(commanders.empty() ? std::filesystem::current_path() : commanders);
  Elite::Platform::Desc desc;
  desc.startMoment = Elite::START_MOMENT; // fixed, so the session's replay plays back exactly (ADR-008)
  // Every routine is native, in place of the original (ADR-010): the native platform runs them over the memory the GameState
  // owns (D19, D33).
  const auto game = std::make_unique<Elite::Game>(files, desc);
  if (!game->Load(Elite::PSP_SEGMENT))
  {
    Fail(L"The game would not load.");
    return 1;
  }
  Elite::Platform* const platform = &game->Host();

  const std::string session = SessionName();
  Elite::ReplayRecorder recorder(std::format("Recorded by Outpost, {}.\nPlay it with ReferenceRunner --replay; see ADR-008.", session));
  const Elite::Cycles start = platform->Clock();
  std::uint64_t elapsedMilliseconds = 0;
  std::uint64_t nextDigest = DIGEST_INTERVAL_MILLISECONDS;
  Elite::Cycles audioCycle = start;
  auto wallStart = std::chrono::steady_clock::now();
  const auto frame = std::make_unique<Elite::FrameImage>();
  std::vector<std::uint32_t> pixels(frame->pixels.size());
  std::vector<std::int16_t> samples;
  const auto palette = Elite::CgaPalette();
  const auto rgb = [&](std::uint8_t _index)
  {
    const Elite::RgbColor& color = palette[_index & 0x0F];
    return (std::uint32_t{color.red} << 16) | (std::uint32_t{color.green} << 8) | color.blue;
  };

  Elite::StopReason reason = Elite::StopReason::Reached;
  while (window.PumpMessages())
  {
    for (const Engine::KeyEvent& key : window.TakeKeyEvents())
    {
      // The XT has no E0 prefix: the cursor block and the right-hand Ctrl and Alt send the codes of the
      // keys they copy. A key the XT does not have is neither sent nor recorded.
      if (recorder.Key(elapsedMilliseconds, key.scanCode, key.down))
      {
        platform->KeyboardController().Inject(static_cast<std::uint8_t>(key.scanCode | (key.down ? 0 : BREAK_BIT)));
      }
    }
    if (window.TakeResized() && !presenter.Resize())
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
    while (reason == Elite::StopReason::Reached && nextDigest <= target)
    {
      reason = platform->RunUntil(Elite::ReplayCycle(start, nextDigest));
      recorder.Digest(nextDigest, std::format("t{}", nextDigest / 1000), Elite::TakeCheckpoint(game->State(), platform->Video()));
      nextDigest += DIGEST_INTERVAL_MILLISECONDS;
    }
    if (reason == Elite::StopReason::Reached)
    {
      reason = platform->RunUntil(Elite::ReplayCycle(start, target));
    }
    elapsedMilliseconds = target;
    if (reason != Elite::StopReason::Reached)
    {
      break;
    }

    const std::size_t count = Elite::Speaker::SampleCount(audioCycle, platform->Clock(), SAMPLE_RATE);
    samples.resize(count);
    samples.resize(platform->Sound().RenderSamples(audioCycle, platform->Clock(), SAMPLE_RATE, samples));
    audio.Submit(samples);
    audioCycle = platform->Clock();
    platform->Sound().DiscardBefore(Elite::Speaker::FirstSampleStart(audioCycle, SAMPLE_RATE));

    platform->Render(*frame);
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
      pixels[index] = rgb(frame->pixels[index]);
    }
    if (!presenter.Present(pixels, rgb(frame->borderColor)))
    {
      Fail(L"The display device was lost.");
      return 1;
    }
  }

  if (!replays.empty() && elapsedMilliseconds > DIGEST_INTERVAL_MILLISECONDS)
  {
    // The last digest is where the session ended, so the replay plays to the end, or to the same stop.
    recorder.Digest(elapsedMilliseconds, "end", Elite::TakeCheckpoint(game->State(), platform->Video()));
    std::ofstream out(replays / (session + ".replay"), std::ios::binary | std::ios::trunc);
    out << recorder.Text();
  }
  if (reason != Elite::StopReason::Reached && reason != Elite::StopReason::Terminated)
  {
    Fail(Describe(reason, *platform));
    return 1;
  }
  return 0;
}

} // namespace Outpost
