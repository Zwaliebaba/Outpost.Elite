// GameLogic/DrawingState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What drawing draws into and with, which several subsystems write: the space view buffer, the colour, the paper, the sun's fringe, the
/// text attribute, the screen's layout and the masking device's background. Its members are the original data segment's, by their names, in
/// their order there; the GameState holds it in GameData.
struct DrawingState
{
  /// the drawing buffer, 256x128 at 2bpp, 64 bytes a row: rows 1-126 are the space view, spaceViewBuffer, which PresentSpaceV...
  std::uint8_t drawingBuffer[8192];
  /// colour 0-3 for the line and circle drawers (DrawDisc indexes the pattern bytes with it); the title sets 2, ApplyEnemyLas...
  std::uint8_t drawColor;
  /// read by DrawDisc as an AND mask on the disc fill: DrawSunOrPlanet sets 1/3/7 for the sun as its radius passes 0x28/0xB4, 0 for the...
  std::uint8_t sunFringeMask;
  /// pixel pattern behind each glyph in the graphics printer (DrawViewChar: ink from _ink where the glyph is set, this word where it is clear); 0...
  std::uint16_t textPaperPattern;
  /// FFh in the file; 0 cockpit (ShowCockpitScreen), 1 chart frame (DrawChartFrame), 2 text mode (EnterDockedScreen); also selects the tex...
  std::uint8_t screenLayout;
  /// colour-select value UpdateDamageFlash writes each frame: 9 (bright blue) while the masking device runs, else 0 (cleared); e...
  std::uint8_t maskingBackgroundColor;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("drawingBuffer", _self.drawingBuffer);
    _visitor("drawColor", _self.drawColor);
    _visitor("sunFringeMask", _self.sunFringeMask);
    _visitor("textPaperPattern", _self.textPaperPattern);
    _visitor("screenLayout", _self.screenLayout);
    _visitor("maskingBackgroundColor", _self.maskingBackgroundColor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }
};

static_assert(std::is_trivially_copyable_v<DrawingState> && std::is_standard_layout_v<DrawingState>);

} // namespace Elite
