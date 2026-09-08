#include "pch.h"

#include "Equipment.h"

#include "TextPrint2x.h"

#include "Dashboard.h"
#include "EliteTypes.h"
#include "LookupTables.h"
#include "Ports.h"
#include "Universe.h"
#include "MarketScreen.h"
#include "ViewChange.h"
#include "TextPrint.h"
#include "SoundEffects.h"
#include "Presenter.h"

/*
 * The equipment shop (slice 2c).
 */

namespace Elite
{

  namespace
  {
    /// Column 12, then two tokens through `spc` and `NLIN3` -- "EQUIP SHIP".
    constexpr std::uint8_t TITLE_COLUMN = 12;
    constexpr std::uint8_t EQUIP_TOKEN = 207;
    constexpr std::uint8_t SHIP_TOKEN = 185;

    /// The item names run from token 105 ("FUEL") to token 118 ("MINING LASER").
    constexpr std::uint8_t ITEM_NAME_BASE = 104;

    /// The price column, six digits with a point, through `TT11`.
    constexpr std::uint8_t PRICE_COLUMN = 25;
    constexpr std::uint8_t PRICE_DIGITS = 6;

    /// The two tokens `prq` complains with -- "ITEM?" and "CASH?".
    constexpr std::uint8_t ITEM_TOKEN = 127;
    constexpr std::uint8_t CASH_TOKEN = 197;

    /// Recursive token 145, "PRESENT".
    constexpr std::uint8_t PRESENT_TOKEN = 31;

    /// The cash line `dn` prints after a successful purchase.
    constexpr std::uint8_t CASH_LINE_TOKEN = 119;

    /*
     * The tech level plus three, capped at fourteen.
     *
     * How many items this station sells, and the cap is the odd part: eleven goes to eleven and
     * twelve goes to FOURTEEN, so no station in the game offers exactly twelve or thirteen items.
     */
    constexpr std::uint8_t TECH_LEVEL_OFFSET = 3;
    constexpr std::uint8_t TECH_CAP_TEST = 12;
    constexpr std::uint8_t TECH_CAP_VALUE = 14;

    /// The item numbers those four lasers occupy in PRXS, which is what refund reads.
    constexpr std::uint8_t PULSE_ITEM = 4;
    constexpr std::uint8_t BEAM_ITEM = 5;
    constexpr std::uint8_t MILITARY_ITEM = 12;
    constexpr std::uint8_t MINING_ITEM = 13;

     /// Four missiles is the maximum, and the fifth is "ALL".
    constexpr std::uint8_t ALL_TOKEN = 124;
    constexpr std::uint8_t MAX_MISSILES = 5;

    /// `CRGO` holds two more than the tonnage, so a large bay is 37 (§6.15).
    constexpr std::uint8_t LARGE_HOLD_CAPACITY = 37;

    /// The energy bomb is stored as 127, not as 1.
    constexpr std::uint8_t ENERGY_BOMB_FITTED = 0x7F;

    /// The tokens `pres` names the offending item with.
    constexpr std::uint8_t LARGE_CARGO_TOKEN = 107;
    constexpr std::uint8_t FUEL_SCOOPS_TOKEN = 111;

    /// The tech level at which the list is long enough to need the screen cleared.
    constexpr std::uint8_t MENU_CLEARS_SCREEN_AT = 8;
    constexpr std::uint8_t MENU_FIRST_ROW = 16;
    constexpr std::uint8_t MENU_LAST_ROW = 20;
    constexpr std::uint8_t MENU_COLUMN = 12;
    constexpr std::uint8_t VIEW_NAME_BASE = 80;
    constexpr std::uint8_t VIEW_TOKEN = 175;
    constexpr int VIEW_COUNT = 4;

    /// A two-byte entry in PRXS, low byte first.
    [[nodiscard]] std::uint16_t PriceAt(std::uint8_t _item) noexcept
    {
      const std::size_t at = static_cast<std::size_t>(_item) * 2u;
      return static_cast<std::uint16_t>(EQUIPMENT_PRICES[at] | (EQUIPMENT_PRICES[at + 1u] << 8));
    }
  } // namespace

  std::uint16_t FuelPrice(LightYearsTenths _fuel) noexcept
  {
    // Seventy tenths less the tank, doubled -- two credits a light year -- and the store
    // is into the TABLE, one byte wide, so a tank emptier than 70 tenths would wrap. It cannot
    // be: `QQ14` is capped at 70.
    const std::uint8_t missing = static_cast<std::uint8_t>(FULL_TANK.tenths - _fuel.tenths);
    return static_cast<std::uint16_t>(RotateLeft(missing, false).value);
  }

