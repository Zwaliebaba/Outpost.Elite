#pragma once

#include "pch.h"

#include "FlightUniverse.h"
#include "UniverseImage.h"

#include "Charts.h"
#include "Game.h"
#include "Controls.h"
#include "DockedKeys.h"
#include "Explosion.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "GameLoop.h"
#include "LoaderScreen.h"
#include "Music.h"
#include "PlanetDraw.h"
#include "ShipBlueprint.h"
#include "SoundEffects.h"
#include "Spawn.h"
#include "StartUp.h"
#include "Tactics.h"
#include "TextPrint.h"
#include "VideoState.h"

#include <array>
#include <cstddef>
#include <cstdint>

/*
 * A flight with no window behind it (Design/Modernize.md slice M0-c).
 *
 * `Outpost::FlightSession` answers the seven seams the flight code reaches through, and most of
 * its answers are calls back into `GameLogic` -- `DrawPlanetOrSun` draws the planet, `SpawnAhead`
 * is `FRS1`. Only a handful reach the platform: the keyboard, the
 * SID and the raster mode. This is the same object with the platform half replaced by data the
 * script owns: the keys held this frame are an array the test fills, the sound goes into the
 * game's own buffer and log, and the raster mode is remembered. Every game-half answer is the
 * routine the executable calls, with the same arguments, so that a flight through this port is
 * the flight the app would run -- which is the property the replay hash pins.
 *
 * IT STEPS AN `Elite::Game` SINCE 2026-09-07, and that is what makes the replay digest mean
 * something. `Main.cpp`'s `Advance` and this port's `Step` each spelled out `M%`, `MLOOP`'s head,
 * the spawner, part 5's tail and the keyboard scan in that order; M3-c moved the executable's copy
 * into `Elite::Game::Step` and left this one, so the digest was measuring a TRANSCRIPTION of the
 * loop rather than the loop (ADR-007 §5). It holds the object now, so the record covers the thing
 * M4-d rewrites -- and taking it found two defects in this fixture, both in §8.
 */
namespace GameLogicTests
{

