#include "pch.h"

#include "Text.h"
#include "TwinRig.h"

#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Commander = Elite::CommanderState;

// The byte _member names, set to _value on the twin.
template <typename Struct> void SetByte(TwinRig& _rig, DataByte<Struct> _member, std::uint8_t _value)
{
  _rig.Both([_member, _value](Elite::Platform&, Elite::GameState& _state) { DataByteIn(_state, _member) = _value; });
}

// The cash, in tenths of credits, on the twin.
void SetCredits(TwinRig& _rig, std::uint32_t _tenths)
{
  _rig.Both([_tenths](Elite::Platform&, Elite::GameState& _state) { _state.Data().commander.creditsTenths = _tenths; });
}

// The amount held of the cargo menu row _row's commodity, or the amount on sale: a member of its lot in cargoHold.
struct HoldByte
{
  std::uint8_t row;
  Elite::Field<std::uint8_t> Commander::CargoHold::Lot::*amount;
};

[[nodiscard]] HoldByte Held(std::uint8_t _row)
{
  return HoldByte{_row, &Commander::CargoHold::Lot::held};
}

[[nodiscard]] HoldByte OnSale(std::uint8_t _row)
{
  return HoldByte{_row, &Commander::CargoHold::Lot::onSale};
}

// The amount _byte names, set to _value on the twin.
void SetHold(TwinRig& _rig, HoldByte _byte, std::uint8_t _value)
{
  _rig.Both([_byte, _value](Elite::Platform&, Elite::GameState& _state)
            { _state.Data().commander.cargoHold.lots[std::size_t{_byte.row} - 1].*_byte.amount = _value; });
}

// B or S on the cargo menu, then the keys _typed names at "Quantity?", separated by spaces, and Return: Replay.h's
// steps.
[[nodiscard]] std::string Trade(std::string_view _key, std::string_view _typed)
{
  std::string steps = "key " + std::string(_key) + "; wait 0.3\n";
  std::size_t start = 0;
  while (start < _typed.size())
  {
    std::size_t end = _typed.find(' ', start);
    if (end == std::string_view::npos)
    {
      end = _typed.size();
    }
    steps += "key " + std::string(_typed.substr(start, end - start)) + "; wait 0.05\n";
    start = end + 1;
  }
  return steps + "key Return; wait 0.3\n";
}

} // namespace

// Twins for the cargo menus' every refusal and the market prices' cut name, which no replay reaches (ADR-016).
TEST_CLASS(MarketTests)
{
public:
  // Every refusal and purchase of the buy screen: nothing on sale, a full hold, a quantity that is no number (a
  // shifted letter), 0, more than is on sale or fits, with and without the cargo bay extension, short of cash, alien
  // items, and gems at and past their cap; the quantity edited with Backspace and typed past its three characters;
  // the keys the menu ignores, and the cursor round both ends by keys and by the steering.
  TEST_METHOD(CargoIsBoughtByTypedQuantity)
  {
    TwinRig rig("TwinCargoBuy");
    rig.Play("key space; wait 4");
    SetCredits(rig, 1000000);
    rig.Play("key F3; wait 1");
    SetHold(rig, OnSale(1), 0);
    rig.Play("key b; wait 0.2");
    SetHold(rig, OnSale(1), 20);
    SetByte(rig, &Commander::CargoHold::cargoUsedTonnes, 20);
    rig.Play("key b; wait 0.2");
    SetByte(rig, &Commander::CargoHold::cargoUsedTonnes, 0);
    rig.Play("key b; wait 0.3\ndown Shift_L; wait 0.05\nkey a; wait 0.05\nup Shift_L; wait 0.05\nkey Return; wait 0.3\n" + Trade("b", "0") +
             Trade("b", "3 0") + Trade("b", "2 5") + Trade("b", "BackSpace 1 0 0 0 BackSpace BackSpace"));
    SetByte(rig, &Commander::largeCargoBayFitted, 1);
    rig.Play(Trade("b", "1"));
    SetCredits(rig, 0);
    rig.Play(Trade("b", "1") + "digest tonnes\nkey s; wait 0.2\nkey F3; wait 0.2\nkey x; wait 0.2\nkey Up; wait 0.2\nkey b; wait 0.2\n"
                               "key Up; wait 0.2");
    SetCredits(rig, 1000000);
    SetHold(rig, Held(16), 0xFA);
    SetHold(rig, OnSale(16), 50);
    rig.Play("key b; wait 0.2");
    SetHold(rig, Held(16), 0xF0);
    rig.Play(Trade("b", "2 0") + Trade("b", "1 2"));
    SetHold(rig, Held(16), 0);
    rig.Play(Trade("b", "5") + "key Down; wait 0.2\nkey Down; wait 0.2");
    SetByte(rig, &Elite::InputState::keyboardPitchRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, &Elite::InputState::keyboardPitchRate, 0xFB);
    rig.Play("wait 0.3\ndigest gems\nkey F9; wait 0.5\ndigest left");
  }

  // Every refusal and sale of the sell screen: nothing held, a quantity that is no number, 0 or more than is held, a
  // sale that fills the market's 255, contraband, alien items, and gold outside the tonnes; and the keys it ignores.
  TEST_METHOD(CargoIsSoldByTypedQuantity)
  {
    TwinRig rig("TwinCargoSell");
    rig.Play("key space; wait 4\nkey F2; wait 1");
    SetHold(rig, Held(1), 0);
    rig.Play("key s; wait 0.2");
    SetHold(rig, Held(1), 10);
    SetHold(rig, OnSale(1), 250);
    SetByte(rig, &Commander::CargoHold::cargoUsedTonnes, 10);
    rig.Play(Trade("s", "x") + Trade("s", "0") + Trade("s", "2 0") + Trade("s", "1 0") + "key Down; wait 0.2\nkey Down; wait 0.2");
    SetHold(rig, Held(3), 5);
    rig.Play(Trade("s", "5") + "key Up; wait 0.2\nkey Up; wait 0.2\nkey Up; wait 0.2");
    SetHold(rig, Held(17), 3);
    SetByte(rig, &Commander::CargoHold::cargoUsedTonnes, 3);
    rig.Play(Trade("s", "3") + "key Up; wait 0.2\nkey Up; wait 0.2\nkey Up; wait 0.2");
    SetHold(rig, Held(14), 10);
    rig.Play(Trade("s", "5") + "digest sold\nkey b; wait 0.2\nkey F2; wait 0.2\nkey F9; wait 0.5\ndigest left");
  }

  // The market prices with a system name that has a space in it, which the screen cuts there for good, and the keys
  // its wait ignores: its own F8, and keys that are neither Esc nor an F-key.
  TEST_METHOD(MarketPricesCutTheSystemNameAtASpace)
  {
    TwinRig rig("TwinMarketPrices");
    rig.Play("key space; wait 4");
    rig.Both(
      [](Elite::Platform&, Elite::GameState& _state)
      {
        constexpr std::string_view NAME = "LA VE";
        for (std::size_t index = 0; index < std::size(_state.Data().commander.currentSystem.name); ++index)
        {
          _state.Data().commander.currentSystem.name[index] =
            index < NAME.size() ? static_cast<std::uint8_t>(NAME[index]) : std::uint8_t{0};
        }
      });
    rig.Play("key F8; wait 1\nkey F8; wait 0.2\nkey x; wait 0.2\nkey Home; wait 0.2\ndigest prices\nkey F9; wait 0.5\ndigest left");
  }
};

} // namespace GameLogicTests
