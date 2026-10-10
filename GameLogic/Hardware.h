// GameLogic/Hardware.h
#pragma once

#include "Pacing.h"
#include "Timing.h"

#include <cstdint>
#include <initializer_list>
#include <optional>

namespace Machine
{
class Pc;
} // namespace Machine

namespace Elite
{

/// What the mouse driver answers to a reset (int 33h AX=0).
struct MouseReset
{
  std::uint16_t status;      ///< AX: FFFFh when a driver is installed
  std::uint16_t buttonCount; ///< BX
};

/// What the mouse driver gives for int 33h AX=5: the buttons held, and the presses of the one button asked about.
struct MousePresses
{
  std::uint16_t buttons; ///< AX: the buttons held, bit 0 the left and bit 1 the right
  std::uint16_t presses; ///< BX: the presses of the button asked about since the last call
  std::uint16_t xPixels; ///< CX: where the last of them was, in the driver's virtual screen
  std::uint16_t yPixels; ///< DX
};

/// What the mouse driver gives for int 33h AX=0Bh: the motion since the last call.
struct MouseMotion
{
  std::uint16_t acrossMickeys; ///< CX, right positive
  std::uint16_t downMickeys;   ///< DX, down positive
};

// ── SaveLoad, StartUp, Timer and Input (level 4, group C) ──

/// What the BIOS's clock gives for int 1Ah AH=0.
struct BiosClock
{
  std::uint32_t ticks;     ///< CX:DX: the timer's ticks since midnight, about 18.2 a second
  std::uint8_t rolledOver; ///< AL: non-zero when midnight has passed since the clock was last read
};

/// What a DOS file service (int 21h) answers: CF, and AX.
struct DosAnswer
{
  bool failed;         ///< CF: the service failed
  std::uint16_t value; ///< AX: the error if it failed; otherwise a handle, a count of bytes, or AX as it went in
};

/// What DOS answers to int 21h AX=4300h: a DosAnswer, and the file's attributes in CX.
struct DosFileAttributes
{
  DosAnswer answer;
  std::uint16_t attributes; ///< CX: the attributes when the service did not fail, and CX as it went in when it did
};

/// The IBM PC's devices as the game drives them (ADR-014): what a de-assembled routine does to the speaker, the timer, the
/// interrupt controller, the keyboard, the game port, the mouse, the BIOS and DOS, named for what it does rather than by port or
/// vector. Each port operation is the original's own sequence of port accesses, in its order and at its width, so the emulated
/// devices and paced time's wait detection see what they saw. Each service operation sets the registers the service reads,
/// calls it as INT does (Pc::CallInterrupt), returns what it gives as a small typed result, and puts the processor's registers
/// back as they were: it has no register effect of its own. When the interpreter goes (D7), the implementation becomes native
/// devices and the routines do not change.
class Hardware
{
public:
  explicit Hardware(Machine::Pc& _pc) noexcept
    : m_pc(_pc)
  {
  }

  /// IN AL,61h: the system control port, the speaker's gate and data in bits 0 and 1.
  [[nodiscard]] std::uint8_t SystemControl();

  /// OUT 61h,_value.
  void SetSystemControl(std::uint8_t _value);

  /// The speaker's tone: OUT 43h,B6h, then _divisor's low and high bytes to the PIT's channel 2 at 42h.
  void SetToneDivisor(std::uint16_t _divisor);

  /// The timer's tick: OUT 43h,_mode, then _divisor's low and high bytes to the PIT's channel 0 at 40h.
  void SetTickDivisor(std::uint8_t _mode, std::uint16_t _divisor);

  /// OUT 20h,20h: the end of an interrupt, to the interrupt controller.
  void EndOfInterrupt();

  /// STI: interrupts are taken again when they fall due.
  void EnableInterrupts() noexcept;

  /// CLI.
  void DisableInterrupts() noexcept;

