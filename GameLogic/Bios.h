// GameLogic/Bios.h
#pragma once

#include <array>
#include <cstdint>

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

/// The IBM PC BIOS's video and time-of-day services and the BIOS data area they keep, at the call level, for
/// exactly what the reference calls (plan §1, ADR-006 item 3). It is the emulated PC's Machine::Bios and the part of
/// Machine::Firmware the game reads, until D33, with the data area no longer memory: its fields are this class's, and nothing
/// reads them but the services.
///
/// Video memory is the GameState's, written through it.
///
/// The data area starts as the BIOS leaves it for DOS, in video mode 3 (80x25 colour), as SetMode(3) leaves it, with the
/// tick count _timerTicks: the time of day DOS was given. The keyboard buffer is not kept: the ROM's int 9 handler discards
/// every key, so ExitToDos's drain of it never finds one.
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
  /// The data area is set as POST leaves it, and then video mode 3 is set on the card, as the PC's start-up leaves it.
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

  /// Writes one character as the BIOS's WRITE_TTY does in a text mode, at the cursor, keeping the attribute there; the cursor
  /// moves on in the data area and on the CRTC (registers 0Eh and 0Fh). The game's one text, exitMessage, has no control
  /// character and does not reach the last column.
  void WriteTeletype(std::uint8_t _character);

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

  /// The BIOS tick count for a time of day: the PC's timer runs at 14.31818 MHz / 12 / 65,536, about 18.2 Hz.
  [[nodiscard]] static std::uint32_t TimerTicksAt(std::uint32_t _centisecondsSinceMidnight) noexcept;

private:
  static constexpr std::size_t CURSOR_PAGES = 8;

  void SetCursor(std::uint8_t _row, std::uint8_t _column) noexcept;

  GameState& m_state;
  Cga& m_cga;

  // The data area at 0040:0000, with the IBM PC Technical Reference's names.
  std::uint8_t m_videoMode = 0;                                // CRT_MODE, 49h
  std::uint16_t m_videoColumns = 0;                            // CRT_COLS, 4Ah
  std::uint16_t m_videoPageBytes = 0;                          // CRT_LEN, 4Ch
  std::uint16_t m_videoStart = 0;                              // CRT_START, 4Eh
  std::array<std::uint16_t, CURSOR_PAGES> m_cursorPositions{}; // CURSOR_POSN, 50h: column low, row high
  std::uint16_t m_cursorShape = 0;                             // CURSOR_MODE, 60h
  std::uint8_t m_activePage = 0;                               // ACTIVE_PAGE, 62h
  std::uint16_t m_crtcAddress = 0;                             // ADDR_6845, 63h
  std::uint8_t m_modeControl = 0;                              // CRT_MODE_SET, 65h: the last value written to 3D8h
  std::uint8_t m_colorSelect = 0;                              // CRT_PALLETTE, 66h: the last value written to 3D9h
  std::uint16_t m_ticksLow;                                    // TIMER_LOW, 6Ch
  std::uint16_t m_ticksHigh;                                   // TIMER_HIGH, 6Eh
  std::uint8_t m_rolledOver = 0;                               // TIMER_OFL, 70h
};

} // namespace Elite
