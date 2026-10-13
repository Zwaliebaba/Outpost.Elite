// GameLogic/Platform.h
#pragma once

#include "Bios.h"
#include "Cga.h"
#include "DosFiles.h"
#include "FileStore.h"
#include "FlightSnapshot.h"
#include "GamePort.h"
#include "InterruptController.h"
#include "IntervalTimer.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "SpaceViewLayers.h"
#include "Speaker.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <semaphore>
#include <string>
#include <thread>

namespace Elite
{

class GameState;

/// Why RunUntil returned.
enum class StopReason : std::uint8_t
{
  Reached,    ///< The clock reached the cycle asked for.
  Terminated, ///< The program ended: its start routine returned, to DOS's int 20h.
  Spinning    ///< The program ran SpinLimit() turns without waiting once.
};

/// The two hardware interrupts the game takes (ADR-041): the timer's, IRQ 0 on int 8, and the keyboard's, IRQ 1 on int 9.
enum class Irq : std::uint8_t
{
  Timer,
  Keyboard
};

/// What serves an interrupt: the BIOS's handler, the game's own, or a vector that names code no routine stands in for, as int 8
/// does once the game writes its segment again while the BIOS's handler is there; one taken there throws std::logic_error.
enum class IrqHandler : std::uint8_t
{
  Bios,
  Game,
  Stray
};

/// A routine in C++ that stands in for the program's own code (ADR-010): its start, or one of its interrupt handlers.
using NativeRoutine = std::function<void()>;

/// The native platform the game runs on (D23, D27, D33): the IBM PC's devices and the services of its ROM and DOS, as the
/// game uses them, with no processor and no memory map. A de-assembled routine reaches it only through Hardware, which takes
/// the platform (ADR-014); the game's memory is the GameState's.
///
/// It holds, each the emulated PC's model moved here without its port bus, its registers or its interrupt-vector plumbing:
/// the clock and its scheduler; the 8259 and the 8253 (InterruptController, IntervalTimer); the keyboard and the system control
/// port (Keyboard); the speaker (Speaker); the game port (GamePort); the mouse driver (Mouse); the CGA (Cga); the BIOS's video
/// and clock services and its data area (Bios), and its two hardware interrupt handlers; DOS's file services (DosFiles) and its
/// print-string; the interrupt vector table; and the ROM bytes the game reads.
///
/// Time is the 8088's cycle count, a third of the crystal (Timing.h), and it is paced (ADR-008): the game's work takes no
/// time. The clock moves only while the game waits, and then straight to the next thing a device will do: the timer's next
/// interrupt, the keyboard's next delivery or the CGA's next change of status, or the end of the run. So interrupts land only
/// inside the game's waits, at the same points however long its work took.
///
/// Waiting is declared (ADR-039). Each wait of the game polls for what it waits for, and when a poll finds nothing it says
/// so (Wait): the clock moves to the next device event. A poll that took something it does not want, and a loop that never
/// ends, say that instead (Spin), and the clock stands still.
///
/// Interrupts are taken where they fall due, as the 8088 takes them: at the end of a wait's poll and of a spin, and when
/// native code lets them in (EnableInterrupts), ends one (Hardware::EndOfInterrupt) or reprograms the timer
/// (Hardware::SetTickDivisor). The interrupt controller's rules hold: in service, end of interrupt, masking. The handler is
/// the one the vector table names: the game's own, the BIOS's, or the ROM's IRET.
///
/// The game runs on a thread of its own, the native thread (ADR-010 item 8): the program's start routine, and everything it
/// calls, which is all of the game's code. When the clock reaches the end of a run there, the native thread hands the
/// platform back and RunUntil returns; the next RunUntil carries on where it stopped. The host thread and the native thread
/// take turns through two semaphores, so only one runs at a time, and a run is a function of the program, its start moment
/// and its inputs: there is no wall clock anywhere.
///
/// The game port is timed differently: its one-shots are measured against the cycles of the instructions the original
/// executed (InstructionCycles), because the program reads a stick by counting polling loops with interrupts off; native code
/// counts the cycles of the instructions it stands in for.
class Platform
{
public:
  /// How the platform powers on.
  struct Desc
  {
    DosDateTime startMoment;   ///< DOS's date and time at power-on; the BIOS's tick count starts at its time of day.
    bool mousePresent = false; ///< Whether a mouse driver is loaded.
    std::string commandTail;   ///< The program's command tail, as COMMAND.COM passes it, leading space included: " cheat".
  };

  /// A native routine at an address, and how often it ran.
  struct HookedRoutine
  {
    NativeRoutine routine;
    std::uint64_t calls = 0;
  };

