// GameLogic/Host.h
#pragma once

#include "Cga.h"
#include "FileStore.h"
#include "FlightSnapshot.h"
#include "Mouse.h"
#include "Scheduler.h"
#include "SpaceViewLayers.h"
#include "Speaker.h"
#include "Task.h"
#include "Timing.h"

#include <array>
#include <coroutine>
#include <cstddef>
#include <cstdint>
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

/// A routine in C++ that stands in for one of the program's interrupt handlers (ADR-010), which run to their end.
using NativeRoutine = std::function<void()>;

/// The program's start (ADR-010), a coroutine that waits (D77): the platform resumes it each run until it ends.
using ProgramRoutine = std::function<Task<void>()>;

/// The host the game runs on (D90): its clock and its waits, the keys and the mouse, the sound, the CGA and the files. Every
/// routine that waits or drives a device is given it beside the GameState, which holds the game's memory.
///
/// It holds, each what the game uses of the PC, without its port bus, its registers or its vector table (ADR-041): the clock and
/// the events it waits for, with the two interrupts they request (Scheduler: the tick, the keys and their requests); the system
/// control port; the sound, the tone and its bits (Speaker); the mouse (Mouse); the CGA (Cga), whose modes the game sets on it;
/// what runs when the game has no handler of its own; and the file store that holds the commander files (FileStore, D78).
///
/// Time is whole milliseconds (Timing.h, D87), and it is paced (ADR-008): the game's work takes no time. The clock moves only
/// while the game waits, and then straight to the next thing a device will do: the next tick, the next key's delivery or the
/// CGA's next change of status, or the end of the run. So interrupts land only inside the game's waits, at the same points
/// however long its work took.
///
/// Waiting is declared (ADR-039). Each wait of the game polls for what it waits for, and when a poll finds nothing it says
/// so (Wait): the clock moves to the next device event. A poll that took something it does not want, and a loop that never
/// ends, say that instead (Spin), and the clock stands still.
///
/// Interrupts are taken where they fall due, as the 8088 takes them: at the end of a wait's poll and of a spin, and when
/// native code lets them in (EnableInterrupts), ends one (Host::EndOfInterrupt) or reprograms the timer
/// (Host::SetTickRunning). The interrupt controller's rules hold: in service and end of interrupt (Scheduler). The handler
/// is the one installed by name: the game's own, the BIOS's, or a stray one, which throws.
///
/// The game runs as a coroutine on the host's thread (D77): the program's start routine, and every routine it calls that waits,
/// each a Task that co_awaits the next. When the clock reaches the end of a run inside a wait, the wait suspends the game and
/// RunUntil returns; the next RunUntil resumes it where it stopped. There is one thread, and a run is a function of the program,
/// its start moment and its inputs: there is no wall clock anywhere.
class Host
{
public:
  /// How the platform powers on.
  struct Desc
  {
    DosDateTime startMoment;   ///< The date and time at power-on, which the commander files' stamps count from.
    bool mousePresent = false; ///< Whether a mouse is attached (Mouse::Present).
    bool cheat = false;        ///< Whether the game starts with the cheat on: the shell's --cheat, where 1987 read ' cheat' (D80).
  };

  /// A native routine at an address, and how often it ran.
  template <typename Routine> struct Hooked
  {
    Routine routine;
    std::uint64_t calls = 0;
  };
  using HookedRoutine = Hooked<NativeRoutine>;
  using HookedProgram = Hooked<ProgramRoutine>;

  /// What a wait's poll that found nothing gives the routine that waits (Wait), to co_await: ready, unless the clock reached the
  /// run's end, when the game is suspended until the next RunUntil. Once it goes on, the interrupts now due are taken.
  class [[nodiscard]] RunAwaiter
  {
  public:
    explicit RunAwaiter(Host& _platform) noexcept
      : m_platform(_platform)
    {
    }

    [[nodiscard]] bool await_ready() const noexcept
    {
      return m_platform.m_clock < m_platform.m_runLimit;
    }

