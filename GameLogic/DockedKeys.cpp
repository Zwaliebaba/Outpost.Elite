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
    // The six comparisons above `fvw`, which run whether docked or in space.
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
      // The only one of these that is TWO calls -- TT111 first, because the data screen
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

    // The docked flag's top bit, tested without loading the byte, and BAY sets the
    // whole byte to &FF.
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
        // Call the disk menu; the carry it returns picks the docking bay or a restart.
        return {KeyAction::DiskAccess, 0};
      }
      if (_key == KEY_SELL_CARGO)
      {
        return {KeyAction::SellCargo, 0};
      }
    }
    else
    {
      // One chain of three loads entered at three different points, and a key that
      // enters high skips the loads below it by falling into a data byte that assembles as a
      // three-byte instruction and swallows them.
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
     * Test the hyperspace key and leave for `hyp` when it is down.
     *
     * The key matrix, not the accumulator. So this fires on H being HELD, whatever key the rest of
     * the routine was given, and the key that was pressed is thrown away.
     */
    if (_hyperspaceHeld)
    {
      return {KeyAction::Hyperspace, 0};
    }

    // The "D" key alone; the view test that follows is T95's own, not this one's.
    if (_key == KEY_DISTANCE)
    {
      return {KeyAction::ShowDistance, 0};
    }

    /*
     * The "F" key -- docked AND on a chart, or nothing happens.
     *
     * "Docked" here is the WHOLE BYTE being non-zero, rather than its top bit being set as the
     * split above tests it. The chart test is the view byte's top two bits.
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
     * The crosshair move, and the two ways out of it before anything moves.
     *
     * Off a chart, or with the hyperspace counter already running, the crosshairs do not move and
     * the routine drops straight into the countdown. The key is stashed in `T1` before the two
     * tests, which is the only reason it is still available below.
     */
    if (!IsChartView(_view) || _countdown != 0u)
    {
      return {KeyAction::CountdownOnly, 0};
    }

    // The "O" key, read back out of `T1` -- and it leaves by a TAIL call, so it is the one
    // path through here that does not reach the countdown at all.
    if (_key == KEY_HOME)
    {
      return {KeyAction::HomeCrosshairs, 0};
    }

    // Move the crosshairs, and then fall into the countdown.
    return {KeyAction::MoveCrosshairs, 0};
  }

} // namespace Elite
