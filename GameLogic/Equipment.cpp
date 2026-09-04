#include "pch.h"

#include "Equipment.h"

#include "EliteTypes.h"
#include "LookupTables.h"

/*
 * The equipment shop (slice 2c).
 */

namespace Elite
{

  namespace
  {
    /// 6502: LDA #12 / JSR DOXC / LDA #207 / JSR spc / LDA #185 / JSR NLIN3 -- "EQUIP SHIP".
    constexpr std::uint8_t TITLE_COLUMN = 12;
    constexpr std::uint8_t EQUIP_TOKEN = 207;
    constexpr std::uint8_t SHIP_TOKEN = 185;

    /// 6502: ADC #104 -- the item names run from token 105 ("FUEL") to token 118 ("MINING LASER").
    constexpr std::uint8_t ITEM_NAME_BASE = 104;

    /// 6502: LDA #25 / JSR DOXC / LDA #6 / JSR TT11 -- the price column, six digits with a point.
    constexpr std::uint8_t PRICE_COLUMN = 25;
    constexpr std::uint8_t PRICE_DIGITS = 6;

    /// 6502: LDA #127 / JSR prq and LDA #197 / JSR prq -- "ITEM?" and "CASH?".
    constexpr std::uint8_t ITEM_TOKEN = 127;
    constexpr std::uint8_t CASH_TOKEN = 197;

    /// 6502: LDA #31 / JSR TT27 -- recursive token 145, "PRESENT".
    constexpr std::uint8_t PRESENT_TOKEN = 31;

    /// 6502: LDA #119 -- the cash line `dn` prints after a successful purchase.
    constexpr std::uint8_t CASH_LINE_TOKEN = 119;

    /*
     * 6502: LDA tek / CLC / ADC #3 / CMP #12 / BCC P%+4 / LDA #14.
     *
     * How many items this station sells, and the cap is the odd part: eleven goes to eleven and
     * twelve goes to FOURTEEN, so no station in the game offers exactly twelve or thirteen items.
     */
    constexpr std::uint8_t TECH_LEVEL_OFFSET = 3;
    constexpr std::uint8_t TECH_CAP_TEST = 12;
    constexpr std::uint8_t TECH_CAP_VALUE = 14;

    /// 6502: LDA #70 / SEC / SBC QQ14 / ASL A -- a full tank, and two credits a light year.
    constexpr std::uint8_t FULL_TANK = 70;

    /// 6502: LDA #POW / #POW+128 / #Armlas / #Mlas, from elite-source.asm.
    constexpr std::uint8_t PULSE_POWER = 15;
    constexpr std::uint8_t BEAM_POWER = 128 + PULSE_POWER;
    constexpr std::uint8_t MILITARY_POWER = 151;
    constexpr std::uint8_t MINING_POWER = 50;

    /// 6502: the item numbers those four lasers occupy in PRXS, which is what refund reads.
    constexpr std::uint8_t PULSE_ITEM = 4;
    constexpr std::uint8_t BEAM_ITEM = 5;
    constexpr std::uint8_t MILITARY_ITEM = 12;
    constexpr std::uint8_t MINING_ITEM = 13;

    /// 6502: LDY #124 / CPX #5 / BCS pres -- four missiles is the maximum, and the fifth is "ALL".
    constexpr std::uint8_t ALL_TOKEN = 124;
    constexpr std::uint8_t MAX_MISSILES = 5;

    /// 6502: LDX #37 / CPX CRGO -- CRGO holds two more than the tonnage, so a large bay is 37 (§6.15).
    constexpr std::uint8_t LARGE_HOLD_CAPACITY = 37;

    /// 6502: LDX #&7F / STX BOMB -- the energy bomb is stored as 127, not as 1.
    constexpr std::uint8_t ENERGY_BOMB_FITTED = 0x7F;

    /// 6502: the tokens `pres` names the offending item with.
    constexpr std::uint8_t LARGE_CARGO_TOKEN = 107;
    constexpr std::uint8_t FUEL_SCOOPS_TOKEN = 111;

    /// 6502: qv -- CMP #8, the tech level at which the list is long enough to need the screen cleared.
    constexpr std::uint8_t MENU_CLEARS_SCREEN_AT = 8;
    constexpr std::uint8_t MENU_FIRST_ROW = 16;
    constexpr std::uint8_t MENU_LAST_ROW = 20;
    constexpr std::uint8_t MENU_COLUMN = 12;
    constexpr std::uint8_t VIEW_NAME_BASE = 80;
    constexpr std::uint8_t VIEW_TOKEN = 175;
    constexpr int VIEW_COUNT = 4;