  std::uint16_t EquipmentPrice(std::uint8_t _item, LightYearsTenths _fuel) noexcept
  {
    // The item number doubled into the price table. Entry 0 is the one `EQSHP` wrote.
    return (_item == 0) ? FuelPrice(_fuel) : PriceAt(_item);
  }

  std::uint8_t ChooseView(Universe& _universe, Ports& _ports) noexcept
  {
    /*
     * The screen is cleared only at tech level 8 and above.
     *
     * The screen is cleared only at tech level 8 and above, because below that the equipment list
     * is short enough not to reach row 16 where the menu starts.
     */
    if (_universe.current.techLevel >= MENU_CLEARS_SCREEN_AT)
    {
      SetUpScreen(_universe, _ports, EQUIP_SHIP_VIEW);
    }

    /*
     * Rows 16 to 19, with the cursor row itself as the loop counter.
     *
     * The loop counter IS the cursor row: it prints the digit from YC and the view name from
     * YC + 80, and INCYC is what advances it. So a routine that moved the cursor differently would
     * print a different menu, not just a misplaced one.
     */
    _universe.text.row = MENU_FIRST_ROW;
    while (_universe.text.row < MENU_LAST_ROW)
    {
      _universe.text.column = MENU_COLUMN;

      // The row turned into a digit -- rows 16 to 19 print "0" to "3".
      PrintThenSpace(_ports.printer,
                     static_cast<std::uint8_t>(AddWithCarry(_universe.text.row, static_cast<std::uint8_t>('0' - 16), false).value));

      // The row plus 80 as a token -- "FRONT", "REAR", "LEFT", "RIGHT".
      _ports.printer.Print(static_cast<std::uint8_t>(_universe.text.row + VIEW_NAME_BASE));

      MoveCursorDown(_universe.text);
    }

    // The rows cleared, token 175 through `prq`, a key read, and "0" taken off it.
    ClearMessageRows(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message,
                       &_universe.picture, _universe.screenLayout);
    for (;;)
    {
      PrintThenQuestion(_ports.printer, VIEW_TOKEN);

      const std::uint8_t key = _ports.keyboard.NextKey();
      const std::uint8_t view = static_cast<std::uint8_t>(key - '0');
      if (view < VIEW_COUNT)
      {
        return view;
      }

      // Back to qv2 -- and there is no way out of this loop but a valid view.
      ClearMessageRows(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message,
                       &_universe.picture, _universe.screenLayout);
    }
  }

  void Refund(Commander& _commander, std::uint8_t _view, Laser _fitted, LightYearsTenths _fuel) noexcept
  {
    Laser& mount = _commander.lasers[_view];
    const Laser existing = mount;

    /*
     * An empty mount is refunded nothing, and the chain of comparisons is skipped
     * entirely rather than falling through to the mining laser's price.
     */
    if (existing.Fitted())
    {
      std::uint8_t item = MINING_ITEM;
      if (existing == LASER_PULSE)
      {
        item = PULSE_ITEM;
      }
      else if (existing == LASER_BEAM)
      {
        item = BEAM_ITEM;
      }
      else if (existing == LASER_MILITARY)
      {
        item = MILITARY_ITEM;
      }

      // `prx` then `MCASH` -- the old laser's price back, whatever the new one costs.
      ReceiveCash(_commander, EquipmentPrice(item, _fuel));
    }

    // The new laser into the mount.
    mount = _fitted;
  }