    void await_suspend(std::coroutine_handle<> _waiting) noexcept
    {
      m_platform.m_suspended = _waiting;
    }

    void await_resume() const
    {
      m_platform.EndTurn();
    }

  private:
    Host& m_platform;
  };

  /// The system control port at power-on (SystemControl).
  static constexpr std::uint8_t POWER_ON_SYSTEM_CONTROL = 0x4C;

  /// Steps the default spin limit allows: far more than the longest stretch of work the game does between two waits, far
  /// fewer than a host would run before someone notices.
  static constexpr std::uint64_t DEFAULT_SPIN_LIMIT = 50'000'000;

  /// Powers on: every device in its power-on state, the CGA with every register clear until the game sets its mode, the tick
  /// stopped, and none of the game's handlers in place. _state is the game's memory and _files where the commander files are;
  /// both must outlive the platform. Not noexcept: the devices allocate.
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

  /// _routine is the program's start, where DOS starts it: the platform makes the coroutine at the first RunUntil and resumes
  /// it each run; when it returns, the program has ended. Throws std::logic_error if the program has one already.
  void SetEntry(ProgramRoutine _routine);

  /// _routine is the game's own handler of _irq, which runs whenever it is the handler there (SetHandler) and the interrupt is
  /// taken, and returns from it as IRET does. Throws std::logic_error if the game has one there already.
  void SetGameHandler(Irq _irq, NativeRoutine _routine);

  /// The program's start routine, and how often it ran.
  [[nodiscard]] const HookedProgram& Entry() const noexcept
  {
    return m_entry;
  }

  /// The game's own handler of _irq, and how often it ran.
  [[nodiscard]] const HookedRoutine& GameHandler(Irq _irq) const noexcept
  {
    return m_gameHandlers[static_cast<std::size_t>(_irq)];
  }

  // ── Running ──

  /// Runs until the clock reaches _time, or until the program can go no further.
  [[nodiscard]] StopReason RunUntil(Milliseconds _time);

  /// The number of steps the program may run without waiting before RunUntil stops with StopReason::Spinning: its spins
  /// (Spin), and the interrupts and handlers it runs. A loop that spins for ever would otherwise run with the clock standing
  /// still.
  void SetSpinLimit(std::uint64_t _turns) noexcept
  {
    m_spinLimit = _turns;
  }

  [[nodiscard]] std::uint64_t SpinLimit() const noexcept
  {
    return m_spinLimit;
  }

  /// The platform's clock.
  [[nodiscard]] Milliseconds Clock() const noexcept
  {
    return m_clock;
  }

  /// The hardware interrupts taken since power-on.
  [[nodiscard]] std::uint64_t InterruptsTaken() const noexcept
  {
    return m_interruptsTaken;
  }

  // ── The devices, for the hosts: the keys, the stick, the mouse, the picture and the sound ──

  /// The tick, the keys and the two interrupts' requests: where a host gives the game its key events (Scheduler::Inject).
  [[nodiscard]] Scheduler& Events() noexcept
  {
    return m_events;
  }

  /// The system control port, as last written: bit 0 is the tone's gate and bit 1 the speaker's data. At power-on
  /// it holds 4Ch, what the XT BIOS's keyboard reset writes last (speaker and gate off).
  [[nodiscard]] std::uint8_t SystemControl() const noexcept
  {
    return m_systemControl;
  }

  /// OUT 61h,_value: the speaker takes bits 0 and 1, the tone's gate and its data. Not noexcept: the speaker logs a change.
  void SetSystemControl(std::uint8_t _value);

  /// The game's tick started, a millisecond from now and every millisecond after (D87), or stopped. With interrupts on, one
  /// now due is taken here.
  void SetTickRunning(bool _running);

  /// The end of an interrupt. With interrupts on, one held back is taken here.
  void EndOfInterrupt();

