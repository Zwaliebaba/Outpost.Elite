// GameLogic/GalaxyState.h
#pragma once

#include "Field.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Galaxy.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct GalaxyState
{
  GalaxyState() noexcept = default;
  explicit GalaxyState(const GalaxyState&) noexcept = default;

  /// first word of the seeds of the system being generated; its high byte is systemY
  Field<std::uint16_t> systemSeed0;
  /// low byte bits 3-5 are the government; high byte is systemX
  Field<std::uint16_t> systemSeed1;
  /// low byte bit 7 clear means human colonials; bit 0 adds 1 to the tech level
  Field<std::uint16_t> systemSeed2;
  /// 'Distance: 100.0 Light Years'; ShowNearestSystemDistance patches the digits at +0x0A-0x0C and +0x0E
  Field<std::uint8_t> distanceText[28];
  /// distanceDigits's fields, DS:29E4-29E8 in 1987, at their places there.
  struct DistanceDigits
  {
    DistanceDigits() noexcept = default;
    explicit DistanceDigits(const DistanceDigits&) noexcept = default;

    Field<std::uint8_t> gap29E4[1];
    Field<std::uint8_t> distanceHundredsDigit;
    Field<std::uint8_t> distanceTensDigit;
    Field<std::uint8_t> distanceUnitsDigit;
    Field<std::uint8_t> distanceTenthsDigit;

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("gap29E4", _self.gap29E4);
      _visitor("distanceHundredsDigit", _self.distanceHundredsDigit);
      _visitor("distanceTensDigit", _self.distanceTensDigit);
      _visitor("distanceUnitsDigit", _self.distanceUnitsDigit);
      _visitor("distanceTenthsDigit", _self.distanceTenthsDigit);
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
  /// FormatDecimal5's five ASCII digits of selectedDistanceTenthsLy
  DistanceDigits distanceDigits;
  /// FindSystemByName's typed name, 8 characters and a terminator
  Field<std::uint8_t> findInput[9];
  /// the name as typed, for the error message
  Field<std::uint8_t> findInputEcho[9];
  /// '1' + galaxyNumber
  Field<std::uint8_t> galacticChartNumber;
  /// items in chartItems: stars first, then labels added by AddChartLabel
  Field<std::uint8_t> chartItemCount;
  /// address of the next free 8-byte item in chartItems
  Field<std::uint16_t> chartItemEnd;
  /// 8 bytes: x min, x max, row min, row max, word centre or text, byte size, byte is-label; the short-range chart's occupied rectangles...
  Field<std::uint8_t> chartItems[32][8];
  /// next pending label in pendingChartLabels; AddChartLabel moves it past the name
  Field<std::uint16_t> chartLabelCursor;
  /// records of word x range, word row range, NUL-terminated system name, written by ShowShortRangeChart
  Field<std::uint8_t> pendingChartLabels[256];
  /// tries so far for the current label; NudgeChartLabel gives up at 35
  Field<std::uint8_t> labelNudgeCount;
  /// counted down by PlaceChartLabels
  Field<std::uint8_t> labelsLeftToPlace;
  Field<std::uint8_t> data7D48;
  Field<std::uint8_t> data7D49;
  /// the selected system's distance as FormatSelectedSystemDistance copies it, its digits and point, back to the space before them (chan...
  Field<std::uint8_t> selectedDistanceText[6];
  /// the population's five digits, the units digit and a point moved into the last two (change 3c names it)
  Field<std::uint8_t> populationDigits[5];
  /// the productivity's five digits, a leading zero blanked (change 3c names it)
  Field<std::uint8_t> productivityDigits[5];
  /// the radius's five digits, the first blanked (change 3c names it)
  Field<std::uint8_t> radiusDigits[5];
  /// 14 lines: anti-ECM device on 'L', title Archangel
  Field<std::uint8_t> invasionDebriefText[416];
  /// control codes 5 and 6
  Field<std::uint8_t> descriptionCapitalize;
  /// expanded description
  Field<std::uint8_t> descriptionBuffer[256];
  /// system or random name for codes 1-3
  Field<std::uint8_t> descriptionNameBuffer[13];

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
    _visitor("chartItemCount", _self.chartItemCount);
    _visitor("chartItemEnd", _self.chartItemEnd);
    _visitor("chartItems", _self.chartItems);
    _visitor("chartLabelCursor", _self.chartLabelCursor);
    _visitor("pendingChartLabels", _self.pendingChartLabels);
    _visitor("labelNudgeCount", _self.labelNudgeCount);
    _visitor("labelsLeftToPlace", _self.labelsLeftToPlace);
    _visitor("data7D48", _self.data7D48);
    _visitor("data7D49", _self.data7D49);
    _visitor("selectedDistanceText", _self.selectedDistanceText);
    _visitor("populationDigits", _self.populationDigits);
    _visitor("productivityDigits", _self.productivityDigits);
    _visitor("radiusDigits", _self.radiusDigits);
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