  void EquipShipScreen(Universe& _universe, Ports& _ports) noexcept
  {
    /*
     * et11 jumps back to `EQSHP` -- the screen redraws itself after every purchase, so
     * this is a loop rather than a routine that returns. Every other exit leaves it.
     */
    for (;;)
    {
      // TRADEMODE on view 32, which sets the cursor and the case flags too, and the
      // screen's own layout for the wide surface (Resolution.md section 6.2, slice RS-5-b).
      SetUpTradeScreen(_universe, _ports, EQUIP_SHIP_VIEW, EQUIP_LAYOUT);

      // Column 12, then the two title tokens through `spc` and `NLIN3`.
      _universe.text.column = TITLE_COLUMN;
      PrintThenSpace(_ports.printer, EQUIP_TOKEN);
      _ports.printer.Print(SHIP_TOKEN);

      // Sentence case and a line down, written out rather than called through `TT69`, so
      // no newline comes with it.
      _ports.printer.SetCaseFlags(0x80);
      MoveCursorDown(_universe.text);

      /*
       * The tech level plus three, capped at fourteen, into both `Q` and `QQ25`, with `Q`
       * then stepped up.
       *
       * `QQ25` is the highest item number `gnum` will accept, and `Q` is one more because the
       * listing loop compares against it and stops below.
       */
      std::uint8_t highest = AddWithCarry(_universe.current.techLevel, TECH_LEVEL_OFFSET, false).value;
      if (highest >= TECH_CAP_TEST)
      {
        highest = TECH_CAP_VALUE;
      }

      const LightYearsTenths fuel = _universe.commander.fuel;

      // The index starts at 1 and counts up to `Q`, so the fuel line is item 1 on
      // screen and item 0 in the table.
      for (std::uint8_t item = 1; item <= highest; ++item)
      {
        PrintNewline(_ports.printer);

        // The number through `pr2`, three wide, then a space.
        PrintByteValue(_ports.characters, item, false);
        PrintSpace(_ports.printer);

        // The item number plus 104, printed as a token.
        _ports.printer.Print(static_cast<std::uint8_t>(ITEM_NAME_BASE + item));

        // `prx-3` for the price, then column 25 and six digits with a point.
        const std::uint16_t price = EquipmentPrice(static_cast<std::uint8_t>(item - 1u), fuel);
        _universe.text.column = PRICE_COLUMN;
        PrintValue(_ports.characters, price, PRICE_DIGITS, true);
      }

      // The rows cleared, token 127 through `prq`, then `gnum`.
      ClearMessageRows(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message,
                       &_universe.picture, _universe.screenLayout);
      PrintThenQuestion(_ports.printer, ITEM_TOKEN);

      const NumberEntry entry = ReadNumber(_ports.keyboard, _ports.characters, _universe.text, highest);

      // `gnum` jumps to `BAY2` -- a letter leaves without a beep and without the docking
      // bay's usual route.
      if (entry.outcome == DigitResult::LeaveScreen)
      {
        return;
      }

      // BEQ bay / BCS bay -- nothing entered, or too large, and out with no sound.
      if (entry.value == 0 || entry.outcome == DigitResult::TooBig)
      {
        return;
      }

      /*
       * A subtraction of ZERO here is a subtraction of ONE.
       *
       * The carry is clear, because the branch above it did not fire, so `A - 0 - (1 - C)` is A - 1.
       * That turns the number the player typed into the table's item number, and it is the whole
       * reason the two numbering schemes never collide.
       */
      const std::uint8_t item = static_cast<std::uint8_t>(entry.value - 1u);

      // Column 2, then a line down.
      _universe.text.column = 2;
      MoveCursorDown(_universe.text);

      /*
       * The price through `prx` and `LCASH`; a borrow complains with token 197.
       *
       * The money goes first. Everything below that finds the item already fitted hands it back.
       */
      if (!SpendCash(_universe.commander, EquipmentPrice(item, fuel)))
      {
        PrintThenQuestion(_ports.printer, CASH_TOKEN);
        (void)Beep(_universe.sound, false);
        _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
        return;
      }

      /*
       * et0 through et10 -- thirteen comparisons, and Y walking alongside them.
       *
       * Y is the token `pres` complains with, and it is threaded through the chain rather than set
       * at each branch: 107 at et1, 111 at et5, and a step up at the top of et6, et7, et8, etA,
       * of et6, et7, et8, etA, etB, et9 and et10. So the token depends on how far the chain got,
       * not on which branch was taken -- which is why the port walks it the same way instead of
       * looking it up.
       */
      std::uint8_t complaint = 0;
      bool alreadyFitted = false;

      // BNE et0 -- item 0 is fuel, and a full tank is not an error.
      if (item == 0)
      {
        _universe.commander.fuel = FULL_TANK;
      }

      // Item 1, the missile.
      if (!alreadyFitted && item == 1)
      {
        const std::uint8_t missiles = static_cast<std::uint8_t>(_universe.commander.missiles + 1u);
        complaint = ALL_TOKEN;
        if (missiles >= MAX_MISSILES)
        {
          alreadyFitted = true;
        }
        else
        {
          _universe.commander.missiles = missiles;
          ResetMissileIndicators(_universe.canvas, _universe.commander.missiles, &_universe.picture); // JSR msblob
        }
      }

      // Token 107, then item 2, the large cargo bay.
      if (!alreadyFitted)
      {
        complaint = LARGE_CARGO_TOKEN;
        if (item == 2)
        {
          if (_universe.commander.cargoCapacity == LARGE_HOLD_CAPACITY)
          {
            alreadyFitted = true;
          }
          else
          {
            _universe.commander.cargoCapacity = LARGE_HOLD_CAPACITY;
          }
        }
      }

      // Item 3, and the token's step is INSIDE this branch rather than before it.
      if (!alreadyFitted && item == 3)
      {
        ++complaint;
        if (_universe.commander.ecm != 0)
        {
          alreadyFitted = true;
        }
        else
        {
          _universe.commander.ecm = 0xFF;
        }
      }

      // Et3 and et4 -- the pulse and beam lasers, which touch neither Y nor the pres path.
      if (!alreadyFitted && (item == 4 || item == 5))
      {
        const std::uint8_t view = ChooseView(_universe, _ports);
        Refund(_universe.commander, view, (item == 4) ? LASER_PULSE : LASER_BEAM, fuel);
      }

      // Token 111, item 6, and the ONLY branch that falls into `pres` rather than
      // jumping to it: a fitted scoop branches away and leaves the fall-through as the error path.
      if (!alreadyFitted)
      {
        complaint = FUEL_SCOOPS_TOKEN;
        if (item == 6)
        {
          if (_universe.commander.fuelScoops != 0)
          {
            alreadyFitted = true;
          }
          else
          {
            _universe.commander.fuelScoops = 0xFF;
          }
        }
      }

      /*
       * Et6 through et10 -- six items whose flags sit consecutively, each preceded by an
       * unconditional INY.
       */
      struct Fitting
      {
        std::uint8_t Commander::*field; ///< the equipment byte the item fits into
        std::uint8_t item;
        std::uint8_t fittedValue;
      };

      // The member pointer goes FIRST so the two bytes share its tail rather than each taking a
      // word of their own. Nothing outside this function sees the layout; `item` is still the key.
      static constexpr std::array<Fitting, 5> FITTINGS = {{
        {&Commander::escapePod, 7, 0xFF}, // ESCP stepped down from zero
        {&Commander::energyBomb, 8, ENERGY_BOMB_FITTED},
        {&Commander::energyUnit, 9, 1}, // ENGY stepped up, from a known zero
        {&Commander::dockingComputer, 10, 0xFF},
        {&Commander::galacticDrive, 11, 0xFF},
      }};

      for (const Fitting& fitting : FITTINGS)
      {
        if (alreadyFitted)
        {
          break;
        }
        ++complaint;
        if (item != fitting.item)
        {
          continue;
        }
        if (_universe.commander.*fitting.field != 0)
        {
          alreadyFitted = true;
        }
        else
        {
          _universe.commander.*fitting.field = fitting.fittedValue;
        }
      }

      // Et9 and et10 -- the military and mining lasers, each preceded by its own INY.
      if (!alreadyFitted)
      {
        ++complaint;
        if (item == 12)
        {
          const std::uint8_t view = ChooseView(_universe, _ports);
          Refund(_universe.commander, view, LASER_MILITARY, fuel);
        }
        ++complaint;
        if (item == 13)
        {
          const std::uint8_t view = ChooseView(_universe, _ports);
          Refund(_universe.commander, view, LASER_MINING, fuel);
        }
      }

      /*
       * The token kept in `K`, the price refunded through `prx` and `MCASH`, then
       * the token and "PRESENT" printed, and on into `err`.
       *
       * The refund uses `prx` with A as it stands, which by this point is the ITEM NUMBER -- so the
       * money handed back is the price of what was being bought, not of what was already fitted.
       */
      if (alreadyFitted)
      {
        ReceiveCash(_universe.commander, EquipmentPrice(item, fuel));
        PrintThenSpace(_ports.printer, complaint);
        _ports.printer.Print(PRESENT_TOKEN);
        // A beep, then fifty frames of `DELAY`
        (void)Beep(_universe.sound, false);
        _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
        return;
      }

      // `dn`, which prints the cash and falls into `dn2`, then back to `EQSHP`.
      PrintSpace(_ports.printer);
      PrintThenSpace(_ports.printer, CASH_LINE_TOKEN);
      (void)Beep(_universe.sound, false);
      _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
    }
  }

} // namespace Elite
