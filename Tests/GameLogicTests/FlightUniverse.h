#pragma once

#include "pch.h"

#include "Cpu6502.h"
#include "NullSeams.h"
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
#include "StartUp.h"
#include "Stardust.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "ViewChange.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

/*
 * The port's whole flight universe, and the oracle's memory beside it.
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
   * The port's whole flight universe, and the `FlightScreen` over it.
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

  /*
   * The seams a screen change never reaches, answered with nothing.
   *
   * `NullSeams` since M3-a-3, when `Ports` grew the docked half's four and every fixture had to
   * name ten. It is in its own header because the docked suites need it too and they must not
   * drag the oracle in with it.
   */
  using UnusedSeams = NullSeams;

  /*
   * The fixture's universe: `Elite::Universe`'s bytes, plus what a test needs beside them.
   *
   * INHERITED RATHER THAN HELD, since M3-a. Every field the library owns is the base's -- so a
   * fixture writing `universe.canvas` writes the same byte the app does, and slicing a fixture
   * gives the state and nothing else. What is added here is the recording ports, the printers
   * (which cannot live in a universe that has to copy: two of them take a seam) and the two bytes
   * that are the fixture's own.
   */
  struct Universe : Elite::Universe
  {
    RecordingView effects; ///< first, because the character printer's bell records into its list

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

      /*
       * A real handler to pass the code on to, when a suite has one (slice 4d-b).
       *
       * `MissionCodes` is `GameLogic`'s answer to nine of these and a mission suite needs it bound
       * to the SAME printer the fixture built, which is a knot: the handler needs a `MissionScreen`
       * and the screen needs the printer. Forwarding unties it -- the printer keeps this object and
       * this object gains the handler afterwards -- and the recording carries on either way, so the
       * suites that assert the list is empty are unaffected.
       */
      Elite::ControlCodes* to = nullptr;

      void Run(std::uint8_t _code) override
      {
        ran.push_back(_code);
        if (to != nullptr)
        {
          to->Run(_code);
        }
      }
    };

    Codes codes;

        
    /// Declared after `rng` because it binds one, and the order here is the construction order.
    Elite::ExtendedTokenPrinter extendedPrinter{characters, printer, rng, &codes};

    
    /*
     * Whether the Trumbles' COORDINATE REGISTERS are mirrored into the oracle and compared back.
     *
     * Off by default, and the reason is `Where::vic`: those registers are `XX21` in a flat image,
     * so mirroring them overwrites the blueprint pointers for ship types 3 to 9 on one side of the
     * comparison and nothing on the other. A fixture turns this on when it intends `MVTRIBS` to
     * run, which is also a promise that the frame stops before any ship is drawn. `TRIBCT` and the
     * three velocity tables are real RAM and are always mirrored, so a fixture that only wants to
     * see the count written leaves this alone.
     */
    bool spriteRegistersAreOurs = false;

    RecordingSight sight;
    RecordingDashboard dashboard;

    /// 6502: QQ14 -- kept only so the fixtures can name it; the byte the port reads is the
    /// commander block's, because part 15's fuel scooping writes it and a copy would drift.
    std::uint8_t fuel = 0;

    Universe()
    {
      printer.SetCursor(&text);
    }

    /*
     * The seams and the text machinery this fixture answers with, as `Elite::Ports` (M3-a-2).
     *
     * It was `Screen()` returning a `FlightScreen` of twenty-seven references, twenty-two of which
     * were the universe's own bytes. What is left is the recordings, and `LoopRecording` supplies
     * the three a frame needs -- so a fixture that only changes screens passes `sight`/`effects`
     * for all five and one that runs a frame passes its recorder.
     */
    UnusedSeams unused;

    [[nodiscard]] Elite::Ports PortsWith(Elite::ShipDrawEffects& _drawing, Elite::FlightLoopEffects& _loop,
                                         Elite::StartUpEffects& _start) noexcept
    {
      return Elite::Ports{printer, characters, characters, sight,  effects, _drawing, _loop,
                          extendedPrinter, _start, unused, unused, unused, unused};
    }

    /// The three a screen change never reaches, answered with nothing.
    [[nodiscard]] Elite::Ports Ports() noexcept
    {
      return PortsWith(unused, unused, unused);
    }
  };

  /// A universe that is not all zeroes, so "cleared" and "left alone" are different answers everywhere.
  /*
   * What `MJP` and `Ghy` reach outside the universe: sounds, the trumbles and the AI, none of which
   * this slice decides. Counted rather than ignored, because `LL164` makes a noise and a
   * comparison that dropped it would agree with a port that had lost the hyperspace sound.
   */
  struct LoopRecording final : Elite::FlightLoopEffects, Elite::ShipDrawEffects
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
    void StartDockingMusic() override {}
    void StopDockingMusic() override {}
    bool SpawnAhead(Elite::ShipType) override
    {
      return false;
    }
    bool Anger(std::uint8_t, Elite::ShipType) override
    {
      return false; // a trap's answer: nothing ran, and no fixture here reaches the seeding that reads it
    }
    bool SpawnChild(std::uint8_t, Elite::ShipType) override
    {
      return true;
    }
    void DrawPlanetOrSun() override {}
    void DrawExplosion() override {}
  };

  /*
   * The port's side of a case: the whole flight universe and the one recorder a frame reaches
   * through.
   *
   * It was a twelve-member aggregate three suites built to call one routine, because `FlightLoop`
   * held a reference to each of the eight bytes below `Universe`. Since M3-a the universe owns them,
   * so what is left is the pairing of a universe with what answers for the platform around it.
   */
  struct LoopUniverse
  {
    Universe universe;
    LoopRecording effects; ///< the drawing and the sounds, both recorded in one place

    /// The seams as `Elite::Ports`: the recorder for the two a frame reaches, nothing for the rest.
    [[nodiscard]] Elite::Ports Ports() noexcept
    {
      return universe.PortsWith(effects, effects, universe.unused);
    }
  };

  inline void Seed(Universe& _universe, std::uint32_t _seed)
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
      _universe.bubble.slots[slot] = TYPES[slot];
    }
    _universe.bubble.Count(Elite::ShipType::Station) = 1u;
    _universe.bubble.junk = 1u;

    /*
     * 6502: XX21+2*SST-2 -- the pointer table's station entry, which the game has held since boot.
     *
     * Zero is not a value the machine can have here (§6.95's rule, second byte): `NWSHP` refuses a
     * type whose entry is zero, so an unseeded bubble would silently stop creating stations. The
     * Coriolis is what `BEGIN` leaves and what every system below tech level ten keeps.
     */
    _universe.bubble.stationType = Elite::ShipType::Station;

    _universe.current.techLevel = 7u; // 6502: tek -- below the Dodo's threshold, so the seeded state is stable

    for (std::size_t slot = 0; slot < _universe.bubble.blocks.size(); ++slot)
    {
      std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = _universe.bubble.blocks[slot].ToBytes();
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        shipBytes[byte] = (byte == 31u) ? 0xFFu : next();
      }
      _universe.bubble.blocks[slot] = Elite::Ship::FromBytes(shipBytes);
    }
    std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> workBytes = _universe.work.ToBytes();
    for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
    {
      workBytes[byte] = next();
    }
    _universe.work = Elite::Ship::FromBytes(workBytes);

    for (std::size_t index = 0; index < _universe.dust.x.size(); ++index)
    {
      _universe.dust.x[index] = next();
      _universe.dust.xLow[index] = next();
      _universe.dust.y[index] = next();
      _universe.dust.yLow[index] = next();
      _universe.dust.z[index] = next();
      _universe.dust.zLow[index] = next();
    }
    _universe.dust.count = 12u; // 6502: NOST

    for (std::size_t index = 0; index < _universe.heaps.sun.size(); ++index)
    {
      _universe.heaps.sun[index] = next();
    }
    for (std::size_t index = 0; index < _universe.heaps.ball.size(); ++index)
    {
      _universe.heaps.ball[index] = next();
    }
    _universe.heaps.lsp = 0x37u;

    _universe.commander.lasers[0] = Elite::LASER_PULSE;
    _universe.commander.lasers[1u] = 0u;
    _universe.commander.lasers[2u] = Elite::LASER_BEAM;
    _universe.commander.lasers[3u] = Elite::LASER_MILITARY;
    _universe.commander.tribbles.lo = 0x40u;
    _universe.commander.tribbles.hi = 0x21u;
    _universe.trumbles.count = 0x5Au;

    _universe.rng.SetState({0x11u, 0x22u, 0x33u, 0x44u});

    _universe.text.column = 0x1Fu;
    _universe.text.row = 0x0Bu;
    _universe.text.cellColour = Elite::TEXT_COLOUR_WHITE;
    _universe.printer.SetCaseFlags(0x40u);
    _universe.characters.state.lowerCaseBits = 0u;
    _universe.characters.state.sentenceStart = 0u;
    _universe.characters.state.alwaysLower = 0xFFu;

    _universe.message.delay = 0x2Au;
    _universe.message.append = 0x3Bu;
    _universe.status.viewLaser = 0x4Cu;
    _universe.status.energy = 180u;
    _universe.status.forwardShield = 90u;
    _universe.status.aftShield = 60u;
    _universe.status.cabinTemperature = 100u;
    _universe.status.laserTemperature = 70u;
    _universe.status.altitude = 120u;
    _universe.status.damageFlash = 0u;
    _universe.status.ecmCountdown = 0u;
    _universe.fuel = 40u;
    _universe.commander.fuel = _universe.fuel; // `Mirror` sends the block, not the byte

    _universe.flight.delta = 14u;
    _universe.flight.alp1 = 5u;
    _universe.flight.alp2 = 128u;
    _universe.flight.beta = 200u;
    _universe.flight.bet1 = 3u;
    _universe.flight.mainLoopCounter = 0u;

    _universe.screen.colourBank = 0x33u;
    _universe.screen.bitmapMode = 0x44u;
    _universe.screen.dashboardShown = 0u;
    _universe.explosions = 0x66u;
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
    std::uint16_t tribvx, tribvxh, tribxh, vic; ///< 6502: the Trumble sprite bank, slice 4d-a
    std::uint16_t tp, mch, messxc, screen;
    std::uint16_t tek, xx21Station, spasto; ///< 6502: tek, XX21+2*SST-2, and BEGIN's saved copy of it

    /// Unresolved -- every address zero -- for the one use that needs none: hashing the image,
    /// which reads cells in table order and never their addresses (`Hash(const Universe&)`).
    Where() = default;

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
      tribvx = _oracle.Label("TRIBVX");
      tribvxh = _oracle.Label("TRIBVXH");
      tribxh = _oracle.Label("TRIBXH");

      /*
       * 6502: VIC, which the source sets to &D000 -- and in a flat image that is `XX21`, the ship
       * blueprint pointer table (§6.108).
       *
       * So writing a Trumble sprite's x coordinate in the oracle overwrites the blueprint pointers
       * for ship types 3 to 9. On the real machine `SETL1` banks the video chip in over that RAM
       * and the table is untouched; there is no bank here, so a comparison that runs `MVTRIBS` and
       * then draws a ship is comparing against a corrupted table. Every fixture that gives a frame
       * Trumbles has to stop before the ships are drawn, and `TheControlRatesMatchM` -- the only
       * one that does -- runs the frame's HEAD.
       */
      vic = _oracle.Label("XX21");
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
      xx21Station = static_cast<std::uint16_t>(_oracle.Label("XX21") + 2u * Elite::Byte(Elite::ShipType::Station) - 2u);
      spasto = _oracle.Label("spasto");

      screen = ScreenBase(_oracle);
    }
  };

  /*
   * The two names the suites use, answered by the universe image (Design/Modernize.md slice M0-b).
   *
   * They were two hand-written functions here, each naming its fields in its own order, so that the
   * map from a port field to a 6502 label existed twice. `UniverseImage.cpp` holds it once, as a
   * table of cells; `Mirror` is its `Materialise` and `CompareState` its `Compare` with the first
   * difference turned into an assertion. Nothing a suite passes has changed.
   */
  void Mirror(const Universe& _universe, Cpu6502& _cpu, const Where& _at);
  void CompareState(const Cpu6502& _cpu, const Universe& _universe, const Where& _at, const std::wstring& _context,
                    bool _compareRng = true);

} // namespace GameLogicTests
