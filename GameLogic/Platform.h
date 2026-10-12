// GameLogic/Platform.h
#pragma once

#include "Bios.h"
#include "Cga.h"
#include "DosFiles.h"
#include "FileStore.h"
#include "GamePort.h"
#include "InterruptController.h"
#include "IntervalTimer.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "Speaker.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <semaphore>
#include <span>
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

/// A far address, segment and offset: what an interrupt vector holds. A plain record (R8).
struct FarAddress
{
  std::uint16_t segment = 0;
  std::uint16_t offset = 0;

  [[nodiscard]] bool operator==(const FarAddress&) const noexcept = default;
};

/// A routine in C++ that stands in for the program's own code (ADR-010): its start, or an interrupt handler a vector names.
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
/// Waiting is recognised, not declared (ADR-015). Every loop of the game that can wait tells the platform where each of its
/// turns ends, with a signature of the values it carries (LoopTurn). A turn with the same signature as the last that changed
/// no byte and wrote no port idles: the clock moves to the next device event. The bytes changed are the GameState's, the
/// BIOS's data area's and the vector table's, each counted where it is written; the ports written are the OUTs the original
/// made, each device operation counting its own.
///
/// Interrupts are taken where they fall due, as the 8088 takes them: at the end of a loop's turn and of a wait, and when
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

  /// The most words a loop's turn signature holds (LoopTurn).
  static constexpr std::size_t MOST_TURN_WORDS = 16;

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

  /// From now on an interrupt whose vector names _address runs _routine, which stands in for the program's code there, and
  /// returns from it as IRET does. Throws std::logic_error if a routine is there already.
  void Hook(FarAddress _address, NativeRoutine _routine);

  /// The program's start routine, and how often it ran.
  [[nodiscard]] const HookedRoutine& Entry() const noexcept
  {
    return m_entry;
  }

  /// Every interrupt handler, by the 20-bit address of its entry.
  [[nodiscard]] const std::map<std::uint32_t, HookedRoutine>& Hooks() const noexcept
  {
    return m_hooks;
  }

  // ── Running ──

  /// Runs until the clock reaches _cycle, or until the program can go no further.
  [[nodiscard]] StopReason RunUntil(Cycles _cycle);

  /// The number of turns the program may run without waiting before RunUntil stops with StopReason::Spinning. A loop that
  /// waits for time without ever completing an idle turn would otherwise run for ever with the clock standing still.
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

  /// One turn of a loop that can wait, ending where the original's backward jump is (ADR-015). _signature stands for the
  /// original's registers there: the loop's address and the values it carries from one turn to the next. A turn with the same
  /// signature as the last that changed no byte and wrote no port idles to the next device event. Then the run ends there if
  /// that is its end, and any interrupt now due is taken. A loop that never idles stops the run as Spinning, as the original's
  /// would. At most MOST_TURN_WORDS words; more throws std::logic_error.
  void LoopTurn(std::span<const std::uint16_t> _signature);

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

  /// _count port writes the original made, for paced time's turn.
  void NotePortWrites(std::uint32_t _count) noexcept
  {
    m_portWrites += _count;
  }

  /// The bytes of memory that writes changed since power-on: the GameState's, the BIOS's data area's and the vector table's.
  [[nodiscard]] std::uint64_t MemoryChanges() const noexcept;

  /// The port writes the original made since power-on.
  [[nodiscard]] std::uint64_t PortWrites() const noexcept;

  /// Interrupt _vector's entry in the vector table.
  [[nodiscard]] FarAddress InterruptVector(std::uint8_t _vector) const noexcept
  {
    return m_vectors[_vector];
  }

  /// Interrupt _vector's entry set to _handler, each byte that changes counted.
  void SetInterruptVector(std::uint8_t _vector, FarAddress _handler) noexcept;

  /// Interrupt _vector's entry as the BIOS and DOS set it at power-on, whatever it holds now: the handler a program that
  /// installs its own puts back when it is done.
  [[nodiscard]] FarAddress PowerOnInterruptVector(std::uint8_t _vector) const noexcept
  {
    return m_powerOnVectors[_vector];
  }

  /// The byte of the BIOS's ROM, F000:0000-FFFF, at _address: the IRET at F000:FD00 + n that each vector the BIOS does not
  /// serve names, the mouse driver's entry at F000:E060 (a NOP and an IRET), the model byte FFh at F000:FFFE, and zero
  /// everywhere else, FC00:0016 included, where an Amstrad's ROM has its name (D12). The BIOS's handlers are native, so their
  /// code is not there. Throws std::out_of_range for an address outside the ROM.
  [[nodiscard]] std::uint8_t RomByte(FarAddress _address) const;

  /// The first byte of the code at _handler, which a vector names: the ROM's (RomByte). The game asks only of int 33h's vector, which
  /// nothing points into its own code (change 3c). Throws std::out_of_range for an address outside the ROM.
  [[nodiscard]] std::uint8_t HandlerOpcode(FarAddress _handler) const;

  /// INT _vector, to the handler its vector names, which runs to its IRET: a native routine (Hook), or the ROM's IRET.
  void SoftwareInterrupt(std::uint8_t _vector);

  /// The handler at _handler, run to its IRET as INT runs one, with the interrupt's frame in place.
  void CallHandler(FarAddress _handler);

private:
  // The state at a loop's turn, kept to tell whether the next turn changed anything.
  struct Turn
  {
    std::array<std::uint16_t, MOST_TURN_WORDS> signature{};
    std::size_t signatureWords = 0;
    std::uint64_t memoryChanges = 0;
    std::uint64_t portWrites = 0;
    bool valid = false;
  };

  [[nodiscard]] StopReason Stopped() const noexcept;
  void Interrupt(std::uint8_t _vector);
  void RunHandler(FarAddress _handler);
  void NoteTurn(const Turn& _turn);
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
  Bios m_bios;
  Mouse m_mouse;
  DosFiles m_files;

  std::array<FarAddress, 256> m_vectors{};
  std::array<FarAddress, 256> m_powerOnVectors{};
  std::uint64_t m_vectorChanges = 0;
  std::uint64_t m_portWrites = 0;
  std::uint16_t m_flags;
  std::string m_commandTail;

  HookedRoutine m_entry;
  std::map<std::uint32_t, HookedRoutine> m_hooks;
  std::uint64_t m_interruptsTaken = 0;

  std::uint64_t m_spinLimit = DEFAULT_SPIN_LIMIT;
  std::uint64_t m_stepsSinceIdle = 0;
  Cycles m_runLimit = NO_EVENT;
  Turn m_lastTurn{};

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
