#pragma once

#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "FlightLoop.h"
#include "Commander.h"
#include "Controls.h"
#include "Dashboard.h"
#include "ExtendedTokens.h"
#include "LookupTables.h"
#include "PlanetDraw.h"
#include "Rng.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "LineHeap.h"
#include "ShipSlot.h"
#include "Spawn.h"
#include "Stardust.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "ViewChange.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

/*
 * The port's whole flight world, and the oracle's memory beside it.
 *
 * Shared by `ViewChangeTests` and `FlightLoopTests` because both compare routines that reach all
 * of it: `TT66` clears everything drawn on the screen, and the flight loop drives everything that
 * draws. Building it twice would be the same eighteen arguments in two disguises.
 */
namespace GameLogicTests
{

  using Elite::Testing::Cpu6502;
  using Elite::Testing::OracleImage;
  using Microsoft::VisualStudio::CppUnitTestFramework::Assert;

  inline bool OracleMissing()
  {
    const OracleImage& oracle = OracleImage::Instance();
    if (oracle.Available())
    {
      return false;
    }
    Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
    return true;
  }

  inline std::wstring WidenText(const std::string& _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

  inline std::uint16_t ScreenBase(const OracleImage& _oracle)
  {
    const Cpu6502 image = _oracle.Fresh();
    return static_cast<std::uint16_t>((image.memory[_oracle.Label("ylookupl")] | (image.memory[_oracle.Label("ylookuph")] << 8)) - 0x20);
  }

  inline void FillScreens(Cpu6502& _cpu, Elite::Canvas& _canvas, std::uint16_t _base, std::uint8_t _marker)
  {
    std::memset(&_cpu.memory[_base], _marker, Elite::Canvas::SCREEN_SIZE);
    for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
    {
      _canvas.Write(offset, _marker);
    }
  }

  inline std::uint32_t CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas, std::uint8_t _marker,
                                      const std::wstring& _context)
  {
    const std::span<const std::uint8_t> ours = _canvas.Screen();
    std::uint32_t touched = 0;

    for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
    {
      const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
      if (expected != ours[offset])
      {
        Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset) + L" -- game has " + std::to_wstring(expected) +
                      L", port has " + std::to_wstring(ours[offset]))
                       .c_str());
      }
      touched += (ours[offset] != _marker) ? 1u : 0u;
    }

    return touched;
  }

  struct RecordingSight final : Elite::SightEffects
  {
    std::vector<std::uint8_t> modes;
    std::vector<std::uint8_t> masks;
    std::vector<std::uint8_t> colours;

    void SetRasterMode(std::uint8_t _mode) override
    {
      modes.push_back(_mode);
    }
    void SetSightColour(std::uint8_t _colour) override
    {
      colours.push_back(_colour);
    }
    void SetSpritesEnabled(std::uint8_t _mask) override
    {
      masks.push_back(_mask);
    }
    void MaskSprites(std::uint8_t _mask) override
    {
      maskedWith.push_back(_mask);
    }

    std::vector<std::uint8_t> maskedWith;
  };

  struct RecordingView final : Elite::ViewEffects
  {
    std::vector<std::uint8_t> palettes;
    std::vector<std::uint8_t> sounds;

    /*
     * The carry each `PlaySound` was handed, parallel to `sounds` (§6.99). Both seams that reach
     * `NOISE` push here, because the 6502 has one routine and the port has two interfaces onto it.
     *
     * `std::uint8_t` AND NOT `bool`, which is not a style choice: `std::vector<bool>` is bit-packed
     * and its `operator[]` hands back a PROXY, and MSVC's `Assert::AreEqual` static-asserts that it
     * has no `ToString` for one. g++ has no such assertion, so a `vector<bool>` here compiles on the
     * Ubuntu leg and fails the Windows one -- which is what it did (§6.116).
     */
    std::vector<std::uint8_t> soundCarries;

    void SetPalette(std::uint8_t _colour) override
    {
      palettes.push_back(_colour);
    }
    bool PlaySound(std::uint8_t _effect, bool _carryIn) override
    {
      sounds.push_back(_effect);
      soundCarries.push_back(_carryIn ? 1u : 0u);
      return true;
    }
  };

  /*
   * The port's whole flight world, and the `FlightScreen` over it.
   *
   * One object because `TT66` genuinely reaches all of it -- the line heaps, the token printer, the
   * message counters, the laser, the stardust and the dashboard -- and building it twice per test
   * method would be the same eighteen arguments in a different disguise.
   */
  /*
   * 6502: the three seams `NOISE`, `NOISE2` and `NOISEOFF` sit behind, recorded rather than played.
   *
   * Separate from `RecordingView`, which answers `LOOK1`'s and `WARP`'s single `PlaySound`: this is
   * the whole sound interface, and `HYPNOISE` is the first routine in the port that needs the
   * pitched entry as well as the plain one.
   */
  struct RecordingDashboard final : Elite::DashboardEffects
  {
    struct Pitched
    {
      std::uint8_t effect;
      std::uint8_t sustain;
      std::uint8_t frequency;
    };

    std::vector<std::uint8_t> sounds;
    std::vector<std::uint8_t> carries;
    std::vector<Pitched> pitched;
    std::vector<std::uint8_t> stopped;

    bool PlaySound(std::uint8_t _effect, bool _carryIn) override
    {
      sounds.push_back(_effect);
      carries.push_back(_carryIn ? 1u : 0u);
      return true;
    }
    bool PlaySoundPitched(std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t _frequency) override
    {
      pitched.push_back({_effect, _sustain, _frequency});
      return true;
    }
    void StopSound(std::uint8_t _effect) override
    {
      stopped.push_back(_effect);
    }
  };

  struct World
  {
    RecordingView effects; ///< first, because the character printer's bell records into its list

    Elite::Canvas canvas;
    Elite::DrawWorkspace draw;
    Elite::MathWorkspace math;
    Elite::GeometryWorkspace geometry;

    Elite::Stardust dust;
    Elite::PlanetSunState heaps;
    Elite::Bubble bubble;
    Elite::ShipBlock work{};

    Elite::ScreenState screen;
    Elite::TextState text;

    /*
     * The real character printer, drawing into the canvas -- `CHPR` is NOT trapped on the oracle's
     * side for this slice. `TT66` prints the view's name and `ee3` prints the countdown, and both
     * belong on the screen the routine has just cleared; trapping them would compare a character
     * stream and leave the pixels they produce out of the whole-canvas compare that everything else
     * here is checked by.
     */
    /*
     * `CHPR`'s two seams, wired to the sound list.
     *
     * Character 7 rings the bell, which is `JSR BEEP` and so `NOISE` -- and the flight loop prints
     * a token that contains one, so a comparison that let the bell fall on the floor would count
     * one sound fewer than the game on every energy warning.
     */
    struct Chars final : Elite::TextEffects
    {
      std::vector<std::uint8_t>& sounds;
      std::vector<std::uint8_t>& carries;
      std::uint32_t cleared = 0;

      Chars(std::vector<std::uint8_t>& _sounds, std::vector<std::uint8_t>& _carries) noexcept
        : sounds(_sounds),
          carries(_carries)
      {
      }

      void Beep() override
      {
        sounds.push_back(SOUND_BEEP_EFFECT);
        carries.push_back(0u); // `BEEP` is `LDY #sfxbeep / JMP NOISE`: the carry is CHPR's (§6.118)
      }
      void ClearScreen() override
      {
        ++cleared;
      }
    };

    /// 6502: sfxbeep -- what `BEEP` asks `NOISE` for.
    static constexpr std::uint8_t SOUND_BEEP_EFFECT = 5;

    Chars chars{effects.sounds, effects.soundCarries};
    Elite::TextPrinter glyphs{canvas, text, &chars};
    Elite::CharacterPrinter characters{glyphs};
    Elite::TokenPrinter printer{characters};
    Elite::MessageState message;

    /*
     * 6502: DETOK's seam, and it RECORDS rather than acts.
     *
     * `TITLE` prints three extended tokens and the port has no answer for a control code outside
     * the shell, so this exists to say out loud whether any of them contains one. The tests assert
     * the list is empty; if a token ever grows a code, the assertion is what says so rather than a
     * screen quietly diverging from the game's.
     */
    struct Codes final : Elite::ControlCodes
    {
      std::vector<std::uint8_t> ran;
      void Run(std::uint8_t _code) override
      {
        ran.push_back(_code);
      }
    };

    Codes codes;

    Elite::FlightState flight;
    Elite::FlightStatus status;
    Elite::Compass compass{0xC3u, 0x9Cu, Elite::COMPASS_AHEAD};
    Elite::Rng rng;

    /// Declared after `rng` because it binds one, and the order here is the construction order.
    Elite::ExtendedTokenPrinter extendedPrinter{characters, printer, rng, &codes};

    Elite::CommanderBlock commander;
    std::uint8_t trumbles = 0;

    RecordingSight sight;
    RecordingDashboard dashboard;

    std::uint8_t view = 0;
    std::uint8_t spaceView = 0;
    std::uint8_t explosions = 0;
    std::uint8_t techLevel = 0; ///< 6502: tek -- part 14's station reads it

    /// 6502: QQ14 -- kept only so the fixtures can name it; the byte the port reads is the
    /// commander block's, because part 15's fuel scooping writes it and a copy would drift.
    std::uint8_t fuel = 0;

    World()
    {
      printer.SetCursor(&text);
    }

    /*
     * 6502: LSO -- the sun's heap, which `NWSPS` hands to the SPACE STATION (§6.112).
     *
     * The `LineHeap` belongs to whoever is running a frame rather than to the world, so this is
     * the world lending its sun window to one. Every fixture that draws a station has to call it,
     * for the same reason `FlightSession` does: without it the station's lines are written out of
     * the arena and dropped, and the comparison against `LSO` compares two sets of nothing.
     */
    void LendSunHeap(Elite::LineHeap& _heap) noexcept
    {
      _heap.AttachSunHeap(Elite::SUN_HEAP_ADDRESS, heaps.sun);
    }

    [[nodiscard]] Elite::FlightScreen Screen() noexcept
    {
      return Elite::FlightScreen{
        canvas,  draw,   math,   geometry, dust, heaps,     bubble,   work,  screen,  text, characters.state, printer,    characters,
        message, flight, status, compass,  rng,  commander, trumbles, sight, effects, view, spaceView,        explosions, techLevel};
    }
  };

  /// A world that is not all zeroes, so "cleared" and "left alone" are different answers everywhere.
  /*
   * What `MJP` and `Ghy` reach outside the world: sounds, the trumbles and the AI, none of which
   * this slice decides. Counted rather than ignored, because `LL164` makes a noise and a
   * comparison that dropped it would agree with a port that had lost the hyperspace sound.
   */
  struct LoopRecording final : Elite::FlightLoopEffects, Elite::ShipEffects, Elite::ShipDrawEffects
  {
    std::vector<std::uint8_t> sounds;

    bool PlaySound(std::uint8_t _effect, bool) override
    {
      sounds.push_back(_effect);
      return true;
    }
    /// The sustain is recorded too, because `MLOOP`'s Trumble squeak and its BURN are the same
    /// effect at two sustains (&80 and &F1), and a list of effect numbers cannot tell them apart.
    std::vector<std::uint8_t> sustains;

    bool PlaySoundPitched(std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t) override
    {
      sounds.push_back(_effect);
      sustains.push_back(_sustain);
      return true;
    }
    void StopSound(std::uint8_t) override {}
    void MoveTrumbles() override {}
    void StartDockingMusic() override {}
    void StopDockingMusic() override {}
    bool SpawnAhead(std::uint8_t) override
    {
      return false;
    }
    void Anger(std::uint8_t, std::uint8_t) override {}
    bool SpawnChild(std::uint8_t, std::uint8_t) override
    {
      return true;
    }
    bool RunTactics(Elite::ShipBlock&) override
    {
      return true;
    }
    void DrawPlanetOrSun() override {}
    void DrawExplosion() override {}
    void SeedExplosionCloud(Elite::LineHeap&, std::uint16_t, std::uint16_t) override {}
  };

  /// The port's side of a case: the whole flight world plus the pieces `FlightLoop` needs. Shared,
  /// because three suites now build the same twelve-member aggregate to call one routine.
  struct LoopWorld
  {
    World world;
    Elite::ControlState control;
    Elite::ControlOptions options;
    Elite::KeyLogger keys{};
    Elite::LaserBurst burst{};
    Elite::LineHeap heap;
    Elite::ClipState clip;
    Elite::Projection projection;
    Elite::K3Block axes{};
    LoopRecording effects;
  };

  inline void Seed(World& _world, std::uint32_t _seed)
  {
    std::uint32_t state = _seed * 0x9E3779B9u + 0x85EBCA6Bu;
    auto next = [&]()
    {
      state = state * 1103515245u + 12345u;
      return static_cast<std::uint8_t>(state >> 17);
    };

    const std::uint8_t TYPES[] = {3u, 5u, 2u};
    for (std::size_t slot = 0; slot < 3u; ++slot)
    {
      _world.bubble.slots[slot] = TYPES[slot];
    }
    _world.bubble.counts[Elite::SHIP_TYPE_STATION] = 1u;
    _world.bubble.junk = 1u;

    /*
     * 6502: XX21+2*SST-2 -- the pointer table's station entry, which the game has held since boot.
     *
     * Zero is not a value the machine can have here (§6.95's rule, second byte): `NWSHP` refuses a
     * type whose entry is zero, so an unseeded bubble would silently stop creating stations. The
     * Coriolis is what `BEGIN` leaves and what every system below tech level ten keeps.
     */
    _world.bubble.stationBlueprint = Elite::BlueprintAddress(Elite::SHIP_TYPE_STATION);

    _world.techLevel = 7u; // 6502: tek -- below the Dodo's threshold, so the seeded state is stable

    for (std::size_t slot = 0; slot < _world.bubble.blocks.size(); ++slot)
    {
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        _world.bubble.blocks[slot][byte] = (byte == 31u) ? 0xFFu : next();
      }
    }
    for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
    {
      _world.work[byte] = next();
    }

    for (std::size_t index = 0; index < _world.dust.x.size(); ++index)
    {
      _world.dust.x[index] = next();
      _world.dust.xLow[index] = next();
      _world.dust.y[index] = next();
      _world.dust.yLow[index] = next();
      _world.dust.z[index] = next();
      _world.dust.zLow[index] = next();
    }
    _world.dust.count = 12u; // 6502: NOST

    for (std::size_t index = 0; index < _world.heaps.sun.size(); ++index)
    {
      _world.heaps.sun[index] = next();
    }
    for (std::size_t index = 0; index < _world.heaps.ball.size(); ++index)
    {
      _world.heaps.ball[index] = next();
    }
    _world.heaps.lsp = 0x37u;

    _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers)] = Elite::LASER_PULSE;
    _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + 1u] = 0u;
    _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + 2u] = Elite::LASER_BEAM;
    _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + 3u] = Elite::LASER_MILITARY;
    _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles)] = 0x40u;
    _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles) + 1u] = 0x21u;
    _world.trumbles = 0x5Au;

    _world.rng.SetState({0x11u, 0x22u, 0x33u, 0x44u});

    _world.text.column = 0x1Fu;
    _world.text.row = 0x0Bu;
    _world.text.cellColour = Elite::TEXT_COLOUR_WHITE;
    _world.printer.SetCaseFlags(0x40u);
    _world.characters.state.lowerCaseBits = 0u;
    _world.characters.state.sentenceStart = 0u;
    _world.characters.state.alwaysLower = 0xFFu;

    _world.message.delay = 0x2Au;
    _world.message.append = 0x3Bu;
    _world.status.viewLaser = 0x4Cu;
    _world.status.energy = 180u;
    _world.status.forwardShield = 90u;
    _world.status.aftShield = 60u;
    _world.status.cabinTemperature = 100u;
    _world.status.laserTemperature = 70u;
    _world.status.altitude = 120u;
    _world.status.damageFlash = 0u;
    _world.status.ecmCountdown = 0u;
    _world.fuel = 40u;
    _world.commander.At(Elite::Field::Fuel) = _world.fuel; // `Mirror` sends the block, not the byte

    _world.flight.delta = 14u;
    _world.flight.alp1 = 5u;
    _world.flight.alp2 = 128u;
    _world.flight.beta = 200u;
    _world.flight.bet1 = 3u;
    _world.flight.mainLoopCounter = 0u;

    _world.screen.colourBank = 0x33u;
    _world.screen.bitmapMode = 0x44u;
    _world.screen.dashboardShown = 0u;
    _world.draw.t2 = 0x88u;
    _world.explosions = 0x66u;
  }

  /// Every label the screen routines touch, looked up once.
  struct Where
  {
    std::uint16_t frin, kPercent, many, inwk, sx, sxl, sy, syl, sz, szl, nostm;
    std::uint16_t lso, lsx2, lsp, xc, yc, qq17, dtw1, dtw2, dtw6, col2;
    std::uint16_t dtw3, dtw4, dtw5, dtw8;
    std::uint16_t dly, de, las2, qq22, viewByte, qq11, mj, junk, ev, rand;
    std::uint16_t abraxas, caravanserai, dflag, comx, comy, comc, t2;
    std::uint16_t delta, alp1, alp2, beta, bet1, energy, fsh, ash, qq14, xx0;
    std::uint16_t cabtmp, gntmp, altit, mcnt, flh, ecma, laser, tribble, tribct;
    std::uint16_t tp, mch, messxc, screen;
    std::uint16_t tek, xx21Station, spasto; ///< 6502: tek, XX21+2*SST-2, and BEGIN's saved copy of it

    explicit Where(const OracleImage& _oracle)
    {
      frin = _oracle.Label("FRIN");
      kPercent = _oracle.Label("K%");
      many = _oracle.Label("MANY");
      inwk = _oracle.Label("INWK");
      sx = _oracle.Label("SX");
      sxl = _oracle.Label("SXL");
      sy = _oracle.Label("SY");
      syl = _oracle.Label("SYL");
      sz = _oracle.Label("SZ");
      szl = _oracle.Label("SZL");
      nostm = _oracle.Label("NOSTM");
      lso = _oracle.Label("LSO");
      lsx2 = _oracle.Label("LSX2");
      lsp = _oracle.Label("LSP");
      xc = _oracle.Label("XC");
      yc = _oracle.Label("YC");
      qq17 = _oracle.Label("QQ17");
      dtw1 = _oracle.Label("DTW1");
      dtw2 = _oracle.Label("DTW2");
      dtw6 = _oracle.Label("DTW6");
      dtw3 = _oracle.Label("DTW3");
      dtw4 = _oracle.Label("DTW4");
      dtw5 = _oracle.Label("DTW5");
      dtw8 = _oracle.Label("DTW8");
      col2 = _oracle.Label("COL2");
      dly = _oracle.Label("DLY");
      de = _oracle.Label("de");
      las2 = _oracle.Label("LAS2");
      qq22 = _oracle.Label("QQ22");
      viewByte = _oracle.Label("VIEW");
      qq11 = _oracle.Label("QQ11");
      mj = _oracle.Label("MJ");
      junk = _oracle.Label("JUNK");
      ev = _oracle.Label("EV");
      rand = _oracle.Label("RAND");
      abraxas = _oracle.Label("abraxas");
      caravanserai = _oracle.Label("caravanserai");
      dflag = _oracle.Label("DFLAG");
      comx = _oracle.Label("COMX");
      comy = _oracle.Label("COMY");
      comc = _oracle.Label("COMC");
      t2 = _oracle.Label("T2");
      delta = _oracle.Label("DELTA");
      alp1 = _oracle.Label("ALP1");
      alp2 = _oracle.Label("ALP2");
      beta = _oracle.Label("BETA");
      bet1 = _oracle.Label("BET1");
      energy = _oracle.Label("ENERGY");
      fsh = _oracle.Label("FSH");
      ash = _oracle.Label("ASH");
      qq14 = _oracle.Label("QQ14");
      xx0 = _oracle.Label("XX0");
      cabtmp = _oracle.Label("CABTMP");
      gntmp = _oracle.Label("GNTMP");
      altit = _oracle.Label("ALTIT");
      mcnt = _oracle.Label("MCNT");
      flh = _oracle.Label("FLH");
      ecma = _oracle.Label("ECMA");
      laser = _oracle.Label("LASER");
      tribble = _oracle.Label("TRIBBLE");
      tribct = _oracle.Label("TRIBCT");
      tp = _oracle.Label("TP");
      mch = _oracle.Label("MCH");
      messxc = _oracle.Label("messXC");
      tek = _oracle.Label("tek");

      /*
       * 6502: XX21+2*SST-2 -- the only two bytes of the pointer table the game writes.
       *
       * Computed from `XX21` and `SST` rather than looked up, because it has no label of its own:
       * the original addresses it as an expression and so does this.
       */
      xx21Station = static_cast<std::uint16_t>(_oracle.Label("XX21") + 2u * Elite::SHIP_TYPE_STATION - 2u);
      spasto = _oracle.Label("spasto");

      screen = ScreenBase(_oracle);
    }
  };

  /// Copy the port's world into the oracle's memory, so both start identical.
  inline void Mirror(const World& _world, Cpu6502& _cpu, const Where& _at)
  {
    auto block = [&](std::uint16_t _base, const std::uint8_t* _from, std::size_t _count)
    {
      for (std::size_t index = 0; index < _count; ++index)
      {
        _cpu.memory[static_cast<std::uint16_t>(_base + index)] = _from[index];
      }
    };

    block(_at.frin, _world.bubble.slots.data(), _world.bubble.slots.size());
    block(_at.many, _world.bubble.counts.data(), _world.bubble.counts.size());
    block(_at.inwk, _world.work.bytes.data(), Elite::SHIP_BLOCK_SIZE);
    for (std::size_t slot = 0; slot < _world.bubble.blocks.size(); ++slot)
    {
      block(static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE), _world.bubble.blocks[slot].bytes.data(),
            Elite::SHIP_BLOCK_SIZE);
    }

    block(_at.sx, _world.dust.x.data(), _world.dust.x.size());
    block(_at.sxl, _world.dust.xLow.data(), _world.dust.xLow.size());
    block(_at.sy, _world.dust.y.data(), _world.dust.y.size());
    block(_at.syl, _world.dust.yLow.data(), _world.dust.yLow.size());
    block(_at.sz, _world.dust.z.data(), _world.dust.z.size());
    block(_at.szl, _world.dust.zLow.data(), _world.dust.zLow.size());
    _cpu.memory[_at.nostm] = _world.dust.count;

    block(_at.lso, _world.heaps.sun.data(), _world.heaps.sun.size());
    block(_at.lsx2, _world.heaps.ball.data(), _world.heaps.ball.size());
    _cpu.memory[_at.lsp] = _world.heaps.lsp;

    /*
     * The WHOLE commander block, because `LASER` and `TRIBBLE` are two fields of one structure and
     * the routines that read the others -- `OUCH` empties a hold slot, `EXNO2` adds to the tally,
     * the flight loop reads `ESCP`, `ECM` and `NOMSL` -- would otherwise be comparing the port's
     * zeroes against whatever the shipped block happens to hold.
     */
    block(_at.tp, _world.commander.bytes.data(), Elite::COMMANDER_BLOCK_SIZE);
    _cpu.memory[_at.tribct] = _world.trumbles;

    const std::array<std::uint8_t, 4> seed = _world.rng.State();
    block(_at.rand, seed.data(), seed.size());

    _cpu.memory[_at.xc] = _world.text.column;
    _cpu.memory[_at.yc] = _world.text.row;
    _cpu.memory[_at.qq17] = _world.printer.CaseFlags();
    _cpu.memory[_at.col2] = _world.text.cellColour;
    _cpu.memory[_at.dtw1] = _world.characters.state.lowerCaseBits;
    _cpu.memory[_at.dtw2] = _world.characters.state.sentenceStart;
    _cpu.memory[_at.dtw6] = _world.characters.state.alwaysLower;

    /*
     * The rest of the extended printer's state, which no screen routine reads but `MESS` does:
     * it turns the justifier into a measuring device (`DTW4` = %11000000, print, read `DTW5`) and
     * a stale byte in either would centre the message in the wrong column. `DTW7` is not in this
     * build -- the Master's literal-character byte has no C64 label.
     */
    _cpu.memory[_at.dtw3] = _world.characters.state.toLineBuffer;
    _cpu.memory[_at.dtw4] = _world.characters.state.justify;
    _cpu.memory[_at.dtw5] = _world.characters.state.bufferLength;
    _cpu.memory[_at.dtw8] = _world.characters.state.caseMask;

    _cpu.memory[_at.dly] = _world.message.delay;
    _cpu.memory[_at.de] = _world.message.append;
    _cpu.memory[_at.mch] = _world.message.token;
    _cpu.memory[_at.messxc] = _world.message.column;
    _cpu.memory[_at.las2] = _world.status.viewLaser;
    _cpu.memory[static_cast<std::uint16_t>(_at.qq22 + 1)] = _world.status.hyperspaceCountdown;
    _cpu.memory[_at.mj] = _world.status.midJump;
    _cpu.memory[_at.junk] = _world.bubble.junk;
    _cpu.memory[_at.ev] = _world.explosions;
    _cpu.memory[_at.viewByte] = _world.spaceView;
    _cpu.memory[_at.qq11] = _world.view;

    _cpu.memory[_at.tek] = _world.techLevel;

    /*
     * 6502: BEGIN's `LDA XX21+SST*2-2 / STA spasto`, which the fixture has to do itself.
     *
     * `spasto` is `EQUW &8888` in the source and `BEGIN` overwrites it at boot with the Coriolis's
     * table entry. `BEGIN` is startup code and `OracleImage::Fresh()` does not run it, so the
     * assembled image still holds the placeholder -- and `NWSPS` copies `spasto` INTO the table, so
     * a comparison against an image that has not booted spawns a station whose blueprint is &8888.
     * That is not a state the machine is ever in, which is §6.95's rule reaching a third byte: the
     * ORACLE has to be put into a state the game could be in, not just the port.
     *
     * The port needs no field for it. Nothing writes `spasto` after `BEGIN`, and the port's ship
     * data region is immutable, so `BlueprintAddress(SHIP_TYPE_STATION)` IS `spasto` for ever.
     */
    const std::uint16_t coriolis = Elite::BlueprintAddress(Elite::SHIP_TYPE_STATION);
    _cpu.memory[_at.spasto] = static_cast<std::uint8_t>(coriolis & 0xFFu);
    _cpu.memory[static_cast<std::uint16_t>(_at.spasto + 1u)] = static_cast<std::uint8_t>(coriolis >> 8);

    // 6502: XX21+2*SST-2 -- RAM, and the port's copy of it is `Bubble::stationBlueprint`.
    _cpu.memory[_at.xx21Station] = static_cast<std::uint8_t>(_world.bubble.stationBlueprint & 0xFFu);
    _cpu.memory[static_cast<std::uint16_t>(_at.xx21Station + 1u)] = static_cast<std::uint8_t>(_world.bubble.stationBlueprint >> 8);

    _cpu.memory[_at.abraxas] = _world.screen.colourBank;
    _cpu.memory[_at.caravanserai] = _world.screen.bitmapMode;
    _cpu.memory[_at.dflag] = _world.screen.dashboardShown;
    _cpu.memory[_at.comx] = _world.compass.x;
    _cpu.memory[_at.comy] = _world.compass.y;
    _cpu.memory[_at.comc] = _world.compass.colour;
    _cpu.memory[_at.t2] = _world.draw.t2;

    _cpu.memory[_at.delta] = _world.flight.delta;
    _cpu.memory[_at.alp1] = _world.flight.alp1;
    _cpu.memory[_at.alp2] = _world.flight.alp2;
    _cpu.memory[_at.beta] = _world.flight.beta;
    _cpu.memory[_at.bet1] = _world.flight.bet1;
    _cpu.memory[_at.mcnt] = _world.flight.mainLoopCounter;
    _cpu.memory[_at.xx0] = static_cast<std::uint8_t>(_world.flight.blueprint & 0xFFu);
    _cpu.memory[static_cast<std::uint16_t>(_at.xx0 + 1u)] = static_cast<std::uint8_t>(_world.flight.blueprint >> 8);

    _cpu.memory[_at.energy] = _world.status.energy;
    _cpu.memory[_at.fsh] = _world.status.forwardShield;
    _cpu.memory[_at.ash] = _world.status.aftShield;
    _cpu.memory[_at.cabtmp] = _world.status.cabinTemperature;
    _cpu.memory[_at.gntmp] = _world.status.laserTemperature;
    _cpu.memory[_at.altit] = _world.status.altitude;
    _cpu.memory[_at.flh] = _world.status.damageFlash;
    _cpu.memory[_at.ecma] = _world.status.ecmCountdown;
    // `QQ14` came over with the whole commander block above; `Seed` keeps `World::fuel` equal to it.
  }

  /// Compare every byte of state the screen routines can touch.
  inline void CompareState(const Cpu6502& _cpu, const World& _world, const Where& _at, const std::wstring& _context,
                           bool _compareRng = true)
  {
    auto same = [&](std::uint16_t _address, std::uint8_t _ours, const std::wstring& _name)
    { Assert::AreEqual(_cpu.memory[_address], _ours, (_context + L": " + _name).c_str()); };

    same(_at.xc, _world.text.column, L"XC");
    same(_at.yc, _world.text.row, L"YC");
    same(_at.qq17, _world.printer.CaseFlags(), L"QQ17");
    same(_at.dtw1, _world.characters.state.lowerCaseBits, L"DTW1");
    same(_at.dtw2, _world.characters.state.sentenceStart, L"DTW2");
    same(_at.dtw6, _world.characters.state.alwaysLower, L"DTW6");
    same(_at.lsp, _world.heaps.lsp, L"LSP");
    same(_at.dly, _world.message.delay, L"DLY");
    same(_at.de, _world.message.append, L"de");
    same(_at.las2, _world.status.viewLaser, L"LAS2");
    same(_at.viewByte, _world.spaceView, L"VIEW");
    same(_at.qq11, _world.view, L"QQ11");
    same(_at.ev, _world.explosions, L"EV");
    same(_at.mcnt, _world.flight.mainLoopCounter, L"MCNT");
    same(_at.xx0, static_cast<std::uint8_t>(_world.flight.blueprint & 0xFFu), L"XX0");
    same(static_cast<std::uint16_t>(_at.xx0 + 1u), static_cast<std::uint8_t>(_world.flight.blueprint >> 8), L"XX0+1");
    same(_at.abraxas, _world.screen.colourBank, L"abraxas");
    same(_at.caravanserai, _world.screen.bitmapMode, L"caravanserai");
    same(_at.dflag, _world.screen.dashboardShown, L"DFLAG");
    same(_at.comc, _world.compass.colour, L"COMC");
    same(_at.tribct, _world.trumbles, L"TRIBCT");
    same(_at.nostm, _world.dust.count, L"NOSTM");
    same(_at.tek, _world.techLevel, L"tek");

    // 6502: XX21+2*SST-2 -- the self-modified table entry. It is compared as state rather than
    // asserted about because `NWSPS` is the only writer, so an unexpected change is a defect.
    same(_at.xx21Station, static_cast<std::uint8_t>(_world.bubble.stationBlueprint & 0xFFu), L"XX21+2*SST-2");
    same(static_cast<std::uint16_t>(_at.xx21Station + 1u), static_cast<std::uint8_t>(_world.bubble.stationBlueprint >> 8), L"XX21+2*SST-1");

    for (std::size_t index = 0; index < _world.heaps.sun.size(); ++index)
    {
      same(static_cast<std::uint16_t>(_at.lso + index), _world.heaps.sun[index], L"LSO");
    }
    for (std::size_t slot = 0; slot < _world.bubble.blocks.size(); ++slot)
    {
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        same(static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte), _world.bubble.blocks[slot][byte],
             L"K% slot " + std::to_wstring(slot) + L" byte " + std::to_wstring(byte));
      }
    }
    for (std::size_t index = 0; index < _world.dust.x.size(); ++index)
    {
      same(static_cast<std::uint16_t>(_at.sx + index), _world.dust.x[index], L"SX");
      same(static_cast<std::uint16_t>(_at.sy + index), _world.dust.y[index], L"SY");
      same(static_cast<std::uint16_t>(_at.sz + index), _world.dust.z[index], L"SZ");
    }
    /*
     * The generator is compared unless the caller says otherwise, and the one caller that says
     * otherwise is the flight loop on a frame that seeds an explosion cloud: `LL9`'s `EE55` block
     * makes four `DORND` calls on a carry that comes out of `LOIN` through `EE51`, and `LOIN` does
     * not return its flags yet (§6.91).
     */
    if (_compareRng)
    {
      for (std::size_t index = 0; index < 4u; ++index)
      {
        same(static_cast<std::uint16_t>(_at.rand + index), _world.rng.State()[index], L"RAND");
      }
    }
  }

} // namespace GameLogicTests
