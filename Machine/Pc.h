// Machine/Pc.h
#pragma once

#include "Cga.h"
#include "Cpu.h"
#include "ExeLoader.h"
#include "GamePort.h"
#include "Keyboard.h"
#include "Memory.h"
#include "PcServices.h"
#include "Pic.h"
#include "Pit.h"
#include "PortRouter.h"
#include "Speaker.h"
#include "Timing.h"

#include <cstdint>
#include <span>

namespace Machine
{

class FileStore;

/// Why RunUntil returned.
enum class StopReason : std::uint8_t
{
  Reached,    ///< The clock reached the cycle asked for.
  Fault,      ///< The services refused a call; PcServices::Fault says which.
  Terminated, ///< The program ended through int 20h.
  Deadlocked  ///< The CPU halted with interrupts off, and nothing can wake it.
};

/// The IBM PC the reference runs on, put together (ADR-006): an 8088, 1 MiB of memory, the 8259, the
/// 8253, the keyboard's 8255, the speaker, the game port and the CGA on one I/O bus, and the ROM, BIOS,
/// DOS and mouse driver at the call level.
///
/// Time is the machine's own cycle count, the 8088's clock at a third of the crystal (Timing.h). Step()
/// runs one CPU step, adds its cycles to the clock, and then lets the devices that raise interrupts
/// catch up with it, so a request raised during a step is seen at the next instruction boundary. There
/// is no wall clock anywhere: a run is a function of the program, its start moment and its inputs, and
/// two runs given the same are identical to the cycle.
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

  /// Loads a program as DOS's EXEC does (ExeLoader) and starts the CPU at its entry, in the registers
  /// MS-DOS gives a program. Nothing changes if the load fails.
  [[nodiscard]] LoadError Load(std::span<const std::uint8_t> _file, const ExeLoader::Desc& _desc, LoadedProgram& _program);

  /// One CPU step, then the timer and the keyboard catch up with the clock.
  void Step();

  /// Steps until the clock reaches _cycle, or until the program can go no further.
  [[nodiscard]] StopReason RunUntil(Cycles _cycle);

  [[nodiscard]] Cycles Clock() const noexcept
  {
    return m_clock;
  }

  [[nodiscard]] Cpu& Processor() noexcept
  {
    return m_cpu;
  }

  [[nodiscard]] Memory& Ram() noexcept
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

private:
  void MapPorts(std::uint16_t _first, std::uint16_t _last, PortBus& _device);
  [[nodiscard]] StopReason Stopped() const noexcept;

  // Declared in the order they are built: each device holds references to the ones above it.
  Cycles m_clock = 0;
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
};

} // namespace Machine
