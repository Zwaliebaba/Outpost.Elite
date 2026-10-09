// Machine/Firmware.h
#pragma once

#include <cstdint>

namespace Machine
{

class Memory;

/// What an IBM PC (model byte FF) with a CGA holds in ROM and low memory when its BIOS hands over to DOS: the
/// interrupt table, the BIOS data area, and the few bytes of ROM code the reference can reach.
///
/// There is no BIOS ROM image. The software interrupts the reference calls are serviced at the call level by
/// PcServices, before the CPU would vector (ADR-005 item 5). The table and the stubs exist so that what the program
/// reads from them, and what the CPU runs when nothing services a call, is what a PC has:
///
///   * Vector n points at its own IRET, F000:FD00 + n, so a trace shows which vector was taken.
///   * Vector 8 (IRQ 0) points at the BIOS timer handler at F000:E000: real 8086 code that counts the tick at
///     0040:006C, rolls it over at 24 hours, calls int 1Ch and sends EOI. The game installs its own int 8 handler,
///     and puts this one back around every disk operation and on the way out to DOS.
///   * Vector 9 (IRQ 1) points at F000:E040, which reads the scan code from port 60h, pulses port 61h bit 7 high
///     and low to acknowledge it, sends EOI and discards the key. Nothing buffers keys, so int 16h never has one.
///   * Vector 22h, DOS's terminate address, points at a CLI and a HLT at F000:E070, because there is no parent
///     program to return to. Int 20h ends the program there (Dos::Terminate).
///   * Vector 33h points at its IRET when there is no mouse. With a mouse it points at F000:E060, a NOP before an
///     IRET: the game's IsMouseDriverInstalled (CS:02D4) takes a non-zero vector whose first byte is not CF (IRET) as
///     a driver. The driver's functions are serviced by PcServices, so that stub never runs.
///
/// The rest of F000 is zero. In particular FC00:0016 does not hold "Amstrad", which the game compares at start-up
/// (ADR-001 D12): the emulated machine is an IBM PC. F000:FFF0, where the 8088 starts after a reset, jumps to the
/// halt, since there is no POST to run. F000:FFFE holds the model byte FF.
class Firmware
{
public:
  struct Desc
  {
    std::uint32_t timerTicks = 0; ///< The BIOS tick count at power-on (0040:006C): the time of day DOS was given.
    bool mousePresent = false;    ///< Whether a mouse driver has hooked int 33h.
  };

  // The ROM segment and what is in it.
  static constexpr std::uint16_t ROM_SEGMENT = 0xF000;
  static constexpr std::uint16_t TIMER_HANDLER_OFFSET = 0xE000;
  static constexpr std::uint16_t KEYBOARD_HANDLER_OFFSET = 0xE040;
  static constexpr std::uint16_t MOUSE_DRIVER_OFFSET = 0xE060;
  static constexpr std::uint16_t HALT_OFFSET = 0xE070;
  static constexpr std::uint16_t IRET_STUBS_OFFSET = 0xFD00;
  static constexpr std::uint16_t RESET_OFFSET = 0xFFF0;
  static constexpr std::uint16_t MODEL_OFFSET = 0xFFFE;
  static constexpr std::uint8_t MODEL_IBM_PC = 0xFF;

  // The BIOS data area at 0040:0000, with the IBM PC Technical Reference's names.
  static constexpr std::uint16_t DATA_SEGMENT = 0x0040;
  static constexpr std::uint16_t EQUIPMENT = 0x10;             // EQUIP_FLAG
  static constexpr std::uint16_t MEMORY_SIZE_KB = 0x13;        // MEMORY_SIZE
  static constexpr std::uint16_t KEYBOARD_HEAD = 0x1A;         // BUFFER_HEAD
  static constexpr std::uint16_t KEYBOARD_TAIL = 0x1C;         // BUFFER_TAIL
  static constexpr std::uint16_t KEYBOARD_BUFFER = 0x1E;       // KB_BUFFER: 16 words, scan code high, ASCII low
  static constexpr std::uint16_t KEYBOARD_BUFFER_END = 0x3E;   // KB_BUFFER_END
  static constexpr std::uint16_t VIDEO_MODE = 0x49;            // CRT_MODE
  static constexpr std::uint16_t VIDEO_COLUMNS = 0x4A;         // CRT_COLS
  static constexpr std::uint16_t VIDEO_PAGE_BYTES = 0x4C;      // CRT_LEN
  static constexpr std::uint16_t VIDEO_START = 0x4E;           // CRT_START
  static constexpr std::uint16_t CURSOR_POSITIONS = 0x50;      // CURSOR_POSN: 8 words, column low, row high
  static constexpr std::uint16_t CURSOR_SHAPE = 0x60;          // CURSOR_MODE: end line low, start line high
  static constexpr std::uint16_t ACTIVE_PAGE = 0x62;           // ACTIVE_PAGE
  static constexpr std::uint16_t CRTC_ADDRESS = 0x63;          // ADDR_6845
  static constexpr std::uint16_t MODE_CONTROL = 0x65;          // CRT_MODE_SET: the last value written to 3D8h
  static constexpr std::uint16_t COLOR_SELECT = 0x66;          // CRT_PALLETTE: the last value written to 3D9h
  static constexpr std::uint16_t TIMER_TICKS = 0x6C;           // TIMER_LOW, then TIMER_HIGH at 6Eh
  static constexpr std::uint16_t TIMER_ROLLOVER = 0x70;        // TIMER_OFL
  static constexpr std::uint16_t KEYBOARD_BUFFER_START = 0x80; // BUFFER_START (PC/XT BIOS and later)
  static constexpr std::uint16_t KEYBOARD_BUFFER_LIMIT = 0x82; // BUFFER_END

  /// The tick count the BIOS takes for 24 hours, at which its timer handler starts again from 0.
  static constexpr std::uint32_t TICKS_PER_DAY = 0x1800B0;

  /// Writes the interrupt table, the BIOS data area and the ROM. Everything from 0000:0000 to 0000:05FF and the whole
  /// of F000 is overwritten. The data area describes video mode 3 (80x25 colour), as POST leaves it; PcServices then
  /// sets that mode through the BIOS so that the CGA agrees with it.
  static void Install(Memory& _memory, const Desc& _desc) noexcept;

  /// The BIOS tick count for a time of day: the PC's timer runs at 14.31818 MHz / 12 / 65,536, about 18.2 Hz.
  [[nodiscard]] static std::uint32_t TimerTicksAt(std::uint32_t _centisecondsSinceMidnight) noexcept;
};

} // namespace Machine