  class FlightPort final : public Elite::SpawnChildEffects,
                           public Elite::ShipDrawEffects,
                           public Elite::ControlEffects,
                           public Elite::Presenter,
                           public Elite::Keyboard
  {
  public:
    /// 6502: what `CIRCLE` would have left in `STP` -- a launch reads it (§6.95), so the port
    /// starts as `FlightSession` does.
    static constexpr std::uint8_t LAST_CIRCLE_STEP = 4;

    /*
     * `NON_STEERING_KEYS` AND `RDKEY_SPRITE_MASK` WERE HERE AND ARE NOT ANY MORE (M3-b-3d).
     *
     * They were the second copy of them -- `FlightSession` in the app had the first -- because
     * `RDKEY` was a seam and every implementation of it had to repeat the whole routine to answer
     * one question. `Elite::ScanKeyboard` is the routine now and this port answers that question.
     */

    /*
     * The seven seams a flight reaches, and `Elite::Game` builds the four text members of `Ports`
     * over the universe exactly as `Outpost::App` does -- which is the point: a flight through this
     * port is the flight the app would run, and it is now the same OBJECT running it.
     *
     * `StartUpEffects` and `CommanderStore` are the null port's: a scripted flight shows no title
     * screen and writes no commander file.
     */
    FlightPort()
      : game(universe, *this, *this, sidLog, universe.unused, *this, *this, universe.unused, *this)
    {
      // What `FlightSession`'s constructor and the cold start do before a launch can happen.
      universe.heaps.stp = LAST_CIRCLE_STEP;
      universe.flight.blueprint = Elite::BlueprintOf(Elite::ShipType::CobraMk3);
      universe.bubble.stationType = Elite::ShipType::Station;
      universe.LendSunHeap();
      Elite::SetUpLoaderScreen(universe.canvas); // 6502: the loader's palette, without which the screen stays black
    }

    FlightPort(const FlightPort&) = delete;
    FlightPort& operator=(const FlightPort&) = delete;

    // ---- the universe and the loop over it ----------------------------------------------------------

    /*
     * The universe, and it is all of them now: the controls, the keys, the burst, the line heap,
     * the clipper's flag, the projection and the axes were eight members here because `FlightLoop`
     * held references to them. `Elite::Universe` owns every one since M3-a, so `universe.keys` is
     * the byte the app's is.
     */
    Universe universe;

    /// 6502: the sound variables, the music player and the SID they write -- the game's own
    /// objects, so that a flight makes the same register writes it would make in the app.
    /// 6502: sound_variables -- the universe's since M3-b-2a, and this is the name the
    /// interrupt tick and the replay hash already used.
    Elite::SoundBuffer& sound = universe.sound;
    Elite::MusicPlayer& music = universe.music;
    Elite::SidWriteLog sidLog;

    /*
     * 6502: QQ12 -- and it is the UNIVERSE'S byte, not a second one beside it.
     *
     * It was `std::uint8_t docked = 0xFF` here until 2026-09-07, which was one byte for the same
     * thing `Universe::dockedFlag` already was: `Launch` cleared this one and `Game::Leave`'s
     * arrival wrote that one, so a replay driven through `Game` would have had the two disagree.
     * A reference rather than a rename because `RESET` and `LAUN` take it by reference and the
     * digest names it.
     */
    std::uint8_t& docked = universe.dockedFlag;

    /// The keyboard as the script holds it: one entry per C64 matrix position, non-zero for held.
    /// `ScanKeyboard` turns it into `keys` the way `RDKEY` fills `KLO`.
    std::array<std::uint8_t, 65> held{};

    // ---- one pass of TT100 ----------------------------------------------------------------------

    /*
     * 6502: TT100 -- the flight frame, then `MLOOP`'s head, the spawner one pass in 256, part 5's
     * tail and the keyboard scan. What `Main.cpp`'s `Advance` does per step, without the pacing
     * and without the window's key queue (no docked key is pressed in a scripted flight).
     */
    [[nodiscard]] Elite::LoopOutcome Step()
    {
      // 6502: `thiskey`, and ZERO IS A KEY -- `TT102` runs every pass, which is how `TT107`'s
      // countdown ticks whether or not anything was pressed (§6.159). A scripted flight presses
      // nothing, and until 2026-09-07 this port did not dispatch the zero either.
      static_cast<void>(game.Step(0u));
      return game.LastOutcome();
    }

    /*
     * The replay digest: the universe image, widened with what the image does not carry and a
     * flight changes -- the pixels, the ship line heap and the flight controls. The image leaves
     * them out because the oracle compares them by other means (`CompareScreens`, the heap
     * comparisons in `FlightLoopTests`); a replay has no oracle and wants all of it. Every part is
     * a byte layout ADR-002 fixes, so the digest survives the data-model slices the same way the
     * image does.
     */
    [[nodiscard]] std::uint64_t Digest() const
    {
      std::uint64_t digest = Hash(universe);
      digest = FoldBytes(digest, universe.canvas.Screen());

      std::array<std::uint8_t, Elite::LineHeap::SIZE> arena{};
      for (std::size_t offset = 0; offset < arena.size(); ++offset)
      {
        arena[offset] = universe.heap.Read(Elite::HeapOffset::FromAddress(static_cast<std::uint16_t>(Elite::LineHeap::BASE + offset)));
      }
      digest = FoldBytes(digest, arena);

      const std::array<std::uint8_t, 6> rest = {universe.control.roll,             universe.control.pitch,
                                                universe.control.dockingComputer,  universe.status.hyperspaceCounter,
                                                universe.status.ecmOurs,           docked};
      return FoldBytes(digest, rest);
    }

    // ---- Elite::SpawnChildEffects ------------------------------------------------------------------

    [[nodiscard]] bool SpawnChild(std::uint8_t _aiFlag, Elite::ShipType _type) override
    {
      return Elite::SpawnChildShip(universe.bubble, universe.work, universe.rng, universe.flight.slot, universe.flight.type, _aiFlag,
                                   _type, universe.flight.blueprint)
        .created;
    }

    // ---- Elite::ShipDrawEffects -----------------------------------------------------------------

    void DrawPlanetOrSun() override
    {
      Elite::DrawPlanetOrSun(universe.canvas, universe.heaps, universe.geometry, universe.math, universe.clip, universe.rng,
                             universe.work, universe.projection, universe.flight.type);
    }
    void DrawExplosion() override
    {
      Elite::DrawExplosionCloud(universe.canvas, universe.math, universe.rng, universe.work, universe.heap, universe.geometry,
                                universe.bubble, universe.video, universe.memoryMap);
    }

    // ---- Elite::Keyboard ------------------------------------------------------------------------

    /*
     * 6502: the matrix walk's `LDA &DC01` for one row, from `held` rather than from a window.
     *
     * IT WAS THE WHOLE OF `RDKEY` UNTIL M3-b-3d and is one line of it now. The `SETL1` bracket, the
     * sprite mask, `ZEKTRAN`, the countdown that leaves `thiskey` holding the lowest-numbered key
     * and the `QQ11` tail are all `Elite::ScanKeyboard`'s, which is the library's and is compared
     * as such -- so this port and the app's cannot drift apart on any of them, which is what two
     * transcriptions of the same routine were always going to do.
     */
    [[nodiscard]] bool Held(std::size_t _key) override
    {
      return _key < held.size() && held[_key] != 0u;
    }

    /// 6502: TT217 and FLKB -- the docked half's, which nothing in a flight reaches.
    [[nodiscard]] std::uint8_t NextKey() override
    {
      return 0;
    }
    void Flush() override {}

    /*
     * 6502: the display, which this port does not have -- so the calls are FORWARDED or dropped.
     *
     * `Presenter` is in `Ports` since M3-b-3c, so there is no null pointer to pass any more and a
     * suite that wants to count presents attaches a recorder here. An unattached port shows
     * nothing, which is what every oracle comparison wants: the 6502 has no present either, and
     * the two sides must agree on PIXELS rather than on time.
     */
    Elite::Presenter* watching = nullptr;

    void WaitFrames(std::uint8_t _frames) override
    {
      if (watching != nullptr)
      {
        watching->WaitFrames(_frames);
      }
    }
    void Present() override
    {
      if (watching != nullptr)
      {
        watching->Present();
      }
    }
    void HoldFlightFrame(std::uint8_t _ships) override
    {
      if (watching != nullptr)
      {
        watching->HoldFlightFrame(_ships);
      }
    }
    void HoldTitleFrame(std::uint8_t _distance) override
    {
      if (watching != nullptr)
      {
        watching->HoldTitleFrame(_distance);
      }
    }

    // ---- Elite::ControlEffects's docking computer -------------------------------------------------

    void RunDockingComputer(Elite::Ship& _work) override
    {
      static_cast<void>(_work);
      static_cast<void>(Elite::RunDockingComputer(universe, game.PortsOf(), 0u));
    }
    // `ClearBottomRows` WAS ANSWERED HERE AND IS NOT ANY MORE (M3-b-3b): `CLYNS` is
    // `Elite::ClearMessageRows`, which `MLOOP`'s head calls itself when a message's countdown ends.

    /*
     * `SightEffects` AND `ExplosionEffects` WERE ANSWERED HERE AND ARE NOT ANY MORE (M3-b-3a).
     *
     * Six overrides, and five of them were already one line into `Universe::video`; the sixth kept
     * `SETL1`'s byte in this object where nothing could read it. Both are library state now.
     */

    /*
     * The game, LAST because it binds every one of the above, and the whole of what this port is
     * for since 2026-09-07: it builds `Ports` and the text chain over the universe exactly as
     * `Outpost::App` does, and it is what `Step` steps.
     */
    Elite::Game game;

    /// The seams as `Ports`, for the replay's own calls into `RESET` and `LAUN` -- the same struct
    /// the game steps through, not a second one built beside it.
    [[nodiscard]] Elite::Ports& Ports() noexcept
    {
      return game.PortsOf();
    }
  };

} // namespace GameLogicTests
