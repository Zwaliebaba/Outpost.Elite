#pragma once

#include "pch.h"

#include "FlightUniverse.h"
#include "UniverseImage.h"

#include "Charts.h"
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
 * `Outpost::FlightSession` answers the eight seams the flight code reaches through, and most of
 * its answers are calls back into `GameLogic` -- `RunTactics` runs the ship AI, `DrawPlanetOrSun`
 * draws the planet, `SpawnAhead` is `FRS1`. Only a handful reach the platform: the keyboard, the
 * SID and the raster mode. This is the same object with the platform half replaced by data the
 * script owns: the keys held this frame are an array the test fills, the sound goes into the
 * game's own buffer and log, and the raster mode is remembered. Every game-half answer is the
 * routine the executable calls, with the same arguments, so that a flight through this port is
 * the flight the app would run -- which is the property the replay hash pins.
 *
 * It is the second copy of `FlightSession`'s wiring, and the plan says so (P6): slice M3-b replaces
 * both with direct calls, and this port is what makes M3-b measurable before it lands.
 */
namespace GameLogicTests
{

  class FlightPort final : public Elite::FlightLoopEffects,
                           public Elite::ShipEffects,
                           public Elite::ShipDrawEffects,
                           public Elite::ControlEffects,
                           public Elite::SightEffects,
                           public Elite::ExplosionEffects,
                           public Elite::ViewEffects,
                           public Elite::ChartShapes,
                           public Elite::ChartEffects
  {
  public:
    /// 6502: what `CIRCLE` would have left in `STP` -- a launch reads it (§6.95), so the port
    /// starts as `FlightSession` does.
    static constexpr std::uint8_t LAST_CIRCLE_STEP = 4;

    /// 6502: the keys `DOKEY` ignores on every screen but the space view; `RDKEY`'s answer to
    /// `QQ11 <> 0`, copied from `FlightSession::ScanMatrix`.
    static constexpr std::size_t NON_STEERING_KEYS[] = {
      Elite::KEY_ENERGY_BOMB, Elite::KEY_ESCAPE_POD, Elite::KEY_ARM_MISSILE,      Elite::KEY_UNARM_MISSILE,  Elite::KEY_FIRE_MISSILE,
      Elite::KEY_ECM,         Elite::KEY_WARP,       Elite::KEY_DOCKING_COMPUTER, Elite::KEY_CANCEL_DOCKING,
    };

    /// 6502: RDKEY's `AND #%11111101` -- sprite 1 off while the matrix is scanned.
    static constexpr std::uint8_t RDKEY_SPRITE_MASK = 0b11111101;

    FlightPort()
      : screen{universe.canvas,   universe.draw,     universe.math,      universe.geometry, universe.dust,    universe.heaps,
               universe.bubble,   universe.work,     universe.screen,    universe.text,     universe.characters.state,
               universe.printer,  universe.characters, universe.message, universe.flight,   universe.status,  universe.compass,
               universe.rng,      universe.commander, universe.trumbles, universe.video,    *this,            *this,
               universe.view,     universe.spaceView, universe.explosions, universe.techLevel},
        loop{screen, keys, control, options, burst, heap, clip, projection, axes, *this, *this, *this}
    {
      // What `FlightSession`'s constructor and the cold start do before a launch can happen.
      universe.heaps.stp = LAST_CIRCLE_STEP;
      universe.flight.blueprint = Elite::BlueprintOf(Elite::ShipType::CobraMk3);
      universe.bubble.stationType = Elite::ShipType::Station;
      universe.LendSunHeap(heap);
      Elite::SetUpLoaderScreen(universe.canvas); // 6502: the loader's palette, without which the screen stays black
    }

    FlightPort(const FlightPort&) = delete;
    FlightPort& operator=(const FlightPort&) = delete;

    // ---- the universe and the loop over it ----------------------------------------------------------

    Universe universe;

    Elite::ControlState control;
    Elite::ControlOptions options;
    Elite::KeyLogger keys{};
    Elite::LaserBurst burst{};
    Elite::LineHeap heap;
    Elite::ClipState clip;
    Elite::Projection projection;
    Elite::K3Block axes{};

    /// 6502: the sound variables, the music player and the SID they write -- the game's own
    /// objects, so that a flight makes the same register writes it would make in the app.
    Elite::SoundBuffer sound;
    Elite::MusicPlayer music;
    Elite::SidWriteLog sidLog;

    /// 6502: QQ2, QQ28, tek and gov -- what the spawner reads about the system you are in.
    Elite::CurrentSystem current;

    std::uint8_t docked = 0xFFu;   ///< 6502: QQ12
    std::uint8_t rasterMode = 0;   ///< 6502: L1M -- what `SETL1` last wrote
    std::uint32_t palettes = 0;    ///< `DOVDU19` calls, counted because the port has no VIC to write

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
      const Elite::LoopOutcome outcome = Elite::MainFlightLoop(loop); // 6502: JSR M%
      if (outcome != Elite::LoopOutcome::Continued)
      {
        return outcome;
      }

      if (Elite::RunLoopHead(loop, *this) == Elite::LoopHead::Spawn)
      {
        Elite::RunSpawning(universe.bubble, universe.work, universe.rng, universe.commander, current, universe.status,
                           universe.explosions, universe.flight.blueprint, false);
      }
      static_cast<void>(Elite::RunLoopTail(loop, universe.commander, options.authorNames, false));
      static_cast<void>(Elite::ScanFlightControls(loop, *this, universe.view)); // 6502: JSR TT17
      return Elite::LoopOutcome::Continued;
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
        arena[offset] = heap.Read(static_cast<std::uint16_t>(Elite::LineHeap::BASE + offset));
      }
      digest = FoldBytes(digest, arena);

