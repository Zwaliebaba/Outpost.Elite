#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Controls.h"

#include "Arith.h"
#include "Canvas.h"
#include "Commander.h"
#include "LookupTables.h"
#include "ShipMove.h"
#include "ShipSlot.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The player's controls (slice 3d-d-ii).
 *
 * `BUMP2` and `REDU2` take one byte in A and one in X and read one configuration byte, so they
 * are swept outright: every rate against every amount against both settings of `DJD`, 131,072
 * calls each. Nothing is sampled and nothing is argued about.
 */
namespace GameLogicTests
{

namespace
{
bool OracleMissing()
{
  const OracleImage& oracle = OracleImage::Instance();
  if (oracle.Available())
  {
    return false;
  }
  Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
  return true;
}

std::wstring Widen(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// The screen's base address, derived the way the game derives it: `ylookup` holds the row
/// addresses with the four-cell left margin already added, so the base is the first row less 32.
std::uint16_t ScreenBase(const OracleImage& _oracle)
{
  const Cpu6502 image = _oracle.Fresh();
  return static_cast<std::uint16_t>(
    (image.memory[_oracle.Label("ylookupl")] | (image.memory[_oracle.Label("ylookuph")] << 8))
    - 0x20);
}

void FillScreens(Cpu6502& _cpu, Elite::Canvas& _canvas, std::uint16_t _base, std::uint8_t _marker)
{
  std::memset(&_cpu.memory[_base], _marker, Elite::Canvas::SCREEN_SIZE);
  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    _canvas.Write(offset, _marker);
  }
}

void CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas,
                    const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();
  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset)
                    + L" -- game has " + std::to_wstring(expected) + L", port has "
                    + std::to_wstring(ours[offset])).c_str());
    }
  }
}
} // namespace


