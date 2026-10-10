#include "pch.h"

#include "DataOverlay.h"
#include "TwinRig.h"

#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

// The data segment byte at _offset, set to _value on the twin.
void SetByte(TwinRig& _rig, std::uint16_t _offset, std::uint8_t _value)
{
  _rig.Both([_offset, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _offset, _value); });
}

void SetByte(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  SetByte(_rig, _field.offset, _value);
}

// The cash, in tenths of credits, on the twin.
void SetCredits(TwinRig& _rig, std::uint32_t _tenths)
{
  _rig.Both(
    [_tenths](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      const std::uint16_t data = Elite::DataSegment(_program);
      _pc.Ram().Write16(data, DS.creditsTenths.offset, static_cast<std::uint16_t>(_tenths));
      _pc.Ram().Write16(data, DS.data75F5.offset, static_cast<std::uint16_t>(_tenths >> 16));
    });
}

// The cargo menu row _row's cargoHold entry: the amount held, then (+1) the amount on sale.
[[nodiscard]] std::uint16_t Held(std::uint8_t _row)
{
  return DS.cargoHold.At(_row - 1u);
}

[[nodiscard]] std::uint16_t OnSale(std::uint8_t _row)
{
  return static_cast<std::uint16_t>(DS.cargoHold.At(_row - 1u) + 1);
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
    SetByte(rig, OnSale(1), 0);
    rig.Play("key b; wait 0.2");
    SetByte(rig, OnSale(1), 20);
    SetByte(rig, DS.cargoUsedTonnes, 20);
    rig.Play("key b; wait 0.2");
    SetByte(rig, DS.cargoUsedTonnes, 0);
    rig.Play("key b; wait 0.3\ndown Shift_L; wait 0.05\nkey a; wait 0.05\nup Shift_L; wait 0.05\nkey Return; wait 0.3\n" + Trade("b", "0") +
             Trade("b", "3 0") + Trade("b", "2 5") + Trade("b", "BackSpace 1 0 0 0 BackSpace BackSpace"));
    SetByte(rig, DS.largeCargoBayFitted, 1);
    rig.Play(Trade("b", "1"));
    SetCredits(rig, 0);
    rig.Play(Trade("b", "1") + "digest tonnes\nkey s; wait 0.2\nkey F3; wait 0.2\nkey x; wait 0.2\nkey Up; wait 0.2\nkey b; wait 0.2\n"
                               "key Up; wait 0.2");
    SetCredits(rig, 1000000);
    SetByte(rig, Held(16), 0xFA);
    SetByte(rig, OnSale(16), 50);
    rig.Play("key b; wait 0.2");
    SetByte(rig, Held(16), 0xF0);
    rig.Play(Trade("b", "2 0") + Trade("b", "1 2"));
    SetByte(rig, Held(16), 0);
    rig.Play(Trade("b", "5") + "key Down; wait 0.2\nkey Down; wait 0.2");
    SetByte(rig, DS.keyboardPitchRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, DS.keyboardPitchRate, 0xFB);
    rig.Play("wait 0.3\ndigest gems\nkey F9; wait 0.5\ndigest left");
  }

  // Every refusal and sale of the sell screen: nothing held, a quantity that is no number, 0 or more than is held, a
  // sale that fills the market's 255, contraband, alien items, and gold outside the tonnes; and the keys it ignores.
  TEST_METHOD(CargoIsSoldByTypedQuantity)
  {
    TwinRig rig("TwinCargoSell");
    rig.Play("key space; wait 4\nkey F2; wait 1");
    SetByte(rig, Held(1), 0);
    rig.Play("key s; wait 0.2");
    SetByte(rig, Held(1), 10);
    SetByte(rig, OnSale(1), 250);
    SetByte(rig, DS.cargoUsedTonnes, 10);
    rig.Play(Trade("s", "x") + Trade("s", "0") + Trade("s", "2 0") + Trade("s", "1 0") + "key Down; wait 0.2\nkey Down; wait 0.2");
    SetByte(rig, Held(3), 5);
    rig.Play(Trade("s", "5") + "key Up; wait 0.2\nkey Up; wait 0.2\nkey Up; wait 0.2");
    SetByte(rig, Held(17), 3);
    SetByte(rig, DS.cargoUsedTonnes, 3);
    rig.Play(Trade("s", "3") + "key Up; wait 0.2\nkey Up; wait 0.2\nkey Up; wait 0.2");
    SetByte(rig, Held(14), 10);
    rig.Play(Trade("s", "5") + "digest sold\nkey b; wait 0.2\nkey F2; wait 0.2\nkey F9; wait 0.5\ndigest left");
  }

  // The market prices with a system name that has a space in it, which the screen cuts there for good, and the keys
  // its wait ignores: its own F8, and keys that are neither Esc nor an F-key.
  TEST_METHOD(MarketPricesCutTheSystemNameAtASpace)
  {
    TwinRig rig("TwinMarketPrices");
    rig.Play("key space; wait 4");
    rig.Both(
      [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
      {
        constexpr std::string_view NAME = "LA VE";
        for (std::size_t index = 0; index < DS.currentSystemName.SIZE_BYTES; ++index)
        {
          _pc.Ram().Write8(Elite::DataSegment(_program), DS.currentSystemName.At(index),
                           index < NAME.size() ? static_cast<std::uint8_t>(NAME[index]) : std::uint8_t{0});
        }
      });
    rig.Play("key F8; wait 1\nkey F8; wait 0.2\nkey x; wait 0.2\nkey Home; wait 0.2\ndigest prices\nkey F9; wait 0.5\ndigest left");
  }
};

} // namespace GameLogicTests
