// GameLogic/Bios.h
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Elite
{

class Cga;
class GameState;

/// What the BIOS's clock gives for int 1Ah AH=0.
struct BiosClock
{
  std::uint32_t ticks;     ///< CX:DX: the timer's ticks since midnight, about 18.2 a second
  std::uint8_t rolledOver; ///< AL: non-zero when midnight has passed since the clock was last read
};

/// The IBM PC BIOS's video, keyboard-buffer and time-of-day services and the BIOS data area they keep, at the call level, for
/// exactly what the reference calls (plan §1, ADR-006 item 3). It is the emulated PC's Machine::Bios and the part of
/// Machine::Firmware the game reads, until D33, with the data area no longer memory: its fields are this class's, and nothing
/// reads them but the services.
///
/// What the services write is counted as the emulated PC's memory counted it: ChangeCount is the bytes of the data area a
/// write changed, as Machine::Memory counted a byte write that changed its byte, and PortWrites the OUTs the ROM's code made,
/// each a write of the CGA's ports; both go into paced time's turn (Platform). Video memory is the GameState's, written
/// through it, which counts its own changes.
///
/// The data area starts as the BIOS leaves it for DOS, in video mode 3 (80x25 colour), as SetMode(3) leaves it, with the
/// keyboard buffer empty and the tick count _timerTicks: the time of day DOS was given.
class Bios
{
public:
  /// The tick count the BIOS takes for 24 hours, at which its timer handler starts again from 0.
  static constexpr std::uint32_t TICKS_PER_DAY = 0x1800B0;

  /// The video mode DOS leaves the program in.
  static constexpr std::uint8_t POWER_ON_VIDEO_MODE = 3;

  /// What one tick of the BIOS's timer handler did to its count, which sets the cycles its code took.
  struct Tick
  {
    bool carried;  ///< the low word wrapped, and the high word was counted too
    bool dayCheck; ///< the high word was 18h, so the low word was compared with B0h as well
    bool dayEnded; ///< a day had passed: the count started again from 0, and the rollover flag was set
  };

  /// _state's video memory is what the services clear and write, and _cga the card they program: both must outlive this.
  /// The data area is set as POST leaves it, and then video mode 3 is set on the card, as the PC's start-up leaves it, with
  /// its writes counted like any other.
  Bios(GameState& _state, Cga& _cga, std::uint32_t _timerTicks);
  Bios(const Bios&) = delete;
  Bios& operator=(const Bios&) = delete;

  // ── Int 10h ──

  /// AH=00h, set mode _mode, 0 to 6: what SET_MODE does, in its order: mode control 0 (video off), CRTC registers 0-15 from
  /// the mode's table, video memory cleared (0720h words in text modes, zero in graphics), mode control the mode's value,
  /// colour select 30h (3Fh in mode 6), and the data area's mode, columns, page size, page, start, cursors and cursor shape
  /// set. False, and nothing done, for a mode above 6, which the BIOS refuses here.
  bool SetMode(std::uint8_t _mode);

  /// AH=0Bh, BH=1: the graphics palette, colour select's bit 5 from _palette's bit 0, by read-modify-write of the data area's
  /// copy.
  void SelectPalette(std::uint8_t _palette);

  /// Whether the teletype can write: in text modes 0 to 3. The graphics modes would need the ROM font, which is not part of
  /// the reference (ADR-001 D14), and nothing prints there.
  [[nodiscard]] bool TeletypeAvailable() const noexcept;

  /// The column of the active page's cursor.
  [[nodiscard]] std::uint8_t CursorColumn() const noexcept;

  /// Writes one character as the BIOS's WRITE_TTY does in a text mode: CR, LF and backspace move the cursor, anything else is
  /// written at the cursor keeping the attribute there, wrapping at the last column, and the screen scrolls up one line, with
  /// the attribute at the cursor, when the cursor would leave row 24. The cursor moves in the data area and on the CRTC
  /// (registers 0Eh and 0Fh). Returns false, and does nothing, outside a text mode or for BEL (07h), whose beep is not
  /// provided. Unlike the ROM it does not blank the CGA while it scrolls.
  bool WriteTeletype(std::uint8_t _character);

  // ── Int 16h ──

  /// AH=01h: the key at the head of the buffer, left there, or nothing when the buffer is empty.
  [[nodiscard]] std::optional<std::uint16_t> PeekKey() const noexcept;

  /// AH=00h: the key at the head of the buffer, taken out of it: the scan code in the high byte and the character in the low.
  /// Nothing when the buffer is empty, where the ROM would wait for ever, because its int 9 handler discards every key.
  [[nodiscard]] std::optional<std::uint16_t> ReadKey() noexcept;

  /// A key put at the buffer's tail, as a keyboard handler that kept keys would: how a test gives the BIOS a key, since the
  /// ROM's own handler keeps none. A full buffer takes it all the same, as a write to the data area would.
  void InsertKey(std::uint16_t _key) noexcept;

  // ── Int 1Ah, and the timer handler's count ──

  /// AH=00h: the tick count and the rollover flag, which is then cleared.
  [[nodiscard]] BiosClock ReadClock() noexcept;

  /// AH=01h: the tick count set, and the rollover flag cleared.
  void SetClock(std::uint32_t _ticks) noexcept;

  /// The timer handler's count of one tick, as the ROM's code does it: the low word counted, the high word when it carried,
  /// and both words zeroed and the rollover flag set once a day has passed.
  Tick CountTick() noexcept;

  /// The tick count.
  [[nodiscard]] std::uint32_t Ticks() const noexcept;

  /// The bytes of the data area that the services' writes changed, since construction.
  [[nodiscard]] std::uint64_t ChangeCount() const noexcept
  {
    return m_changes;
  }

  /// The port writes the services made, since construction.
  [[nodiscard]] std::uint64_t PortWrites() const noexcept
  {
    return m_portWrites;
  }

  /// The BIOS tick count for a time of day: the PC's timer runs at 14.31818 MHz / 12 / 65,536, about 18.2 Hz.
  [[nodiscard]] static std::uint32_t TimerTicksAt(std::uint32_t _centisecondsSinceMidnight) noexcept;

private:
  static constexpr std::size_t KEYBOARD_BUFFER_WORDS = 16;
  static constexpr std::size_t CURSOR_PAGES = 8;

  // A write to the data area, counting each byte it changes.
  void Store(std::uint8_t& _field, std::uint8_t _value) noexcept;
  void Store(std::uint16_t& _field, std::uint16_t _value) noexcept;

  void WriteCrtc(std::uint8_t _register, std::uint8_t _value) noexcept;
  void SetCursor(std::uint8_t _row, std::uint8_t _column) noexcept;
  void ScrollUp(std::uint8_t _attribute) noexcept;

  // The keyboard buffer's word at _offset in the data area, 1Eh to 3Ch.
  [[nodiscard]] std::uint16_t& KeyAt(std::uint16_t _offset) noexcept;
  [[nodiscard]] std::uint16_t KeyAt(std::uint16_t _offset) const noexcept;

  GameState& m_state;
  Cga& m_cga;
  std::uint64_t m_changes = 0;
  std::uint64_t m_portWrites = 0;

  // The data area at 0040:0000, with the IBM PC Technical Reference's names.
  std::uint16_t m_keyboardHead;                                        // BUFFER_HEAD, 1Ah
  std::uint16_t m_keyboardTail;                                        // BUFFER_TAIL, 1Ch
  std::array<std::uint16_t, KEYBOARD_BUFFER_WORDS> m_keyboardBuffer{}; // KB_BUFFER, 1Eh: scan code high, character low
  std::uint8_t m_videoMode = 0;                                        // CRT_MODE, 49h
  std::uint16_t m_videoColumns = 0;                                    // CRT_COLS, 4Ah
  std::uint16_t m_videoPageBytes = 0;                                  // CRT_LEN, 4Ch
  std::uint16_t m_videoStart = 0;                                      // CRT_START, 4Eh
  std::array<std::uint16_t, CURSOR_PAGES> m_cursorPositions{};         // CURSOR_POSN, 50h: column low, row high
  std::uint16_t m_cursorShape = 0;                                     // CURSOR_MODE, 60h
  std::uint8_t m_activePage = 0;                                       // ACTIVE_PAGE, 62h
  std::uint16_t m_crtcAddress = 0;                                     // ADDR_6845, 63h
  std::uint8_t m_modeControl = 0;                                      // CRT_MODE_SET, 65h: the last value written to 3D8h
  std::uint8_t m_colorSelect = 0;                                      // CRT_PALLETTE, 66h: the last value written to 3D9h
  std::uint16_t m_ticksLow;                                            // TIMER_LOW, 6Ch
  std::uint16_t m_ticksHigh;                                           // TIMER_HIGH, 6Eh
  std::uint8_t m_rolledOver = 0;                                       // TIMER_OFL, 70h
};

} // namespace Elite
