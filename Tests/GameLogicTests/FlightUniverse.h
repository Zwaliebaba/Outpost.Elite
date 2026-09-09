#pragma once

#include "pch.h"

#include "NullSeams.h"

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
 * The port's whole flight universe, as a fixture.
 *
 * Shared by `ViewChangeTests`, `LaunchTests`, `FlightLoopTests` and `FlightPort.h` because all of
 * them drive routines that reach all of it: a screen change clears everything drawn, and the
 * flight loop drives everything that draws. Building it four times would be the same eighteen
 * arguments in four disguises.
 */
namespace GameLogicTests
{

  using Microsoft::VisualStudio::CppUnitTestFramework::Assert;

  inline std::wstring WidenText(const std::string& _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }


  /*
   * The port's whole flight universe, and the `FlightScreen` over it.
   *
   * One object because `TT66` genuinely reaches all of it -- the line heaps, the token printer, the
   * message counters, the laser, the stardust and the dashboard -- and building it twice per test
   * method would be the same eighteen arguments in a different disguise.
   */

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
    /*
     * 6502: SID -- what the game side of the code writes, which for a flight fixture is nothing.
     *
     * `Ports::sid` needs somewhere to point and this is it. `stopat` and `BDENTRY` are the only
     * routines that reach it, and the suites that run them -- `StartUpTests`, `LaunchTests` --
     * assert on `Universe::music` rather than on the log, because the player's state is what the
     * oracle can be compared against and a register write is not (yet: §6.108's harness slice).
     */
    Elite::SidWriteLog sid;

    /*
     * The real character printer, drawing into the canvas -- `CHPR` is NOT trapped on the oracle's
     * side for this slice. `TT66` prints the view's name and `ee3` prints the countdown, and both
     * belong on the screen the routine has just cleared; trapping them would compare a character
     * stream and leave the pixels they produce out of the whole-canvas compare that everything else
     * here is checked by.
     */
    /*
     * `CHPR`'s ONE remaining seam, which is the screen clear.
     *
     * The bell was the other and is not a seam any more (M3-b-2b): character 7 is `JSR BEEP` and
     * `BEEP` has been `Elite::Beep` over a `SoundBuffer` since slice 5a, so `TextPrinter` takes the
     * buffer and rings it. `CHPR` is not trapped on the oracle's side here, so the game's bell
     * reaches the real `NOISE` and writes `sound_variables` -- and the flight loop prints a token
     * that contains one, so a port that only counted the call would be one effect short on every
     * energy warning.
     */
    Elite::TextPrinter glyphs{canvas, text, &sound};
    Elite::CharacterPrinter characters{glyphs, sentences};
    Elite::TokenPrinter printer{characters, text};

    /*
     * `Codes` WAS HERE AND IS NOT ANY MORE (M3-b-4b).
     *
     * It recorded which control codes left the text system, and forwarded them to a `MissionCodes`
     * when a suite had one. `Elite::RunControlCode` is the whole dispatch now, so a suite that
     * wants the codes to RUN calls `RunCodesThrough` and the comparison is the state they produced
     * -- which is §6.73's corollary for the twelfth time: the seam was what a suite counted, and
     * the count goes with it.
     */

    /// Declared after `rng` because it binds one, and the order here is the construction order.
    Elite::ExtendedTokenPrinter extendedPrinter{characters, printer, rng};

    /*
     * Point the extended printer at a `Ports` the caller owns, so a control code that leaves the
     * text system runs in the library instead of being ignored.
     *
     * THE `Ports` MUST OUTLIVE THE PRINTING. `Ports()` and `PortsWith` return by value, so this
     * takes a reference to a named local rather than binding a temporary -- a fixture that passed
     * `RunCodesThrough(Ports())` would leave the printer pointing at a dead struct of references.
     *
     * A fixture that does NOT call this has a printer that ignores the codes that leave, which is
     * what a null `ControlCodes*` meant before M3-b-4b and is what the token suites are built on.
     */
    void RunCodesThrough(Elite::Ports& _ports) noexcept
    {
      extendedPrinter.SetGame(*this, _ports);
    }

    
    // `spriteRegistersAreOurs` WAS HERE AND IS NOT ANY MORE (M6-0-a): the interpreter banks the I/O
    // page, so the sprite registers are ordinary cells and no fixture has to claim them.


    /*
     * The seams and the text machinery this fixture answers with, as `Elite::Ports` (M3-a-2).
     *
     * It was `Screen()` returning a `FlightScreen` of twenty-seven references, twenty-two of which
     * were the universe's own bytes. What is left is the recordings, and `LoopRecording` supplies
     * the two a frame needs -- so a fixture that only changes screens passes `unused` for all three
     * and one that runs a frame passes its recorder.
     */
    UnusedSeams unused;

