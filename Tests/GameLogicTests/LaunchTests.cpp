#include "pch.h"

#include "FlightPort.h"
#include "FlightUniverse.h"

#include "Commander.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "Market.h"
#include "PlanetDraw.h"
#include "Combat.h"
#include "ShipDraw.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * Leaving the station, and dying (the launch path).
 *
 * `TT110` is what the "1" key reaches from the docked screens, and the comparisons against the
 * shipped routines under it -- the resets, the fine, the rings, the death screen's set-up -- went
 * with the oracle (M6-b-5). What is left is what the original could not have answered: that both
 * tunnels are paced one frame per circle, that dying clears the keys and stops the ship, and that
 * the wreckage stays inside the space view.
 */
namespace GameLogicTests
{

  /*
   * 6502: LAUN and the `HFS2` it falls into -- the tunnel a launch and an arrival both open with.
   *
   * The port had this behind a seam until slice 3d, which is why the tunnel was the last thing on
   * the launch path never compared against the shipped game (§6.109). That comparison went with
   * the oracle; what stays is the pacing, which it never covered: the rings are drawn one frame
   * per circle, and 34 circles is what a full tunnel is.
   */
  TEST_CLASS(TheLaunchTunnel)
  {
  public:
    TEST_METHOD(ThePacingIsOneFramePerCircle)
    {
      struct Counting final : Elite::Presenter
      {
        std::uint32_t circles = 0;
        void Present() override
        {
          ++circles;
        }
        void WaitFrames(std::uint8_t) override {}
        void HoldFlightFrame(std::uint8_t) override {}
        void HoldTitleFrame(std::uint8_t) override {}
      };

      for (const std::uint8_t step : {std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{8}})
      {
        Universe universe;
        Seed(universe, 0x71u);
        universe.heaps.lowestVisibleRow = 143u;
        universe.heaps.ballHeapTop = 0u;
        universe.heaps.circleStep = step;

        Elite::ClipState clip;
        Counting counting;
        Elite::DrawHyperspaceRings(universe.canvas, universe.heaps, universe.geometry, universe.math, clip, counting);

        Assert::AreEqual<std::uint32_t>(34u, counting.circles, (WidenText("STP " + std::to_string(step)) + L": circles shown").c_str());
      }

      // And a null pacing is the same drawing with nobody watching -- which is how every other
      // caller runs it, so the count above must not be reachable through a screen difference.
      Universe unpaced;
      Seed(unpaced, 0x71u);
      unpaced.heaps.lowestVisibleRow = 143u;
      unpaced.heaps.ballHeapTop = 0u;
      unpaced.heaps.circleStep = 8u;

      Universe paced;
      Seed(paced, 0x71u);
      paced.heaps.lowestVisibleRow = 143u;
      paced.heaps.ballHeapTop = 0u;
      paced.heaps.circleStep = 8u;

      Elite::ClipState clipA;
      Elite::ClipState clipB;
      Counting counting;
      Elite::DrawHyperspaceRings(unpaced.canvas, unpaced.heaps, unpaced.geometry, unpaced.math, clipA, unpaced.unused);
      Elite::DrawHyperspaceRings(paced.canvas, paced.heaps, paced.geometry, paced.math, clipB, counting);

      const std::span<const std::uint8_t> quiet = unpaced.canvas.Screen();
      const std::span<const std::uint8_t> watched = paced.canvas.Screen();
      for (std::size_t index = 0; index < quiet.size(); ++index)
      {
        Assert::AreEqual(quiet[index], watched[index], (L"pacing changes no pixel, byte " + std::to_wstring(index)).c_str());
      }
    }
  };

  namespace
  {
    /*
     * THE LAUNCH PATH HAS NO SEAMS LEFT, and this object is what is left of the ones it had.
     *
     * `LAUN` was the last: the docking tunnel needed the ball line heap, which arrived in 3c, and
     * the stub outlived its reason by two slices as every other one on this path did (§6.109). It
     * is ported now and runs for real on both sides, so the whole-bitmap compare below covers the
     * tunnel as well as everything after it. What remains here is `StartUpEffects` for the
     * routines that still take one; `Launch` no longer does.
     */
    struct RecordingStart final : Elite::Presenter, Elite::Keyboard
    {
      void Present() override {}
      void HoldFlightFrame(std::uint8_t) override {}
      void HoldTitleFrame(std::uint8_t) override {}