  /// The key event the keyboard's interrupt delivered (D88), taken: the next one can then arrive. Only inside the keyboard's
  /// handler.
  [[nodiscard]] KeyEvent TakeKey() noexcept
  {
    return m_events.TakeKey();
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

  /// STI: interrupts are taken again when they fall due, and those due now are taken here.
  void EnableInterrupts();

  /// CLI.
  void DisableInterrupts() noexcept;

  [[nodiscard]] bool InterruptsEnabled() const noexcept;

  /// A wait's poll that found nothing (ADR-039): the clock moves to the next device event; the game co_awaits what this gives,
  /// which suspends it there if that is the run's end; then the interrupts now due are taken.
  [[nodiscard]] RunAwaiter Wait();

  /// A turn of a loop that waits for nothing: a wait's poll that took something it does not want, or a loop that never ends.
  /// The clock stands still. Past SpinLimit() such steps with no wait between, the run stops as Spinning, as the original's
  /// would never end.
  void Spin();

  /// _duration passes as it passes in a wait, a step at a time to each device event, the game suspended there if that is the
  /// run's end and the interrupts that fall due taken: paced time's charge for work the 8088 took time over where the program
  /// sets no pace of its own (ADR-013).
  [[nodiscard]] Task<void> Spend(Milliseconds _duration);

  /// The interrupts now due are taken, each handler run to its return, as the 8088 takes them at the next instruction once
  /// interrupts are on. If the program stops on the way, the game is abandoned by an exception that leaves its coroutines, and
  /// RunUntil reports the stop.
  void TakeDueInterrupts();

  /// Moves the clock to the next device event, or to _limit if that comes first, and lets the devices raise what falls due:
  /// what paced time does when the program waits.
  void Idle(Milliseconds _limit);

  /// What serves _irq now. At power-on the host's default serves both (IrqHandler::Bios): it ends the interrupt, and takes and
  /// drops a key.
  [[nodiscard]] IrqHandler Handler(Irq _irq) const noexcept
  {
    return m_handlers[static_cast<std::size_t>(_irq)];
  }

  /// _handler serves _irq from now on: how the game installs its own handlers and puts the BIOS's back.
  void SetHandler(Irq _irq, IrqHandler _handler) noexcept
  {
    m_handlers[static_cast<std::size_t>(_irq)] = _handler;
  }

private:
  [[nodiscard]] StopReason Stopped() const noexcept;
  void Interrupt(Irq _irq);
  void EndTurn();
  void Resume();
  void RunBiosTimerHandler();
  void RunBiosKeyboardHandler();

  GameState& m_state;

  // Declared in the order they are built: each device holds references to the ones above it.
  Milliseconds m_clock = 0;
  Speaker m_speaker;
  Scheduler m_events;
  std::uint8_t m_systemControl = POWER_ON_SYSTEM_CONTROL;
  Cga m_cga;
  std::array<FlightSnapshot, 2> m_flightSnapshots{}; // the previous and the latest
  SpaceViewLayers m_spaceView;
  std::array<SnapshotBeam, FlightSnapshot::MOST_BEAMS> m_beams{}; // the frame's beams, since BeginSpaceViewFrame
  std::size_t m_beamCount = 0;
  Mouse m_mouse;
  FileStore& m_files;
  DosDateTime m_startMoment;

  std::array<IrqHandler, 2> m_handlers{IrqHandler::Bios, IrqHandler::Bios};
  std::uint16_t m_flags;
  bool m_cheat = false;

  HookedProgram m_entry;
  std::array<HookedRoutine, 2> m_gameHandlers{};
  std::uint64_t m_interruptsTaken = 0;

  std::uint64_t m_spinLimit = DEFAULT_SPIN_LIMIT;
  std::uint64_t m_stepsSinceIdle = 0;
  Milliseconds m_runLimit = NO_EVENT;

  bool m_terminated = false; // the program's start routine has returned

  // The program's coroutine (D77), made at the first RunUntil, and the wait it is suspended in, if it is. Destroying the
  // platform destroys the coroutine, and every routine it was waiting in, as a host's Game ends.
  std::optional<Task<void>> m_program;
  std::coroutine_handle<> m_suspended;
};

} // namespace Elite
