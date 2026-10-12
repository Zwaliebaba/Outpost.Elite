// GameLogic/Hardware.h
#pragma once

#include "Counted.h"
#include "Pacing.h"
#include "Platform.h"
#include "Timing.h"

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace Elite
{

/// The IBM PC's devices as the game drives them (ADR-014): what a de-assembled routine does to the speaker, the timer, the
/// interrupt controller, the keyboard, the game port, the mouse, the CGA, the BIOS and DOS, named for what it does rather than
/// by port or vector. Since D33 every operation is the native Platform's (ADR-021), and the routines did not change.
///
/// What paced time sees is kept: each operation counts the port writes the original's own sequence of port accesses made, in
/// the BIOS's and DOS's services as in the game's own OUTs, and every byte of memory a service changes is counted where it
/// lies, so a loop's turn idles exactly where it did (ADR-015). Each service operation returns what the service gives as a
/// small typed result. Hardware holds no state of its own: a device's state is the platform's.
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

  /// Int 33h AX=0: the mouse driver reset. With no driver, int 33h is the ROM's IRET. The game reads nothing it answers.
  void ResetMouse();

  /// Int 33h AX=5, BX=1: AX, the buttons held, bit 0 the left and bit 1 the right. With no driver AX comes back as it went: 5.
  /// The game reads nothing else of the answer.
  [[nodiscard]] std::uint16_t ReadMousePresses();

  /// Int 33h AX=0Bh: the mouse's motion since the last call. With no driver, CX and DX come back as 0.
  [[nodiscard]] MouseMotion ReadMouseMotion();

  /// Int 10h AH=0, AL=_mode: the BIOS sets the CGA's mode. Throws std::logic_error for a mode above 6, which the game never sets.
  void SetVideoMode(std::uint8_t _mode);

  /// Int 21h AH=9: DOS prints _text, the text DS:DX names, up to its '$', a character at a time through the BIOS's teletype. A
  /// text with no '$' throws std::logic_error, and nothing is printed.
  void PrintDosString(std::string_view _text);

  /// IN AL,3DAh: the CGA's status, bit 3 set during the vertical retrace.
  [[nodiscard]] std::uint8_t CgaStatus();

  /// OUT 3D9h,_value: the CGA's colour select register: the border's colour, and in graphics mode the background's, in
  /// bits 0-3, the bright palette in bit 4.
  void SetColorSelect(std::uint8_t _value);

  /// The frame of flight just drawn, as _snapshot holds it, to the platform for a renderer (ADR-032): a write to no port and no
  /// byte of the game's state. _continues is false for the first frame of a flight, which continues nothing before it.
  void PublishFlightSnapshot(const FlightSnapshot& _snapshot, bool _continues) noexcept
  {
    m_platform.PublishFlightSnapshot(_snapshot, _continues);
  }

  /// The space view's layers (ADR-033), which watch the drawing buffer and write no port and no byte of the game's state: a
  /// frame of flight's drawing begins, an object is about to be drawn, the one being drawn is a blueprint's model, and the
  /// object in slot _slot is drawn (SpaceViewLayers).
  void BeginSpaceViewFrame() noexcept
  {
    m_platform.BeginSpaceViewFrame();
  }

  void BeginSpaceViewObject() noexcept
  {
    m_platform.BeginSpaceViewObject();
  }

  void NoteSpaceViewModel() noexcept
  {
    m_platform.NoteSpaceViewModel();
  }

  void EndSpaceViewObject(std::uint8_t _slot) noexcept
  {
    m_platform.EndSpaceViewObject(_slot);
  }

  /// A background drawn between BeginSpaceViewObject and this, the stardust or the title's planet, goes under the models.
  void EndSpaceViewBackground() noexcept
  {
    m_platform.EndSpaceViewObject(SpaceViewLayers::NO_SLOT);
  }

  /// OUT 3D4h,0Eh, then OUT 3D5h,_value: the CRTC's register 14, the high byte of the cursor's address.
  void SetCursorAddressHigh(std::uint8_t _value);

  /// OUT 3D8h,_value: the CGA's mode control register: bit 3 the video on, bit 5 blinking.
  void SetModeControl(std::uint8_t _value);

  /// Int 10h AH=0Bh, BH=1, BL=_palette: the BIOS selects mode 4's palette.
  void SelectPalette(std::uint8_t _palette);

  /// OUT 201h,AL: fires the game port's one-shots, each high until its stick's resistance times it out. The port does not
  /// read the byte written, so none is passed.
  void FireGamePort();

  /// IN AL,201h: the game port, its one-shots in bits 0 to 3, each high while it runs, and the buttons in bits 4 to 7.
  [[nodiscard]] std::uint8_t GamePortOneShots();

  /// The 8088's cycles over instructions that native code stands in for, counted for the devices the instructions executed
  /// time (Platform::CountInstructionCycles): the game port's one-shots, which a stick read measures by counting polls.
  void CountInstructionCycles(Cycles _cycles) noexcept;

  /// Int 1Ah AH=0: the BIOS's clock, its rollover flag cleared.
  [[nodiscard]] BiosClock ReadBiosClock();

  // DOS's file services, int 21h: native, on the platform's DosFiles, which answers as the emulated DOS did. Where the original
  // hands DOS an address in DS:DX, a call takes the name there, up to its NUL, or the buffer there, a span of the member that
  // holds it (D38).

  /// Int 21h AH=1Ah: DOS's disk transfer area, where find-first and find-next leave what they find: _area, of the game's state.
  void SetDiskTransferArea(std::span<Counted<std::uint8_t>> _area);

  /// Int 21h AX=4300h: the attributes of the file _name names.
  [[nodiscard]] DosFileAttributes ReadFileAttributes(std::string_view _name);

  /// Int 21h AX=4301h, CX=_attributes: the attributes of the file _name names set.
  DosAnswer SetFileAttributes(std::string_view _name, std::uint16_t _attributes);

  /// Int 21h AH=3Ch, CX=_attributes: the file _name names created, or emptied, and stamped with DOS's time at the platform's
  /// clock; AX its handle.
  [[nodiscard]] DosAnswer CreateFile(std::string_view _name, std::uint16_t _attributes);

  /// Int 21h AH=3Dh, AL=_access: the file _name names opened, 0 to read, 1 to write, 2 both; AX its handle.
  [[nodiscard]] DosAnswer OpenFile(std::string_view _name, std::uint8_t _access);

  /// Int 21h AH=3Fh: up to all of _buffer's bytes, of the game's state, from the file _handle into it; AX the count read.
  [[nodiscard]] DosAnswer ReadFile(std::uint16_t _handle, std::span<Counted<std::uint8_t>> _buffer);

  /// Int 21h AH=40h: _buffer's bytes written to the file _handle, stamped with DOS's time at the platform's clock; AX the count
  /// written.
  [[nodiscard]] DosAnswer WriteFile(std::uint16_t _handle, std::span<const std::uint8_t> _buffer);

  /// Int 21h AH=3Eh: the file _handle closed.
  DosAnswer CloseFile(std::uint16_t _handle);

  /// Int 21h AH=41h: the file _name names deleted.
  DosAnswer DeleteFile(std::string_view _name);

  /// Int 21h AH=4Eh, CX=_attributes: the first file matching _pattern, with no attribute beyond _attributes' hidden, system
  /// and directory bits, into the disk transfer area.
  [[nodiscard]] DosAnswer FindFirstFile(std::string_view _pattern, std::uint16_t _attributes);

  /// Int 21h AH=4Fh: the next file of the search the disk transfer area holds, into it.
  [[nodiscard]] DosAnswer FindNextFile();

  /// _point's cycles pass as they pass in a wait (Platform::Spend): the time the IBM PC spent on work the reference sets no
  /// pace for, paid where an interpreted run paid it (ADR-013).
  void Spend(const PacingPoint& _point);

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