  /// What DOS leaves in BP for the program it starts, as MS-DOS does and DOSBox-X copies (ADR-006 item 4).
  static constexpr std::uint16_t ENTRY_BASE_POINTER = 0x091C;

  /// Steps the default spin limit allows: far more than the longest stretch of work the game does between two waits, far
  /// fewer than a host would run before someone notices.
  static constexpr std::uint64_t DEFAULT_SPIN_LIMIT = 50'000'000;

  /// What a hardware interrupt's entry costs the 8088, on the instruction clock: Intel's 8086 figure plus five word transfers
  /// over the 8-bit bus (three pushes, two vector reads).
  static constexpr Cycles HARDWARE_INTERRUPT_CYCLES = 61 + 5 * 4;

  /// Powers on: every device in its power-on state, the vector table and the BIOS's data area as the BIOS leaves them for
  /// DOS, video mode 3 set on the CGA and in _state's video memory, and the BIOS's two hardware interrupt handlers in place.
  /// _state is the game's memory and _files where DOS's files are; both must outlive the platform. Not noexcept: the devices
  /// allocate.
  Platform(GameState& _state, FileStore& _files, const Desc& _desc);
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;
  ~Platform();

  // ── The program ──

  /// DOS starts the program: its files closed, and its start routine (SetEntry) run from the next RunUntil. The PSP is not
  /// memory any more.
  void StartProgram() noexcept;

  /// The program's command tail (Desc::commandTail).
  [[nodiscard]] const std::string& CommandTail() const noexcept
  {
    return m_commandTail;
  }

  /// _routine is the program's start, where DOS starts it. It runs on the native thread; when it returns, the program has
  /// ended. Throws std::logic_error if the program has one already.
  void SetEntry(NativeRoutine _routine);

  /// _routine is the game's own handler of _irq, which runs whenever it is the handler there (SetHandler) and the interrupt is
  /// taken, and returns from it as IRET does. Throws std::logic_error if the game has one there already.
  void SetGameHandler(Irq _irq, NativeRoutine _routine);

  /// The program's start routine, and how often it ran.
  [[nodiscard]] const HookedRoutine& Entry() const noexcept
  {
    return m_entry;
  }

  /// The game's own handler of _irq, and how often it ran.
  [[nodiscard]] const HookedRoutine& GameHandler(Irq _irq) const noexcept
  {
    return m_gameHandlers[static_cast<std::size_t>(_irq)];
  }

  // ── Running ──

  /// Runs until the clock reaches _cycle, or until the program can go no further.
  [[nodiscard]] StopReason RunUntil(Cycles _cycle);

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
  [[nodiscard]] Cycles Clock() const noexcept
  {
    return m_clock;
  }

  /// The cycles of every instruction the original executed that native code stands in for, and of the interrupts it took,
  /// since power-on: the game port's clock.
  [[nodiscard]] Cycles InstructionCycles() const noexcept
  {
    return m_instructionCycles;
  }

  /// The hardware interrupts taken since power-on.
  [[nodiscard]] std::uint64_t InterruptsTaken() const noexcept
  {
    return m_interruptsTaken;
  }

  // ── The devices, for the hosts: the keys, the stick, the mouse, the picture and the sound ──

  [[nodiscard]] Keyboard& KeyboardController() noexcept
  {
    return m_keyboard;
  }

  [[nodiscard]] GamePort& Joystick() noexcept
  {
    return m_gamePort;
  }

  [[nodiscard]] Mouse& MouseDriver() noexcept
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

  /// The space view's watch of a frame's drawing (Hardware::BeginSpaceViewFrame and the rest), over the game's state.
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

  [[nodiscard]] IntervalTimer& Timer() noexcept
  {
    return m_timer;
  }

  [[nodiscard]] InterruptController& Interrupts() noexcept
  {
    return m_controller;
  }

  /// The BIOS's services and its data area.
  [[nodiscard]] Bios& BiosServices() noexcept
  {
    return m_bios;
  }

  /// DOS's file services.
  [[nodiscard]] DosFiles& Files() noexcept
  {
    return m_files;
  }

  // ── For native code, through Hardware ──

  /// STI: interrupts are taken again when they fall due, and those due now are taken here.
  void EnableInterrupts();

  /// CLI.
  void DisableInterrupts() noexcept;

  [[nodiscard]] bool InterruptsEnabled() const noexcept;

  /// A wait's poll that found nothing (ADR-039): the clock moves to the next device event, the run ends there if that is its
  /// end, and the interrupts now due are taken.
  void Wait();

  /// A turn of a loop that waits for nothing: a wait's poll that took something it does not want, or a loop that never ends.
  /// The clock stands still. Past SpinLimit() such steps with no wait between, the run stops as Spinning, as the original's
  /// would never end.
  void Spin();

