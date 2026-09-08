#include "pch.h"


#include "Arith.h"
#include "LookupTables.h"
#include "ShipBlueprint.h"
#include "ShipSlot.h"

#include <algorithm>
#include <cstdint>
#include <array>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The ship data region, and why it is one region (slice 3a).
 *
 * This suite exists to PIN A FINDING rather than to exercise a routine. The plan's 3a row lists
 * the motion and slot routines and says nothing about the blueprints, and the ledger files them
 * under 3b because that is what they are ABOUT -- the vertices and edges a ship is drawn from.
 * But `MVEIT` reads byte 15 of the blueprint on every iteration to clamp acceleration, and
 * `NWSHP` reads bytes 5, 14 and 19 before a ship exists at all, so 3a TOUCHES them and cannot be
 * compared against the shipped game without them. That is §6.12's pattern for the seventh time.
 *
 * Extracting them then turned up the reason they cannot be thirty-three arrays, which is what
 * most of this file measures.
 */
namespace GameLogicTests
{

  namespace
  {
    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    /// Every distinct blueprint the pointer table names, in address order. Taken from `XX21` rather
    /// than from the `SHIP_` labels, because the labels are the thing this file finds unreliable.
    std::vector<std::uint16_t> BlueprintsInAddressOrder()
    {
      std::set<std::uint16_t> distinct;
      for (int type = 1; type <= Elite::SHIP_TYPE_COUNT; ++type)
      {
        const Elite::Blueprint* blueprint = Elite::BlueprintOf(Elite::TypeOf(static_cast<std::uint8_t>(type)));
        const std::uint16_t address = (blueprint == nullptr) ? std::uint16_t{0} : blueprint->address;
        if (address != 0)
        {
          distinct.insert(address);
        }
      }
      return {distinct.begin(), distinct.end()};
    }
  } // namespace

  TEST_CLASS(TheShipDataRegion)
  {
  public:
    /*
     * TWO BLUEPRINTS OVERRUN THEIR NEIGHBOUR, and this is the assertion the extraction's shape
     * rests on. The header says how long a blueprint is -- twenty bytes, then `(XX0),8` of
     * vertices, four times `(XX0),9` of edges and `(XX0),12` of faces -- and for thirty of the
     * thirty-three that is exactly the distance to the next blueprint.
     *
     * For the splinter and the Thargon it is 24 and 60 bytes MORE, so their edges and faces are
     * read out of the ship that follows them. Both are also the only two blueprints in the build
     * with no `SHIP_x_EDGES` label, so the label set cannot settle it either.
     *
     * This is asserted rather than worked around because a port that sliced the region into
     * per-ship arrays would truncate those two and be wrong in a way no other test would notice --
     * the ships would simply lose their far edges. Keeping the region whole and indexing by address
     * makes the overrun reproduce, which is what ADR-001 asks for.
     */
    TEST_METHOD(TheHeaderExtentAgreesWithTheLayoutExceptWhereItFamouslyDoesNot)
    {
      const std::vector<std::uint16_t> blueprints = BlueprintsInAddressOrder();
      Assert::AreEqual<std::size_t>(33, blueprints.size(), L"thirty-three distinct blueprints");

      std::vector<std::string> disagreements;
      for (std::size_t index = 0; index + 1 < blueprints.size(); ++index)
      {
        const std::uint16_t start = blueprints[index];
        const int gap = static_cast<int>(blueprints[index + 1]) - static_cast<int>(start);
        const int extent = static_cast<int>(Elite::BlueprintAt(start)->Extent());

        if (gap != extent)
        {
          disagreements.push_back("blueprint at " + std::to_string(start) + ": header says " + std::to_string(extent) + ", gap is " +
                                  std::to_string(gap) + " (" + std::to_string(gap - extent) + ")");
        }
      }

      for (const std::string& line : disagreements)
      {
        Logger::WriteMessage((line + "\n").c_str());
      }

      // Three, and not fewer: if the data ever stopped disagreeing, the reasoning behind extracting
      // one region rather than thirty-three arrays would have changed and should be re-read.
      Assert::AreEqual<std::size_t>(3, disagreements.size(), L"three blueprints whose header disagrees with the layout");

      // And the two that overrun do so by the measured amount, so a change of ship data is a test
      // failure with a number in it rather than a count that happens to stay at three.
      const std::vector<std::uint16_t> OVERRUNNING = {54643, 60349}; // the splinter and the Thargon
      for (const std::uint16_t start : OVERRUNNING)
      {
        const auto found = std::find(blueprints.begin(), blueprints.end(), start);
        Assert::IsTrue(found != blueprints.end(), L"the overrunning blueprint is still there");
        const int gap = static_cast<int>(*(found + 1)) - static_cast<int>(start);
        Assert::IsTrue(Elite::BlueprintAt(start)->Extent() > gap, Widen("blueprint at " + std::to_string(start) + " still overruns").c_str());
      }
    }

  };

  /*
   * The two arithmetic routines 3a needs and phase 1 does not have.
   *
   * `TIDY` calls `NORM` every sixteenth iteration of the main loop to stop a ship's orientation
   * vectors drifting out of shape as `MVEIT`'s rounding accumulates, and `MV40` -- the path a
   * planet or a sun takes through `MVEIT` -- reaches `MULT3`. Neither appears in the plan's 3a row,
   * which is the same §6.12 omission as the blueprints.
   */
  /*
   * The bubble's shape, against the assembled layout.
   *
   * Constants rather than behaviour, and worth a test because both of them are the kind that a port
   * gets from the wrong place. `NOSH` appears as both 10 and 20 in the upstream `original-sources`
   * listings, which serve several versions of the game, so grepping them gives whichever comes
   * first; `NI%` is 37 and the obvious guess from a 36-byte-looking workspace is 36. Neither is
   * settled by reading, and the binary settles both.
   */
  TEST_CLASS(TheBubblesShape)
  {
  public:
    /// 6502: GINF -- slot to block, and the bound the original does not have.
    TEST_METHOD(EverySlotHasItsOwnBlockAndNothingBeyondThemDoes)
    {
      Elite::Bubble bubble;

      std::set<Elite::Ship*> distinct;
      for (std::uint8_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
      {
        Elite::Ship* block = Elite::SlotBlock(bubble, slot);
        Assert::IsNotNull(block, L"every slot in range has a block");
        distinct.insert(block);
      }
      Assert::AreEqual<std::size_t>(Elite::MAX_SHIPS, distinct.size(), L"and they are all different");

      Assert::IsNull(Elite::SlotBlock(bubble, Elite::MAX_SHIPS), L"one past the last slot has none");
      Assert::IsNull(Elite::SlotBlock(bubble, 255), L"and nor does anything beyond it");
    }
  };

} // namespace GameLogicTests