  /// One turn of a loop that can wait, where the original jumps back to CS:_loop (ADR-015). _carried are values the
  /// original holds in registers there: every one the next turn reads before it writes it, and every one the turn read
  /// from a device; one that cannot change between turns may be left out. A turn that changed no byte and no port, with
  /// the same values as the last, idles to the next device event, as the original's would; then the interrupts that fall
  /// due are taken. Only a routine hooked as one that waits may call it.
  void LoopTurn(std::uint16_t _loop, std::initializer_list<std::uint16_t> _carried);
  /// IN AL,60h: the scan code the keyboard sent.
  [[nodiscard]] std::uint8_t KeyboardData();

  /// The XT's acknowledgement of a scan code, a pulse on bit 7 of port 61h: IN AL,61h, then OUT 61h with bit 7 set and
  /// OUT 61h with it clear.
  void AcknowledgeKeyboard();

  /// IN AL,201h: the game port, the sticks' buttons in bits 4 to 7, each low while pressed.
  [[nodiscard]] std::uint8_t GamePortButtons();

  /// Int 33h AX=0: the mouse driver reset. With no driver, int 33h is the ROM's IRET, and AX and BX come back as they went.
  MouseReset ResetMouse();

  /// Int 33h AX=5, BX=_button: the buttons held, and _button's presses (0 the left, 1 the right). With no driver, AX, BX,
  /// CX and DX come back as they went: AX=5, BX=_button, and the CX and DX the processor held.
  [[nodiscard]] MousePresses ReadMousePresses(std::uint16_t _button);

  /// Int 33h AX=0Bh: the mouse's motion since the last call. With no driver, CX and DX come back as the processor held
  /// them.
  [[nodiscard]] MouseMotion ReadMouseMotion();

  /// Int 10h AH=0, AL=_mode: the BIOS sets the CGA's mode.
  void SetVideoMode(std::uint8_t _mode);

  /// Int 21h AH=9: DOS prints the text at _segment:_offset, up to its '$'.
  void PrintDosString(std::uint16_t _segment, std::uint16_t _offset);

  /// Int 16h AH=1: the key at the head of the BIOS's buffer, left there, if there is one.
  [[nodiscard]] std::optional<std::uint16_t> PeekBiosKey();

  /// Int 16h AH=0: the key at the head of the BIOS's buffer, taken out of it: the scan code in the high byte and the
  /// character in the low.
  std::uint16_t ReadBiosKey();

  // ── SaveLoad, StartUp, Timer and Input (level 4, group C) ──

  /// IN AL,3DAh: the CGA's status, bit 3 set during the vertical retrace.
  [[nodiscard]] std::uint8_t CgaStatus();

  /// OUT 3D9h,_value: the CGA's colour select register: the border's colour, and in graphics mode the background's, in
  /// bits 0-3, the bright palette in bit 4.
  void SetColorSelect(std::uint8_t _value);

  /// OUT 3D4h,0Eh, then OUT 3D5h,_value: the CRTC's register 14, the high byte of the cursor's address.
  void SetCursorAddressHigh(std::uint8_t _value);

  /// OUT 3D8h,_value: the CGA's mode control register: bit 3 the video on, bit 5 blinking.
  void SetModeControl(std::uint8_t _value);

  /// Int 10h AH=0Bh, BH=1, BL=_palette: the BIOS selects mode 4's palette.
  void SelectPalette(std::uint8_t _palette);

  /// OUT 201h,_value: fires the game port's one-shots, each high until its stick's resistance times it out. The port does
  /// not read the byte written.
  void FireGamePort(std::uint8_t _value);

  /// IN AL,201h: the game port, its one-shots in bits 0 to 3, each high while it runs, and the buttons in bits 4 to 7.
  [[nodiscard]] std::uint8_t GamePortOneShots();

  /// The 8088's cycles over instructions that native code stands in for, counted for the devices the instructions executed
  /// time (Pc::CountInstructionCycles): the game port's one-shots, which a stick read measures by counting polls.
  void CountInstructionCycles(Machine::Cycles _cycles) noexcept;