      const std::array<std::uint8_t, 6> rest = {control.roll,         control.pitch, control.dockingComputer,
                                                universe.status.hyperspaceCounter, universe.status.ecmOurs, docked};
      return FoldBytes(digest, rest);
    }

    // ---- Elite::FlightLoopEffects, and Elite::DashboardEffects under it -------------------------

    bool PlaySound(std::uint8_t _effect, bool _carryIn) override
    {
      return Elite::PlaySoundEffect(sound, _effect, _carryIn);
    }
    bool PlaySoundPitched(std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t _frequency) override
    {
      return Elite::PlaySoundEffectPitched(sound, _effect, _sustain, _frequency, false);
    }
    void StopSound(std::uint8_t _effect) override
    {
      Elite::StopSoundEffect(sound, _effect); // 6502: NOISEOFF
    }
    void StartDockingMusic() override
    {
      Elite::StartDockingMusic(music, sidLog);
    }
    void StopDockingMusic() override
    {
      Elite::StopDockingMusic(music, universe.status.titleReset, sound, sidLog);
    }
    [[nodiscard]] bool SpawnAhead(Elite::ShipType _type) override
    {
      return Elite::SpawnShipAhead(universe.bubble, universe.work, _type, universe.flight.delta, universe.bubble.missileTarget,
                                   universe.flight.blueprint)
        .created;
    }
    void Anger(std::uint8_t _slot, Elite::ShipType _type) override
    {
      Elite::Anger(universe.bubble, universe.flight, _slot, _type);
    }
    [[nodiscard]] bool SpawnChild(std::uint8_t _aiFlag, Elite::ShipType _type) override
    {
      return Elite::SpawnChildShip(universe.bubble, universe.work, universe.rng, universe.math, universe.flight.slot, universe.flight.type,
                                   _aiFlag, _type, universe.flight.blueprint)
        .created;
    }

    // ---- Elite::ShipEffects and Elite::ShipDrawEffects ------------------------------------------

    [[nodiscard]] bool RunTactics(Elite::Ship& _work) override
    {
      static_cast<void>(_work);
      return Elite::RunTactics(loop, universe.flight.slot);
    }
    void DrawPlanetOrSun() override
    {
      Elite::DrawPlanetOrSun(universe.canvas, universe.heaps, universe.draw, universe.geometry, universe.math, clip, universe.rng,
                             universe.work, projection, universe.flight.type);
    }
    void DrawExplosion() override
    {
      Elite::DrawExplosionCloud(universe.canvas, universe.draw, universe.math, universe.rng, universe.work, heap, universe.geometry,
                                universe.bubble, *this);
    }
    void SeedExplosionCloud(Elite::LineHeap&, std::uint16_t, std::uint8_t) override {}

    // ---- Elite::ControlEffects ------------------------------------------------------------------

    /// 6502: RDKEY, from `held` rather than from a window -- `FlightSession::ScanMatrix` with the
    /// matrix replaced by the script's array and the same two masks after it.
    void ScanKeyboard() override
    {
      rasterMode = 0b101;                                         // 6502: LDA #%101 / JSR SETL1
      Elite::ApplyMaskSprites(universe.video, RDKEY_SPRITE_MASK); // 6502: AND #%11111101 -- sprite 1 off
      keys.fill(0u);                                              // 6502: JSR ZEKTRAN
      for (std::size_t key = keys.size(); key-- > 0u;)
      {
        if (held[key] != 0u)
        {
          keys[key] = 0xFFu; // 6502: DEC KEYLOOK,X, on a byte that has just been zeroed
        }
      }
      if (universe.view != 0u)
      {
        for (const std::size_t index : NON_STEERING_KEYS)
        {
          keys[index] = 0u;
        }
      }
      if (Elite::IsChartView(universe.view))
      {
        for (const std::size_t index : {Elite::KEY_ROLL_LEFT, Elite::KEY_ROLL_RIGHT, Elite::KEY_PITCH_UP, Elite::KEY_PITCH_DOWN})
        {
          keys[index] = 0u;
        }
      }
      rasterMode = 0b100; // 6502: LDA #%100 / JSR SETL1
    }

    // ---- Elite::ChartShapes and Elite::ChartEffects ---------------------------------------------

    void DrawRangeCircle(const Elite::RangeCircle& _circle) override
    {
      universe.heaps.lsp = 1u;
      universe.heaps.stp = _circle.step;
      universe.math.k[0] = _circle.radius;
      const Elite::Projection centre{_circle.x, 0u, _circle.y, 0u};
      Elite::DrawBall(universe.canvas, universe.heaps, universe.draw, universe.geometry, universe.math, clip, centre, false);
    }
    void DrawSystemDisc(std::uint8_t _x, std::uint8_t _y, std::uint8_t _radius) override
    {
      Elite::ClearSunHeap(universe.heaps);
      universe.math.k[0] = _radius;
      const Elite::Projection centre{_x, 0u, _y, 0u};
      Elite::DrawSun(universe.canvas, universe.heaps, universe.draw, universe.math, universe.rng, centre);
      Elite::ClearSunHeap(universe.heaps);
    }
    void RunDockingComputer(Elite::Ship& _work) override
    {
      static_cast<void>(_work);
      static_cast<void>(Elite::RunDockingComputer(loop, 0u));
    }
    /// 6502: CLYNS, which `MLOOP`'s head runs when a message's countdown expires -- what
    /// `GameShell::ClearBottomRows` does.
    void ClearBottomRows() override
    {
      Elite::ClearMessageRows(universe.canvas, universe.printer, universe.text, universe.characters.state, universe.message);
    }

    // ---- Elite::SightEffects, Elite::ExplosionEffects and Elite::ViewEffects --------------------

    void SetRasterMode(std::uint8_t _mode) override
    {
      rasterMode = _mode;
    }
    void SetSightColour(std::uint8_t _colour) override
    {
      Elite::ApplySightColour(universe.video, _colour);
    }
    void SetSpritesEnabled(std::uint8_t _mask) override
    {
      Elite::ApplySpritesEnabled(universe.video, _mask);
    }
    void SetSpriteExpansion(std::uint8_t _mask) override
    {
      Elite::ApplySpriteExpansion(universe.video, _mask);
    }
    void ShowExplosionSprite(std::uint16_t _x, std::uint8_t _y) override
    {
      Elite::ApplyExplosionSprite(universe.video, _x, _y);
    }
    void MaskSprites(std::uint8_t _mask) override
    {
      Elite::ApplyMaskSprites(universe.video, _mask);
    }
    void SetPalette(std::uint8_t) override
    {
      ++palettes;
    }

    // ---- the two aggregates, last because every reference in them is bound at construction -----

    Elite::FlightScreen screen;
    Elite::FlightLoop loop;
  };

} // namespace GameLogicTests
