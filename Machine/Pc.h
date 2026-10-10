// Machine/Pc.h
#pragma once

#include "Cga.h"
#include "Dispatcher.h"
#include "ExeLoader.h"
#include "GamePort.h"
#include "Keyboard.h"
#include "Memory.h"
#include "NativeCode.h"
#include "PcServices.h"
#include "Pic.h"
#include "Pit.h"
#include "PortRouter.h"
#include "Speaker.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <semaphore>
#include <span>
#include <string>
#include <thread>

namespace Machine
{

class FileStore;

/// Why RunUntil returned.
enum class StopReason : std::uint8_t
{
  Reached,    ///< The clock reached the cycle asked for.
  Fault,      ///< The services refused a call; PcServices::Fault says which.
  Terminated, ///< The program ended through int 20h.
  Spinning,   ///< Paced time only: the program ran SpinLimit() steps without waiting once.
  Overran,    ///< Native code off the native thread waited past the end of the run (NativeCode::Overran).
  Unported    ///< The Dispatcher reached code no native routine stands in for.
};

/// How the machine's clock advances (ADR-008).
enum class TimeMode : std::uint8_t
{
  /// Every step takes its 8088 cycles, as on the real machine: the interrupt entries and the ROM's IRETs the
  /// Dispatcher runs move the clock. What the DOSBox-X comparisons ran in while there was an interpreter (ADR-003).
  Clocked,

  /// Instructions take no time. The clock moves only while the program waits, and then straight to
  /// the next thing a device will do: the next timer interrupt, keyboard delivery or change of the
  /// CGA's status. So interrupts land only inside the program's waits, at the same points however
  /// long its work took. What replays and the game run in.
  Paced
};

/// The IBM PC the reference runs on, put together (ADR-006): an 8088, which is a Dispatcher, since every routine of
/// the program is native (ADR-011), 1 MiB of memory, the 8259, the 8253, the keyboard's 8255, the speaker, the game
/// port and the CGA on one I/O bus, and the ROM, BIOS, DOS and mouse driver at the call level.
///
/// Time is the machine's own cycle count, the 8088's clock at a third of the crystal (Timing.h). How it
/// advances is the TimeMode. When Clocked, Step() runs one step of the Dispatcher, adds its cycles to the
/// clock, and then lets the devices that raise interrupts catch up with it. When Paced, the clock moves only
/// when the program waits.
///
/// Waiting is recognised, not declared. A program waits by going round a loop until an interrupt or a
/// device changes something. One turn of such a loop changes nothing: at its backward jump, the registers,
/// or a de-assembled loop's signature, are what they were at the last turn, no byte of memory has changed
/// and no port has been written. The native code tells paced time where each turn ends (LoopTurn), and when
/// one closes a turn like that, the clock moves to the next device event (Idle()). A loop that counts,
/// copies or draws changes something each turn, so it is work and takes no time.
///
/// Paced time has one more consequence: the CGA reports each vertical retrace only to the first status
/// read that sees it (Cga::SetRetraceSeenOnce), because the game waits for a retrace's level rather than
/// its edge, and only its drawing time made those the same.
///
/// The game port is timed differently in both modes: its one-shots are measured against the cycles of
/// the instructions executed (InstructionCycles()), because the program reads a stick by counting
/// polling loops with interrupts off; native code counts the cycles of the instructions it stands in for.
///
/// There is no wall clock anywhere: a run is a function of the program, its start moment and its
/// inputs, and two runs given the same are identical to the cycle.
class Pc
{
public:
  using Desc = PcServices::Desc;

  /// Powers on: every device in its power-on state, the ROM, the interrupt table and the BIOS data
  /// area installed, video mode 3 set, and the ROM's hardware interrupt handlers native (NativeFirmware.h).
  /// _files holds DOS's files and must outlive the machine. Not noexcept: the devices allocate.
  Pc(FileStore& _files, const Desc& _desc);
  Pc(const Pc&) = delete;
  Pc& operator=(const Pc&) = delete;
  ~Pc();

  /// Loads a program as DOS's EXEC does (ExeLoader) and starts the processor at its entry, in the registers
  /// MS-DOS gives a program. Nothing changes if the load fails.
  [[nodiscard]] LoadError Load(std::span<const std::uint8_t> _file, const ExeLoader::Desc& _desc, LoadedProgram& _program);

  /// Clocked (the default) or Paced. Changing it mid-run is allowed: the clock carries on from where
  /// it is.
  void SetTimeMode(TimeMode _mode) noexcept;

  [[nodiscard]] TimeMode Mode() const noexcept
  {
    return m_timeMode;
  }

  /// Paced time: the number of steps the program may run without waiting before RunUntil stops with
  /// StopReason::Spinning. A loop that waits for time without ever completing an idle turn would
  /// otherwise run for ever with the clock standing still.
  void SetSpinLimit(std::uint64_t _steps) noexcept;