      /*
       * WHICH KEYS ARE DOWN, scripted, because the loop `RDKEY` drives is key-driven and nothing
       * else decides how many frames the title screen runs for.
       *
       * It answered `TitleKey` until M3-b-3d, when `RDKEY` stopped being a seam: the walk is
       * `Elite::ScanKeyboard`'s now and this is the one line of it that was ever the platform's.
       * `quiet` walks answer "nothing down"; the one after holds `key`, and `KY7` too when `fire`
       * is on so that the loop takes `BMI TL3` instead of `INC JSTK`.
       *
       * A WALK IS COUNTED AT ITS FIRST KEY. `ScanKeyboard` counts DOWN from the top of the logger,
       * so the highest index is where a scan begins and `scans` still counts scans.
       */
      static constexpr std::size_t WALK_START = std::tuple_size_v<Elite::KeyLogger> - 1u;

      std::uint32_t quiet = 0;
      std::uint8_t key = 0;
      bool fire = false;
      std::uint32_t scans = 0;

      [[nodiscard]] bool Held(std::size_t _key) override
      {
        if (_key == WALK_START)
        {
          ++scans;
        }
        if (scans <= quiet)
        {
          return false;
        }
        return (_key == key) || (fire && _key == Elite::KEY_FIRE);
      }

      [[nodiscard]] std::uint8_t NextKey() override { return 0; }
      void Flush() override {}

      void WaitFrames(std::uint8_t) override {}
    };

    /*
     * `StopDockingMusic` WAS COUNTED HERE AND IS NOT ANY MORE (M3-b-2b).
     *
     * `RES2` opens with `JSR stopbd` and the seam counted it, with the original trapped at
     * `stopbd` so that neither machine ran it. The library runs it, and on this path it does
     * nothing: no tune is playing, so `stopbd` falls to `stopat` and `stopat`'s first test
     * returns. What the seam was counting is silence, which is not a thing a call count can
     * assert.
     */
    /*
     * `RecordingLaunch` WAS HERE AND IS NOT ANY MORE (M4-a-1). It was `SFS1` answering "there was
     * room" for a launch that never spawns a child, so it was one seam this fixture had to declare
     * because `Ports` carried it.
     */

    /*
     * `RecordingOutside` -- `ShipDrawEffects` answered with nothing -- WAS HERE AND IS NOT ANY
     * MORE (M6-0-a-3). A launch draws the tunnel and the station and never a body or a cloud, so
     * it was one seam this fixture had to declare because `Ports` carried it.
     */

    /// Everything the launch works on.
    struct Leaving
    {
      Universe universe; ///< every byte of it, since M3-a
      RecordingStart start;

      // 6502: LSO -- `NWSPS` hands the station the sun's heap, and this launch creates one, so the
      // arena has to be lent that window or the station's lines go nowhere (§6.112).
      Leaving()
      {
        universe.LendSunHeap();
      }

      /// The seams a launch reaches: the sounds, and `RESET`'s own.
      [[nodiscard]] Elite::Ports Ports() noexcept
      {
        return universe.PortsWith(start, start);
      }
    };

    /// A universe with something in every byte the reset is supposed to clear.
    void Occupy(Leaving& _leaving, std::uint32_t _seed)
    {
      Universe& universe = _leaving.universe;
      Seed(universe, _seed);

      universe.message.token = 101u;
      universe.message.column = 9u;
      universe.message.append = 1u;
      universe.message.delay = 12u;
      universe.flight.blueprint = Elite::BlueprintOf(Elite::ShipType::CobraMk3);

      universe.bubble.heapBottom = Elite::HeapOffset::FromAddress(static_cast<std::uint16_t>(Elite::SHIP_HEAP_TOP - 64u));
      universe.heaps.lowestVisibleRow = 199u;
      universe.heaps.ballHeapTop = 0x20u;
      universe.status.ecmCountdown = 20u;
      universe.status.ecmOurs = 0xFFu;
      universe.status.hyperspaceCounter = 5u;
      universe.status.hyperspaceCountdown = 9u;
      universe.screen.hyperspaceEffect = 0xFFu;
      universe.trumbles.count = 0x5Au;
      universe.spaceView = 2u;
      universe.explosions = 0x66u;

      _leaving.universe.control.roll = 200u;
      _leaving.universe.control.pitch = 40u;
      _leaving.universe.control.dockingComputer = 0xFFu;
      _leaving.universe.clip.clippingOff = 0x80u;
      universe.heaps.circleStep = 4u; // what the short-range chart's fuel circle leaves behind
    }

  } // namespace