TEST_CLASS(TheControlRates)
{
public:
  /*
   * 6502: BUMP2 and REDU2 -- every rate, every amount, both settings of `DJD`.
   *
   * The two are one routine over two files and each ends by branching into the other, so they
   * are swept together: a port that got `BUMP2`'s half right and re-entered `REDU2`'s tail at the
   * wrong point would pass one sweep and fail the other.
   *
   * A IS PART OF THE ANSWER. Both routines save the amount in `T` and restore it before the
   * `RTS`, so a caller reading A after the call gets what it passed in -- `DOKEY` relies on that,
   * calling `BUMP2` and then `REDU2` with the same 14 still in A. The port returns only the rate,
   * so the sweep checks that the game really does leave A alone rather than the port assuming it.
   *
   * The counters at the end are what stops this being a sweep that proves nothing: they say that
   * the clamps, the re-centring and the zero below were all actually reached.
   */
  TEST_METHOD(TheControlRatesMatchBUMP2AndREDU2)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t bump2 = oracle.Label("BUMP2");
    const std::uint16_t redu2 = oracle.Label("REDU2");
    const std::uint16_t djd = oracle.Label("DJD");

    Cpu6502 cpu = oracle.Fresh();

    std::uint32_t compared = 0;
    std::uint32_t clampedHigh = 0;
    std::uint32_t clampedLow = 0;
    std::uint32_t recentred = 0;
    std::uint32_t zeroes = 0;

    for (const std::uint8_t disabled : { std::uint8_t{ 0 }, std::uint8_t{ 0xFF } })
    {
      for (std::uint32_t rate = 0; rate < 256; ++rate)
      {
        for (std::uint32_t amount = 0; amount < 256; ++amount)
        {
          const std::uint8_t value = static_cast<std::uint8_t>(rate);
          const std::uint8_t step = static_cast<std::uint8_t>(amount);

          cpu.memory[djd] = disabled;
          cpu.x = value;
          cpu.a = step;
          const Elite::Testing::RunResult up = cpu.CallSubroutine(bump2, 200);
          Assert::IsTrue(up.completed, L"BUMP2 returned");
          const std::uint8_t gameBumped = cpu.x;
          const std::uint8_t gameBumpedA = cpu.a;

          cpu.memory[djd] = disabled;
          cpu.x = value;
          cpu.a = step;
          const Elite::Testing::RunResult down = cpu.CallSubroutine(redu2, 200);
          Assert::IsTrue(down.completed, L"REDU2 returned");
          const std::uint8_t gameReduced = cpu.x;
          const std::uint8_t gameReducedA = cpu.a;

          const std::uint8_t ourBumped = Elite::BumpControl(value, step, disabled);
          const std::uint8_t ourReduced = Elite::ReduceControl(value, step, disabled);

          const std::wstring where =
            Widen("(rate " + std::to_string(rate) + ", amount " + std::to_string(amount)
                  + ", DJD " + std::to_string(disabled) + ")");

          Assert::AreEqual(gameBumped, ourBumped, (L"BUMP2 " + where).c_str());
          Assert::AreEqual(gameReduced, ourReduced, (L"REDU2 " + where).c_str());
          Assert::AreEqual(step, gameBumpedA, (L"BUMP2 leaves A alone " + where).c_str());
          Assert::AreEqual(step, gameReducedA, (L"REDU2 leaves A alone " + where).c_str());

          clampedHigh += (gameBumped == 0xFFu && rate + amount > 0xFFu) ? 1u : 0u;
          clampedLow += (gameReduced == 1u && amount > rate) ? 1u : 0u;
          recentred += (gameBumped == 128u || gameReduced == 128u) ? 1u : 0u;
          zeroes += (gameReduced == 0u) ? 1u : 0u;
          ++compared;
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(2u * 256u * 256u, compared, L"the whole sweep ran");
    Assert::IsTrue(clampedHigh > 0u, L"the 255 clamp was reached");
    Assert::IsTrue(clampedLow > 0u, L"the 1 clamp was reached");
    Assert::IsTrue(recentred > 0u, L"and the auto-recentre fired");

    /*
     * The hole in `REDU2`'s clamp, counted rather than described. `SBC` leaves the carry set when
     * it did not borrow, and an exact match does not borrow, so every rate equal to its amount
     * comes out as zero -- 256 of them, one per amount, in each of the two `DJD` settings. The
     * routine's own comment says the rate runs from 1 to 255.
     */
    Assert::AreEqual<std::uint32_t>(2u * 256u, zeroes, L"and REDU2 produced zero exactly where it must");
  }
};


TEST_CLASS(TheFlightControls)
{
public:
  /*
   * 6502: DOKEY's flight half, from `.DOKEY` to `.ant`.
   *
   * `.ant` IS `DK4`: the same address carries both labels, so trapping `DK4` stops the oracle at
   * exactly the instruction the port's function ends before. The fall-through needs no guessing.
   *
   * `RDKEY` is trapped because it is the CIA scan. `DOCKIT` is not trapped -- it is REPLACED, by
   * twenty-one bytes of stub that store four chosen values into `INWK+27` to `INWK+30` and
   * return. A plain trap would leave those bytes as `ZINF` left them, which is zero, and zero is
   * the one case where the roll and pitch paths below do nothing at all: the whole of what this
   * routine does with the autopilot's answer would have gone uncompared. The port's own stub
   * stores the same four values, so both sides run the same fake autopilot and everything
   * downstream of it is compared for real.
   */
  TEST_METHOD(TheFlightControlsMatchDOKEY)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t dokey = oracle.Label("DOKEY");
    const std::uint16_t dockit = oracle.Label("DOCKIT");
    const std::uint16_t klo = oracle.Label("KLO");
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t jstx = oracle.Label("JSTX");
    const std::uint16_t jsty = oracle.Label("JSTY");
    const std::uint16_t jstk = oracle.Label("JSTK");
    const std::uint16_t djd = oracle.Label("DJD");
    const std::uint16_t autoPilot = oracle.Label("auto");
    const std::uint16_t delta = oracle.Label("DELTA");
    const std::uint16_t type = oracle.Label("TYPE");

    struct Autopilot
    {
      std::uint8_t speed, acceleration, roll, pitch;
    };

    struct Case
    {
      std::uint8_t docking, joystick, recentre;
      std::uint8_t roll, pitch;
      std::uint8_t keys; ///< bits 0-3: KY3, KY4, KY5, KY6 held
      Autopilot autopilot;
    };

    // The rates that matter: both clamps, both sides of the centre, the centre itself, and the
    // exact 14 that `REDU2` turns into zero.
    const std::vector<std::uint8_t> RATES = { 1, 14, 15, 64, 127, 128, 129, 200, 241, 255 };

    // What the autopilot can ask for. `ASL` doubles it, so 0x40 is the smallest roll request
    // whose doubling sets bit 7 -- the boundary the `BIT` after the shift is testing.
    const std::vector<std::uint8_t> REQUESTS = { 0x00, 0x01, 0x3F, 0x40, 0x7F, 0x80, 0xC0, 0xFF };
    // EORed with DELTA, which every case starts at 7, so the speeds coming back out of the
    // stub are 7, 23, 22, 21 and 248 -- the clamp's boundary from both sides and one far
    // above it.
    const std::vector<std::uint8_t> SPEEDS = { 0, 16, 17, 18, 255 };

    std::vector<Case> cases;

    // ---- the player flying ------------------------------------------------------------------
    for (const std::uint8_t joystick : { std::uint8_t{ 0 }, std::uint8_t{ 0xFF } })
    {
      for (const std::uint8_t recentre : { std::uint8_t{ 0 }, std::uint8_t{ 0xFF } })
      {
        for (std::uint8_t keys = 0; keys < 16u; ++keys)
        {
          for (const std::uint8_t rate : RATES)
          {
            cases.push_back({ 0, joystick, recentre, rate,
                              static_cast<std::uint8_t>(255u - rate), keys, { 0, 0, 0, 0 } });
          }
        }
      }
    }

    // ---- the docking computer flying --------------------------------------------------------
    for (const std::uint8_t joystick : { std::uint8_t{ 0 }, std::uint8_t{ 0xFF } })
    {
      for (const std::uint8_t speed : SPEEDS)
      {
        for (const std::uint8_t acceleration : { std::uint8_t{ 0 }, std::uint8_t{ 1 },
                                                 std::uint8_t{ 0x7F }, std::uint8_t{ 0x80 },
                                                 std::uint8_t{ 0xFF } })
        {
          for (const std::uint8_t roll : REQUESTS)
          {
            for (const std::uint8_t pitch : REQUESTS)
            {
              cases.push_back({ 0xFF, joystick, 0, 100, 190, 0,
                                { speed, acceleration, roll, pitch } });
            }
          }
        }
      }
    }

    Cpu6502 cpu = oracle.Fresh();
    cpu.AddTrap(oracle.Label("RDKEY"));
    cpu.AddTrap(oracle.Label("DK4")); // which is `.ant`, where the port's function ends

    std::uint32_t recentredByStick = 0;
    std::uint32_t bigRollRequests = 0;
    std::uint32_t clampedSpeed = 0;

    for (const Case& item : cases)
    {
      /*
       * 6502: JSR DOCKIT, replaced -- LDA INWK+27 / EOR #n / STA INWK+27, then LDA #n /
       * STA INWK+28 .. INWK+30, then RTS.
       *
       * The speed is EORed rather than stored because the real `DOCKIT` READS `INWK+27`: it is
       * handed the current speed and gives back an adjusted one. A stub that only wrote it would
       * make `LDA DELTA / STA INWK+27` invisible, and a port that dropped that instruction would
       * pass -- which is exactly what the mutation sweep found before this was an EOR.
       */
      const std::uint8_t stub[] = {
        0xAD, static_cast<std::uint8_t>((inwk + 27) & 0xFFu), static_cast<std::uint8_t>((inwk + 27) >> 8),
        0x49, item.autopilot.speed,
        0x8D, static_cast<std::uint8_t>((inwk + 27) & 0xFFu), static_cast<std::uint8_t>((inwk + 27) >> 8),
        0xA9, item.autopilot.acceleration,
        0x8D, static_cast<std::uint8_t>((inwk + 28) & 0xFFu), static_cast<std::uint8_t>((inwk + 28) >> 8),
        0xA9, item.autopilot.roll,
        0x8D, static_cast<std::uint8_t>((inwk + 29) & 0xFFu), static_cast<std::uint8_t>((inwk + 29) >> 8),
        0xA9, item.autopilot.pitch,
        0x8D, static_cast<std::uint8_t>((inwk + 30) & 0xFFu), static_cast<std::uint8_t>((inwk + 30) >> 8),
        0x60,
      };
      cpu.Load(dockit, stub, sizeof(stub));

      Elite::KeyLogger keys{};
      keys[Elite::KEY_ROLL_LEFT] = ((item.keys & 1u) != 0u) ? 0xFFu : 0u;
      keys[Elite::KEY_ROLL_RIGHT] = ((item.keys & 2u) != 0u) ? 0xFFu : 0u;
      keys[Elite::KEY_PITCH_UP] = ((item.keys & 4u) != 0u) ? 0xFFu : 0u;
      keys[Elite::KEY_PITCH_DOWN] = ((item.keys & 8u) != 0u) ? 0xFFu : 0u;

      for (std::size_t slot = 0; slot < keys.size(); ++slot)
      {
        cpu.memory[static_cast<std::uint16_t>(klo + slot)] = keys[slot];
      }

      Elite::ShipBlock work{};
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        // A block that is not already zero, so `ZINF` has something to clear.
        work[byte] = static_cast<std::uint8_t>(0x5Au + byte);
        cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = work[byte];
      }

      cpu.memory[autoPilot] = item.docking;
      cpu.memory[jstk] = item.joystick;
      cpu.memory[djd] = item.recentre;
      cpu.memory[jstx] = item.roll;
      cpu.memory[jsty] = item.pitch;
      cpu.memory[delta] = 7u;
      cpu.memory[type] = 0u;

      cpu.ClearTrapHits();
      const Elite::Testing::RunResult run = cpu.CallSubroutine(dokey, 20'000);
      Assert::IsTrue(run.completed, L"DOKEY reached .ant");

      // ---- the port -------------------------------------------------------------------------
      struct Stub final : Elite::ControlEffects
      {
        Autopilot answer{};
        std::uint32_t scans = 0;
        std::uint32_t runs = 0;

        void ScanKeyboard() override { ++scans; }
        void RunDockingComputer(Elite::ShipBlock& _work) override
        {
          _work[27] = static_cast<std::uint8_t>(_work[27] ^ answer.speed);
          _work[28] = answer.acceleration;
          _work[29] = answer.roll;
          _work[30] = answer.pitch;
          ++runs;
        }
      } effects;
      effects.answer = item.autopilot;

      Elite::ControlState control;
      control.roll = item.roll;
      control.pitch = item.pitch;
      control.dockingComputer = item.docking;

      Elite::ControlOptions options;
      options.recentreDisabled = item.recentre;
      options.joystick = item.joystick;

      Elite::FlightState flight;
      flight.delta = 7u;
      flight.type = 0u;

      Elite::ReadFlightControls(keys, control, options, work, flight, effects);

      const std::wstring where =
        Widen("DOKEY(auto " + std::to_string(item.docking) + ", JSTK " + std::to_string(item.joystick)
              + ", DJD " + std::to_string(item.recentre) + ", JSTX " + std::to_string(item.roll)
              + ", JSTY " + std::to_string(item.pitch) + ", keys " + std::to_string(item.keys)
              + ", autopilot " + std::to_string(item.autopilot.speed) + "/"
              + std::to_string(item.autopilot.acceleration) + "/"
              + std::to_string(item.autopilot.roll) + "/" + std::to_string(item.autopilot.pitch) + ")");

      Assert::AreEqual(cpu.memory[jstx], control.roll, (where + L": JSTX").c_str());
      Assert::AreEqual(cpu.memory[jsty], control.pitch, (where + L": JSTY").c_str());
      Assert::AreEqual(cpu.memory[delta], flight.delta, (where + L": DELTA").c_str());
      Assert::AreEqual(cpu.memory[type], flight.type, (where + L": TYPE").c_str());

      for (std::size_t slot = 0; slot < keys.size(); ++slot)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(klo + slot)], keys[slot],
                         (where + L": KLO+" + std::to_wstring(slot)).c_str());
      }
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + byte)], work[byte],
                         (where + L": INWK+" + std::to_wstring(byte)).c_str());
      }

      Assert::AreEqual<std::uint32_t>(1u, effects.scans, (where + L": one keyboard scan").c_str());
      Assert::AreEqual<std::uint32_t>(item.docking != 0u ? 1u : 0u, effects.runs,
                                      (where + L": the autopilot ran only when it is on").c_str());

      if (item.docking == 0u && item.joystick != 0u && item.keys == 0u)
      {
        recentredByStick += (control.roll == 128u && control.pitch == 128u) ? 1u : 0u;
      }
      bigRollRequests += (item.docking != 0u && control.roll == 64u) ? 1u : 0u;
      clampedSpeed += (item.docking != 0u && flight.delta == 22u) ? 1u : 0u;
    }

    Assert::IsTrue(cases.size() > 3'000u, L"the sweep is worth its name");
    Assert::IsTrue(recentredByStick > 0u, L"the joystick's spring-back fired");
    Assert::IsTrue(bigRollRequests > 0u, L"the autopilot's direct write to JSTX fired");
    Assert::IsTrue(clampedSpeed > 0u, L"and its speed was clamped at 22");
  }
};