    [[nodiscard]] Elite::Ports PortsWith(Elite::Presenter& _present, Elite::Keyboard& _keyboard) noexcept
    {
      return Elite::Ports{printer, characters, characters, sid, extendedPrinter, _present, _keyboard, unused};
    }

    /// The same, for a fixture that does not reach the keyboard -- which `RDKEY` made most of them
    /// until M3-b-3d, when the walk became `Elite::ScanKeyboard` and its callers started asking.
    [[nodiscard]] Elite::Ports PortsWith(Elite::Presenter& _present) noexcept
    {
      return PortsWith(_present, unused);
    }

    /// The same, for a fixture whose start recorder is its presenter too -- which is most of them,
    /// because `DELAY` is declared beside `TITLE` in the game.
    /// The two a screen change never reaches, answered with nothing.
    [[nodiscard]] Elite::Ports Ports() noexcept
    {
      return PortsWith(unused, unused);
    }
  };

  /// A universe that is not all zeroes, so "cleared" and "left alone" are different answers everywhere.
  /*
   * What `MJP` and `Ghy` reach outside the universe: sounds, the trumbles and the AI, none of which
   * this slice decides. Counted rather than ignored, because `LL164` makes a noise and a
   * comparison that dropped it would agree with a port that had lost the hyperspace sound.
   */
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
    /*
     * `LoopRecording` WAS A SECOND CLASS HERE AND IS NOT ANY MORE (M3-b-4c).
     *
     * It was named for a recording it had stopped making: two empty draw methods and a `SpawnChild`
     * that returned true, where `NullSeams` returned false. One boolean, and it became `spawnRoom`
     * on the null port -- which is §4.5's "one class" for the tests, arrived at by deleting the
     * other one rather than by merging two. M4-a-1 removed the boolean as well.
     */
    /*
     * `spawnRoom` WAS SET HERE AND THERE IS NO SUCH BYTE ANY MORE (M4-a-1).
     *
     * It was `SFS1`'s carry -- "the bubble had room" -- which a frame worth running needs because a
     * kill spawns debris. `Elite::PerformDrop` spawns into the real bubble now, so the answer comes
     * from the slot list rather than from a fixture's boolean, and there is nothing to set.
     */

    Universe universe;

    /// The seams as `Elite::Ports`: nothing, and `SFS1` answering that the bubble had room.
    [[nodiscard]] Elite::Ports Ports() noexcept
    {
      return universe.PortsWith(universe.unused, universe.unused);
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
    _universe.heaps.ballHeapTop = 0x37u;
    /*
     * 6502: SUNX(1 0) -- an old sun for the frame to rub out (M6-0-d), with a centre the game could
     * have LEFT THERE: `SUN` writes it from `K3` only when it has drawn, so the high byte is 0 or 1.
     * A random high byte reaches a path no game state reaches -- `WPLS`'s `EDGES` finds the row's
     * right end off the LEFT of the screen, takes `ED1` without ever writing `X1`, and `HLOIN2`
     * draws from whatever `X1` last held. The original reads a stale zero-page byte there and the
     * port, whose `X1` is a local since M2-c, cannot; §8 (M6-0-d) records the deviation.
     */
    _universe.heaps.sunX = next();
    _universe.heaps.sunXNext = static_cast<std::uint8_t>(next() & 0x01u);
    _universe.heaps.lowestVisibleRow = 143u;    // 6502: Yx2M1 -- what RES2 leaves, and what CHKON reads

    _universe.commander.lasers[0] = Elite::LASER_PULSE;
    _universe.commander.lasers[1u] = Elite::LASER_NONE;
    _universe.commander.lasers[2u] = Elite::LASER_BEAM;
    _universe.commander.lasers[3u] = Elite::LASER_MILITARY;
    _universe.commander.tribbles.lo = 0x40u;
    _universe.commander.tribbles.hi = 0x21u;
    _universe.trumbles.count = 0x5Au;

    _universe.rng.SetState({0x11u, 0x22u, 0x33u, 0x44u});

    _universe.text.column = 0x1Fu;
    _universe.text.row = 0x0Bu;
    _universe.text.palette = Elite::TEXT_COLOUR_WHITE;
    _universe.printer.SetCaseFlags(0x40u);
    _universe.sentences.lowerCaseBits = 0u;
    _universe.sentences.sentenceStart = 0u;
    _universe.sentences.alwaysLower = 0xFFu;

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
    _universe.commander.fuel.tenths = 40u;

    _universe.flight.speed = 14u;
    _universe.flight.rollMagnitude = 5u;
    _universe.flight.rollSign = 128u;
    _universe.flight.pitchRate = 200u;
    _universe.flight.pitchMagnitude = 3u;
    _universe.flight.mainLoopCounter = 0u;

    _universe.screen.colourBank = 0x33u;
    _universe.screen.bitmapMode = 0x44u;
    _universe.screen.dashboardShown = 0u;
    _universe.explosions = 0x66u;
  }


} // namespace GameLogicTests
