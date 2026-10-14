// GameLogic/MenusState.h
#pragma once

#include "Field.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// The docked menus' rows, prices and typed numbers, which the equipment, market and docked screens share. Its members are the 1987 data
/// segment's, by their names, in their order there; the GameState holds it in GameData.
struct MenusState
{
  MenusState() noexcept = default;
  explicit MenusState(const MenusState&) noexcept = default;

  /// FormatTenths output, "nnnn.n", and its NUL
  Field<std::uint8_t> priceText[7];
  /// the price ComputeResalePrice reads
  Field<std::uint16_t> resalePriceInput;
  /// rows in the current cursor menu (17 for cargo, the listed equipment)
  Field<std::uint8_t> menuRowCount;
  /// text-memory address of the first row's first attribute byte
  Field<std::uint16_t> menuFirstRowAttr;
  /// 1-based; also the item index SelectLaserType tests
  Field<std::uint8_t> menuSelectedRow;
  /// A row's two prices on a trade screen or the shipyard's, in tenths of credits.
  struct ScreenPrice
  {
    ScreenPrice() noexcept = default;
    explicit ScreenPrice(const ScreenPrice&) noexcept = default;

    /// a cargo row's buy price; an equipment row's price
    Field<std::uint16_t> price;
    /// a cargo row's sell price; an equipment row's resale price
    Field<std::uint16_t> resale;

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
  Field<std::uint8_t> quantityText[6];

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("priceText", _self.priceText);
    _visitor("resalePriceInput", _self.resalePriceInput);
    _visitor("menuRowCount", _self.menuRowCount);
    _visitor("menuFirstRowAttr", _self.menuFirstRowAttr);
    _visitor("menuSelectedRow", _self.menuSelectedRow);
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