TEST_CLASS(TheLaserSights)
{
public:
  /*
   * 6502: SIGHT -- the sights and the Trumbles, compared on the canvas and on both registers.
   *
   * THE SHIP BLUEPRINTS LIVE UNDERNEATH THE VIC-II REGISTERS. `VIC` is &D000 and so is `XX21`;
   * the C64 banks between the chip and the RAM behind it, and the oracle's memory is flat, so
   * `STA VIC+&27` lands on `SHIPS.bin` byte &27 -- which is inside the blueprint pointer table.
   * That is why this uses a fresh image per case rather than one for the sweep: a run that wrote
   * a register would corrupt the blueprints for every case after it (§6.75).
   *
   * It also means the two register writes can be READ BACK, which is what makes them comparable
   * at all. The port puts them behind a seam, the oracle catches them as memory, and the two are
   * compared against each other.
   *
   * `SETL1` is trapped and its two calls are checked for order as well as value: the routine
   * brackets everything it does between %101 and %100, and a port that switched the raster mode
   * once would agree on every byte.
   */
  TEST_METHOD(TheLaserSightsMatchSIGHT)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t sight = oracle.Label("SIGHT");
    const std::uint16_t setl1 = oracle.Label("SETL1");
    const std::uint16_t laserBase = oracle.Label("LASER");
    const std::uint16_t view = oracle.Label("VIEW");
    const std::uint16_t tribble = oracle.Label("TRIBBLE");
    const std::uint16_t tribct = oracle.Label("TRIBCT");
    const std::uint16_t t = oracle.Label("T");
    const std::uint16_t screen = ScreenBase(oracle);

    // 6502: VIC, which is &D000 -- the same address the ship blueprints load at.
    const std::uint16_t vicColour = 0xD027u;
    const std::uint16_t vicEnable = 0xD015u;

    const std::vector<std::uint8_t> LASERS = {
      0,                          // none fitted on this view
      Elite::LASER_PULSE,
      Elite::LASER_BEAM,
      Elite::LASER_MILITARY,
      50,                         // 6502: Mlas -- the mining laser, which nothing tests for
      99,                         // a power the game does not have, which gets the same sprite
    };
    const std::vector<std::uint8_t> POPULATIONS = { 0, 0x0F, 0x10, 0x2F, 0x60, 0x70, 0x80, 0xFF };

    std::uint32_t compared = 0;
    std::set<std::uint32_t> pointers;
    std::set<std::uint32_t> masks;

    for (const std::uint8_t laser : LASERS)
    {
      for (std::uint8_t which = 0; which < 4u; ++which)
      {
        for (const std::uint8_t population : POPULATIONS)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(setl1);
          Elite::Canvas canvas;

          FillScreens(cpu, canvas, screen, 0x6Du);

          Elite::CommanderBlock commander;
          for (std::uint8_t slot = 0; slot < 4u; ++slot)
          {
            // A different laser on every other view, so a port that ignored VIEW would be caught.
            const std::uint8_t fitted = (slot == which) ? laser : Elite::LASER_BEAM;
            commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + slot] = fitted;
            cpu.memory[static_cast<std::uint16_t>(laserBase + slot)] = fitted;
          }

          commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles)] = 0x77u;
          commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles) + 1u] = population;
          cpu.memory[tribble] = 0x77u;
          cpu.memory[static_cast<std::uint16_t>(tribble + 1)] = population;

          cpu.memory[view] = which;
          cpu.memory[tribct] = 0x9Cu;
          cpu.memory[t] = 0x9Cu;
          cpu.memory[vicColour] = 0x00u;
          cpu.memory[vicEnable] = 0x00u;

          const Elite::Testing::RunResult run = cpu.CallSubroutine(sight, 5'000);
          Assert::IsTrue(run.completed, L"SIGHT returned");

          struct Recorder final : Elite::SightEffects
          {
            std::vector<std::uint8_t> modes;
            std::uint8_t colour = 0;
            std::uint32_t colours = 0;
            std::uint8_t enabled = 0;

            void SetRasterMode(std::uint8_t _mode) override { modes.push_back(_mode); }
            void SetSightColour(std::uint8_t _colour) override { colour = _colour; ++colours; }
            void SetSpritesEnabled(std::uint8_t _mask) override { enabled = _mask; }
          } effects;

          Elite::MathWorkspace math;
          math.t = 0x9Cu;
          std::uint8_t trumbleSprites = 0x9Cu;
          Elite::DrawLaserSights(canvas, math, commander, trumbleSprites, which, effects);

          const std::wstring where =
            Widen("SIGHT(laser " + std::to_string(laser) + " on view " + std::to_string(which)
                  + ", Trumbles " + std::to_string(population) + ")");

          CompareScreens(cpu, screen, canvas, where);
          Assert::AreEqual(cpu.memory[tribct], trumbleSprites, (where + L": TRIBCT").c_str());
          Assert::AreEqual(cpu.memory[t], math.t, (where + L": T").c_str());
          Assert::AreEqual(cpu.memory[vicEnable], effects.enabled, (where + L": VIC+&15").c_str());

          // The colour register is only written when a laser was found, so a case with none
          // leaves the marker rather than a colour -- and the port must not call the seam.
          Assert::AreEqual<std::uint32_t>(laser != 0u ? 1u : 0u, effects.colours,
                                          (where + L": how often the colour was set").c_str());
          if (laser != 0u)
          {
            Assert::AreEqual(cpu.memory[vicColour], effects.colour, (where + L": VIC+&27").c_str());
          }
          else
          {
            Assert::AreEqual<std::uint32_t>(0u, cpu.memory[vicColour],
                                            (where + L": the game left it alone too").c_str());
          }

          Assert::AreEqual<std::size_t>(2u, effects.modes.size(),
                                        (where + L": two raster switches").c_str());
          Assert::AreEqual<std::uint32_t>(0x05u, effects.modes[0], (where + L": the way in").c_str());
          Assert::AreEqual<std::uint32_t>(0x04u, effects.modes[1], (where + L": and out").c_str());
          Assert::AreEqual<std::size_t>(2u, cpu.trapHits.size(),
                                        (where + L": the game switched twice too").c_str());
          Assert::AreEqual<std::uint32_t>(effects.modes[0], cpu.trapHits[0].a,
                                          (where + L": with the same mode").c_str());
          Assert::AreEqual<std::uint32_t>(effects.modes[1], cpu.trapHits[1].a,
                                          (where + L": and the same one back").c_str());

          pointers.insert(canvas.Read(Elite::SIGHT_SPRITE_CELL));
          masks.insert(effects.enabled);
          ++compared;
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(6u * 4u * 8u, compared, L"the whole sweep ran");
    Assert::IsTrue(pointers.size() >= 4u, L"every laser got its own sprite");
    Assert::IsTrue(masks.size() >= 6u, L"and the Trumble population moved the enable mask");
  }
};

} // namespace GameLogicTests
