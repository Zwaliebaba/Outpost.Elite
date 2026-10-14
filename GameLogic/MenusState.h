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

  /// priceText's fields, DS:803C-8042 in 1987, at their places there.
  struct PriceText
  {
    PriceText() noexcept = default;
    explicit PriceText(const PriceText&) noexcept = default;

    Field<std::uint8_t> gap803C[4];
    Field<std::uint8_t> data8040;
    Field<std::uint8_t> data8041;
    Field<std::uint8_t> gap8042[1];

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("gap803C", _self.gap803C);
      _visitor("data8040", _self.data8040);
      _visitor("data8041", _self.data8041);
      _visitor("gap8042", _self.gap8042);
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
  /// FormatTenths output, "nnnn.n"
  PriceText priceText;
  /// the price ComputeResalePrice reads
  Field<std::uint16_t> resalePriceInput;
  /// rows in the current cursor menu (17 for cargo, the listed equipment)
  Field<std::uint8_t> menuRowCount;
  /// text-memory address of the first row's first attribute byte
  Field<std::uint16_t> menuFirstRowAttr;
  /// 1-based; also the item index SelectLaserType tests
  Field<std::uint8_t> menuSelectedRow;
  /// screenPrices's fields, DS:823B-827E in 1987, at their places there.
  struct ScreenPrices
  {
    ScreenPrices() noexcept = default;
    explicit ScreenPrices(const ScreenPrices&) noexcept = default;

    Field<std::uint8_t> gap823B[4];
    Field<std::uint16_t> data823F;
    Field<std::uint8_t> gap8241[4];
    Field<std::uint16_t> data8245;
    Field<std::uint8_t> gap8247[56];

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("gap823B", _self.gap823B);
      _visitor("data823F", _self.data823F);
      _visitor("gap8241", _self.gap8241);
      _visitor("data8245", _self.data8245);
      _visitor("gap8247", _self.gap8247);
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
  /// word pair; cargo rows from 0: buy price, sell price; equipment rows from index 1: price, resale price; tenths of credits
  ScreenPrices screenPrices;
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