    /// 6502: a two-byte entry in PRXS, low byte first.
    [[nodiscard]] std::uint16_t PriceAt(std::uint8_t _item) noexcept
    {
      const std::size_t at = static_cast<std::size_t>(_item) * 2u;
      return static_cast<std::uint16_t>(EQUIPMENT_PRICES[at] | (EQUIPMENT_PRICES[at + 1u] << 8));
    }
  } // namespace

  std::uint16_t FuelPrice(std::uint8_t _fuel) noexcept
  {
    // 6502: LDA #70 / SEC / SBC QQ14 / ASL A / STA PRXS -- and the store is into the TABLE, one
    // byte wide, so a tank emptier than 70 tenths would wrap. It cannot be: QQ14 is capped at 70.
    const std::uint8_t missing = static_cast<std::uint8_t>(FULL_TANK - _fuel);
    return static_cast<std::uint16_t>(RotateLeft(missing, false).value);
  }

  std::uint16_t EquipmentPrice(std::uint8_t _item, std::uint8_t _fuel) noexcept
  {
    // 6502: prx -- ASL A / TAY / LDX PRXS,Y / LDA PRXS+1,Y / TAY. Entry 0 is the one EQSHP wrote.
    return (_item == 0) ? FuelPrice(_fuel) : PriceAt(_item);
  }

  std::uint8_t ChooseView(TradeScreen& _screen, std::uint8_t _techLevel) noexcept
  {
    /*
     * 6502: qv -- LDA tek / CMP #8 / BCC P%+7 / LDA #32 / JSR TT66.
     *
     * The screen is cleared only at tech level 8 and above, because below that the equipment list
     * is short enough not to reach row 16 where the menu starts.
     */
    if (_techLevel >= MENU_CLEARS_SCREEN_AT)
    {
      _screen.effects.ClearToView(EQUIP_SHIP_VIEW);
    }

    /*
     * 6502: LDA #16 / TAY / JSR DOYC / qv1: ... / LDY YC / CPY #20 / BCC qv1.
     *
     * The loop counter IS the cursor row: it prints the digit from YC and the view name from
     * YC + 80, and INCYC is what advances it. So a routine that moved the cursor differently would
     * print a different menu, not just a misplaced one.
     */
    _screen.text.row = MENU_FIRST_ROW;
    while (_screen.text.row < MENU_LAST_ROW)
    {
      _screen.text.column = MENU_COLUMN;

      // 6502: TYA / CLC / ADC #'0'-16 / JSR spc -- rows 16 to 19 print "0" to "3".
      PrintThenSpace(_screen.printer,
                     static_cast<std::uint8_t>(AddWithCarry(_screen.text.row, static_cast<std::uint8_t>('0' - 16), false).value));

      // 6502: LDA YC / CLC / ADC #80 / JSR TT27 -- "FRONT", "REAR", "LEFT", "RIGHT".
      _screen.printer.Print(static_cast<std::uint8_t>(_screen.text.row + VIEW_NAME_BASE));

      MoveCursorDown(_screen.text);
    }

    // 6502: JSR CLYNS / qv2: LDA #175 / JSR prq / JSR TT217 / SEC / SBC #'0' / CMP #4 / BCC qv3.
    _screen.effects.ClearBottomRows();
    for (;;)
    {
      PrintThenQuestion(_screen.printer, VIEW_TOKEN);

      const std::uint8_t key = _screen.keys.NextKey();
      const std::uint8_t view = static_cast<std::uint8_t>(key - '0');
      if (view < VIEW_COUNT)
      {
        return view;
      }

      // 6502: JSR CLYNS / JMP qv2 -- and there is no way out of this loop but a valid view.
      _screen.effects.ClearBottomRows();
    }
  }

  void Refund(CommanderBlock& _commander, std::uint8_t _view, std::uint8_t _newPower, std::uint8_t _fuel) noexcept
  {
    const std::size_t mount = static_cast<std::size_t>(Field::Lasers) + static_cast<std::size_t>(_view);
    const std::uint8_t existing = _commander.bytes[mount];

    /*
     * 6502: LDA LASER,X / BEQ ref3 -- an empty mount is refunded nothing, and the chain of CMPs is
     * skipped entirely rather than falling through to the mining laser's price.
     */
    if (existing != 0)
    {
      std::uint8_t item = MINING_ITEM;
      if (existing == PULSE_POWER)
      {
        item = PULSE_ITEM;
      }
      else if (existing == BEAM_POWER)
      {
        item = BEAM_ITEM;
      }
      else if (existing == MILITARY_POWER)
      {
        item = MILITARY_ITEM;
      }

      // 6502: JSR prx / JSR MCASH -- the old laser's price back, whatever the new one costs.
      ReceiveCash(_commander, EquipmentPrice(item, _fuel));
    }

    // 6502: ref3 -- LDA T1 / STA LASER,X.
    _commander.bytes[mount] = _newPower;
  }

