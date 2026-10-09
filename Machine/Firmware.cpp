#include "pch.h"

#include "Firmware.h"

#include "Memory.h"
#include "Timing.h"

#include <array>
#include <span>

namespace Machine
{

namespace
{

// The BIOS timer handler, int 8 (IRQ 0), at F000:E000. It is the IBM PC BIOS's TIMER_INT without the diskette
// motor time-out, because the machine has no diskette controller. The comments give each instruction's offset.
constexpr std::array<std::uint8_t, 0x37> TIMER_HANDLER = {
  0xFB,                               // 00  sti                       ; as the BIOS: interrupts back on
  0x1E,                               // 01  push ds
  0x50,                               // 02  push ax
  0xB8, 0x40, 0x00,                   // 03  mov ax, 0040h             ; the BIOS data area
  0x8E, 0xD8,                         // 06  mov ds, ax
  0xFF, 0x06, 0x6C, 0x00,             // 08  inc word [006Ch]          ; TIMER_LOW
  0x75, 0x04,                         // 0C  jnz 12h
  0xFF, 0x06, 0x6E, 0x00,             // 0E  inc word [006Eh]          ; TIMER_HIGH
  0x83, 0x3E, 0x6E, 0x00, 0x18,       // 12  cmp word [006Eh], 18h     ; 24 hours is 1800B0h ticks
  0x75, 0x15,                         // 17  jnz 2Eh
  0x81, 0x3E, 0x6C, 0x00, 0xB0, 0x00, // 19  cmp word [006Ch], 00B0h
  0x75, 0x0D,                         // 1F  jnz 2Eh
  0x31, 0xC0,                         // 21  xor ax, ax                ; a day has passed: start again from 0
  0xA3, 0x6E, 0x00,                   // 23  mov [006Eh], ax
  0xA3, 0x6C, 0x00,                   // 26  mov [006Ch], ax
  0xC6, 0x06, 0x70, 0x00, 0x01,       // 29  mov byte [0070h], 1       ; TIMER_OFL, which int 1Ah AH=00 reports
  0xCD, 0x1C,                         // 2E  int 1Ch                   ; the user timer hook (an IRET here)
  0xB0, 0x20,                         // 30  mov al, 20h               ; non-specific EOI
  0xE6, 0x20,                         // 32  out 20h, al               ; to the 8259
  0x58,                               // 34  pop ax
  0x1F,                               // 35  pop ds
  0xCF,                               // 36  iret
};

// The keyboard handler, int 9 (IRQ 1), at F000:E040. It does what the BIOS's KB_INT does to the hardware -- read the
// scan code, acknowledge it on port 61h, end the interrupt -- and keeps nothing.
constexpr std::array<std::uint8_t, 0x14> KEYBOARD_HANDLER = {
  0xFB,       // 00  sti
  0x50,       // 01  push ax
  0xE4, 0x60, // 02  in al, 60h                ; the scan code, discarded
  0xE4, 0x61, // 04  in al, 61h
  0x0C, 0x80, // 06  or al, 80h                ; bit 7 high: clear the keyboard
  0xE6, 0x61, // 08  out 61h, al
  0x24, 0x7F, // 0A  and al, 7Fh               ; and low again: enable it
  0xE6, 0x61, // 0C  out 61h, al
  0xB0, 0x20, // 0E  mov al, 20h               ; non-specific EOI
  0xE6, 0x20, // 10  out 20h, al
  0x58,       // 12  pop ax
  0xCF,       // 13  iret
};

// The int 33h entry when a mouse driver is present, at F000:E060. Never executed (Firmware.h).
constexpr std::array<std::uint8_t, 2> MOUSE_DRIVER = {
  0x90, // 00  nop                             ; anything but CF: IsMouseDriverInstalled looks at this byte
  0xCF, // 01  iret
};

// The terminate address, at F000:E070: where a program goes when it ends, and where a reset lands.
constexpr std::array<std::uint8_t, 4> HALT = {
  0xFA,       // 00  cli
  0xF4,       // 01  hlt
  0xEB, 0xFD, // 02  jmp short 01h             ; should anything wake it
};

// F000:FFF0, where the 8088 starts after a reset: jmp far F000:E070.
constexpr std::array<std::uint8_t, 5> RESET = {0xEA, 0x70, 0xE0, 0x00, 0xF0};

constexpr std::uint8_t IRET = 0xCF;
constexpr std::uint32_t VECTOR_COUNT = 256;
constexpr std::uint8_t VECTOR_TIMER = 0x08;
constexpr std::uint8_t VECTOR_KEYBOARD = 0x09;
constexpr std::uint8_t VECTOR_TERMINATE = 0x22;
constexpr std::uint8_t VECTOR_MOUSE = 0x33;

// The interrupt table, the BIOS data area and the DOS communication area at 0050:0000.
constexpr std::uint32_t LOW_MEMORY_BYTES = 0x600;
constexpr std::uint32_t ROM_BYTES = 0x10000;

// Equipment: an IPL diskette (bit 0), 64 KB on the planar (bits 2-3 = 11), an 80x25 colour display (bits 4-5 = 10),
// two diskette drives (bits 6-7 = 01) and a game adapter (bit 12). No serial ports, no printer, no coprocessor.
constexpr std::uint16_t EQUIPMENT_WORD = 0x106D;
constexpr std::uint16_t MEMORY_KB = 640;

// Video mode 3 as the BIOS's SET_MODE leaves it (Bios.cpp has the table for every mode).
constexpr std::uint8_t POWER_ON_MODE = 3;
constexpr std::uint16_t POWER_ON_COLUMNS = 80;
constexpr std::uint16_t POWER_ON_PAGE_BYTES = 4096;
constexpr std::uint16_t POWER_ON_CURSOR_SHAPE = 0x0607;
constexpr std::uint16_t CRTC_INDEX_PORT = 0x3D4;
constexpr std::uint8_t POWER_ON_MODE_CONTROL = 0x29;
constexpr std::uint8_t POWER_ON_COLOR_SELECT = 0x30;

// The PC's timer input is the 14.31818 MHz crystal divided by 12, and the BIOS counts one tick per 65,536 of those.
constexpr std::uint64_t TIMER_DIVISOR = 65536;
constexpr std::uint64_t CENTISECONDS_PER_SECOND = 100;

void SetVector(Memory& _memory, std::uint8_t _vector, std::uint16_t _offset) noexcept
{
  const std::uint32_t entry = static_cast<std::uint32_t>(_vector) * 4u;
  _memory.Write16(entry, _offset);
  _memory.Write16(entry + 2u, Firmware::ROM_SEGMENT);
}

void WriteRom(Memory& _memory, std::uint16_t _offset, std::span<const std::uint8_t> _bytes) noexcept
{
  _memory.Load(Memory::Linear(Firmware::ROM_SEGMENT, _offset), _bytes);
}

} // namespace

void Firmware::Install(Memory& _memory, const Desc& _desc) noexcept
{
  for (std::uint32_t linear = 0; linear < LOW_MEMORY_BYTES; ++linear)
  {
    _memory.Write8(linear, 0);
  }
  const std::uint32_t romBase = Memory::Linear(ROM_SEGMENT, 0);
  for (std::uint32_t offset = 0; offset < ROM_BYTES; ++offset)
  {
    _memory.Write8(romBase + offset, 0);
  }

  // The ROM.
  for (std::uint32_t vector = 0; vector < VECTOR_COUNT; ++vector)
  {
    _memory.Write8(Memory::Linear(ROM_SEGMENT, static_cast<std::uint16_t>(IRET_STUBS_OFFSET + vector)), IRET);
  }
  WriteRom(_memory, TIMER_HANDLER_OFFSET, TIMER_HANDLER);
  WriteRom(_memory, KEYBOARD_HANDLER_OFFSET, KEYBOARD_HANDLER);
  WriteRom(_memory, MOUSE_DRIVER_OFFSET, MOUSE_DRIVER);
  WriteRom(_memory, HALT_OFFSET, HALT);
  WriteRom(_memory, RESET_OFFSET, RESET);
  _memory.Write8(ROM_SEGMENT, MODEL_OFFSET, MODEL_IBM_PC);

  // The interrupt table.
  for (std::uint32_t vector = 0; vector < VECTOR_COUNT; ++vector)
  {
    SetVector(_memory, static_cast<std::uint8_t>(vector), static_cast<std::uint16_t>(IRET_STUBS_OFFSET + vector));
  }
  SetVector(_memory, VECTOR_TIMER, TIMER_HANDLER_OFFSET);
  SetVector(_memory, VECTOR_KEYBOARD, KEYBOARD_HANDLER_OFFSET);
  SetVector(_memory, VECTOR_TERMINATE, HALT_OFFSET);
  if (_desc.mousePresent)
  {
    SetVector(_memory, VECTOR_MOUSE, MOUSE_DRIVER_OFFSET);
  }

  // The BIOS data area. Everything not set here is zero: no serial or parallel ports, empty keyboard flags.
  _memory.Write16(DATA_SEGMENT, EQUIPMENT, EQUIPMENT_WORD);
  _memory.Write16(DATA_SEGMENT, MEMORY_SIZE_KB, MEMORY_KB);
  _memory.Write16(DATA_SEGMENT, KEYBOARD_HEAD, KEYBOARD_BUFFER);
  _memory.Write16(DATA_SEGMENT, KEYBOARD_TAIL, KEYBOARD_BUFFER);
  _memory.Write16(DATA_SEGMENT, KEYBOARD_BUFFER_START, KEYBOARD_BUFFER);
  _memory.Write16(DATA_SEGMENT, KEYBOARD_BUFFER_LIMIT, KEYBOARD_BUFFER_END);
  _memory.Write8(DATA_SEGMENT, VIDEO_MODE, POWER_ON_MODE);
  _memory.Write16(DATA_SEGMENT, VIDEO_COLUMNS, POWER_ON_COLUMNS);
  _memory.Write16(DATA_SEGMENT, VIDEO_PAGE_BYTES, POWER_ON_PAGE_BYTES);
  _memory.Write16(DATA_SEGMENT, VIDEO_START, 0);
  _memory.Write16(DATA_SEGMENT, CURSOR_SHAPE, POWER_ON_CURSOR_SHAPE);
  _memory.Write8(DATA_SEGMENT, ACTIVE_PAGE, 0);
  _memory.Write16(DATA_SEGMENT, CRTC_ADDRESS, CRTC_INDEX_PORT);
  _memory.Write8(DATA_SEGMENT, MODE_CONTROL, POWER_ON_MODE_CONTROL);
  _memory.Write8(DATA_SEGMENT, COLOR_SELECT, POWER_ON_COLOR_SELECT);
  _memory.Write16(DATA_SEGMENT, TIMER_TICKS, static_cast<std::uint16_t>(_desc.timerTicks & 0xFFFF));
  _memory.Write16(DATA_SEGMENT, TIMER_TICKS + 2, static_cast<std::uint16_t>(_desc.timerTicks >> 16));
  _memory.Write8(DATA_SEGMENT, TIMER_ROLLOVER, 0);
}

std::uint32_t Firmware::TimerTicksAt(std::uint32_t _centisecondsSinceMidnight) noexcept
{
  const std::uint64_t ticks = std::uint64_t{_centisecondsSinceMidnight} * CRYSTAL_HZ /
                              (CPU_CLOCK_DIVISOR * CYCLES_PER_PIT_TICK * TIMER_DIVISOR * CENTISECONDS_PER_SECOND);
  return ticks < TICKS_PER_DAY ? static_cast<std::uint32_t>(ticks) : TICKS_PER_DAY - 1;
}

} // namespace Machine