  /*
   * A LAUNCH PACES BOTH OF ITS TUNNELS, and only a mutation could have asked for this test.
   *
   * `TT110` draws the effect twice: once inside `LAUN`, over the docked screen, and once through
   * `HFS1` after the bubble has been rebuilt. Every comparison against the original passed a null
   * pacing, because the 6502 has no present and the two sides had to agree on pixels rather than
   * on time -- so replacing the FIRST call's argument with `nullptr` changed no pixel, failed no
   * assertion, and quietly put half of §6.109 back: the tunnel instantly, the rings paced. Nothing
   * in the suite could see it. This is what sees it.
   *
   * Sixty-eight is thirty-four twice, and thirty-four is what `ThePacingIsOneFramePerCircle`
   * derives from the doubling: five circles each from the rings starting at radius 8 and 9,
   * four from each of the six starting at 10 to 15.
   */
  TEST_CLASS(TheLaunchPacing)
  {
  public:
    TEST_METHOD(ALaunchPacesBothOfItsTunnels)
    {
      struct Counting final : Elite::Presenter
      {
        std::uint32_t circles = 0;
        void Present() override
        {
          ++circles;
        }
        void WaitFrames(std::uint8_t) override {}
        void HoldFlightFrame(std::uint8_t) override {}
        void HoldTitleFrame(std::uint8_t) override {}
      };

      Leaving leaving;
      Occupy(leaving, 0x4Du);
      leaving.universe.view = 1u;

      Counting counting;
      Elite::Ports ports = leaving.universe.PortsWith(counting);

      leaving.universe.dockedFlag = 0xFFu; // 6502: QQ12 -- docked, so the launch is not the refusal path
      Elite::SystemSeeds selected{};
      Elite::Launch(leaving.universe, ports, leaving.universe.commander.systemX,
                    leaving.universe.commander.systemY, selected);

      Assert::AreEqual<std::uint32_t>(68u, counting.circles, L"both tunnels are paced, not just the second");

      // And the refusal path draws nothing at all, so it asks the platform for nothing either.
      Leaving flying;
      Occupy(flying, 0x4Du);

      Counting none;
      Elite::Ports flyingPorts = flying.universe.PortsWith(none);

      flying.universe.dockedFlag = 0u; // 6502: LDX QQ12 / BEQ NLUNCH
      Elite::SystemSeeds ignored{};
      Elite::Launch(flying.universe, flyingPorts, flying.universe.commander.systemX,
                    flying.universe.commander.systemY, ignored);

      Assert::AreEqual<std::uint32_t>(0u, none.circles, L"pressing 1 in flight is a view change and draws no tunnel");
    }
  };

