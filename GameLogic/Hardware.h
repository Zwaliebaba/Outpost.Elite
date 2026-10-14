// GameLogic/Hardware.h
#pragma once

#include "Pacing.h"
#include "Platform.h"
#include "Timing.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace Elite
{

/// The IBM PC's devices as the game drives them (ADR-014): what a de-assembled routine does to the speaker, the timer, the
/// interrupt controller, the keyboard, the game port, the mouse, the CGA, the BIOS and DOS, named for what it does rather than
/// by port or vector. Since D33 every operation is the native Platform's (ADR-021), and the routines did not change.
///
/// Each service operation returns what the service gives as a small typed result. Hardware holds no state of its own: a device's state is the platform's.
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

  /// The game's tick started, a millisecond from now and every millisecond after (D87), or stopped: where the original
  /// programmed the timer's channel 0 to about 1 kHz and back. With interrupts on, one now due is taken here.
  void SetTickRunning(bool _running);

  /// OUT 20h,20h: the end of an interrupt, to the interrupt controller. With interrupts on, one it held back is taken here.
  void EndOfInterrupt();

  /// STI: interrupts are taken again when they fall due, and those due now are taken here, as the CPU takes them after the
  /// instruction that follows STI.
  void EnableInterrupts();

  /// CLI.
  void DisableInterrupts() noexcept;

  /// A wait's poll that found what it waits for missing (ADR-039): the clock moves to the next device event, as the original's
  /// loop idles there; the routine that waits co_awaits what this gives (D77), and then the interrupts that fall due are taken.
  [[nodiscard]] Platform::RunAwaiter Wait();

  /// A turn of a loop that waits for nothing at that turn: a poll that took something it does not want, or a loop the original
  /// never leaves. The clock stands still, and a run that does nothing else stops as Spinning (Platform::Spin).
  void Spin();

  /// The key event the keyboard's interrupt delivered (D88), taken: the next one can then arrive. Only inside the keyboard's
  /// handler.
  [[nodiscard]] KeyEvent TakeKey() noexcept;

  /// The mouse (ADR-051): the game reads it directly, where the original called its driver through int 33h.
  [[nodiscard]] Mouse& MouseDevice() noexcept;

  /// The CGA set to _mode (Cga::SetMode), where the original asked the BIOS's int 10h.
  void SetVideoMode(CgaMode _mode) noexcept;

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

  /// A frame of a tunnel, which the renderer draws nothing of, to the platform as the game's own picture of the view (D68).
  void PublishSpaceViewPicture(const FlightSnapshot& _snapshot) noexcept
  {
    m_platform.PublishSpaceViewPicture(_snapshot);
  }

  /// The space view's layers (ADR-033, ADR-035), which watch the drawing buffer and write no port and no byte of the game's state:
  /// a frame of flight's drawing begins, an object is about to be drawn, the one being drawn is a blueprint's model or the
  /// renderer's to draw, and the object in slot _slot is drawn (SpaceViewLayers).
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

  void NoteSpaceViewRendered() noexcept
  {
    m_platform.NoteSpaceViewRendered();
  }

  void EndSpaceViewObject(std::uint8_t _slot) noexcept
  {
    m_platform.EndSpaceViewObject(_slot);
  }

  /// What was drawn between BeginSpaceViewObject and this, no slot's, is the renderer's to draw: the stardust, the beams, the
  /// sights, the title's planet (ADR-035).
  void EndSpaceViewRendered() noexcept
  {
    m_platform.NoteSpaceViewRendered();
    m_platform.EndSpaceViewObject(SpaceViewLayers::NO_SLOT);
  }

  /// A laser's beam drawn across the view, for the frame's snapshot (ADR-035): from (_fromX, _fromRow) to (_toX, _toRow) of the
  /// drawing buffer, in pixel value _color. No port and no byte of the game's state is written.
  void NoteSpaceViewBeam(std::int16_t _fromX, std::int16_t _fromRow, std::int16_t _toX, std::int16_t _toRow, std::uint8_t _color) noexcept
  {
    m_platform.NoteSpaceViewBeam(SnapshotBeam{.fromX = _fromX, .fromRow = _fromRow, .toX = _toX, .toRow = _toRow, .color = _color});
  }

  /// OUT 3D4h,0Eh, then OUT 3D5h,_value: the CRTC's register 14, the high byte of the cursor's address.
  void SetCursorAddressHigh(std::uint8_t _value);

  /// OUT 3D8h,_value: the CGA's mode control register: bit 3 the video on, bit 5 blinking.
  void SetModeControl(std::uint8_t _value);

  /// The platform's file store, where the commander files are (D78): the game reads and writes it directly, where the original
  /// went through DOS's int 21h.
  [[nodiscard]] FileStore& Files() noexcept;

  /// The stamp a file written now takes: DOS's date and time at the platform's clock (Platform::FileStampNow).
  [[nodiscard]] FileStamp FileStampNow() const;

  /// _point's time passes as it passes in a wait (Platform::Spend): the time the IBM PC spent on work the reference sets no
  /// pace for, paid where an interpreted run paid it (ADR-013).
  [[nodiscard]] Task<void> Spend(const PacingPoint& _point);

  // ── What the emulated PC kept in memory, outside the game's (D33) ──

  /// What serves _irq now (Platform::Handler): the BIOS's handler, the game's own, or a stray vector.
  [[nodiscard]] IrqHandler Handler(Irq _irq) const noexcept;

  /// _handler serves _irq from now on: how the game installs its own handlers and puts the BIOS's back, where the original
  /// writes the interrupt's vector (ADR-041).
  void SetHandler(Irq _irq, IrqHandler _handler) noexcept;

  /// Whether the game was started with the cheat on (Platform::CheatRequested, D80).
  [[nodiscard]] bool CheatRequested() const noexcept;

private:
  Platform& m_platform;
};

} // namespace Elite
