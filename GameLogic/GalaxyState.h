// GameLogic/GalaxyState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Galaxy.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members
/// are the original data segment's, by their names, in their order there; the GameState holds it in GameData.
struct GalaxyState
{
  /// first word of the seeds of the system being generated; its high byte is systemY
  std::uint16_t systemSeed0;
  /// low byte bits 3-5 are the government; high byte is systemX
  std::uint16_t systemSeed1;
  /// low byte bit 7 clear means human colonials; bit 0 adds 1 to the tech level
  std::uint16_t systemSeed2;
  /// 'Distance: 100.0 Light Years'; NoteNearestSystem patches the digits at +0x0A-0x0C and +0x0E
  std::uint8_t distanceText[28];
  /// FormatDecimal5's five ASCII digits of selectedSystem.distanceTenthsLy: the first blank, then the hundreds, tens, units and
  /// tenths
  std::uint8_t distanceDigits[5];
  /// FindTypedSystem's typed name, 8 characters and a terminator
  std::uint8_t findInput[9];
  /// the name as typed, for the error message
  std::uint8_t findInputEcho[9];
  /// '1' + galaxyNumber
  std::uint8_t galacticChartNumber;
  /// 14 lines: anti-ECM device on 'L', title Archangel
  std::uint8_t invasionDebriefText[416];
  /// control codes 5 and 6
  std::uint8_t descriptionCapitalize;
  /// expanded description
  std::uint8_t descriptionBuffer[256];
  /// system or random name for codes 1-3
  std::uint8_t descriptionNameBuffer[13];

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("systemSeed0", _self.systemSeed0);
    _visitor("systemSeed1", _self.systemSeed1);
    _visitor("systemSeed2", _self.systemSeed2);
    _visitor("distanceText", _self.distanceText);
    _visitor("distanceDigits", _self.distanceDigits);
    _visitor("findInput", _self.findInput);
    _visitor("findInputEcho", _self.findInputEcho);
    _visitor("galacticChartNumber", _self.galacticChartNumber);
    _visitor("invasionDebriefText", _self.invasionDebriefText);
    _visitor("descriptionCapitalize", _self.descriptionCapitalize);
    _visitor("descriptionBuffer", _self.descriptionBuffer);
    _visitor("descriptionNameBuffer", _self.descriptionNameBuffer);
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

static_assert(std::is_trivially_copyable_v<GalaxyState> && std::is_standard_layout_v<GalaxyState>);

} // namespace Elite
