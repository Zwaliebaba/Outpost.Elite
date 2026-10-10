// Machine/Pc.h
#pragma once

#include "Cga.h"
#include "Cpu.h"
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

#include <cstdint>
#include <exception>
#include <memory>
#include <semaphore>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Machine
{

class FileStore;

/// Why RunUntil returned.
enum class StopReason : std::uint8_t
{
  Reached,    ///< The clock reached the cycle asked for.
  Fault,      ///< The services refused a call; PcServices::Fault says which.
  Terminated, ///< The program ended through int 20h.
  Deadlocked, ///< The CPU halted with interrupts off, and nothing can wake it.
  Spinning,   ///< Paced time only: the program ran SpinLimit() steps without waiting once.
  Overran     ///< Native code off the native thread waited past the end of the run (NativeCode::Overran).
};

/// How the machine's clock advances (ADR-008).
enum class TimeMode : std::uint8_t
{
  /// Every instruction takes its 8088 cycles, as on the real machine: interrupts land wherever the
  /// clock says, between any two instructions. What the DOSBox-X comparisons run in (ADR-003).
  Clocked,

  /// Instructions take no time. The clock moves only while the program waits, and then straight to
  /// the next thing a device will do: the next timer interrupt, keyboard delivery or change of the
  /// CGA's status. So interrupts land only inside the program's waits, at the same points however
  /// long its work took. What replays and the game run in.
  Paced
};

/// The IBM PC the reference runs on, put together (ADR-006): an 8088, 1 MiB of memory, the 8259, the
/// 8253, the keyboard's 8255, the speaker, the game port and the CGA on one I/O bus, and the ROM, BIOS,
/// DOS and mouse driver at the call level.
///
/// Time is the machine's own cycle count, the 8088's clock at a third of the crystal (Timing.h). How it
/// advances is the TimeMode. When Clocked, Step() runs one CPU step, adds its cycles to the clock, and
/// then lets the devices that raise interrupts catch up with it, so a request raised during a step is
/// seen at the next instruction boundary. When Paced, the clock moves only when the program waits.
///
/// Waiting is recognised, not declared. A program waits by going round a loop until an interrupt or a
/// device changes something. One turn of such a loop changes nothing: when it takes its backward jump,
/// the registers are what they were the last time that jump was taken, no byte of memory has changed
/// and no port has been written. Paced time watches every taken backward jump, and when one closes a
/// turn like that, the clock moves to the next device event (Idle()). A loop that counts, copies or
/// draws changes something each turn, so it is work and takes no time.
///
/// Paced time has one more consequence: the CGA reports each vertical retrace only to the first status
/// read that sees it (Cga::SetRetraceSeenOnce), because the game waits for a retrace's level rather than
/// its edge, and only its drawing time made those the same.
///
/// The game port is timed differently in both modes: its one-shots are measured against the cycles of
/// the instructions executed (InstructionCycles()), because the program reads a stick by counting
/// polling loops with interrupts off.
///
/// There is no wall clock anywhere: a run is a function of the program, its start moment and its
/// inputs, and two runs given the same are identical to the cycle.
class Pc
{
public:
  using Desc = PcServices::Desc;

  /// Powers on: every device in its power-on state, the ROM, the interrupt table and the BIOS data
  /// area installed, video mode 3 set. _files holds DOS's files and must outlive the machine. Not
  /// noexcept: the devices allocate.
  Pc(FileStore& _files, const Desc& _desc);
  Pc(const Pc&) = delete;
  Pc& operator=(const Pc&) = delete;
  ~Pc();

  /// Loads a program as DOS's EXEC does (ExeLoader) and starts the CPU at its entry, in the registers
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

  /// One CPU step, then the timer and the keyboard catch up with the clock. While a native routine
  /// that waits is in progress, it runs to its next wait instead (ADR-010 item 8).
  void Step();

  /// Steps until the clock reaches _cycle, or until the program can go no further.
  [[nodiscard]] StopReason RunUntil(Cycles _cycle);

  /// Moves the clock to the next device event, or to _limit if that comes first, and lets the devices
  /// raise what falls due. What paced time does when the program waits; native code that replaces a
  /// waiting loop calls it in the loop's place (Phase 3). Clocked, it does the same.
  void Idle(Cycles _limit);

  /// The machine's clock.
  [[nodiscard]] Cycles Clock() const noexcept
  {
    return m_clock;
  }

  /// The cycles of every instruction executed since power-on, in either mode: the game port's clock.
  [[nodiscard]] Cycles InstructionCycles() const noexcept
  {
    return m_instructionCycles;
  }

  [[nodiscard]] Cpu& Processor() noexcept
  {
    return m_cpu;
  }

  [[nodiscard]] const Cpu& Processor() const noexcept
  {
    return m_cpu;
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
  /// there: at the start of any step, after the CPU has taken an interrupt that was due. Native code
  /// takes no time, which is what paced time expects of work (ADR-008). Throws std::logic_error if a
  /// routine is there already.
  ///
  /// A routine that can wait (_wait not Never) runs on a thread of its own, the native thread, and with
  /// it everything it calls. When the clock reaches the end of a run there, in Wait or in original
  /// code it calls, the native thread hands the machine back and RunUntil returns; the next RunUntil
  /// carries on where it stopped. Only one thread runs at a time, so a run is still a function of its
  /// inputs (ADR-010 item 8). Any other native routine runs as a plain call.
  void Hook(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine, const NativeContract& _contract,
            NativeReturn _exit = NativeReturn::Near, NativeWait _wait = NativeWait::Never);

  /// The native routines, whether they are being compared with the original, and what that found.
  [[nodiscard]] NativeCode& Native() noexcept
  {
    return m_native;
  }

  [[nodiscard]] const NativeCode& Native() const noexcept
  {
    return m_native;
  }

  /// For native code: calls the program's code at CS:_offset as a near CALL from CS:IP would, and runs it
  /// until it returns. Hooked entries it reaches run natively. If the program stops on the way (a
  /// fault, the end of the program), the native code is abandoned by an exception that the step which
  /// started it catches, and RunUntil reports the stop. Off the native thread, a call that waits past
  /// the end of a run stops the run there: StopReason::Overran. If the code returns past the native
  /// routine that called it, dropping return addresses as RunPauseScreen's abort does, the routine is
  /// unwound by an exception that its hook catches, and its own caller carries on from where the
  /// program is, or is unwound in turn.
  void CallNear(std::uint16_t _offset);

  /// For native code: returns from a near call as RET _popBytes does.
  void ReturnNear(std::uint16_t _popBytes = 0) noexcept;

  /// For native code: returns from a far call as RETF _popBytes does.
  void ReturnFar(std::uint16_t _popBytes = 0) noexcept;

  /// For native code: returns from an interrupt handler as IRET does.
  void ReturnInterrupt() noexcept;

  /// For native code that stands in for a waiting loop: one turn of it that found nothing to do. The
  /// clock moves to the next device event (Idle), the run ends there if that is its end, and then any
  /// interrupt now due is taken, as the CPU takes it at the loop's next instruction. Only a routine
  /// hooked as one that waits may call it.
  void Wait();

  /// For native code that stands in for a loop: the turn ends where the original's backward jump is.
  /// Paced time looks at it as it looks at the original's (ADR-008): a turn that changed no register,
  /// no byte and no port since the last idles to the next device event. Then the run ends there if
  /// that is its end, and any interrupt now due is taken. Native code that calls it wherever the
  /// original jumps back, with the registers the original has there, waits on exactly the turns the
  /// original would. A loop that never idles stops the run as Spinning, as the original's would. Only a
  /// routine hooked as one that waits may call it.
  void LoopTurn();

  /// For native code: does what INT _vector does at CS:IP. The BIOS, DOS and mouse services take the
  /// call if they serve that vector; otherwise the handler the vector table names runs until its IRET.
  /// Like CallNear, a stop on the way abandons the native code.
  void CallInterrupt(std::uint8_t _vector);

  /// Steps the default spin limit allows: far more than the longest stretch of work the game does
  /// between two waits, far fewer than a host would run before someone notices.
  static constexpr std::uint64_t DEFAULT_SPIN_LIMIT = 50'000'000;

private:
  // The state at a taken backward jump, kept to tell whether the next turn of the loop changed
  // anything.
  struct Turn
  {
    Registers registers{};
    std::uint64_t memoryChanges = 0;
    std::uint64_t portWrites = 0;
    bool valid = false;
  };

  // A comparison's working state, made at the first Hook (ADR-010).
  struct Comparison
  {
    WriteJournal originalWrites{NativeCode::JOURNAL_CAPACITY};
    WriteJournal nativeWrites{NativeCode::JOURNAL_CAPACITY};
    std::vector<std::uint8_t> originalAfter;
    std::vector<PortRouter::Access> originalPorts;
    std::uint16_t segment = 0;           // the compared entry's code segment
    std::vector<std::uint16_t> executed; // offsets in it the original ran, in order and repeated
    bool active = false;
  };

  void MapPorts(std::uint16_t _first, std::uint16_t _last, PortBus& _device);
  void RunHook();
  void Dispatch(NativeCode::Hook& _hook);
  void StartNative(NativeCode::Hook& _hook);
  void ResumeNative();
  void HandToNative() noexcept;
  void NativeMain();
  void ReachedRunLimit();
  void TakeDueInterrupts();
  void Compare(NativeCode::Hook& _hook);
  void RunNative(NativeCode::Hook& _hook);
  void RunToReturn(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _stackPointer, Comparison* _original = nullptr);
  void StepPaced();
  void NoteBackwardJump();
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
  Cpu m_cpu;
  NativeCode m_native;
  std::unique_ptr<Comparison> m_comparison;

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
  // Native routines running on each thread: a call that returns past native code unwinds it, but one
  // that a host makes from outside any of them ends as calls do.
  std::uint32_t m_hostRoutines = 0;
  std::uint32_t m_nativeRoutines = 0;
};

} // namespace Machine
