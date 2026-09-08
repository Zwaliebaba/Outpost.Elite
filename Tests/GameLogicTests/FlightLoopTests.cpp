#include "pch.h"

#include "Charts.h"
#include "FlightUniverse.h"

#include "Arith.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "Rng.h"
#include "Dashboard.h"
#include "Explosion.h"
#include "ShipBlueprint.h"
#include "ShipSlot.h"
#include "Tactics.h"

#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The one thing the flight loop is asked here: that a launch leaves a station behind you.
 *
 * The frame-by-frame comparisons against the original -- the distance helpers, the control rates,
 * the gun, the per-ship loop, the docking checks, the frame tail and the whole-canvas compares --
 * were this file and went with the oracle (M6-b-5). What is left is the test that was never a
 * comparison: every routine on the path agreed with the shipped game one at a time, and the
 * station still vanished, because the port kept the line heap in a different object from the ship
 * arena and every line the station drew was written out of range.
 */
namespace GameLogicTests
{

  /*
   * What one frame is made of.
   *
   * `Frame` is the state a flight frame runs over, seeded and then corrected in the few places
   * where a seeded byte is not a state the game reaches: no Trumbles, the station's heap lent from
   * the sun's, a real message already up, and a real blueprint in `flight`. The corrections are
   * each worth a paragraph because each of them was a defect first.
   */
  namespace
  {
    /// Everything one frame needs that the shared `Universe` does not carry.
    struct Frame
    {
      Universe universe; ///< every byte of it, since M3-a -- the controls, the keys, the burst,
                         ///< the heap, the clipper's flag, the projection and the axes were eight
                         ///< members here while `FlightLoop` held references to them

      explicit Frame(std::uint32_t _seed)
      {
        Seed(universe, _seed);

        // 6502: TRIBCT -- zero, where `Seed` leaves 90. A launch has no Trumbles aboard, and the
        // frames below draw ships: the two are independent and mixing them would only make a
        // failure here harder to read.
        universe.trumbles.count = 0u;

        // 6502: LSO -- the station draws into the SUN's heap, which lives in the universe (§6.112).
        universe.LendSunHeap();

        /*
         * A message already up, and a REAL one.
         *
         * `Seed` leaves `MCH` at zero, and a message that jams sends `MESS` down `me1` to erase
         * whatever is showing -- so a zero would be printed, and `TT27` opens `TAX / BEQ csh`, which
         * prints the player's cash. The game cannot reach that: `MCH` is written only by `MESS`
         * itself, and the erase only runs when `DLY` is non-zero, which only `MESS` makes it.
         */
        universe.message.token = 101u;
        universe.message.column = 9u;
        universe.message.append = 1u;
        universe.message.delay = 12u;

        /*
         * 6502: XX0 -- a REAL blueprint, because zero is not a state the game reaches.
         *
         * Part 4 leaves `XX0` alone for the planet and the sun, so a body inherits whatever the last
         * ship put there (§6.90) -- and by the time the flight loop runs, something always has. With
         * zero the game reads `(XX0),15` out of its own zero page and the port reads a guarded zero,
         * which is a disagreement about an address neither would ever form.
         */
        universe.flight.blueprint = Elite::BlueprintOf(Elite::ShipType::CobraMk3);
        universe.screen.upperBitmapMode = 0xC0u;
        universe.status.laserCount = 0u;
        universe.status.laserPower = 0u;
        universe.status.missileArmed = 0u;
        universe.status.ecmOurs = 0u;
        universe.bubble.missileTarget = 0xFFu;
        universe.flight.speedTimes4Low = 0u;
        universe.flight.speedTimes4High = 0u;
        universe.control.roll = 128u;
        universe.control.pitch = 128u;
      }
    };

  } // namespace

  TEST_CLASS(TheWholeFlightFrame)
  {
  public:
    /*
     * The station you have just left is still there when you look back (§6.112).
     *
     * `TT110` spawns two ships: the planet ahead and the station BEHIND, at `INWK+7` = 1 with the
     * z sign set, which is 256 units back. Nothing in the loop is supposed to take it away -- part
     * 14 only ever ADDS one -- so a launch followed by a few frames should leave it in the bubble,
     * moving away as the ship pulls out.
     *
     * This is a whole-loop test rather than a routine comparison because that is where the fault
     * was: every routine on the path agreed with the shipped game one at a time.
     */
    TEST_METHOD(TheStationSurvivesTheLaunch)
    {
      Frame frame(4242u);

      // A clean bubble, because the launch builds its own: `RES2` empties it and then the two
      // spawns fill it, and a fixture's fleet would be a third thing that is not the game's.
      frame.universe.bubble.slots.fill(0u);
      frame.universe.bubble.counts.fill(0u);
      frame.universe.bubble.junk = 0u;
      frame.universe.view = 0u;
      frame.universe.spaceView = 0u;

      Elite::Ports ports = frame.universe.Ports();

      frame.universe.dockedFlag = 0xFFu; // 6502: QQ12 -- docked, which is the path that launches
      Elite::SystemSeeds selected{};
      Elite::Launch(frame.universe, ports, frame.universe.commander.systemX, frame.universe.commander.systemY, selected);

      Assert::AreEqual<std::uint32_t>(1u, frame.universe.bubble.Count(Elite::ShipType::Station),
                                      L"the launch leaves the station in the bubble");

      // 6502: LOOK1, which the launch ends with -- the front view, and `QQ11` back to zero.
      frame.universe.view = 0u;

      // 6502: LOOK1 with X = 1 -- the rear view, which is where a station you have just left is.
      frame.universe.spaceView = 1u;

      for (int pass = 0; pass < 40; ++pass)
      {
        const Elite::LoopOutcome outcome = Elite::MainFlightLoop(frame.universe, ports);
        Assert::IsTrue(outcome == Elite::LoopOutcome::Continued,
                       (L"frame " + std::to_wstring(pass) + L" should not end the flight").c_str());

        Assert::AreEqual<std::uint32_t>(1u, frame.universe.bubble.Count(Elite::ShipType::Station),
                                        (L"the station is still there after " + std::to_wstring(pass + 1) + L" frames").c_str());
      }

      /*
       * AND IT IS ON THE SCREEN, which is the half that was actually broken (§6.112).
       *
       * `NWSPS` points the station's heap at `LSO` -- the SUN's -- and the port keeps that region
       * in a different object from the ship arena, so every line the station drew was written out
       * of range and dropped. Byte 31 still said "drawn", the bubble still held it, the scanner
       * still counted it, and the rear view was empty. So this asserts the LINES: a station in the
       * heap it was given, and pixels on the canvas from drawing them.
       */
      const std::uint16_t heapAt = frame.universe.bubble.blocks[1].heap.Address();
      Assert::AreEqual<std::uint32_t>(Elite::SUN_HEAP_ADDRESS, heapAt, L"the station draws through the sun's heap");
      Assert::IsTrue(frame.universe.heap.Read(Elite::HeapOffset::FromAddress(heapAt)) > 1u, L"and the heap holds the lines it last drew");

      std::size_t lit = 0;
      for (int y = 0; y < Elite::Canvas::SPACE_VIEW_HEIGHT; ++y)
      {
        for (int column = 0; column < Elite::Canvas::CELL_COLUMNS; ++column)
        {
          lit += (frame.universe.canvas.Read(static_cast<std::uint16_t>(y / 8 * Elite::Canvas::ROW_BYTES + column * 8 + (y % 8))) != 0u)
                   ? 1u
                   : 0u;
        }
      }
      Assert::IsTrue(lit > 20u, L"and the space view has the station drawn in it");
    }

  };

} // namespace GameLogicTests