  /// Int 1Ah AH=0: the BIOS's clock, its rollover flag cleared.
  [[nodiscard]] BiosClock ReadBiosClock();

  /// Int 1Ah AH=1, CX:DX=_ticks: the BIOS's clock set.
  void SetBiosClock(std::uint32_t _ticks);

  /// The BIOS's handler for the timer's tick at _segment:_offset, run to its IRET as the game's own handler chains to it, by a
  /// far jump with the interrupt's frame in place. Pc::CallInterrupt reaches a handler through the vector table, so for the
  /// call int 8's vector at 0000:0020 names the BIOS's handler, and after it what it named before.
  void RunBiosTimerTick(std::uint16_t _segment, std::uint16_t _offset);

  /// Int 21h AX=0C0Ah: DOS empties its keyboard buffer and reads a line typed at the keyboard into the buffer at
  /// _segment:_offset, whose first byte is its size and whose second gets the count of characters read.
  void ReadDosLine(std::uint16_t _segment, std::uint16_t _offset);

  /// Int 21h AH=1Ah: DOS's disk transfer area, where find-first and find-next leave what they find, at _segment:_offset.
  void SetDiskTransferArea(std::uint16_t _segment, std::uint16_t _offset);

  /// Int 21h AX=4300h: the attributes of the file named at _segment:_name.
  [[nodiscard]] DosFileAttributes ReadFileAttributes(std::uint16_t _segment, std::uint16_t _name);

  /// Int 21h AX=4301h, CX=_attributes: the attributes of the file named at _segment:_name set.
  DosAnswer SetFileAttributes(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes);

  /// Int 21h AH=3Ch, CX=_attributes: the file named at _segment:_name created, or emptied; AX its handle.
  [[nodiscard]] DosAnswer CreateFile(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes);

  /// Int 21h AH=3Dh, AL=_access: the file named at _segment:_name opened, 0 to read, 1 to write, 2 both; AX its handle.
  [[nodiscard]] DosAnswer OpenFile(std::uint16_t _segment, std::uint16_t _name, std::uint8_t _access);

  /// Int 21h AH=3Fh: up to _bytes from the file _handle into _segment:_buffer; AX the count read.
  [[nodiscard]] DosAnswer ReadFile(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes);

  /// Int 21h AH=40h: _bytes from _segment:_buffer written to the file _handle; AX the count written.
  [[nodiscard]] DosAnswer WriteFile(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes);

  /// Int 21h AH=3Eh: the file _handle closed.
  DosAnswer CloseFile(std::uint16_t _handle);

  /// Int 21h AH=41h: the file named at _segment:_name deleted.
  DosAnswer DeleteFile(std::uint16_t _segment, std::uint16_t _name);

  /// Int 21h AH=4Eh, CX=_attributes: the first file matching the pattern at _segment:_pattern, with no attribute beyond
  /// _attributes' hidden, system and directory bits, into the disk transfer area.
  [[nodiscard]] DosAnswer FindFirstFile(std::uint16_t _segment, std::uint16_t _pattern, std::uint16_t _attributes);

  /// Int 21h AH=4Fh: the next file of the search the disk transfer area holds, into it.
  [[nodiscard]] DosAnswer FindNextFile();

  // ── Maths, Input, Text and SaveScreenshot (level 5, group A) ──

  /// The interrupts that fell due while they were off, taken now that they are on (Pc::TakeDueInterrupts): what the
  /// original's CPU does at the instruction after an STI that finds one pending. Reprogramming the PIT raises IRQ 0 at once
  /// when its output was low, and the original takes it with whichever handler the table names then.
  void TakeDueInterrupts();

  // ── Docked, Galaxy, StartUp and Scene (level 5, group B3) ──

  /// _point's cycles pass as they pass in a wait (Pc::Spend): the time the IBM PC spent on work the reference sets no pace
  /// for, paid where an interpreted run pays it (ADR-013). Only a routine hooked as one that waits may call it.
  void Spend(const PacingPoint& _point);

private:
  Machine::Pc& m_pc;
};

} // namespace Elite
