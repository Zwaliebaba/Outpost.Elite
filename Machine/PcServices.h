// Machine/PcServices.h
#pragma once

#include "Bios.h"
#include "Dos.h"
#include "HostServices.h"
#include "Mouse.h"
#include "ServiceFault.h"
#include "Timing.h"

#include <cstdint>
#include <optional>

namespace Machine
{

class FileStore;
class Memory;
class PortBus;

/// The software side of the PC the reference runs on: its ROM, BIOS, DOS and mouse driver, as the CPU's HostServices
/// (ADR-005 item 5). Named for the machine it completes rather than for one of its layers, because it is the one
/// object the integrator attaches to the CPU and checks after every step.
///
/// It routes int 10h, 16h and 1Ah to the Bios, int 20h and 21h to Dos, and int 33h to the Mouse when a driver is
/// present. Every other vector returns false, so the CPU vectors through the interrupt table: the game installs its
/// own int 0, 8, 9 and 24h handlers, and those run as written. A routed call the services refuse is serviced as
/// nothing at all, and its ServiceFault is kept for the integrator; only the first is kept until ClearFault.
///
/// Use: construct, PowerOn(), load the program with ExeLoader, StartProgram() with its PSP, attach to the Cpu with
/// SetHostServices, and after every step check Fault() and Terminated(). After int 20h the CPU sits halted in the ROM.
class PcServices final : public HostServices
{
public:
  struct Desc
  {
    Dos::DateTime startMoment; ///< DOS's date and time at power-on; the BIOS tick count starts at its time of day.
    bool mousePresent = false; ///< Whether a mouse driver is loaded.
  };

  /// _ports reaches the CGA; _files holds DOS's files; _clock is the machine's cycle counter, which the integrator
  /// owns and advances. All four must outlive the services.
  PcServices(Memory& _memory, PortBus& _ports, FileStore& _files, const Cycles& _clock, const Desc& _desc);

  /// What the PC's ROM leaves when it boots DOS: Firmware::Install, then video mode 3 set through the BIOS so the
  /// CGA and the BIOS data area agree.
  void PowerOn();

  /// Tells DOS which program runs (Dos::StartProgram), and clears any fault.
  void StartProgram(std::uint16_t _pspSegment);

  [[nodiscard]] bool ServiceInterrupt(Cpu& _cpu, std::uint8_t _vector) override;

  /// The first call refused since construction or ClearFault, if any.
  [[nodiscard]] const std::optional<ServiceFault>& Fault() const noexcept
  {
    return m_fault;
  }

  void ClearFault() noexcept
  {
    m_fault.reset();
  }

  /// Whether the program has ended through int 20h, and with what code.
  [[nodiscard]] bool Terminated() const noexcept
  {
    return m_dos.Terminated();
  }

  [[nodiscard]] std::uint8_t ExitCode() const noexcept
  {
    return m_dos.ExitCode();
  }

  /// Where the host feeds the mouse's motion and buttons.
  [[nodiscard]] Mouse& MouseDriver() noexcept
  {
    return m_mouse;
  }

  /// DOS, for its clock.
  [[nodiscard]] const Dos& DosKernel() const noexcept
  {
    return m_dos;
  }

private:
  Memory& m_memory;
  Desc m_desc;
  Bios m_bios;
  Dos m_dos;
  Mouse m_mouse;
  std::optional<ServiceFault> m_fault;
};

} // namespace Machine