  [[nodiscard]] std::uint64_t SpinLimit() const noexcept
  {
    return m_spinLimit;
  }

  /// One step of the Dispatcher, then the timer and the keyboard catch up with the clock. While a native
  /// routine that waits is in progress, it runs to its next wait instead (ADR-010 item 8).
  void Step();

  /// Steps until the clock reaches _cycle, or until the program can go no further.
  [[nodiscard]] StopReason RunUntil(Cycles _cycle);

  /// Moves the clock to the next device event, or to _limit if that comes first, and lets the devices
  /// raise what falls due. What paced time does when the program waits; native code that stands in for a
  /// waiting loop calls it in the loop's place. Clocked, it does the same.
  void Idle(Cycles _limit);

  /// The machine's clock.
  [[nodiscard]] Cycles Clock() const noexcept
  {
    return m_clock;
  }

  /// The cycles of every instruction executed, or stood in for, since power-on, in either mode: the game port's clock.
  [[nodiscard]] Cycles InstructionCycles() const noexcept
  {
    return m_instructionCycles;
  }

  /// For native code that stands in for instructions a device times by the instructions executed: adds
  /// the cycles the 8088 takes over them to InstructionCycles(). The game port times its one-shots so, and
  /// the game reads a stick by counting turns of a polling loop.
  void CountInstructionCycles(Cycles _cycles) noexcept
  {
    m_instructionCycles += _cycles;
  }

  [[nodiscard]] Dispatcher& Processor() noexcept
  {
    return m_processor;
  }

  [[nodiscard]] const Dispatcher& Processor() const noexcept
  {
    return m_processor;
  }

  [[nodiscard]] Memory& Ram() noexcept
  {
    return m_memory;
  }

  [[nodiscard]] const Memory& Ram() const noexcept
  {
    return m_memory;
  }

  [[nodiscard]] const Cga& Video() const noexcept
  {
    return m_cga;
  }

  [[nodiscard]] Keyboard& KeyboardController() noexcept
  {
    return m_keyboard;
  }

  [[nodiscard]] GamePort& Joystick() noexcept
  {
    return m_gamePort;
  }

  [[nodiscard]] Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  [[nodiscard]] PcServices& Services() noexcept
  {
    return m_services;
  }

  [[nodiscard]] const PcServices& Services() const noexcept
  {
    return m_services;
  }

  [[nodiscard]] const PortRouter& Ports() const noexcept
  {
    return m_ports;
  }

  /// The I/O bus, for native code's port reads and writes.
  [[nodiscard]] PortRouter& Ports() noexcept
  {
    return m_ports;
  }

  // ── Native code (ADR-010) ──

  /// From now on, execution that reaches _segment:_offset runs _routine in place of the program's code
  /// there: at the start of any step, after the Dispatcher has taken an interrupt that was due. Native code
  /// takes no time, which is what paced time expects of work (ADR-008). Throws std::logic_error if a
  /// routine is there already.
  ///
  /// A routine that can wait (_wait not Never) runs on a thread of its own, the native thread, and with
  /// it everything it calls. When the clock reaches the end of a run there, in Wait, LoopTurn, Spend or an
  /// interrupt it makes, the native thread hands the machine back and RunUntil returns; the next RunUntil
  /// carries on where it stopped. Only one thread runs at a time, so a run is still a function of its
  /// inputs (ADR-010 item 8). Any other native routine runs as a plain call; if it waits past the end of a
  /// run after all, the run stops there as Overran.
  void Hook(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine, const NativeContract& _contract,
            NativeReturn _exit = NativeReturn::Near, NativeWait _wait = NativeWait::Never);

  /// The native routines, and how often each was called.
  [[nodiscard]] NativeCode& Native() noexcept
  {
    return m_native;
  }

  [[nodiscard]] const NativeCode& Native() const noexcept
  {
    return m_native;
  }

  /// For native code: returns from a near call as RET _popBytes does.
  void ReturnNear(std::uint16_t _popBytes = 0) noexcept;

  /// For native code: returns from a far call as RETF _popBytes does.
  void ReturnFar(std::uint16_t _popBytes = 0) noexcept;

  /// For native code: returns from an interrupt handler as IRET does.
  void ReturnInterrupt() noexcept;

  /// For native code that stands in for a waiting loop: one turn of it that found nothing to do. The
  /// clock moves to the next device event (Idle), the run ends there if that is its end, and then any
  /// interrupt now due is taken, as the 8088 takes it at the loop's next instruction. Only a routine
  /// hooked as one that waits may call it.
  void Wait();