  void EquipShipScreen(TradeScreen& _screen, CommanderBlock& _commander, std::uint8_t _techLevel) noexcept
  {
    /*
     * 6502: et11's `JMP EQSHP` -- the screen redraws itself after every purchase, so this is a loop
     * rather than a routine that returns. Every other exit leaves it.
     */
    for (;;)
    {
      // 6502: LDA #32 / JSR TRADEMODE -- which sets the cursor and the case flags too.
      _screen.effects.SetUpTradeScreen(EQUIP_SHIP_VIEW);

      // 6502: LDA #12 / JSR DOXC / LDA #207 / JSR spc / LDA #185 / JSR NLIN3.
      _screen.text.column = TITLE_COLUMN;
      PrintThenSpace(_screen.printer, EQUIP_TOKEN);
      _screen.printer.Print(SHIP_TOKEN);

      // 6502: LDA #%10000000 / STA QQ17 / JSR INCYC -- written out rather than JSR TT69, so no
      // newline comes with it.
      _screen.printer.SetCaseFlags(0x80);
      MoveCursorDown(_screen.text);

      /*
       * 6502: LDA tek / CLC / ADC #3 / CMP #12 / BCC P%+4 / LDA #14 / STA Q / STA QQ25 / INC Q.
       *
       * QQ25 is the highest item number gnum will accept, and Q is one more because the listing
       * loop runs `CPX Q / BCC EQL1`.
       */
      std::uint8_t highest = AddWithCarry(_techLevel, TECH_LEVEL_OFFSET, false).value;
      if (highest >= TECH_CAP_TEST)
      {
        highest = TECH_CAP_VALUE;
      }

      const std::uint8_t fuel = _commander.At(Field::Fuel);

      // 6502: EQL1 -- LDX #1 and count up to Q, so the fuel line is item 1 on screen and item 0 in
      // the table.
      for (std::uint8_t item = 1; item <= highest; ++item)
      {
        PrintNewline(_screen.printer);

        // 6502: LDX XX13 / CLC / JSR pr2 / JSR TT162 -- the number, three wide, then a space.
        PrintByteValue(_screen.characters, item, false);
        PrintSpace(_screen.printer);

        // 6502: LDA XX13 / CLC / ADC #104 / JSR TT27.
        _screen.printer.Print(static_cast<std::uint8_t>(ITEM_NAME_BASE + item));

        // 6502: LDA XX13 / JSR prx-3 / SEC / LDA #25 / JSR DOXC / LDA #6 / JSR TT11.
        const std::uint16_t price = EquipmentPrice(static_cast<std::uint8_t>(item - 1u), fuel);
        _screen.text.column = PRICE_COLUMN;
        PrintValue(_screen.characters, price, PRICE_DIGITS, true);
      }

      // 6502: JSR CLYNS / LDA #127 / JSR prq / JSR gnum.
      _screen.effects.ClearBottomRows();
      PrintThenQuestion(_screen.printer, ITEM_TOKEN);

      const NumberEntry entry = ReadNumber(_screen.keys, _screen.characters, _screen.text, highest);

      // 6502: gnum's JMP BAY2 -- a letter leaves without a beep and without the docking bay's
      // usual route.
      if (entry.outcome == DigitResult::LeaveScreen)
      {
        return;
      }

      // 6502: BEQ bay / BCS bay -- nothing entered, or too large, and out with no sound.
      if (entry.value == 0 || entry.outcome == DigitResult::TooBig)
      {
        return;
      }

      /*
       * 6502: SBC #0 -- and this is a subtraction of ONE, not of nothing.
       *
       * The carry is clear here, because `BCS bay` did not branch, so `A - 0 - (1 - C)` is A - 1.
       * That turns the number the player typed into the table's item number, and it is the whole
       * reason the two numbering schemes never collide.
       */
      const std::uint8_t item = static_cast<std::uint8_t>(entry.value - 1u);

      // 6502: LDA #2 / JSR DOXC / JSR INCYC.
      _screen.text.column = 2;
      MoveCursorDown(_screen.text);

      /*
       * 6502: eq -- JSR prx / JSR LCASH / BCS c / LDA #197 / JSR prq / JMP err.
       *
       * The money goes first. Everything below that finds the item already fitted hands it back.
       */
      if (!SpendCash(_commander, EquipmentPrice(item, fuel)))
      {
        PrintThenQuestion(_screen.printer, CASH_TOKEN);
        _screen.effects.BeepAndPause();
        return;
      }

      /*
       * 6502: et0 through et10 -- thirteen comparisons, and Y walking alongside them.
       *
       * Y is the token `pres` complains with, and it is threaded through the chain rather than set
       * at each branch: `LDY #107` at et1, `LDY #111` at et5, and an unconditional `INY` at the top
       * of et6, et7, et8, etA, etB, et9 and et10. So the token depends on how far the chain got,
       * not on which branch was taken -- which is why the port walks it the same way instead of
       * looking it up.
       */
      std::uint8_t complaint = 0;
      bool alreadyFitted = false;

      // 6502: BNE et0 -- item 0 is fuel, and a full tank is not an error.
      if (item == 0)
      {
        _commander.At(Field::Fuel) = FULL_TANK;
      }

      // 6502: et0 -- CMP #1, the missile.
      if (!alreadyFitted && item == 1)
      {
        const std::uint8_t missiles = static_cast<std::uint8_t>(_commander.At(Field::Missiles) + 1u);
        complaint = ALL_TOKEN;
        if (missiles >= MAX_MISSILES)
        {
          alreadyFitted = true;
        }
        else
        {
          _commander.At(Field::Missiles) = missiles;
          _screen.effects.ResetMissileIndicators();
        }
      }

      // 6502: et1 -- LDY #107, then CMP #2, the large cargo bay.
      if (!alreadyFitted)
      {
        complaint = LARGE_CARGO_TOKEN;
        if (item == 2)
        {
          if (_commander.At(Field::CargoCapacity) == LARGE_HOLD_CAPACITY)
          {
            alreadyFitted = true;
          }
          else
          {
            _commander.At(Field::CargoCapacity) = LARGE_HOLD_CAPACITY;
          }
        }
      }

      // 6502: et2 -- CMP #3, and the INY is INSIDE this branch rather than before it.
      if (!alreadyFitted && item == 3)
      {
        ++complaint;
        if (_commander.At(Field::Ecm) != 0)
        {
          alreadyFitted = true;
        }
        else
        {
          _commander.At(Field::Ecm) = 0xFF;
        }
      }

      // 6502: et3 and et4 -- the pulse and beam lasers, which touch neither Y nor the pres path.
      if (!alreadyFitted && (item == 4 || item == 5))
      {
        const std::uint8_t view = ChooseView(_screen, _techLevel);
        Refund(_commander, view, (item == 4) ? PULSE_POWER : BEAM_POWER, fuel);
      }

      // 6502: et5 -- LDY #111, CMP #6, and the ONLY branch that falls into pres rather than
      // jumping to it: `LDX BST / BEQ ed9` leaves the fall-through as the error path.
      if (!alreadyFitted)
      {
        complaint = FUEL_SCOOPS_TOKEN;
        if (item == 6)
        {
          if (_commander.At(Field::FuelScoops) != 0)
          {
            alreadyFitted = true;
          }
          else
          {
            _commander.At(Field::FuelScoops) = 0xFF;
          }
        }
      }

      /*
       * 6502: et6 through et10 -- six items whose flags sit consecutively, each preceded by an
       * unconditional INY.
       */
      struct Fitting
      {
        std::uint8_t item;
        Field field;
        std::uint8_t fittedValue;
      };
      static constexpr Fitting FITTINGS[] = {
        {7, Field::EscapePod, 0xFF}, // 6502: DEC ESCP
        {8, Field::EnergyBomb, ENERGY_BOMB_FITTED},
        {9, Field::EnergyUnit, 1}, // 6502: INC ENGY, from a known zero
        {10, Field::DockingComputer, 0xFF},
        {11, Field::GalacticDrive, 0xFF},
      };

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
        if (_commander.At(fitting.field) != 0)
        {
          alreadyFitted = true;
        }
        else
        {
          _commander.At(fitting.field) = fitting.fittedValue;
        }
      }

      // 6502: et9 and et10 -- the military and mining lasers, each preceded by its own INY.
      if (!alreadyFitted)
      {
        ++complaint;
        if (item == 12)
        {
          const std::uint8_t view = ChooseView(_screen, _techLevel);
          Refund(_commander, view, MILITARY_POWER, fuel);
        }
        ++complaint;
        if (item == 13)
        {
          const std::uint8_t view = ChooseView(_screen, _techLevel);
          Refund(_commander, view, MINING_POWER, fuel);
        }
      }

      /*
       * 6502: pres -- STY K / JSR prx / JSR MCASH / LDA K / JSR spc / LDA #31 / JSR TT27, then err.
       *
       * The refund uses `prx` with A as it stands, which by this point is the ITEM NUMBER -- so the
       * money handed back is the price of what was being bought, not of what was already fitted.
       */
      if (alreadyFitted)
      {
        ReceiveCash(_commander, EquipmentPrice(item, fuel));
        PrintThenSpace(_screen.printer, complaint);
        _screen.printer.Print(PRESENT_TOKEN);
        _screen.effects.BeepAndPause();
        return;
      }

      // 6502: et11 -- JSR dn, which prints the cash and falls into dn2, then JMP EQSHP.
      PrintSpace(_screen.printer);
      PrintThenSpace(_screen.printer, CASH_LINE_TOKEN);
      _screen.effects.BeepAndPause();
    }
  }

} // namespace Elite
