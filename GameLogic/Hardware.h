// GameLogic/Hardware.h
#pragma once

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

private:
  Machine::Pc& m_pc;
};

} // namespace Elite
