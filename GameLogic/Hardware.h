// GameLogic/Hardware.h
#pragma once

#include "Pacing.h"
#include "Platform.h"
#include "Timing.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace Elite
{

// ── Start, GameLoop and RunFlight (level 5, group F) ──

/// What DOS answers to int 21h AH=30h: its version.
struct DosVersion
{
  std::uint8_t major; ///< AL: 0 from DOS 1, which does not know the call
  std::uint8_t minor; ///< AH
};

/// What DOS answers to int 21h AH=2Ch: the time of day.
struct DosTime
{
  std::uint8_t hours;      ///< CH
  std::uint8_t minutes;    ///< CL
  std::uint8_t seconds;    ///< DH
  std::uint8_t hundredths; ///< DL
};

/// The IBM PC's devices as the game drives them (ADR-014): what a de-assembled routine does to the speaker, the timer, the
/// interrupt controller, the keyboard, the game port, the mouse, the CGA, the BIOS and DOS, named for what it does rather than
/// by port or vector. Since D33 every operation is the native Platform's (ADR-021), and the routines did not change.
///
/// What paced time sees is kept: each operation counts the port writes the original's own sequence of port accesses made, in
/// the BIOS's and DOS's services as in the game's own OUTs, and every byte of memory a service changes is counted where it
/// lies, so a loop's turn idles exactly where it did (ADR-015). Each service operation returns what the service gives as a
/// small typed result, and a call the services refuse stops the run (Platform::Refuse). Hardware holds no state of its own: a
/// device's state is the platform's.
class Hardware
{
public:
  explicit Hardware(Platform& _platform) noexcept
    : m_platform(_platform)
  {
  }

  /// IN AL,61h: the system control port, the speaker's gate and data in bits 0 and 1.
  [[nodiscard]] std::uint8_t SystemControl();

  /// OUT 61h,_value.
  void SetSystemControl(std::uint8_t _value);

  /// The speaker's tone: OUT 43h,B6h, then _divisor's low and high bytes to the timer's channel 2 at 42h.
  void SetToneDivisor(std::uint16_t _divisor);

  /// The timer's tick: OUT 43h,_mode, then _divisor's low and high bytes to the timer's channel 0 at 40h. A control word can
  /// raise IRQ 0 at once: with interrupts on, it is taken here, as the CPU takes it at the next instruction.
  void SetTickDivisor(std::uint8_t _mode, std::uint16_t _divisor);

  /// OUT 20h,20h: the end of an interrupt, to the interrupt controller. With interrupts on, one it held back is taken here.
  void EndOfInterrupt();

  /// STI: interrupts are taken again when they fall due, and those due now are taken here, as the CPU takes them after the
  /// instruction that follows STI.
  void EnableInterrupts();

  /// CLI.
  void DisableInterrupts() noexcept;

  /// One turn of a loop that can wait, where the original jumps back to CS:_loop (ADR-015). _carried are values the
  /// original holds in registers there: every one the next turn reads before it writes it, and every one the turn read
  /// from a device; one that cannot change between turns may be left out. A turn that changed no byte and no port, with
  /// the same values as the last, idles to the next device event, as the original's would; then the interrupts that fall
  /// due are taken.
  void LoopTurn(std::uint16_t _loop, std::initializer_list<std::uint16_t> _carried);

  /// IN AL,60h: the scan code the keyboard sent.
  [[nodiscard]] std::uint8_t KeyboardData();

  /// The XT's acknowledgement of a scan code, a pulse on bit 7 of port 61h: IN AL,61h, then OUT 61h with bit 7 set and
  /// OUT 61h with it clear.
  void AcknowledgeKeyboard();

  /// IN AL,201h: the game port, the sticks' buttons in bits 4 to 7, each low while pressed.
  [[nodiscard]] std::uint8_t GamePortButtons();

  /// Int 33h AX=0: the mouse driver reset. With no driver, int 33h is the ROM's IRET, and AX comes back as it went: 0, and
  /// BX as 0.
  MouseReset ResetMouse();

  /// Int 33h AX=5, BX=_button: the buttons held, and _button's presses (0 the left, 1 the right). With no driver, AX and BX
  /// come back as they went, AX=5 and BX=_button, and CX and DX as 0.
  [[nodiscard]] MousePresses ReadMousePresses(std::uint16_t _button);

  /// Int 33h AX=0Bh: the mouse's motion since the last call. With no driver, CX and DX come back as 0.
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
  /// time (Platform::CountInstructionCycles): the game port's one-shots, which a stick read measures by counting polls.
  void CountInstructionCycles(Cycles _cycles) noexcept;

  /// Int 1Ah AH=0: the BIOS's clock, its rollover flag cleared.
  [[nodiscard]] BiosClock ReadBiosClock();

  /// Int 1Ah AH=1, CX:DX=_ticks: the BIOS's clock set.
  void SetBiosClock(std::uint32_t _ticks);

  /// The BIOS's handler for the timer's tick at _segment:_offset, run to its IRET as the game's own handler chains to it, by a
  /// far jump with the interrupt's frame in place (Platform::CallHandler).
  void RunBiosTimerTick(std::uint16_t _segment, std::uint16_t _offset);

  // DOS's file services, int 21h: native, on the platform's DosFiles, which answers as the emulated DOS did.

  /// Int 21h AH=1Ah: DOS's disk transfer area, where find-first and find-next leave what they find, at _segment:_offset.
  void SetDiskTransferArea(std::uint16_t _segment, std::uint16_t _offset);

  /// Int 21h AX=4300h: the attributes of the file named at _segment:_name.
  [[nodiscard]] DosFileAttributes ReadFileAttributes(std::uint16_t _segment, std::uint16_t _name);

  /// Int 21h AX=4301h, CX=_attributes: the attributes of the file named at _segment:_name set.
  DosAnswer SetFileAttributes(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes);

  /// Int 21h AH=3Ch, CX=_attributes: the file named at _segment:_name created, or emptied, and stamped with DOS's time at the
  /// platform's clock; AX its handle.
  [[nodiscard]] DosAnswer CreateFile(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes);

  /// Int 21h AH=3Dh, AL=_access: the file named at _segment:_name opened, 0 to read, 1 to write, 2 both; AX its handle.
  [[nodiscard]] DosAnswer OpenFile(std::uint16_t _segment, std::uint16_t _name, std::uint8_t _access);

  /// Int 21h AH=3Fh: up to _bytes from the file _handle into _segment:_buffer; AX the count read.
  [[nodiscard]] DosAnswer ReadFile(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes);

  /// Int 21h AH=40h: _bytes from _segment:_buffer written to the file _handle, stamped with DOS's time at the platform's
  /// clock; AX the count written.
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

  // ── Docked, Galaxy, StartUp and Scene (level 5, group B3) ──

  /// _point's cycles pass as they pass in a wait (Platform::Spend): the time the IBM PC spent on work the reference sets no
  /// pace for, paid where an interpreted run paid it (ADR-013).
  void Spend(const PacingPoint& _point);

  // ── Start, GameLoop and RunFlight (level 5, group F) ──

  /// Int 21h AH=30h, AL=_al: DOS's version, 3.30. AL goes in as the caller has it: DOS 5 and later read it as which of two
  /// answers to give in BH.
  [[nodiscard]] DosVersion ReadDosVersion(std::uint8_t _al);

  /// Int 21h AH=2Ch: DOS's time of day.
  [[nodiscard]] DosTime ReadDosTime();

  // ── What the emulated PC kept in memory, outside the game's (D33) ──

  /// Interrupt _vector's entry in the vector table, at 0000:_vector*4 on the PC: the handler an INT or an IRQ reaches.
  [[nodiscard]] FarAddress InterruptVector(std::uint8_t _vector);

  /// Interrupt _vector's entry set to _handler: how the game installs its own handlers and puts the BIOS's back.
  void SetInterruptVector(std::uint8_t _vector, FarAddress _handler);

  /// Interrupt _vector's entry as the BIOS set it at power-on (Platform::PowerOnInterruptVector): the handler the game puts
  /// back when it leaves, where the original puts back the entry it kept in its own memory before it installed its own (D33,
  /// piece 3). The game's handlers are the only ones it replaces, and it puts each back before it installs it again, so the
  /// entry it kept was always this one.
  [[nodiscard]] FarAddress BiosInterruptVector(std::uint8_t _vector) const noexcept;

  /// The byte of the BIOS's ROM at _segment:_offset (Platform::RomByte): where the game looks for an Amstrad's name, and
  /// where a vector the BIOS serves points.
  [[nodiscard]] std::uint8_t RomByte(std::uint16_t _segment, std::uint16_t _offset);

  /// The first byte of the code a vector names (Platform::HandlerOpcode): whether a driver is behind int 33h, or the ROM's
  /// IRET.
  [[nodiscard]] std::uint8_t HandlerOpcode(FarAddress _handler);

  /// The command tail DOS gave the program, which its PSP held at 81h, its length at 80h: what the program was started with.
  [[nodiscard]] std::string_view CommandTail() const noexcept;

private:
  Platform& m_platform;
};

} // namespace Elite
