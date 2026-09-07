#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Charts.h"
#include "Controls.h"
#include "NullSeams.h"

#include "Arith.h"
#include "Canvas.h"
#include "Commander.h"
#include "ExtendedTokens.h"
#include "LookupTables.h"
#include "Ports.h"
#include "ShipMove.h"
#include "ShipSlot.h"
#include "SoundEffects.h"
#include "Tokens.h"
#include "Universe.h"

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

    /*
     * 6502: RDKEY's walk, run backwards -- press on the oracle's CIA the keys a logger says are held.
     *
     * The walk starts at `KEYLOOK+&40` with column 0 selected and bit 0 of the rows, and `DEX`es
     * once per key through eight rows and then eight columns, so logger index X is column
     * `(&40 - X) / 8`, row `(&40 - X) % 8`: Space (column 7, row 4) is 4, "A" (column 1, row 2)
     * is 54, RETURN (column 0, row 1) is &3F. Index 0 is the ninth pass, on which every column is
     * deselected and port B reads &FF, so no matrix position answers it.
     *
     * Port A is left as the previous scan's `LDA #%01111111 / STA &DC00` leaves it, because that
     * is what `RDKEY` reads first when `JSTK` is set: with bits 0 to 4 high the joystick is idle
     * and the routine falls into the matrix walk, which is the path the port's `ScanKeyboard` is.
     */
    void HoldOnMatrix(Cpu6502& _cpu, const Elite::KeyLogger& _keys)
    {
      _cpu.keysDown.fill(0u); // exactly these keys, on a CPU the sweep reuses from case to case
      for (std::size_t index = 1; index < _keys.size(); ++index)
      {
        if (_keys[index] != 0u)
        {
          const std::size_t at = 0x40u - index;
          _cpu.HoldKey(static_cast<std::uint8_t>(at >> 3), static_cast<std::uint8_t>(at & 0x07u));
        }
      }
      _cpu.Io(Cpu6502::CIA1_PORT_A) = 0x7Fu;
    }

    /// The screen's base address, derived the way the game derives it: `ylookup` holds the row
    /// addresses with the four-cell left margin already added, so the base is the first row less 32.
    std::uint16_t ScreenBase(const OracleImage& _oracle)
    {
      const Cpu6502 image = _oracle.Fresh();
      return static_cast<std::uint16_t>((image.memory[_oracle.Label("ylookupl")] | (image.memory[_oracle.Label("ylookuph")] << 8)) - 0x20);
    }

    void FillScreens(Cpu6502& _cpu, Elite::Canvas& _canvas, std::uint16_t _base, std::uint8_t _marker)
    {
      std::memset(&_cpu.memory[_base], _marker, Elite::Canvas::SCREEN_SIZE);
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        _canvas.Write(offset, _marker);
      }
    }

    void CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas, const std::wstring& _context)
    {
      const std::span<const std::uint8_t> ours = _canvas.Screen();
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
        if (expected != ours[offset])
        {
          Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset) + L" -- game has " + std::to_wstring(expected) +
                        L", port has " + std::to_wstring(ours[offset]))
                         .c_str());
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

      for (const std::uint8_t disabled : {std::uint8_t{0}, std::uint8_t{0xFF}})
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
              Widen("(rate " + std::to_string(rate) + ", amount " + std::to_string(amount) + ", DJD " + std::to_string(disabled) + ")");

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
      const std::uint16_t klo = oracle.Label("KLO");
      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t jstx = oracle.Label("JSTX");
      const std::uint16_t jsty = oracle.Label("JSTY");
      const std::uint16_t jstk = oracle.Label("JSTK");
      const std::uint16_t djd = oracle.Label("DJD");
      const std::uint16_t autoPilot = oracle.Label("auto");
      const std::uint16_t delta = oracle.Label("DELTA");
      const std::uint16_t type = oracle.Label("TYPE");
      const std::uint16_t frin = oracle.Label("FRIN");
      const std::uint16_t many = oracle.Label("MANY");
      const std::uint16_t kPercent = oracle.Label("K%");
      const std::uint16_t rand = oracle.Label("RAND");
      const std::uint16_t k3 = oracle.Label("K3");
      const std::uint16_t rat = oracle.Label("RAT");
      const std::uint16_t rat2 = oracle.Label("RAT2");
      const std::uint16_t cnt2 = oracle.Label("CNT2");

      /*
       * 6502: the bubble `DOCKIT` steers by, since M6-0-h-3 -- the real autopilot runs on both
       * machines, so what varies is WHERE THE STATION IS rather than what a stub answers.
       *
       * `auton` builds the player's block in `INWK` at the origin with the nose along +z, so the
       * station's position IS the vector `VCSU1` takes, and its nose is the slot `DOCKIT` measures
       * the approach against. Any high byte with magnitude sends the routine to the planet (`GOPL`),
       * which is the first approach; a bubble with no station at all is the other way there.
       */
      struct Approach
      {
        const char* what;
        bool station;
        std::uint8_t xLo, xHi, xSign, yLo, yHi, ySign, zLo, zHi, zSign;
        std::uint8_t noseX, noseY, noseZ;
        std::uint8_t roofX = 0x08u, roofY = 0x60u, roofZ = 0x08u;
      };
      const std::vector<Approach> APPROACHES = {
        {"no station, planet ahead", false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {"station too far, planet ahead", true, 0, 0x40, 0, 0, 0x02, 0, 0, 0x02, 0, 0x60, 0x10, 0x20},
        {"slot facing us, straight ahead", true, 0x40, 0, 0, 0x02, 0, 0, 0x02, 0, 0, 0x60, 0x10, 0x20},
        {"slot facing us, dead ahead on z", true, 0, 0, 0, 0, 0, 0, 0x40, 0, 0, 0x08, 0x08, 0xE0},
        {"slot turned away", true, 0x40, 0, 0, 0x02, 0, 0, 0x02, 0, 0, 0xE0, 0x10, 0x20},
        {"station behind us", true, 0x40, 0, 0x80, 0x02, 0, 0, 0x02, 0, 0x80, 0x60, 0x10, 0x20},
        {"station above", true, 0x04, 0, 0, 0x50, 0, 0, 0x04, 0, 0, 0x60, 0x10, 0x20},
        {"station below and left", true, 0x30, 0, 0x80, 0x30, 0, 0x80, 0x08, 0, 0, 0x60, 0x10, 0x20},
        {"close along a diagonal slot", true, 0x30, 0, 0, 0x30, 0, 0, 0x30, 0, 0, 0x60, 0x60, 0x60},
        {"lined up on the slot, far down z", true, 0x02, 0, 0, 0x02, 0, 0, 0xF0, 0, 0, 0x08, 0x08, 0x60},
        {"lined up on the slot, close", true, 0x02, 0, 0, 0x02, 0, 0, 0x30, 0, 0, 0x08, 0x08, 0x60},
        {"off the slot's axis", true, 0x14, 0, 0x80, 0x0A, 0, 0, 0x40, 0, 0, 0x08, 0x08, 0x60},
        /*
         * The fine approach -- our nose along the slot within 12 of the axis, the slot's nose along
         * ours -- and then the station's ROOF against our side, which is the last test before a
         * ship is lined up: a roof across our side rolls hard (`TN11`, roll 127 and speed up), a
         * roof along it stops and turns (`PH22`). Ours is (0, 0, +96) with the side on +x, so the
         * slot has to point back down z at us and the roof either along x or along y.
         */
        {"lined up, roof across our side", true, 0x02, 0, 0, 0x02, 0, 0, 0x60, 0, 0, 0x02, 0x02, 0xE0, 0x60, 0x08, 0x08},
        {"lined up, roof along our side", true, 0x02, 0, 0, 0x02, 0, 0, 0x60, 0, 0, 0x02, 0x02, 0xE0, 0x08, 0x60, 0x08},
      };

      struct Case
      {
        std::uint8_t docking, joystick, recentre;
        std::uint8_t roll, pitch;
        std::uint8_t keys;     ///< bits 0-3: KY3, KY4, KY5, KY6 held
        std::uint8_t delta;    ///< 6502: DELTA going in, which `auton` hands `DOCKIT` in `INWK+27`
        std::uint8_t approach; ///< an index into `APPROACHES`, for the docking computer
        std::uint8_t faces;    ///< 6502: K3+10, the last drawn ship's eleventh face, which `DOCKIT` reads
      };

      // The rates that matter: both clamps, both sides of the centre, the centre itself, and the
      // exact 14 that `REDU2` turns into zero.
      const std::vector<std::uint8_t> RATES = {1, 14, 15, 64, 127, 128, 129, 200, 241, 255};

      // `DELTA` going in for the autopilot: 22 is the clamp, from both sides and from far above.
      const std::vector<std::uint8_t> SPEEDS = {0, 7, 21, 22, 23, 255};

      std::vector<Case> cases;

      // ---- the player flying ------------------------------------------------------------------
      for (const std::uint8_t joystick : {std::uint8_t{0}, std::uint8_t{0xFF}})
      {
        for (const std::uint8_t recentre : {std::uint8_t{0}, std::uint8_t{0xFF}})
        {
          for (std::uint8_t keys = 0; keys < 16u; ++keys)
          {
            for (const std::uint8_t rate : RATES)
            {
              cases.push_back({0, joystick, recentre, rate, static_cast<std::uint8_t>(255u - rate), keys, 7, 0, 0});
            }
          }
        }
      }

      // ---- the docking computer flying --------------------------------------------------------
      for (const std::uint8_t joystick : {std::uint8_t{0}, std::uint8_t{0xFF}})
      {
        for (const std::uint8_t speed : SPEEDS)
        {
          for (std::uint8_t approach = 0; approach < APPROACHES.size(); ++approach)
          {
            for (const std::uint8_t faces : {std::uint8_t{0}, std::uint8_t{0xFF}})
            {
              for (const std::uint8_t roll : {std::uint8_t{1}, std::uint8_t{100}, std::uint8_t{128}, std::uint8_t{255}})
              {
                cases.push_back({0xFF, joystick, 0, roll, static_cast<std::uint8_t>(255u - roll), 0, speed, approach, faces});
              }
            }
          }
        }
      }

      Cpu6502 cpu = oracle.Fresh();
      // `DK4` is `.ant`, where the port's `ReadFlightControls` ends; its head -- the key into `KL`
      // and the pause test -- is `Game::Step`'s and is compared in `PauseScreenTests` (M6-0-e).
      cpu.AddTrap(oracle.Label("DK4"));
      cpu.AddTrap(oracle.Label("NOISE")); // `DOCKIT` reaches neither; `TacticsTests` traps both too
      cpu.AddTrap(oracle.Label("MESS"));

      /*
       * The port's side of `RDKEY`, and IT IS A COMPARISON OF `RDKEY` since M6-0-a-4.
       *
       * `DOKEY` opens `JSR RDKEY`, and the oracle trapped it from M3-b-3d until M6-0-a-4 because
       * it is the CIA matrix scan: the walk reads `&DC00` and `&DC01` eight columns at a time, and
       * a flat image had one byte at `&DC01` whatever column was selected. `Cpu6502` banks the I/O
       * page now and answers port B from a matrix (M6-0-a-1, -4), so the oracle RUNS the routine
       * -- `SETL1`, the sprite register, `ZEKTRAN`, the walk, the `QQ11` tail -- over keys pressed
       * on that matrix, and the port runs `Elite::ScanKeyboard` over this keyboard holding the same
       * four steering keys. What is compared is the logger both scans built, all sixty-five bytes,
       * and a logger seeded full on both sides beforehand is what proves the scans CLEAR.
       */
      struct ScriptedMatrix final : Elite::Keyboard
      {
        std::uint8_t held = 0;     ///< bits 0-3: KY3, KY4, KY5, KY6 held, as the case names them
        std::uint32_t scans = 0; ///< walks, counted at the key each one starts on

        [[nodiscard]] bool Held(std::size_t _key) override
        {
          // `ScanKeyboard` counts DOWN from the top of the logger, so the highest index is where a
          // walk begins -- which is what makes "one scan per DOKEY" still an assertion about scans.
          if (_key + 1u == std::tuple_size_v<Elite::KeyLogger>)
          {
            ++scans;
          }
          if (_key == Elite::KEY_ROLL_LEFT)
          {
            return (held & 1u) != 0u;
          }
          if (_key == Elite::KEY_ROLL_RIGHT)
          {
            return (held & 2u) != 0u;
          }
          if (_key == Elite::KEY_PITCH_UP)
          {
            return (held & 4u) != 0u;
          }
          if (_key == Elite::KEY_PITCH_DOWN)
          {
            return (held & 8u) != 0u;
          }
          return false;
        }
        [[nodiscard]] std::uint8_t NextKey() override { return 0; }
        void Flush() override {}
      } board;

      /*
       * `Stub` -- `ControlEffects` answering `DOCKIT` with the four bytes a case asked for -- WAS
       * HERE AND IS NOT ANY MORE (M6-0-h-3), and so is the eleven-byte stub the oracle ran in
       * `DOCKIT`'s place. Both machines run the autopilot for real over the bubble each case seeds.
       */

      /*
       * The universe and the ports, built ONCE -- `DOKEY` reaches eight of its bytes and none of
       * the text machinery, but a `Ports` binds every reference in it whether or not it is read.
       */
      struct Discard final : Elite::TextSink
      {
        void Put(std::uint8_t) override {}
      } discard;

      Elite::Universe universe;
      NullSeams nulls;
      Elite::CharacterPrinter characters{discard, universe.sentences};
      Elite::TokenPrinter printer{characters, universe.text};
      Elite::ExtendedTokenPrinter extended{characters, printer, universe.rng};
      Elite::SidWriteLog sid;
      Elite::Ports ports{printer,  characters, characters, sid,
                         extended, nulls, board, nulls};

      std::uint32_t recentredByStick = 0;
      std::uint32_t bigRollRequests = 0;
      std::uint32_t clampedSpeed = 0;
      std::uint32_t autopilotRan = 0;
      std::uint32_t pressedFaster = 0, pressedSlower = 0, pressedRollLeft = 0, pressedRollRight = 0, pressedPitchUp = 0, pressedPitchDown = 0;

      for (const Case& item : cases)
      {
        /*
         * 6502: the bubble on both machines -- the planet in slot 0, the station the case places
         * in slot 1 (or not), `MANY` counting it (which is `SSPR`), `K3+10` and `RAND`. `DOKEY`
         * builds `INWK` itself.
         */
        const Approach& approach = APPROACHES[item.approach];
        universe.bubble.slots.fill(0u);
        universe.bubble.counts.fill(0u);
        universe.bubble.slots[0] = 128u;
        for (std::size_t slot = 0; slot < 2u; ++slot)
        {
          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> blockBytes{};
          universe.bubble.blocks[slot] = Elite::Ship::FromBytes(blockBytes);
        }
        universe.bubble.blocks[0].z.hi = 0x60u; // the planet, a way off ahead, for `GOPL` to aim at
        universe.bubble.blocks[0].y.hi = 0x08u;
        if (approach.station)
        {
          universe.bubble.slots[1] = Elite::Byte(Elite::ShipType::Station);
          universe.bubble.counts[Elite::Byte(Elite::ShipType::Station)] = 1u;
          Elite::Ship& station = universe.bubble.blocks[1];
          station.x.lo = approach.xLo;
          station.x.hi = approach.xHi;
          station.x.sgn = approach.xSign;
          station.y.lo = approach.yLo;
          station.y.hi = approach.yHi;
          station.y.sgn = approach.ySign;
          station.z.lo = approach.zLo;
          station.z.hi = approach.zHi;
          station.z.sgn = approach.zSign;
          station.nose.x.hi = approach.noseX;
          station.nose.y.hi = approach.noseY;
          station.nose.z.hi = approach.noseZ;
          station.roof.x.hi = approach.roofX;
          station.roof.y.hi = approach.roofY;
          station.roof.z.hi = approach.roofZ;
          station.side.x.hi = 0x60u;
          station.side.z.hi = 0x08u;
        }
        for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
        {
          cpu.memory[static_cast<std::uint16_t>(frin + slot)] = universe.bubble.slots[slot];
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] =
              universe.bubble.blocks[slot].ToBytes()[byte];
          }
        }
        for (std::size_t kind = 0; kind < universe.bubble.counts.size(); ++kind)
        {
          cpu.memory[static_cast<std::uint16_t>(many + kind)] = universe.bubble.counts[kind];
        }
        universe.geometry.xx2[10] = item.faces;
        cpu.memory[static_cast<std::uint16_t>(k3 + 10u)] = item.faces;
        const std::array<std::uint8_t, 4> seed = {0x3Cu, 0xA5u, 0x5Au, 0xC3u};
        for (std::size_t byte = 0; byte < seed.size(); ++byte)
        {
          cpu.memory[static_cast<std::uint16_t>(rand + byte)] = seed[byte];
        }
        universe.rng.SetState(seed);
        // 6502: RAT, RAT2, CNT2 -- `DOCKIT` writes all three on entry, so a value nothing else
        // writes is how "the autopilot ran" is read off the state on both sides.
        cpu.memory[rat] = 0x55u;
        cpu.memory[rat2] = 0x55u;
        cpu.memory[cnt2] = 0x55u;

        Elite::KeyLogger keys{};
        keys[Elite::KEY_ROLL_LEFT] = ((item.keys & 1u) != 0u) ? 0xFFu : 0u;
        keys[Elite::KEY_ROLL_RIGHT] = ((item.keys & 2u) != 0u) ? 0xFFu : 0u;
        keys[Elite::KEY_PITCH_UP] = ((item.keys & 4u) != 0u) ? 0xFFu : 0u;
        keys[Elite::KEY_PITCH_DOWN] = ((item.keys & 8u) != 0u) ? 0xFFu : 0u;

        // 6502: KEYLOOK -- full of last frame's presses on both sides, which the scan must clear;
        // the keys the case holds are pressed on the CIA, and the port's keyboard holds the same.
        for (std::size_t slot = 0; slot < keys.size(); ++slot)
        {
          cpu.memory[static_cast<std::uint16_t>(klo + slot)] = 0xFFu;
        }
        HoldOnMatrix(cpu, keys);

        Elite::Ship work{};
        std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          // A block that is not already zero, so `ZINF` has something to clear.
          shipBytes[byte] = static_cast<std::uint8_t>(0x5Au + byte);
          cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = shipBytes[byte];
        }
        work = Elite::Ship::FromBytes(shipBytes);

        cpu.memory[autoPilot] = item.docking;
        cpu.memory[jstk] = item.joystick;
        cpu.memory[djd] = item.recentre;
        cpu.memory[jstx] = item.roll;
        cpu.memory[jsty] = item.pitch;
        cpu.memory[delta] = item.delta;
        cpu.memory[type] = 0u;

        cpu.ClearTrapHits();
        const Elite::Testing::RunResult run = cpu.CallSubroutine(dokey, 20'000);
        Assert::IsTrue(run.completed, L"DOKEY reached .ant");

        // ---- the port -------------------------------------------------------------------------
        board.held = item.keys;
        board.scans = 0;

        universe.keys.fill(0xFFu);
        universe.view = 0u; // 6502: QQ11 -- the space view, where `RDKEY` forgets nothing

        universe.control = Elite::ControlState{};
        universe.control.roll = item.roll;
        universe.control.pitch = item.pitch;
        universe.control.dockingComputer = item.docking;

        universe.options = Elite::ControlOptions{};
        universe.options.recentreDisabled = item.recentre;
        universe.options.joystick = item.joystick;

        universe.flight = Elite::FlightState{};
        universe.flight.delta = item.delta;
        universe.flight.type = Elite::ShipType::None;
        universe.flight.rat = 0x55u;
        universe.flight.rat2 = 0x55u;
        universe.flight.steerCone = 0x55u;

        universe.work = work;

        Elite::ReadFlightControls(universe, ports);

        Elite::ControlState& control = universe.control;
        Elite::FlightState& flight = universe.flight;
        work = universe.work;
        keys = universe.keys;

        const std::wstring where = Widen("DOKEY(auto " + std::to_string(item.docking) + ", JSTK " + std::to_string(item.joystick) +
                                         ", DJD " + std::to_string(item.recentre) + ", JSTX " + std::to_string(item.roll) + ", JSTY " +
                                         std::to_string(item.pitch) + ", keys " + std::to_string(item.keys) + ", DELTA " +
                                         std::to_string(item.delta) + ", " + approach.what + ", faces " + std::to_string(item.faces) + ")");

        Assert::AreEqual(cpu.memory[jstx], control.roll, (where + L": JSTX").c_str());
        Assert::AreEqual(cpu.memory[jsty], control.pitch, (where + L": JSTY").c_str());
        Assert::AreEqual(cpu.memory[delta], flight.delta, (where + L": DELTA").c_str());
        Assert::AreEqual(cpu.memory[type], Elite::Byte(flight.type), (where + L": TYPE").c_str());
        Assert::AreEqual(cpu.memory[rat], flight.rat, (where + L": RAT").c_str());
        Assert::AreEqual(cpu.memory[rat2], flight.rat2, (where + L": RAT2").c_str());
        Assert::AreEqual(cpu.memory[cnt2], flight.steerCone, (where + L": CNT2").c_str());

        for (std::size_t slot = 0; slot < keys.size(); ++slot)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(klo + slot)], keys[slot],
                           (where + L": KLO+" + std::to_wstring(slot)).c_str());
        }
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + byte)], work.ToBytes()[byte],
                           (where + L": INWK+" + std::to_wstring(byte)).c_str());
        }

        Assert::AreEqual<std::uint32_t>(1u, board.scans, (where + L": one keyboard scan").c_str());
        Assert::AreEqual(item.docking != 0u, flight.rat2 != 0x55u, (where + L": the autopilot ran only when it is on").c_str());
        autopilotRan += (flight.rat2 != 0x55u) ? 1u : 0u;
        if (item.docking != 0u)
        {
          pressedFaster += (keys[Elite::KEY_SPEED_UP] != 0u) ? 1u : 0u;
          pressedSlower += (keys[Elite::KEY_SLOW_DOWN] != 0u) ? 1u : 0u;
          pressedRollLeft += (keys[Elite::KEY_ROLL_LEFT] != 0u) ? 1u : 0u;
          pressedRollRight += (keys[Elite::KEY_ROLL_RIGHT] != 0u) ? 1u : 0u;
          pressedPitchUp += (keys[Elite::KEY_PITCH_UP] != 0u) ? 1u : 0u;
          pressedPitchDown += (keys[Elite::KEY_PITCH_DOWN] != 0u) ? 1u : 0u;
        }

        if (item.docking == 0u && item.joystick != 0u && item.keys == 0u)
        {
          recentredByStick += (control.roll == 128u && control.pitch == 128u) ? 1u : 0u;
        }
        bigRollRequests += (item.docking != 0u && control.roll == 64u) ? 1u : 0u;
        clampedSpeed += (item.docking != 0u && flight.delta == 22u) ? 1u : 0u;
      }

      Logger::WriteMessage(("DOKEY: " + std::to_string(cases.size()) + " cases; the autopilot ran " + std::to_string(autopilotRan) +
                            " times and pressed faster/slower/left/right/up/down " + std::to_string(pressedFaster) + "/" +
                            std::to_string(pressedSlower) + "/" + std::to_string(pressedRollLeft) + "/" + std::to_string(pressedRollRight) +
                            "/" + std::to_string(pressedPitchUp) + "/" + std::to_string(pressedPitchDown) + ", wrote JSTX directly " +
                            std::to_string(bigRollRequests) + ", clamped the speed " + std::to_string(clampedSpeed) + "\n")
                             .c_str());
      Assert::IsTrue(cases.size() > 1'500u, L"the sweep is worth its name"); // 3,840 with a stubbed DOCKIT; the real one is the depth now
      Assert::IsTrue(recentredByStick > 0u, L"the joystick's spring-back fired");
      Assert::IsTrue(bigRollRequests > 0u, L"the autopilot's direct write to JSTX fired");
      Assert::IsTrue(clampedSpeed > 0u, L"and its speed was clamped at 22");
      // §6.36's rule for the autopilot's six synthetic presses: a sweep in which `DOCKIT` never
      // asked for one of them would agree with the game about not pressing it.
      Assert::IsTrue(pressedFaster > 0u && pressedSlower > 0u, L"the autopilot asked for both speeds");
      Assert::IsTrue(pressedRollLeft > 0u && pressedRollRight > 0u, L"and both rolls");
      Assert::IsTrue(pressedPitchUp > 0u && pressedPitchDown > 0u, L"and both pitches");
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
     * a register would corrupt the blueprints for every case after it (§6.76).
     *
     * It also means the two register writes can be READ BACK, which is what makes them comparable
     * at all. The port puts them behind a seam, the oracle catches them as memory, and the two are
     * compared against each other.
     *
     * `SETL1` RUNS ON BOTH SIDES since M3-b-3a and is trapped on neither, so the bracket is a
     * `MemoryMap` rather than a list of calls -- and one byte still tells no call, one call and
     * both apart, because the two modes differ. The assertion says how.
     */
    TEST_METHOD(TheLaserSightsMatchSIGHT)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t sight = oracle.Label("SIGHT");
      const std::uint16_t laserBase = oracle.Label("LASER");
      const std::uint16_t view = oracle.Label("VIEW");
      const std::uint16_t tribble = oracle.Label("TRIBBLE");
      const std::uint16_t tribct = oracle.Label("TRIBCT");
      const std::uint16_t t = oracle.Label("T");
      const std::uint16_t screen = ScreenBase(oracle);

      // 6502: VIC, which is &D000 -- the same address the ship blueprints load at.
      const std::uint16_t vicColour = 0xD027u;
      const std::uint16_t vicEnable = 0xD015u;

      // 6502: l1 -- a seed whose top five bits are non-zero, so that "SETL1 left them alone" is a
      // statement with something in it. %100 in the bottom three is where the bracket ends.
      constexpr std::uint8_t PORT_SEED = 0xE7;
      constexpr std::uint8_t PORT_AFTER_IN = (PORT_SEED & 0xF8u) | Elite::MEMORY_MAP_IO;
      constexpr std::uint8_t PORT_AFTER_BOTH = (PORT_SEED & 0xF8u) | Elite::MEMORY_MAP_RAM;

      const std::vector<Elite::Laser> LASERS = {
        Elite::LASER_NONE, // none fitted on this view
        Elite::LASER_PULSE,
        Elite::LASER_BEAM,
        Elite::LASER_MILITARY,
        Elite::LASER_MINING, // which nothing tests for
        Elite::Laser{99},    // a power the game does not have, which gets the same sprite
      };
      const std::vector<std::uint8_t> POPULATIONS = {0, 0x0F, 0x10, 0x2F, 0x60, 0x70, 0x80, 0xFF};

      std::uint32_t compared = 0;
      std::set<std::uint32_t> pointers;
      std::set<std::uint32_t> masks;

      for (const Elite::Laser laser : LASERS)
      {
        for (std::uint8_t which = 0; which < 4u; ++which)
        {
          for (const std::uint8_t population : POPULATIONS)
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::Canvas canvas;

            // 6502: l1 (&0001) -- every write the routine makes to the port register, in order.
            // `SETL1` is not trapped since M3-b-3a: the port runs it too.
            cpu.LogStores(0x0001u, 0x0001u);

            FillScreens(cpu, canvas, screen, 0x6Du);

            Elite::Commander commander;
            for (std::uint8_t slot = 0; slot < 4u; ++slot)
            {
              // A different laser on every other view, so a port that ignored VIEW would be caught.
              const Elite::Laser fitted = (slot == which) ? laser : Elite::LASER_BEAM;
              commander.lasers[slot] = fitted;
              cpu.memory[static_cast<std::uint16_t>(laserBase + slot)] = fitted.byte;
            }

            commander.tribbles.lo = 0x77u;
            commander.tribbles.hi = population;
            cpu.memory[tribble] = 0x77u;
            cpu.memory[static_cast<std::uint16_t>(tribble + 1)] = population;

            cpu.memory[view] = which;
            cpu.memory[tribct] = 0x9Cu;
            cpu.memory[t] = 0x9Cu;
            cpu.Io(vicColour) = 0x00u;
            cpu.Io(vicEnable) = 0x00u;
            cpu.memory[0x0001u] = PORT_SEED; // 6502: l1 -- see the bracket assertion below

            const Elite::Testing::RunResult run = cpu.CallSubroutine(sight, 5'000);
            Assert::IsTrue(run.completed, L"SIGHT returned");

            Elite::TrumbleSprites trumbles;
            trumbles.count = 0x9Cu;

            // 6502: VIC+&27 and VIC+&15 -- seeded with the same marker as the oracle's, so "left
            // alone" and "written with zero" are different answers on both sides.
            Elite::VideoState video{};
            video.colour[0] = Elite::Colour::Black;
            video.enabled = 0x00u;
            Elite::MemoryMap map;
            map.port = PORT_SEED;

            Elite::DrawLaserSights(canvas, commander, trumbles, which, video, map);

            const std::wstring where = Widen("SIGHT(laser " + std::to_string(laser.byte) + " on view " + std::to_string(which) + ", Trumbles " +
                                             std::to_string(population) + ")");

            CompareScreens(cpu, screen, canvas, where);
            Assert::AreEqual(cpu.memory[tribct], trumbles.count, (where + L": TRIBCT").c_str());
            // `T` is `SIGHT`'s own since M2-c-3 -- one if a laser was found, zero if not -- and
            // what it produced is the sprite-enable byte compared on the next line.
            Assert::AreEqual(cpu.Io(vicEnable), video.enabled, (where + L": VIC+&15").c_str());

            // The colour register is only written when a laser was found, so a case with none
            // leaves the marker on BOTH sides rather than a colour.
            // The register takes four bits, so the two sides are compared through the same latch
            // the chip applies -- `sightcol` never sets the others, and a byte that did would be
            // the game's business rather than a difference (slice 5a).
            Assert::AreEqual<std::uint32_t>(Elite::ColourIndex(Elite::ColourOf(cpu.Io(vicColour))),
                                            Elite::ColourIndex(video.colour[0]), (where + L": VIC+&27").c_str());
            if (!laser.Fitted())
            {
              Assert::AreEqual<std::uint32_t>(0u, cpu.Io(vicColour), (where + L": and neither wrote it").c_str());
            }

            /*
             * 6502: SETL1 -- the bracket, as one byte instead of two counted calls (M3-b-3a).
             *
             * This used to compare the port's two `SetRasterMode` calls against the oracle's two
             * trap hits. Both machines run `SETL1` now, and the byte it leaves says as much: the
             * two modes DIFFER, so no call leaves the seed, one leaves %101 and both leave %100.
             * The top five bits are the datasette's and neither call touches them, which is why the
             * seed is not zero. What a final byte cannot distinguish is a doubled bracket, and the
             * oracle's own store log below is what pins the shipped routine to exactly two.
             */
            Assert::AreEqual(cpu.memory[0x0001u], map.port, (where + L": l1 -- the map both machines end on").c_str());
            Assert::AreEqual<std::uint32_t>(PORT_AFTER_BOTH, map.port, (where + L": in and back out").c_str());
            // The values are the COMPOSED byte and not the mode: `SETL1`'s store is `LDA l1 /
            // AND #%11111000 / ORA L1M / STA l1`, so the datasette bits ride along.
            Assert::AreEqual<std::size_t>(2u, cpu.stores.size(), (where + L": the game switched the map twice").c_str());
            Assert::AreEqual<std::uint32_t>(PORT_AFTER_IN, cpu.stores[0].value, (where + L": the way in").c_str());
            Assert::AreEqual<std::uint32_t>(PORT_AFTER_BOTH, cpu.stores[1].value, (where + L": and out").c_str());

            pointers.insert(canvas.Read(Elite::SIGHT_SPRITE_CELL));
            masks.insert(video.enabled);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 4u * 8u, compared, L"the whole sweep ran");
      Assert::IsTrue(pointers.size() >= 4u, L"every laser got its own sprite");
      Assert::IsTrue(masks.size() >= 6u, L"and the Trumble population moved the enable mask");
    }

    /*
     * 6502: TT17's `TJ1` path -- the crosshair steps, against the shipped routine.
     *
     * EXHAUSTIVE IN THE ONLY DIMENSION IT HAS. Five key-logger entries decide the answer -- the two
     * cursor keys, the two SHIFTs and RETURN -- so all thirty-two combinations of them are the
     * whole input space, and X and Y coming back are the whole output.
     *
     * `TT17` calls `DOKEY` first and that is left to run: with `auto` at zero its docking-computer
     * half is skipped, and what remains touches `JSTX` and `JSTY` rather than anything read here.
     * `JSTK` is zero because a keyboard player's is -- the title screen sets it the moment you
     * dismiss it with a key -- and that is what sends the routine down `TJ1` rather than into the
     * joystick path.
     */
    TEST_METHOD(TheCrosshairKeysMatchTT17)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t tt17 = oracle.Label("TT17");
      const std::uint16_t klo = oracle.Label("KLO");
      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t qq11 = oracle.Label("QQ11");
      const std::uint16_t jstk = oracle.Label("JSTK");
      const std::uint16_t autoPilot = oracle.Label("auto");

      const std::size_t WATCHED[] = {
        Elite::KEY_CURSOR_X, Elite::KEY_CURSOR_Y, Elite::KEY_SHIFT_LEFT, Elite::KEY_SHIFT_RIGHT, Elite::KEY_CROSSHAIR_FAST,
      };

      std::uint32_t moved = 0;

      for (std::uint32_t held = 0; held < 32u; ++held)
      {
        Cpu6502 cpu = oracle.Fresh();

        Elite::KeyLogger keys{};
        for (std::size_t index = 0; index < 5u; ++index)
        {
          keys[WATCHED[index]] = ((held & (1u << index)) != 0u) ? 0xFFu : 0u;
        }

        // 6502: KEYLOOK -- last frame's presses, which `RDKEY` clears before the walk fills it
        // from the keys this case presses on the CIA.
        for (std::size_t slot = 0; slot < keys.size(); ++slot)
        {
          cpu.memory[static_cast<std::uint16_t>(klo + slot)] = 0xFFu;
        }
        HoldOnMatrix(cpu, keys);

        // A chart is showing, nothing is flying the ship, and the player is on the keyboard.
        cpu.memory[qq11] = Elite::LONG_RANGE_CHART_VIEW;
        cpu.memory[jstk] = 0u;
        cpu.memory[autoPilot] = 0u;
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = 0u;
        }

        /*
         * `RDKEY` WAS TRAPPED HERE until M6-0-a-4, and finding out why cost a failing assertion:
         * `TT17` calls `DOKEY`, `DOKEY` opens with `JSR RDKEY`, and `RDKEY` walks the CIA -- which
         * in a flat 64 KB image was whatever bytes happened to sit at the I/O addresses, so the
         * first run of this test compared the port against a keyboard with keys held down that
         * nothing had pressed. The CIA is a matrix in `Cpu6502` now and the walk runs for real over
         * the keys this case holds; `ReadCrosshairKeys` reads the logger the port's scan would
         * have built from the same keys.
         */
        const Elite::Testing::RunResult run = cpu.CallSubroutine(tt17, 20'000);
        Assert::IsTrue(run.completed, L"TT17 returned");

        const Elite::CrosshairStep step = Elite::ReadCrosshairKeys(keys);

        const std::wstring where =
          Widen("TT17(cursor x " + std::to_string((held & 1u) != 0u) + ", cursor y " + std::to_string((held & 2u) != 0u) + ", shift " +
                std::to_string((held & 12u) != 0u) + ", return " + std::to_string((held & 16u) != 0u) + ")");

        Assert::AreEqual<std::uint32_t>(cpu.x, step.x, (where + L": X").c_str());
        Assert::AreEqual<std::uint32_t>(cpu.y, step.y, (where + L": Y").c_str());

        moved += (step.x != 0u || step.y != 0u) ? 1u : 0u;
      }

      // Two thirds of the sweep press a cursor key, and a sweep where nothing ever moved would
      // agree with the game about doing nothing -- §6.36's rule, applied to a five-bit input.
      Assert::AreEqual<std::uint32_t>(24u, moved, L"the cases that press a cursor key all moved");
    }
  };

} // namespace GameLogicTests
