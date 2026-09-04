#include "pch.h"

#include "DockedKeys.h"

/*
 * The top-level keyboard dispatch (slice 2e).
 */

namespace Elite
{

  KeyOutcome ActionForKey(std::uint8_t _key, std::uint8_t _dockedFlag, std::uint8_t _view, std::uint8_t _countdown,
                          bool _hyperspaceHeld) noexcept
  {
    // 6502: the six comparisons above `fvw`, which run whether docked or in space.
    if (_key == KEY_STATUS)
    {
      return {KeyAction::StatusMode, 0};
    }
    if (_key == KEY_LONG_RANGE)
    {
      return {KeyAction::LongRangeChart, 0};
    }
    if (_key == KEY_SHORT_RANGE)
    {
      return {KeyAction::ShortRangeChart, 0};
    }
    if (_key == KEY_DATA_ON_SYSTEM)
    {
      // 6502: JSR TT111 / JMP TT25 -- the only one of these that is two calls, because the screen
      // reads the system TT111 leaves behind rather than finding it itself.
      return {KeyAction::DataOnSystem, 0};
    }
    if (_key == KEY_INVENTORY)
    {
      return {KeyAction::Inventory, 0};
    }
    if (_key == KEY_MARKET_PRICE)
    {
      return {KeyAction::MarketPrice, 0};
    }
    if (_key == KEY_LAUNCH)
    {
      return {KeyAction::Launch, 0};
    }

    // 6502: fvw -- BIT QQ12 / BPL INSP. Bit 7, and BAY sets the whole byte to &FF.
    if ((_dockedFlag & 0x80u) != 0u)
    {
      if (_key == KEY_EQUIP_SHIP)
      {
        return {KeyAction::EquipShip, 0};
      }
      if (_key == KEY_BUY_CARGO)
      {
        return {KeyAction::BuyCargo, 0};
      }
      if (_key == KEY_DISK_ACCESS)
      {
        // 6502: JSR SVE / BCC P%+5 / JMP QU5 / JMP BAY -- the menu decides which, on its carry.
        return {KeyAction::DiskAccess, 0};
      }
      if (_key == KEY_SELL_CARGO)
      {
        return {KeyAction::SellCargo, 0};
      }
    }
    else
    {
      // 6502: INSP -- and the three land on LDX #1, #2 and #3 by falling through two EQUB &2C.
      if (_key == KEY_REAR_VIEW)
      {
        return {KeyAction::ChangeView, VIEW_REAR};
      }
      if (_key == KEY_LEFT_VIEW)
      {
        return {KeyAction::ChangeView, VIEW_LEFT};
      }
      if (_key == KEY_RIGHT_VIEW)
      {
        return {KeyAction::ChangeView, VIEW_RIGHT};
      }
    }

    /*
     * 6502: LABEL_3 -- BIT KLO+HINT / BPL P%+5 / JMP hyp.
     *
     * The key matrix, not the accumulator. So this fires on H being HELD, whatever key the rest of
     * the routine was given, and the key that was pressed is thrown away.
     */
    if (_hyperspaceHeld)
    {
      return {KeyAction::Hyperspace, 0};
    }

    // 6502: NWDAV5 -- CMP #DINT / BEQ T95. The view test is T95's own, not this one's.
    if (_key == KEY_DISTANCE)
    {
      return {KeyAction::ShowDistance, 0};
    }

    /*
     * 6502: CMP #FINT / BNE HME1 / LDA QQ12 / BEQ t95 / LDA QQ11 / AND #%11000000 / BEQ t95.
     *
     * Docked AND on a chart, and "docked" here is `LDA QQ12 / BEQ` -- the byte being non-zero,
     * rather than bit 7 being set as the split above tests it.
     */
    if (_key == KEY_FIND_SYSTEM)
    {
      if (_dockedFlag == 0u || !IsChartView(_view))
      {
        return {KeyAction::Nothing, 0};
      }
      return {KeyAction::SearchBySystemName, 0};
    }

    /*
     * 6502: HME1 -- STA T1 / LDA QQ11 / AND #%11000000 / BEQ TT107 / LDA QQ22+1 / BNE TT107.
     *
     * Off a chart, or with the hyperspace counter already running, the crosshairs do not move and
     * the routine drops straight into the countdown. The key is preserved in T1 across the two
     * tests, which is the only reason it is still available below.
     */
    if (!IsChartView(_view) || _countdown != 0u)
    {
      return {KeyAction::CountdownOnly, 0};
    }

    // 6502: LDA T1 / CMP #OINT / BNE ee2 -- and "O" is a TAIL call, so it is the one path through
    // here that does not reach the countdown at all.
    if (_key == KEY_HOME)
    {
      return {KeyAction::HomeCrosshairs, 0};
    }

    // 6502: ee2 -- JSR TT16, and then TT107.
    return {KeyAction::MoveCrosshairs, 0};
  }

} // namespace Elite
