#include "pch.h"

#include "FlightPort.h"
#include "FlightUniverse.h"
#include "StateHash.h"
#include "UniverseImage.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * `Elite::HashState`, the library-native state hash (Modernize.md M5-e-3).
 *
 * The hash's definition is a hand-written fold over `Universe`'s fields, and the risk of a
 * hand-written fold is a field it forgot. The check is the label table: `UniverseImage` names
 * every byte of state that has a 6502 label, so if changing any one of those bytes moves this hash,
 * the fold sees at least everything the image sees -- and the image is what ADR-007 §6 holds every
 * byte of `Universe` to. The table is walked UNRESOLVED (every address zero), the way the replay
 * hashes it: the cells' getters and setters are all this needs, so the suite loads no oracle. The
 * pixels, the heap arena and the controls, which the image leaves to the replay's `Digest`, are
 * checked by hand below.
 */
namespace GameLogicTests
{

  TEST_CLASS(TheStateHash)
  {
  public:
    TEST_METHOD(EveryCellTheImageNamesMovesTheHash)
    {
      const Where at{};

      Universe universe;
      universe.spriteRegistersAreOurs = true;
      Seed(universe, 5u);
      const std::uint64_t base = Elite::HashState(universe);

      std::size_t checked = 0;
      std::size_t inert = 0;
      std::wstring unseen; // every cell the fold walked past, reported together rather than first-only
      for (const Cell& cell : ImageCells(universe, at))
      {
        const std::uint8_t before = cell.get();
        cell.set(static_cast<std::uint8_t>((before ^ 0xFFu) & cell.mask));
        if (cell.get() == before)
        {
          // A setter the table declares as a no-op (the high byte of `XX0`), or the low half of a
          // word pair, which `AddressPair` holds back until the high half commits it -- restore it
          // all the same, so that a pending half is not carried into the next cell's commit.
          cell.set(before);
          ++inert;
          continue;
        }
        if (Elite::HashState(universe) == base)
        {
          unseen += L" " + cell.name;
        }
        cell.set(before);
        Assert::AreEqual(base, Elite::HashState(universe), (L"cell " + cell.name + L" restored and the state hash did not").c_str());
        ++checked;
      }
      Logger::WriteMessage(("state hash: " + std::to_string(checked) + " cells move it, " + std::to_string(inert) + " inert\n").c_str());
      Assert::IsTrue(unseen.empty(), (L"cells the state hash does not see:" + unseen).c_str());
      Assert::IsTrue(checked > 500u, L"the table is far shorter than the state it describes");
    }

    /// The three things the image leaves to the replay's own digest are bytes of `Universe` here.
    TEST_METHOD(SeesThePixelsTheHeapAndTheControls)
    {
      Elite::Universe universe;
      const std::uint64_t base = Elite::HashState(universe);

      universe.canvas.Write(0x1234u, 0x5Au);
      const std::uint64_t pixels = Elite::HashState(universe);
      Assert::AreNotEqual(base, pixels, L"a pixel byte did not move the hash");

      universe.heap.Write(Elite::HeapOffset::FromAddress(static_cast<std::uint16_t>(Elite::LineHeap::BASE + 10u)), 0x77u);
      const std::uint64_t heap = Elite::HashState(universe);
      Assert::AreNotEqual(pixels, heap, L"a line-heap byte did not move the hash");

      universe.control.roll = 0x40u;
      Assert::AreNotEqual(heap, Elite::HashState(universe), L"a control byte did not move the hash");
    }

    TEST_METHOD(IsAFunctionOfTheStateAlone)
    {
      Universe first;
      Universe second;
      Seed(first, 3u);
      Seed(second, 3u);
      Assert::AreEqual(Elite::HashState(first), Elite::HashState(second), L"the same state hashed differently");

      second.dust.x[0] = static_cast<std::uint8_t>(second.dust.x[0] + 1u);
      Assert::AreNotEqual(Elite::HashState(first), Elite::HashState(second), L"one changed byte of stardust was not noticed");
    }

    /// `Game::StateHash` is this hash over the universe `Game` owns, and nothing else.
    TEST_METHOD(GameAnswersItOverItsOwnUniverse)
    {
      auto port = std::make_unique<FlightPort>();
      Assert::AreEqual(Elite::HashState(port->universe), port->game.StateHash());
      port->universe.flight.delta = static_cast<std::uint8_t>(port->universe.flight.delta + 1u);
      Assert::AreEqual(Elite::HashState(port->universe), port->game.StateHash(), L"the game hashed something other than its state");
    }
  };

} // namespace GameLogicTests
