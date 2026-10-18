// GameLogic/MenusState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// The docked menus' rows, prices and typed numbers, which the equipment, market and docked screens share. Its members are the original
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct MenusState
{
  /// the price ComputeResalePrice reads
  std::uint16_t resalePriceInput;
  /// rows in the current cursor menu (17 for cargo, the listed equipment)
  std::uint8_t menuRowCount;
  /// text-memory address of the first row's first attribute byte
  std::uint16_t menuFirstRowAttr;
  /// A row's two prices on a trade screen or the shipyard's, in tenths of credits.
  struct ScreenPrice
  {
    /// a cargo row's buy price; an equipment row's price
    std::uint16_t price;
    /// a cargo row's sell price; an equipment row's resale price
    std::uint16_t resale;

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("price", _self.price);
      _visitor("resale", _self.resale);
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
  /// cargo rows from 0; equipment rows from index 1
  ScreenPrice screenPrices[17];
  /// number text for the trade screens and the typed quantity
  std::uint8_t quantityText[6];

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("resalePriceInput", _self.resalePriceInput);
    _visitor("menuRowCount", _self.menuRowCount);
    _visitor("menuFirstRowAttr", _self.menuFirstRowAttr);
    _visitor("screenPrices", _self.screenPrices);
    _visitor("quantityText", _self.quantityText);
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

static_assert(std::is_trivially_copyable_v<MenusState> && std::is_standard_layout_v<MenusState>);

} // namespace Elite
