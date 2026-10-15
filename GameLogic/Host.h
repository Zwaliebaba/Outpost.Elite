// GameLogic/Host.h
#pragma once

#include "Cga.h"
#include "FileStore.h"
#include "FlightSnapshot.h"
#include "MarketView.h"
#include "Mouse.h"
#include "KeyboardState.h"
#include "SpaceViewLayers.h"
#include "Speaker.h"
#include "Task.h"
#include "Timing.h"

#include <array>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace Elite
{

class GameState;

/// Why RunUntil returned.
enum class StopReason : std::uint8_t
{
  Reached,    ///< The clock reached the time asked for.
  Terminated, ///< The program ended: its start routine returned, to DOS's int 20h.
  Spinning    ///< The program ran SpinLimit() turns without waiting once.
};

/// The game's tick: its 1 kHz work, the music, the effects and the counters (TimerInterrupt), run to its end.
using TickRoutine = std::function<void()>;

/// The game's handler of a key event (KeyboardInterrupt), run to its end.
using KeyRoutine = std::function<void(const KeyEvent&)>;

/// The program's start (ADR-010), a coroutine that waits (D77): the Host resumes it each run until it ends.
using ProgramRoutine = std::function<Task<void>()>;

/// The host the game runs on (D90): its clock and its waits, the keys and the mouse, the sound, the CGA and the files. Every
/// routine that waits or drives a device is given it beside the GameState, which holds the game's memory.
///
/// It holds the clock and what it waits for: the game's tick, the keys the host queues, and the CGA's retrace; the sound, the
/// tone and its two bits (Speaker); the mouse (Mouse); the CGA (Cga), whose modes the game sets on it; and the file store that
/// holds the commander files (FileStore, D78).
///
/// Time is whole milliseconds (Timing.h, D87), and it is paced (ADR-008): the game's work takes no time. The clock moves only
/// while the game waits, and then straight to the next thing that falls due: the next tick, the next key or the CGA's next
/// change of status, or the end of the run.
///
/// Waiting is declared (ADR-039). Each wait of the game polls for what it waits for, and when a poll finds nothing it says
/// so (Wait): the clock moves to the next event. A poll that took something it does not want, and a loop that never ends, say
/// that instead (Spin), and the clock stands still.
///
/// What falls due runs when the wait goes on, and only then (D91): the game's tick work, while the tick runs (SetTickRunning),
/// and then one key event, to the game's handler while it takes them (SetKeysToGame) and dropped otherwise. There are no
/// interrupts: nothing runs inside the game's work, so nothing of it needs guarding.
///
/// The game runs as a coroutine on the host's thread (D77): the program's start routine, and every routine it calls that waits,
/// each a Task that co_awaits the next. When the clock reaches the end of a run inside a wait, the wait suspends the game and
/// RunUntil returns; the next RunUntil resumes it where it stopped. There is one thread, and a run is a function of the program,
/// its start moment and its inputs: there is no wall clock anywhere.
class Host
{
public:
  /// How the Host powers on.
  struct Desc
  {
    DosDateTime startMoment;   ///< The date and time at power-on, which the commander files' stamps count from.
    bool mousePresent = false; ///< Whether a mouse is attached (Mouse::Present).
    bool cheat = false;        ///< Whether the game starts with the cheat on: the shell's --cheat, where the original read ' cheat' (D80).
  };

  /// A native routine at an address, and how often it ran.
  template <typename Routine> struct Hooked
  {
    Routine routine;
    std::uint64_t calls = 0;
  };
  using HookedTick = Hooked<TickRoutine>;
  using HookedKeys = Hooked<KeyRoutine>;
  using HookedProgram = Hooked<ProgramRoutine>;

  /// What a wait's poll that found nothing gives the routine that waits (Wait), to co_await: ready, unless the clock reached the
  /// run's end, when the game is suspended until the next RunUntil. Once it goes on, what fell due runs.
  class [[nodiscard]] RunAwaiter
  {
  public:
    explicit RunAwaiter(Host& _host) noexcept
      : m_host(_host)
    {
    }

    [[nodiscard]] bool await_ready() const noexcept
    {
      return m_host.m_clock < m_host.m_runLimit;
    }

    void await_suspend(std::coroutine_handle<> _waiting) noexcept
    {
      m_host.m_suspended = _waiting;
    }

    void await_resume() const
    {
      m_host.RunDue();
    }

  private:
    Host& m_host;
  };

  /// Steps the default spin limit allows: far more than the longest stretch of work the game does between two waits, far
  /// fewer than a host would run before someone notices.
  static constexpr std::uint64_t DEFAULT_SPIN_LIMIT = 50'000'000;

  /// Powers on: every device in its power-on state, the CGA with every register clear until the game sets its mode, the tick
  /// stopped, and the keys not yet the game's. _state is the game's memory and _files where the commander files are;
  /// both must outlive the Host. Not noexcept: the devices allocate.
  Host(GameState& _state, FileStore& _files, const Desc& _desc);
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;
  ~Host();

  // ── The program ──

  /// Whether the game was started with the cheat on (Desc::cheat).
  [[nodiscard]] bool CheatRequested() const noexcept
  {
    return m_cheat;
  }

  /// _routine is the program's start: the Host makes the coroutine at the first RunUntil and resumes
  /// it each run; when it returns, the program has ended. Throws std::logic_error if the program has one already.
  void SetEntry(ProgramRoutine _routine);

  /// _routine is the game's tick work, run at each tick while the tick runs (SetTickRunning). Throws std::logic_error if the
  /// game has one already.
  void SetTickHandler(TickRoutine _routine);

  /// _routine is the game's handler of a key event, run with each event while the keys are the game's (SetKeysToGame). Throws
  /// std::logic_error if the game has one already.
  void SetKeyHandler(KeyRoutine _routine);

  /// The program's start routine, and how often it ran.
  [[nodiscard]] const HookedProgram& Entry() const noexcept
  {
    return m_entry;
  }

  /// The game's tick work, and how often it ran.
  [[nodiscard]] const HookedTick& TickHandler() const noexcept
  {
    return m_tickHandler;
  }

  /// The game's handler of key events, and how often it ran.
  [[nodiscard]] const HookedKeys& KeyHandler() const noexcept
  {
    return m_keyHandler;
  }

  // ── Running ──

  /// Runs until the clock reaches _time, or until the program can go no further.
  [[nodiscard]] StopReason RunUntil(Milliseconds _time);

  /// The number of steps the program may run without waiting before RunUntil stops with StopReason::Spinning: its spins
  /// (Spin). A loop that spins for ever would otherwise run with the clock standing
  /// still.
  void SetSpinLimit(std::uint64_t _turns) noexcept
  {
    m_spinLimit = _turns;
  }

  [[nodiscard]] std::uint64_t SpinLimit() const noexcept
  {
    return m_spinLimit;
  }

  /// The Host's clock.
  [[nodiscard]] Milliseconds Clock() const noexcept
  {
    return m_clock;
  }

  /// The ticks that have fallen due since power-on.
  [[nodiscard]] std::uint64_t TicksRun() const noexcept
  {
    return m_ticksRun;
  }

  // ── The devices, for the hosts: the keys, the mouse, the picture and the sound ──

  /// A key event from the host, queued: one is delivered each millisecond, in order (D88).
  void Inject(const KeyEvent& _event);

  /// The key events queued and not yet delivered.
  [[nodiscard]] std::size_t QueuedKeys() const noexcept
  {
    return m_keys.size();
  }

  /// The speaker's two bits as last set: bit 0 the tone's gate, bit 1 its data. Both clear at power-on.
  [[nodiscard]] std::uint8_t SpeakerBits() const noexcept
  {
    return m_speakerBits;
  }

  /// Bits 0 and 1 of _bits to the speaker, the tone's gate and its data; the other bits are not the speaker's. Not noexcept:
  /// the speaker logs a change.
  void SetSpeakerBits(std::uint8_t _bits);

  /// The game's tick started, a millisecond from now and every millisecond after (D87), or stopped.
  void SetTickRunning(bool _running) noexcept;

  /// The keys given to the game's handler from now on, or dropped as they come.
  void SetKeysToGame(bool _toGame) noexcept
  {
    m_keysToGame = _toGame;
  }

  /// The mouse, which the host moves and the game reads (ADR-051).
  [[nodiscard]] Mouse& MouseDevice() noexcept
  {
    return m_mouse;
  }

  [[nodiscard]] Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  [[nodiscard]] Cga& Video() noexcept
  {
    return m_cga;
  }

  /// The picture the CGA shows now, from the GameState's video memory.
  void Render(FrameImage& _image) const noexcept;

  /// The snapshot of the last frame of flight the game published (ADR-032), and the one before it: a renderer draws between the
  /// two. Each is empty, sequence 0, until one is published.
  [[nodiscard]] const FlightSnapshot& LatestFlightSnapshot() const noexcept
  {
    return m_flightSnapshots[1];
  }

  [[nodiscard]] const FlightSnapshot& PreviousFlightSnapshot() const noexcept
  {
    return m_flightSnapshots[0];
  }

  /// _snapshot published as the latest: numbered, stamped with the clock, and, when _continues, made continuous with the latest
  /// before it (ContinueFrom), which becomes the previous.
  void PublishFlightSnapshot(const FlightSnapshot& _snapshot, bool _continues) noexcept;

  /// _snapshot published as the latest frame of the game's own picture of the view, a tunnel's (D68): as PublishFlightSnapshot
  /// publishes it, continuing nothing, with the layers holding the whole view as the screen shows it (SpaceViewLayers::FinishPicture).
  void PublishSpaceViewPicture(const FlightSnapshot& _snapshot) noexcept;

  /// _view published as what the market screen, D01, shows now (ADR-058): numbered, one more than the last. While one is shown,
  /// the shell draws it in place of the CGA picture.
  void ShowMarket(MarketView _view);

  /// D01 no longer shown: the market screen's routine ended. The clicks not yet taken go with it.
  void HideMarket() noexcept;

  /// The market screen's view while it is shown, or nothing.
  [[nodiscard]] const MarketView* ShownMarket() const noexcept
  {
    return m_marketShown ? &m_marketView : nullptr;
  }

  /// A click on one of D01's controls, from the shell (D83), queued for the market screen; dropped while it is not shown.
  void Click(const MarketClick& _click);

  /// The oldest click queued, taken, or nothing.
  [[nodiscard]] std::optional<MarketClick> TakeClick() noexcept;

  /// The space view's layers as the last published snapshot's frame drew them (ADR-033): what a renderer that draws the ships
  /// itself draws under and over them. PublishFlightSnapshot finishes them and marks each object drawn as a model.
  [[nodiscard]] const SpaceViewLayers& SpaceView() const noexcept
  {
    return m_spaceView;
  }

  /// Whether the screen still shows the last published frame's view: the CGA in its graphics mode from the start of video memory
  /// (Cga::ShowsGraphicsFromStart), and the view's bytes unchanged there (SpaceViewLayers::Shown). While it does, a renderer may
  /// draw the frame's scene in its place.
  [[nodiscard]] bool SpaceViewShown() const noexcept
  {
    return m_cga.ShowsGraphicsFromStart() && m_spaceView.Shown(m_state);
  }

  /// The space view's watch of a frame's drawing (Host::BeginSpaceViewFrame and the rest), over the game's state.
  void BeginSpaceViewFrame() noexcept
  {
    m_spaceView.BeginFrame();
    m_beamCount = 0;
  }

  void BeginSpaceViewObject() noexcept
  {
    m_spaceView.BeginObject(m_state);
  }

  void NoteSpaceViewModel() noexcept
  {
    m_spaceView.NoteModel();
  }

  void NoteSpaceViewRendered() noexcept
  {
    m_spaceView.NoteRendered();
  }

  /// What was drawn since BeginSpaceViewObject, no slot's, is the renderer's to draw: the stardust, the beams, the sights, the
  /// title's planet (ADR-035).
  void EndSpaceViewRendered() noexcept
  {
    m_spaceView.NoteRendered();
    m_spaceView.EndObject(m_state, SpaceViewLayers::NO_SLOT);
  }

  /// A beam of the frame being drawn, which PublishFlightSnapshot hands to its snapshot; past MOST_BEAMS, none.
  void NoteSpaceViewBeam(const SnapshotBeam& _beam) noexcept
  {
    if (m_beamCount < m_beams.size())
    {
      m_beams[m_beamCount] = _beam;
      ++m_beamCount;
    }
  }

  void EndSpaceViewObject(std::uint8_t _slot) noexcept
  {
    m_spaceView.EndObject(m_state, _slot);
  }

  /// The colours, 0-15 of CgaPalette, the 320x200 graphics mode shows its four pixel values in now (Cga::GraphicsColors).
  [[nodiscard]] std::array<std::uint8_t, 4> GraphicsColors() const noexcept
  {
    return m_cga.GraphicsColors();
  }

  /// Where the commander files are (D78): the game reads and writes them directly.
  [[nodiscard]] FileStore& Files() noexcept
  {
    return m_files;
  }

  /// The stamp a file written now takes: the date and time at the clock, from the start moment (Desc::startMoment).
  [[nodiscard]] FileStamp FileStampNow() const;

  // ── For the game's routines ──

  /// A wait's poll that found nothing (ADR-039): the clock moves to the next device event; the game co_awaits what this gives,
  /// which suspends it there if that is the run's end; then what fell due runs (RunDue).
  [[nodiscard]] RunAwaiter Wait();

  /// A turn of a loop that waits for nothing: a wait's poll that took something it does not want, or a loop that never ends.
  /// The clock stands still. Past SpinLimit() such steps with no wait between, the run stops as Spinning, as the original's
  /// would never end.
  void Spin();

  /// _duration passes as it passes in a wait, a step at a time to each device event, the game suspended there if that is the
  /// run's end and what falls due run: paced time's charge for work the 8088 took time over where the program
  /// sets no pace of its own (ADR-013).
  [[nodiscard]] Task<void> Spend(Milliseconds _duration);

  /// What fell due when the clock last moved, run: the tick's work, then a key. If the program stops on the way, the game is
  /// abandoned by an exception that leaves its coroutines, and RunUntil reports the stop.
  void RunDue();

  /// Moves the clock to the next event, or to _limit if that comes first, and marks what falls due there: what paced time
  /// does when the program waits.
  void Idle(Milliseconds _limit);

private:
  [[nodiscard]] StopReason Stopped() const noexcept;
  void Resume();

  GameState& m_state;

  // Declared in the order they are built: each device holds references to the ones above it.
  Milliseconds m_clock = 0;
  Speaker m_speaker;
  std::uint8_t m_speakerBits = 0;
  Cga m_cga;
  std::array<FlightSnapshot, 2> m_flightSnapshots{}; // the previous and the latest
  SpaceViewLayers m_spaceView;
  std::array<SnapshotBeam, FlightSnapshot::MOST_BEAMS> m_beams{}; // the frame's beams, since BeginSpaceViewFrame
  std::size_t m_beamCount = 0;
  Mouse m_mouse;
  FileStore& m_files;
  DosDateTime m_startMoment;

  bool m_cheat = false;

  // The tick and the keys (D87, D88): when each next falls due, and what has fallen due for RunDue.
  Milliseconds m_nextTickAt = NO_EVENT;
  std::deque<KeyEvent> m_keys;
  MarketView m_marketView;          // the market screen's latest view
  bool m_marketShown = false;       // while the market screen's routine runs
  std::deque<MarketClick> m_clicks; // the shell's clicks on it, oldest first
  Milliseconds m_nextKeyAt = NO_EVENT;
  bool m_keysToGame = false;
  bool m_tickDue = false;
  std::optional<KeyEvent> m_keyDue;
  std::uint64_t m_ticksRun = 0;

  HookedProgram m_entry;
  HookedTick m_tickHandler;
  HookedKeys m_keyHandler;

  std::uint64_t m_spinLimit = DEFAULT_SPIN_LIMIT;
  std::uint64_t m_stepsSinceIdle = 0;
  Milliseconds m_runLimit = NO_EVENT;

  bool m_terminated = false; // the program's start routine has returned

  // The program's coroutine (D77), made at the first RunUntil, and the wait it is suspended in, if it is. Destroying the
  // Host destroys the coroutine, and every routine it was waiting in, as a host's Game ends.
  std::optional<Task<void>> m_program;
  std::coroutine_handle<> m_suspended;
};

} // namespace Elite
