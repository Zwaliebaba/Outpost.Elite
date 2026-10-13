// GameLogic/VideoModes.h
#pragma once

#include <cstdint>

namespace Elite
{

class Cga;
class GameState;

/// The IBM PC BIOS's video services as the game calls them (ADR-041): int 10h's mode, palette and teletype, at the call level
/// (plan §1, ADR-006 item 3), and the little of the BIOS's data area they read back. Video memory is the GameState's, written
/// through it; the card is the CGA (Cga).
///
/// Of the data area, three things reach what the game sees: the columns of the mode, for where the teletype writes; the cursor,
/// which it moves; and the copy of the colour select register, which the palette call reads and writes back, whatever the game
/// wrote to the card itself since. The game sets no page and no start address, so both are 0 throughout.
///
/// It starts as the BIOS leaves the PC for DOS: video mode 3 (80x25 colour) set on the card and in video memory.
class VideoModes
{
public:
  /// The video mode DOS leaves the program in.
  static constexpr std::uint8_t POWER_ON_VIDEO_MODE = 3;

  /// _state's video memory is what the services clear and write, and _cga the card they program: both must outlive this.
  VideoModes(GameState& _state, Cga& _cga);
  VideoModes(const VideoModes&) = delete;
  VideoModes& operator=(const VideoModes&) = delete;

  /// AH=00h, set mode _mode, 0 to 6: what SET_MODE does, in its order: mode control 0 (video off), CRTC registers 0-15 from
  /// the mode's table, video memory cleared (0720h words in text modes, zero in graphics), mode control the mode's value,
  /// colour select 30h (3Fh in mode 6), and the mode's columns, the cursor home and the colour select's copy kept. False, and
  /// nothing done, for a mode above 6, which the BIOS refuses here.
  bool SetMode(std::uint8_t _mode);

  /// AH=0Bh, BH=1: the graphics palette, colour select's bit 5 from _palette's bit 0, by read-modify-write of the data area's
  /// copy.
  void SelectPalette(std::uint8_t _palette);

  /// Writes one character as the BIOS's WRITE_TTY does in a text mode, at the cursor, keeping the attribute there; the cursor
  /// moves on, and on the CRTC (registers 0Eh and 0Fh). The game's one text, exitMessage, has no control character and does
  /// not reach the last column.
  void WriteTeletype(std::uint8_t _character);

private:
  GameState& m_state;
  Cga& m_cga;

  std::uint16_t m_columns = 0;     // CRT_COLS, 0040:004A
  std::uint8_t m_cursorRow = 0;    // CURSOR_POSN, 0040:0050, page 0's row
  std::uint8_t m_cursorColumn = 0; // and its column
  std::uint8_t m_colorSelect = 0;  // CRT_PALLETTE, 0040:0066: the last value the BIOS wrote to 3D9h
};

} // namespace Elite