  /*
   * 6502: DEATH -- the death screen, compared against the shipped routine.
   *
   * The interesting half is the setup rather than the animation: `DET1` is a bare `RTS` on this
   * build, so the view `TT66` is called with is whatever `RES2` left in A (§6.117), and this is
   * where that byte is pinned. The rest is the wreckage -- five pieces, a random type each, and a
   * laser count that decides how long the flight loop runs afterwards.
   */
  TEST_CLASS(TheDeathScreen)
  {
  public:
    /*
     * `Die` after the scene: the keys, the speed and the count, by what is left when it returns.
     *
     * Never a comparison, and it said so even when there was one to make: the 6502 runs `M%`
     * sixty-four times over the wreckage and never comes back, so the two sides could only be
     * compared frame by frame. What THIS pins is the five instructions between the scene and the
     * loop -- `JSR U%`, `STA DELTA`, and a count that must reach zero -- because a mutation that
     * dropped any of them passed the scene comparison.
     */
    TEST_METHOD(DyingClearsTheKeysAndStopsTheShip)
    {
      Leaving leaving;
      Occupy(leaving, 0x2Fu);
      leaving.universe.heaps.circleStep = 4u;
      for (std::size_t index = 0; index < leaving.universe.keys.size(); ++index)
      {
        leaving.universe.keys[index] = 0xFFu;
      }

      Elite::Ports ports = leaving.Ports();

      Elite::Die(leaving.universe, ports);

      Assert::AreEqual<std::uint8_t>(0xFFu, leaving.universe.keys[0], L"KLO+0 is below U%'s range and is untouched");
      for (std::size_t index = 1; index <= Elite::FLIGHT_KEYS_CLEARED; ++index)
      {
        Assert::AreEqual<std::uint8_t>(0u, leaving.universe.keys[index], (L"U% cleared KLO+" + std::to_wstring(index)).c_str());
      }
      Assert::AreEqual<std::uint8_t>(0xFFu, leaving.universe.keys[64], L"and the byte above U%'s range is untouched");
      Assert::AreEqual<std::uint8_t>(0u, leaving.universe.status.laserCount, L"LASCT counted down to zero");
      Assert::IsTrue(leaving.universe.flight.speed <= 1u, L"STA DELTA stopped the ship before the loop ran");
    }

    /*
     * The wreckage stays inside the space view, on every one of the sixty-five frames.
     *
     * A `FlightPort`, because the fault this pins needed every routine real. Half the wreckage is
     * spawned dead and `DOEXP` runs on it -- and until 2026-09-06 the cloud it grew had never been
     * seeded: byte 2 of the heap read as zero, the vertex copy ran from index 0 down through 255 to
     * 7, and two hundred and fifty bytes of `XX3` landed across every line heap above the dying
     * piece's. The pieces owning those heaps drew them as lines, past the bottom of the bitmap into
     * screen RAM -- the coloured blocks in the border -- and their own lines were never erased, so
     * the screen filled with wreckage that did not move (§6.157). `Leaving` could not have shown
     * it: its explosion is a counter.
     *
     * So three things, every frame: no piece that is not exploding has more on its heap than its
     * blueprint gave it; after the first frame, nothing in screen RAM outside the space view's
     * thirty-two columns changes; and the dashboard's block of screen RAM does not change at all.
     */
    TEST_METHOD(TheWreckageStaysInsideTheSpaceView)
    {
      auto port = std::make_unique<FlightPort>();
      port->universe.commander = Elite::DefaultCommander();
      Elite::ResetGame(port->universe, port->Ports()); // 6502: RESET

      Elite::SystemSeeds selected{};
      Elite::Launch(port->universe, port->Ports(), port->universe.commander.systemX, port->universe.commander.systemY,
                    selected); // 6502: TT110

      // Some way out from the station at speed, so `ASL DELTA` twice has something to work on.
      for (int step = 0; step < 60; ++step)
      {
        port->held.fill(0u);
        port->held[Elite::KEY_SPEED_UP] = 1u;
        Assert::IsTrue(port->Step() == Elite::LoopOutcome::Continued, L"flying");
      }

      /*
       * The checks RECORD their first failure and the test asserts it after `Die` returns: this
       * callback is reached from `noexcept` code, and an assertion that throws from inside it ends
       * the process rather than the test -- which is what the mutation harness saw when the cloud's
       * vertex count was zeroed (the run aborted with no summary, on both runners' shims).
       */
      struct Watching final : Elite::Presenter
      {
        FlightPort& port;
        std::vector<std::uint8_t> cells;
        std::uint32_t frames = 0;
        std::wstring failure; // the first frame that went wrong, and how

        explicit Watching(FlightPort& _port) noexcept
          : port(_port)
        {
        }

      void WaitFrames(std::uint8_t) override {}
      void Present() override {}
      void HoldTitleFrame(std::uint8_t) override {}
      void HoldFlightFrame(std::uint8_t) override
        {
          ++frames;
          if (!failure.empty())
          {
            return; // the first failure is the one worth reading
          }
          const std::wstring where = L"death frame " + std::to_wstring(frames);

          for (std::size_t slot = 0; slot < port.universe.bubble.slots.size(); ++slot)
          {
            const std::uint8_t type = port.universe.bubble.slots[slot];
            if (type == 0u)
            {
              break;
            }
            const Elite::Ship& piece = port.universe.bubble.blocks[slot];
            if (Elite::Has(piece.state, Elite::ShipStateBit::Exploding))
            {
              continue; // a cloud's byte 0 is its size, not a line count
            }
            const std::uint8_t allowed = Elite::BlueprintOf(Elite::TypeOf(type))->heapBytes;
            if (port.universe.heap.Read(piece.heap) > allowed)
            {
              failure = where + L": slot " + std::to_wstring(slot) + L" has more on its heap than its blueprint allows";
              return;
            }
          }

          const auto screen = port.universe.canvas.Screen();
          if (frames == 1u)
          {
            cells.assign(screen.begin() + Elite::Canvas::SCREEN_CELLS, screen.end());
            return;
          }

          for (std::size_t offset = 0; offset < cells.size(); ++offset)
          {
            const std::size_t at = Elite::Canvas::SCREEN_CELLS + offset;
            const bool firstBlock = at < Elite::Canvas::DASHBOARD_CELLS;
            const std::size_t column = offset % Elite::Canvas::CELL_COLUMNS;
            const bool spaceView = firstBlock &&
                                   offset < static_cast<std::size_t>(Elite::Canvas::CELL_COLUMNS * Elite::Canvas::CELL_ROWS) &&
                                   column >= 4u && column < 36u;
            if (spaceView)
            {
              continue;
            }
            if (cells[offset] != screen[at])
            {
              failure = where + L": screen RAM byte " + std::to_wstring(at) + L" outside the space view changed -- was " +
                        std::to_wstring(cells[offset]) + L", is " + std::to_wstring(screen[at]);
              return;
            }
          }
        }
      };

      Watching watching(*port);
      port->watching = &watching;
      Elite::Die(port->universe, port->Ports());

      Assert::IsTrue(watching.failure.empty(), watching.failure.c_str());
      Assert::AreEqual<std::uint32_t>(Elite::DEATH_FRAMES + 1u, watching.frames, L"every frame of the sequence was shown");
    }
  };

} // namespace GameLogicTests