  /// _cycles pass as they pass in a wait, a step at a time to each device event, the run ending there if that is its end and
  /// the interrupts that fall due taken: paced time's charge for work the 8088 took time over where the program sets no pace
  /// of its own (ADR-013).
  void Spend(Cycles _cycles);

  /// The interrupts now due are taken, each handler run to its return, as the 8088 takes them at the next instruction once
  /// interrupts are on. If the program stops on the way, the native code is abandoned by an exception that the native thread
  /// catches, and RunUntil reports the stop.
  void TakeDueInterrupts();

  /// Moves the clock to the next device event, or to _limit if that comes first, and lets the devices raise what falls due:
  /// what paced time does when the program waits.
  void Idle(Cycles _limit);

  /// Adds the cycles the 8088 takes over instructions native code stands in for to InstructionCycles: the game port's clock.
  void CountInstructionCycles(Cycles _cycles) noexcept
  {
    m_instructionCycles += _cycles;
  }

  /// What serves _irq now. At power-on the BIOS's handlers serve both.
  [[nodiscard]] IrqHandler Handler(Irq _irq) const noexcept
  {
    return m_handlers[static_cast<std::size_t>(_irq)];
  }

  /// _handler serves _irq from now on: how the game installs its own handlers and puts the BIOS's back.
  void SetHandler(Irq _irq, IrqHandler _handler) noexcept
  {
    m_handlers[static_cast<std::size_t>(_irq)] = _handler;
  }

  /// Whether a mouse driver is loaded, which int 33h's vector says on the PC: set from Desc::mousePresent at power-on.
  [[nodiscard]] bool MouseDriverLoaded() const noexcept
  {
    return m_mouseDriverLoaded;
  }

  /// A mouse driver loaded or not, whatever the mouse is: a test's driver with no mouse behind it, whose calls throw
  /// std::logic_error, as a driver no routine stands in for did.
  void SetMouseDriverLoaded(bool _loaded) noexcept
  {
    m_mouseDriverLoaded = _loaded;
  }

  /// Int 33h with no mouse behind it: the ROM's IRET, with its cycles, when no driver is loaded; a driver loaded with no mouse
  /// behind it throws std::logic_error.
  void CallAbsentMouse();

private:
  [[nodiscard]] StopReason Stopped() const noexcept;
  void Interrupt(std::uint8_t _vector);
  void EndTurn();
  void StartNative();
  void ResumeNative();
  void HandToNative() noexcept;
  void NativeMain();
  void ReachedRunLimit();
  void RunBiosTimerHandler();
  void RunBiosKeyboardHandler();

  GameState& m_state;

  // Declared in the order they are built: each device holds references to the ones above it.
  Cycles m_clock = 0;
  Cycles m_instructionCycles = 0;
  InterruptController m_controller;
  Speaker m_speaker;
  IntervalTimer m_timer;
  Keyboard m_keyboard;
  GamePort m_gamePort;
  Cga m_cga;
  std::array<FlightSnapshot, 2> m_flightSnapshots{}; // the previous and the latest
  SpaceViewLayers m_spaceView;
  std::array<SnapshotBeam, FlightSnapshot::MOST_BEAMS> m_beams{}; // the frame's beams, since BeginSpaceViewFrame
  std::size_t m_beamCount = 0;
  Bios m_bios;
  Mouse m_mouse;
  DosFiles m_files;

  std::array<IrqHandler, 2> m_handlers{IrqHandler::Bios, IrqHandler::Bios};
  bool m_mouseDriverLoaded = false;
  std::uint16_t m_flags;
  std::string m_commandTail;

  HookedRoutine m_entry;
  std::array<HookedRoutine, 2> m_gameHandlers{};
  std::uint64_t m_interruptsTaken = 0;

  std::uint64_t m_spinLimit = DEFAULT_SPIN_LIMIT;
  std::uint64_t m_stepsSinceIdle = 0;
  Cycles m_runLimit = NO_EVENT;

  bool m_started = false;    // the program's start routine has been begun
  bool m_terminated = false; // and it has returned

  // The native thread (ADR-010 item 8), made for the program's start. The host thread and it take turns, handing over through
  // the two semaphores; these flags are read only by the one holding the platform.
  std::thread m_nativeThread;
  std::binary_semaphore m_toNative{0};
  std::binary_semaphore m_toHost{0};
  std::exception_ptr m_nativeError;
  bool m_nativeActive = false;   // the program's routine is in progress on the native thread
  bool m_onNativeThread = false; // the native thread holds the platform
  bool m_abandon = false;        // the native thread is to unwind what it is running
  bool m_shutdown = false;       // the native thread is to end
};

} // namespace Elite