  /// For native code that stands in for a loop: the turn ends where the original's backward jump is.
  /// Paced time looks at it as it looked at the original's (ADR-008): a turn that changed no register,
  /// no byte and no port since the last idles to the next device event. Then the run ends there if
  /// that is its end, and any interrupt now due is taken. Native code that calls it wherever the
  /// original jumps back, with the registers the original has there, waits on exactly the turns the
  /// original would. A loop that never idles stops the run as Spinning, as the original's would. Only a
  /// routine hooked as one that waits may call it.
  void LoopTurn();

  /// The most words a de-assembled loop's turn signature holds (LoopTurn(_signature)).
  static constexpr std::size_t MOST_TURN_WORDS = 16;

  /// LoopTurn for a de-assembled loop, which has no register file to compare (ADR-015): _signature stands for it, the
  /// loop's address and the values it carries from one turn to the next. A turn that changed no byte and no port since
  /// the last, with the same signature, idles. A signature never equals an original's register file. Only a routine
  /// hooked as one that waits may call it; at most MOST_TURN_WORDS words.
  void LoopTurn(std::span<const std::uint16_t> _signature);

  /// For native code that waits: _cycles pass as they pass in a wait, a step at a time to each device
  /// event, the run ending there if that is its end and the interrupts that fall due taken. Paced time's
  /// charge for work the 8088 took time over where the program sets no pace of its own (ADR-013). Only a
  /// routine hooked as one that waits may call it.
  void Spend(Cycles _cycles);

  /// For native code: does what INT _vector does at CS:IP. The BIOS, DOS and mouse services take the
  /// call if they serve that vector; otherwise the handler the vector table names runs until its IRET.
  /// If the program stops on the way (a fault, the end of the program), the native code is abandoned by an
  /// exception that the step which started it catches, and RunUntil reports the stop.
  void CallInterrupt(std::uint8_t _vector);

  /// For native code that has let interrupts in: the interrupts now due are taken, each handler run to its IRET, as the 8088
  /// takes them at the next instruction once interrupts are on. A port write can raise one at once while they are off: the
  /// PIT's IRQ 0, when a control word sets its output high. Like CallInterrupt, a stop on the way abandons the native code.
  void TakeDueInterrupts();

  /// Steps the default spin limit allows: far more than the longest stretch of work the game does
  /// between two waits, far fewer than a host would run before someone notices.
  static constexpr std::uint64_t DEFAULT_SPIN_LIMIT = 50'000'000;

private:
  // The state at a taken backward jump, kept to tell whether the next turn of the loop changed
  // anything.
  struct Turn
  {
    Registers registers{};
    // A de-assembled loop's signature in place of the registers (LoopTurn(_signature)).
    std::array<std::uint16_t, MOST_TURN_WORDS> signature{};
    std::size_t signatureWords = 0;
    bool fromSignature = false;
    std::uint64_t memoryChanges = 0;
    std::uint64_t portWrites = 0;
    bool valid = false;
  };

  void MapPorts(std::uint16_t _first, std::uint16_t _last, PortBus& _device);
  void RunHook();
  void Dispatch(NativeCode::Hook& _hook);
  void StartNative(NativeCode::Hook& _hook);
  void ResumeNative();
  void HandToNative() noexcept;
  void NativeMain();
  void ReachedRunLimit();
  void RunToReturn(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _stackPointer);
  void StepPaced();
  void NoteBackwardJump();
  void NoteTurn(const Turn& _turn);
  void EndTurn();
  [[nodiscard]] StopReason Stopped() const noexcept;

  TimeMode m_timeMode = TimeMode::Clocked;
  std::uint64_t m_spinLimit = DEFAULT_SPIN_LIMIT;
  std::uint64_t m_stepsSinceIdle = 0;
  Cycles m_runLimit = NO_EVENT;
  Turn m_lastTurn{};

  // Declared in the order they are built: each device holds references to the ones above it.
  Cycles m_clock = 0;
  Cycles m_instructionCycles = 0;
  Memory m_memory;
  Pic m_pic;
  Speaker m_speaker;
  Pit m_pit;
  Keyboard m_keyboard;
  GamePort m_gamePort;
  Cga m_cga;
  PortRouter m_ports;
  PcServices m_services;
  Dispatcher m_processor;
  NativeCode m_native;

  // The native thread (ADR-010 item 8), made for the first routine that waits. The host thread and
  // it take turns, handing over through the two semaphores; these flags are read only by the one
  // holding the machine.
  std::thread m_nativeThread;
  std::binary_semaphore m_toNative{0};
  std::binary_semaphore m_toHost{0};
  NativeCode::Hook* m_nativeHook = nullptr;
  std::exception_ptr m_nativeError;
  bool m_nativeActive = false;   // a waiting routine is in progress on the native thread
  bool m_onNativeThread = false; // the native thread holds the machine
  bool m_abandon = false;        // the native thread is to unwind what it is running
  bool m_shutdown = false;       // the native thread is to end
  bool m_overran = false;        // native code off the native thread waited past the end of a run
};

} // namespace Machine
