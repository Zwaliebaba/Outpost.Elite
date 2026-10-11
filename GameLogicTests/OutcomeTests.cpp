#include "pch.h"

#include "SpaceObject.h"
#include "Outcome.h"
#include "ReferenceRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// A product's cargoHold entry: the units held, then the units on sale.
constexpr std::size_t HOLD_ENTRY_BYTES = 2;

// The value OutcomeValue gives for _name, which must be one of OUTCOMES.
[[nodiscard]] std::string Value(const Elite::GameState& _state, std::string_view _name)
{
  const std::optional<std::string> value = Elite::OutcomeValue(_state, _name);
  if (!value.has_value())
    Assert::Fail(L"an outcome has a value");
  return value.value_or("");
}

} // namespace

// The outcomes (Outcome.h, D33): the named facts of the game's state that a player could notice, read through OutcomeValue
// and checked beside every state digest of the corpus and the twins.
TEST_CLASS(OutcomeTests)
{
public:
  // OutcomeText holds every one of OUTCOMES, in its order, as OutcomeValue reads it, and is well formed; a name that is
  // not an outcome has no value.
  TEST_METHOD(EveryOutcomeIsReadByItsName)
  {
    ReferenceRig rig("Outcomes");
    const Elite::GameState& state = rig.State();
    const std::string text = Elite::OutcomeText(state);
    std::string expected;
    for (const Elite::OutcomeName& outcome : Elite::OUTCOMES)
    {
      expected += (expected.empty() ? "" : " ") + std::string(outcome.name) + "=" + Value(state, outcome.name);
      Assert::AreEqual(Value(state, outcome.name).c_str(), std::string(Elite::FindOutcome(text, outcome.name).value_or("")).c_str());
    }
    Assert::AreEqual(expected.c_str(), text.c_str());
    std::string error;
    Assert::IsTrue(Elite::CheckOutcomeText(text, error), L"well formed");
    Assert::IsFalse(Elite::OutcomeValue(state, "fuelTonnes").has_value(), L"not an outcome");
    Assert::IsTrue(Elite::CompareOutcomes(text, text).empty(), L"nothing differs from itself");
  }

  // Each outcome reads the state its name says: the credits' two words, the hold's commodities, the title's characters, with
  // one that would not survive in a line escaped, and the active ship slots of the first shipSlotCount.
  TEST_METHOD(EachOutcomeReadsWhatItNames)
  {
    ReferenceRig rig("OutcomeFields");
    Elite::GameState& state = rig.State();
    state.SetWordOf(state.Data().commander.creditsTenths, 0x2345);
    state.Data().commander.creditsTenths.data75F5 = 0x0001;
    Assert::AreEqual("74565", Value(state, "creditsTenths").c_str(), false, L"10000h + 2345h tenths");

    const std::span<Elite::Counted<std::uint8_t>> hold =
      state.BytesFrom(state.Data().commander.cargoHold, sizeof(Elite::CommanderState::CargoHold));
    for (std::size_t commodity = 0; commodity < 17; ++commodity)
      hold[commodity * HOLD_ENTRY_BYTES] = static_cast<std::uint8_t>(commodity);
    hold[2 * HOLD_ENTRY_BYTES + 1] = 99; // on sale, not held
    Assert::AreEqual("0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16", Value(state, "cargo").c_str());

    constexpr std::string_view TITLE = "ARCH ANG=";
    for (std::size_t index = 0; index < TITLE.size(); ++index)
      state.Data().commander.commanderRankText[index] = static_cast<std::uint8_t>(TITLE[index]);
    Assert::AreEqual("ARCH%20ANG%3D", Value(state, "title").c_str());

    state.Data().world.shipSlotCount = 3;
    for (std::uint16_t slot = 0; slot < 4; ++slot)
      state.Slot(static_cast<Elite::SlotIndex>(slot)).type = Elite::SpaceObject::ACTIVE;
    state.Slot(static_cast<Elite::SlotIndex>(1)).type = 0x0A; // type 5, not active
    Assert::AreEqual("2", Value(state, "shipSlotsInUse").c_str(), false, L"slots 0 and 2: slot 1 is not active, slot 3 not walked");

    state.Data().playerShip.playerEnergy = 0x3FF;
    Assert::AreEqual("1023", Value(state, "energy").c_str());
  }

  // What differs is told by kind: an outcome of the player's progress, a timing one, and one the recorded outcomes lack,
  // which fails whatever its kind. A text that is not outcomes is refused, saying why.
  TEST_METHOD(DifferencesAreToldByKind)
  {
    ReferenceRig rig("OutcomeDifferences");
    Elite::GameState& state = rig.State();
    const std::string recorded = Elite::OutcomeText(state);
    state.Data().commander.killCount = static_cast<std::uint8_t>(state.Data().commander.killCount + 1);
    state.Data().playerShip.playerEnergy = static_cast<std::uint16_t>(state.Data().playerShip.playerEnergy ^ 1);
    std::string lacking = recorded;
    lacking.erase(lacking.find(" fuel="), lacking.find(' ', lacking.find(" fuel=") + 1) - lacking.find(" fuel="));

    const std::vector<Elite::OutcomeDifference> differences = Elite::CompareOutcomes(lacking, Elite::OutcomeText(state));
    Assert::AreEqual(std::size_t{3}, differences.size());
    Assert::AreEqual("kills", std::string(differences[0].name).c_str());
    Assert::IsTrue(differences[0].kind == Elite::OutcomeKind::Progress, L"kills are progress");
    Assert::AreEqual("energy", std::string(differences[1].name).c_str());
    Assert::IsTrue(differences[1].kind == Elite::OutcomeKind::Timing, L"energy is timing");
    Assert::IsTrue(differences[1].recorded.has_value(), L"and was recorded");
    Assert::AreEqual("fuel", std::string(differences[2].name).c_str());
    Assert::IsFalse(differences[2].recorded.has_value(), L"fuel was not recorded");

    const auto refuses = [](std::string_view _text, std::string_view _why)
    {
      std::string error;
      Assert::IsFalse(Elite::CheckOutcomeText(_text, error), L"refused");
      Assert::IsTrue(error.find(_why) != std::string::npos, L"says why");
    };
    refuses("", "no outcome");
    refuses("kills=1 kills=1", "given twice");
    refuses("kills=1 fuelTonnes=3", "not an outcome");
    refuses("kills=", "not a name=value pair");
    refuses("kills", "not a name=value pair");
    refuses("kills=1  fuel=3", "not a name=value pair");
  }
};

} // namespace GameLogicTests
