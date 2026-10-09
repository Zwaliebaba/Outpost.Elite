// Machine/Bios.h
#pragma once

#include "Registers.h"
#include "ServiceFault.h"

#include <cstdint>
#include <optional>

namespace Machine
{

class Memory;
class PortBus;

/// The IBM PC BIOS's video, keyboard and time-of-day services, at the call level, for exactly what the reference
/// calls (plan §1). Each service returns nothing when it serviced the call and the fault when it refused it, in which
/// case nothing was changed. The registers a call returns are the ones the IBM BIOS listing leaves, as noted.
///
/// The BIOS reaches the hardware the way the ROM does: the CGA through OUT on the port bus, video memory and the BIOS
/// data area through memory. It never calls a device directly.
class Bios
{
public:
  Bios(Memory& _memory, PortBus& _ports) noexcept;

  /// Int 10h.
  ///  * AH=00h, set mode AL, 0 to 6: what SET_MODE does, in its order -- 3D8h <- 0 (video off), CRTC registers 0-15
  ///    from the mode's table through 3D4h/3D5h, video memory B800:0000-3FFF cleared (0720h words in text modes,
  ///    zero in graphics), 3D8h <- the mode's control value, 3D9h <- 30h (3Fh in mode 6), and the data area's mode,
  ///    columns, page size, page, start, cursors and cursor shape set. Returns AX = the 3D9h value, as SET_MODE
  ///    leaves it. Mode 7 (monochrome), above 7, or AL bit 7 set: UnsupportedRequest.
  ///  * AH=0Bh, colour palette: BH=0 sets the background and border (3D9h bits 0-4 from BL), BH=1 the graphics
  ///    palette (3D9h bit 5 from BL bit 0), both read-modify-write of the data area's copy. Returns AL = the value
  ///    written and AH = the current mode. Any other BH: UnknownSubfunction.
  ///  * Anything else: UnknownFunction. The teletype (AH=0Eh) is not exposed: only DOS's string output uses it.
  [[nodiscard]] std::optional<FaultKind> Video(Registers& _regs);

  /// Int 16h. AH=01h: ZF set when the buffer at 0040:001E is empty, and AX = the word at its head either way, as
  /// KEYBOARD_IO does. AH=00h: the key at the head, which is removed; with an empty buffer it would wait forever,
  /// because the ROM's int 9 discards every key, so it is WouldBlock. Anything else: UnknownFunction.
  [[nodiscard]] std::optional<FaultKind> Keyboard(Registers& _regs) noexcept;

  /// Int 1Ah. AH=00h: CX:DX = the tick count, AL = the 24-hour flag, which is then cleared, AH = 0. AH=01h: the tick
  /// count from CX:DX, the flag cleared, AH = 0. Anything else: UnknownFunction.
  [[nodiscard]] std::optional<FaultKind> TimeOfDay(Registers& _regs) noexcept;

  /// Sets a video mode as int 10h AH=00h does. Returns false, and does nothing, for a mode above 6.
  bool SetMode(std::uint8_t _mode);

  /// Whether the teletype can write: in text modes 0 to 3. The graphics modes would need the ROM font, which is not
  /// part of the reference (ADR-001 D14), and nothing prints there.
  [[nodiscard]] bool TeletypeAvailable() const noexcept;

  /// The column of the active page's cursor.
  [[nodiscard]] std::uint8_t CursorColumn() const noexcept;

  /// Writes one character as the BIOS's WRITE_TTY does in a text mode: CR, LF and backspace move the cursor, anything
  /// else is written at the cursor keeping the attribute there, wrapping at the last column, and the screen scrolls
  /// up one line, with the attribute at the cursor, when the cursor would leave row 24. The cursor moves in the data
  /// area and on the CRTC (registers 0Eh and 0Fh). Returns false, and does nothing, outside a text mode or for BEL
  /// (07h), whose beep is not provided. Unlike the ROM it does not blank the CGA while it scrolls.
  bool WriteTeletype(std::uint8_t _character);

private:
  [[nodiscard]] std::uint8_t BiosByte(std::uint16_t _offset) const noexcept;
  [[nodiscard]] std::uint16_t BiosWord(std::uint16_t _offset) const noexcept;
  void SetCursor(std::uint8_t _row, std::uint8_t _column);
  void ScrollUp(std::uint8_t _attribute) noexcept;

  Memory& m_memory;
  PortBus& m_ports;
};

} // namespace Machine
