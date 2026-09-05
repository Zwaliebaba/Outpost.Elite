#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "Scanner.h"
#include "ShipBlueprint.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ShipSlot.h"
#include "Tactics.h"

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Slice 4a-a: the vectors `TACTICS` and `DOCKIT` decide with.
 *
 * Six routines of pure arithmetic, so every one of them is compared the strongest way available --
 * against the shipped code on every byte it writes, over a sweep chosen to reach both sides of
 * each sign test rather than to be large. Sign-magnitude is where this port has been wrong most
 * often (§6.42, §6.53, §6.68, §6.87, §6.117), and all six of these routines are sign-magnitude.
 *
 * `V(1 0)` IS AN ARGUMENT AND THE PORT DOES NOT HAVE ONE. `TAS1` reads through a zero-page pointer
 * and the port takes a `ShipBlock` instead, so each case writes the other object into a real ship
 * slot and points `V` at it -- which is what both callers do anyway (`VCSU1` at the station's slot,
 * `TACTICS` at whichever slot `UNIV` names).
 */
namespace GameLogicTests
{

  namespace
  {
    // `OracleMissing` and `Widen` come from FlightWorld.h, which this file needs for `World`.

    struct Labels
    {
      std::uint16_t inwk = 0, k3 = 0, kPercent = 0, v = 0, x1 = 0, y1 = 0, x2 = 0;
      std::uint16_t q = 0, r = 0, s = 0, u = 0, k = 0;
      std::uint16_t frin = 0, many = 0, rand = 0, inf = 0, xx0 = 0, type = 0, ecma = 0, fist = 0, slsp = 0;
      std::uint16_t cnt = 0, cnt2 = 0, rat = 0, rat2 = 0, junk = 0;
      std::uint16_t energy = 0, fsh = 0, ash = 0, dly = 0;

      explicit Labels(const OracleImage& _oracle)
      {
        frin = _oracle.Label("FRIN");
        many = _oracle.Label("MANY");
        rand = _oracle.Label("RAND");
        inf = _oracle.Label("INF");
        xx0 = _oracle.Label("XX0");
        type = _oracle.Label("TYPE");
        ecma = _oracle.Label("ECMA");
        fist = _oracle.Label("FIST");
        slsp = _oracle.Label("SLSP");
        energy = _oracle.Label("ENERGY");
        fsh = _oracle.Label("FSH");
        ash = _oracle.Label("ASH");
        dly = _oracle.Label("DLY");
        cnt = _oracle.Label("CNT");
        cnt2 = _oracle.Label("CNT2");
        rat = _oracle.Label("RAT");
        rat2 = _oracle.Label("RAT2");
        junk = _oracle.Label("JUNK");
        inwk = _oracle.Label("INWK");
        k3 = _oracle.Label("K3");
        kPercent = _oracle.Label("K%");
        v = _oracle.Label("V");
        x1 = _oracle.Label("X1");
        y1 = _oracle.Label("Y1");
        x2 = _oracle.Label("X2");
        q = _oracle.Label("Q");
        r = _oracle.Label("R");
        s = _oracle.Label("S");
        u = _oracle.Label("U");
        k = _oracle.Label("K");
      }
    };

    /// The second ship block, which is where `NWSPS` puts the station and where `VCSU1` looks.
    [[nodiscard]] std::uint16_t StationBlock(const Labels& _at)
    {
      return static_cast<std::uint16_t>(_at.kPercent + Elite::SHIP_BLOCK_SIZE);
    }

    /*
     * A spread of sign-magnitude bytes: zero, small, the bit that decides a shift, the sign on its
     * own, and both extremes. Six values per axis is what makes a three-axis sweep affordable while
     * still crossing every branch these routines have.
     */
    constexpr std::array<std::uint8_t, 6> SPREAD = {0x00u, 0x01u, 0x40u, 0x7Fu, 0x80u, 0xFFu};

    void WriteBlock(Cpu6502& _cpu, std::uint16_t _base, const Elite::ShipBlock& _block)
    {
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        _cpu.memory[static_cast<std::uint16_t>(_base + byte)] = _block[byte];
      }
    }

    void WriteAxes(Cpu6502& _cpu, const Labels& _at, const Elite::K3Block& _axes)
    {
      for (std::size_t byte = 0; byte < _axes.size(); ++byte)
      {
        _cpu.memory[static_cast<std::uint16_t>(_at.k3 + byte)] = _axes[byte];
      }
    }

    void CompareAxes(const Cpu6502& _cpu, const Labels& _at, const Elite::K3Block& _axes, const std::wstring& _where)
    {
      for (std::size_t byte = 0; byte < _axes.size(); ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.k3 + byte)], _axes[byte],
                         (_where + L": K3+" + std::to_wstring(byte)).c_str());
      }
    }
  } // namespace

  TEST_CLASS(TheTacticsVectors)
  {
  public:
    /*
     * 6502: VCSUB, and `TAS1` three times inside it.
     *
     * The two are swept together rather than apart because `TAS1` alone cannot be wrong in a way
     * `VCSUB` hides: the three calls touch disjoint bytes. What the sweep is really asking about is
     * `MVT3`'s sign-magnitude subtraction reached from a new caller -- the same arithmetic slice 3a
     * verified from `MV40`, with the operands the other way round.
     *
     * ALL TEN BYTES OF `K3` ARE COMPARED, including the tenth that nothing here writes: a routine
     * that scribbled on `TAS2`'s shift counter would break the compass three slices away.
     */
    TEST_METHOD(TheVectorToAnotherShipMatchesVCSUB)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t vcsub = oracle.Label("VCSUB");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;

      for (const std::uint8_t mineHigh : SPREAD)
      {
        for (const std::uint8_t theirHigh : SPREAD)
        {
          for (const std::uint8_t mineSign : {0x00u, 0x80u})
          {
            for (const std::uint8_t theirSign : {0x00u, 0x80u})
            {
              Elite::ShipBlock mine{};
              Elite::ShipBlock theirs{};

              // Three axes at once, each with a different low byte, so a routine that used one
              // axis's bytes for another's would not agree by luck.
              for (std::size_t axis = 0; axis < 3u; ++axis)
              {
                const std::size_t base = axis * 3u;
                mine[base] = static_cast<std::uint8_t>(0x11u * (axis + 1u));
                mine[base + 1u] = mineHigh;
                mine[base + 2u] = static_cast<std::uint8_t>(mineSign);
                theirs[base] = static_cast<std::uint8_t>(0x37u * (axis + 1u));
                theirs[base + 1u] = theirHigh;
                theirs[base + 2u] = static_cast<std::uint8_t>(theirSign);
              }

              Elite::K3Block axes{};
              axes[9] = 0xA5u; // scribbled, so a write to the tenth byte would be visible

              WriteBlock(cpu, at.inwk, mine);
              WriteBlock(cpu, StationBlock(at), theirs);
              WriteAxes(cpu, at, axes);

              // 6502: what `VCSU1` does before jumping in -- V(1 0) = the other ship's block.
              cpu.memory[at.v] = static_cast<std::uint8_t>(StationBlock(at) & 0xFFu);
              cpu.a = static_cast<std::uint8_t>(StationBlock(at) >> 8u);

              const Elite::Testing::RunResult run = cpu.CallSubroutine(vcsub, 20'000);
              Assert::IsTrue(run.completed, L"VCSUB returned");

              Elite::MathWorkspace math;
              Elite::SubtractShipAxes(theirs, mine, axes, math);

              const std::wstring where = WidenText("VCSUB mine " + std::to_string(mineHigh) + "/" + std::to_string(mineSign) + " theirs " +
                                                   std::to_string(theirHigh) + "/" + std::to_string(theirSign));
              CompareAxes(cpu, at, axes, where);

              // 6502: K+1 to K+3 -- the scratch `TAS1` leaves behind, which `TACTICS` does not read
              // and which says the subtraction went through `MVT3` rather than round it.
              for (std::size_t byte = 1; byte < 4u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.k + byte)], math.k[byte],
                                 (where + L": K+" + std::to_wstring(byte)).c_str());
              }
              Assert::AreEqual(cpu.memory[at.u], math.u, (where + L": U").c_str());
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 6u * 2u * 2u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: TAS3 and TAS4 -- one body, two labels, and the sweep runs both.
     *
     * The answer is `MAD`'s (A X), so both bytes are compared and so is `S` and `R`: the routine
     * stages its running total through them, and a port that returned the right pair while leaving
     * the wrong scratch would be caught here rather than by whichever caller reads `R` next.
     */
    TEST_METHOD(TheDotProductMatchesTAS3AndTAS4)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t tas3 = oracle.Label("TAS3");
      const std::uint16_t tas4 = oracle.Label("TAS4");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;

      const std::uint8_t VECTORS[] = {Elite::ORIENTATION_NOSE, Elite::ORIENTATION_ROOF, Elite::ORIENTATION_SIDE};

      for (const std::uint8_t vx : SPREAD)
      {
        for (const std::uint8_t vy : SPREAD)
        {
          for (const std::uint8_t sx : SPREAD)
          {
            for (const std::uint8_t which : VECTORS)
            {
              for (int station = 0; station < 2; ++station)
              {
                Elite::ShipBlock block{};
                block[which] = vx;
                block[which + 2u] = vy;
                block[which + 4u] = sx;

                Elite::DrawWorkspace draw;
                draw.x1 = sx;
                draw.y1 = vx;
                draw.x2 = vy;

                const std::uint16_t base = (station != 0) ? StationBlock(at) : at.inwk;
                WriteBlock(cpu, base, block);
                cpu.memory[at.x1] = draw.x1;
                cpu.memory[at.y1] = draw.y1;
                cpu.memory[at.x2] = draw.x2;
                cpu.memory[at.q] = 0x5Au;
                cpu.memory[at.r] = 0x5Au;
                cpu.memory[at.s] = 0x5Au;

                cpu.y = which;
                const Elite::Testing::RunResult run = cpu.CallSubroutine((station != 0) ? tas4 : tas3, 20'000);
                Assert::IsTrue(run.completed, L"the dot product returned");

                Elite::MathWorkspace math;
                math.q = 0x5Au;
                math.r = 0x5Au;
                math.s = 0x5Au;
                const Elite::AddSignedResult got = Elite::DotProductWithShip(block, draw, math, which);

                const std::wstring where = WidenText(std::string(station != 0 ? "TAS4" : "TAS3") + " vector " + std::to_string(which) +
                                                     " (" + std::to_string(vx) + "," + std::to_string(vy) + "," + std::to_string(sx) + ")");
                Assert::AreEqual(cpu.a, got.high, (where + L": A").c_str());
                Assert::AreEqual(cpu.x, got.low, (where + L": X").c_str());
                Assert::AreEqual(cpu.memory[at.q], math.q, (where + L": Q").c_str());
                Assert::AreEqual(cpu.memory[at.r], math.r, (where + L": R").c_str());
                Assert::AreEqual(cpu.memory[at.s], math.s, (where + L": S").c_str());
                ++compared;
              }
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 6u * 6u * 3u * 2u, compared, L"the whole sweep ran");
    }

    /// 6502: TAS6 -- three sign bits and nothing else, so the sweep is every combination of them
    /// over a magnitude that would show through if a byte were negated arithmetically instead.
    TEST_METHOD(TheNegationMatchesTAS6)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t tas6 = oracle.Label("TAS6");

      Cpu6502 cpu = oracle.Fresh();

      for (const std::uint8_t x : SPREAD)
      {
        for (const std::uint8_t y : SPREAD)
        {
          for (const std::uint8_t z : SPREAD)
          {
            cpu.memory[at.x1] = x;
            cpu.memory[at.y1] = y;
            cpu.memory[at.x2] = z;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(tas6, 2'000);
            Assert::IsTrue(run.completed, L"TAS6 returned");

            Elite::DrawWorkspace draw;
            draw.x1 = x;
            draw.y1 = y;
            draw.x2 = z;
            Elite::NegateVector(draw);

            const std::wstring where = WidenText("TAS6 " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
            Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": XX15").c_str());
            Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": XX15+1").c_str());
            Assert::AreEqual(cpu.memory[at.x2], draw.x2, (where + L": XX15+2").c_str());
          }
        }
      }
    }

    /*
     * 6502: DCS1 -- the routine that runs twice by calling itself.
     *
     * THIS IS THE ONE TEST THAT COULD NOT BE WRITTEN FROM THE HEADER. The upstream summary says
     * `K3 = K3 - nosev * 4`; the code subtracts `nosev * 2` and gets the four from the `JSR P%+3`
     * above it. Both readings write the same bytes when the port implements whichever one it
     * believes, so only the shipped routine can say which is right -- and a sweep that crosses the
     * sign test in `TAS7` is what makes the difference visible, because a doubled vector and a
     * quadrupled one borrow differently.
     */
    TEST_METHOD(TheDockingOffsetMatchesDCS1)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t dcs1 = oracle.Label("DCS1");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;

      for (const std::uint8_t nose : SPREAD)
      {
        for (const std::uint8_t low : SPREAD)
        {
          for (const std::uint8_t high : SPREAD)
          {
            for (const std::uint8_t sign : {0x00u, 0x80u})
            {
              Elite::ShipBlock station{};

              // Three different nose bytes, so an axis reading another axis's would show.
              station[Elite::NOSE_VECTOR_X] = nose;
              station[Elite::NOSE_VECTOR_Y] = static_cast<std::uint8_t>(nose ^ 0x80u);
              station[Elite::NOSE_VECTOR_Z] = static_cast<std::uint8_t>(nose ^ 0x3Fu);

              Elite::K3Block axes{};
              for (std::size_t axis = 0; axis < 3u; ++axis)
              {
                const std::size_t base = axis * 3u;
                axes[base] = static_cast<std::uint8_t>(low + axis);
                axes[base + 1u] = high;
                axes[base + 2u] = static_cast<std::uint8_t>(sign);
              }
              axes[9] = 0xA5u;

              WriteBlock(cpu, StationBlock(at), station);
              WriteAxes(cpu, at, axes);

              const Elite::Testing::RunResult run = cpu.CallSubroutine(dcs1, 20'000);
              Assert::IsTrue(run.completed, L"DCS1 returned");

              Elite::Bubble bubble;
              bubble.blocks[1] = station;
              Elite::OffsetDockingPosition(bubble, axes);

              const std::wstring where = WidenText("DCS1 nose " + std::to_string(nose) + " K3 " + std::to_string(low) + "/" +
                                                   std::to_string(high) + "/" + std::to_string(sign));
              CompareAxes(cpu, at, axes, where);
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 6u * 6u * 2u, compared, L"the whole sweep ran");
    }
    /*
     * 6502: ANGRY -- and the sweep's job is the four branches, not the arithmetic.
     *
     * Bit 5 of `NEWB` (anger the station too), a zero AI byte (leave the ship entirely alone), and
     * `TYPE` on both sides of `CYL` are three tests a small sweep can cross exhaustively; the
     * fourth is the ship BEING the station, which returns before any of them. `TYPE` is set
     * independently of the type in A on purpose -- they are different bytes in the original and
     * the test is what says the port kept them different.
     */
    TEST_METHOD(TheAngerMatchesANGRY)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t angry = oracle.Label("ANGRY");
      const std::uint16_t inf = oracle.Label("INF");
      const std::uint16_t typeAt = oracle.Label("TYPE");

      const std::uint8_t NEWBS[] = {0x00u, 0x20u, 0x04u, 0x24u, 0xFFu};
      const std::uint8_t AI[] = {0x00u, 0x01u, 0x7Fu, 0x80u, 0xFEu};
      const std::uint8_t LOOP_TYPES[] = {0u, 1u, 10u, 11u, 29u, 128u};
      const std::uint8_t CALLED[] = {Elite::SHIP_TYPE_STATION, Elite::SHIP_TYPE_MISSILE, Elite::SHIP_TYPE_COBRA_MK3};

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;
      std::uint32_t angered = 0;

      for (const std::uint8_t newb : NEWBS)
      {
        for (const std::uint8_t ai : AI)
        {
          for (const std::uint8_t loopType : LOOP_TYPES)
          {
            for (const std::uint8_t called : CALLED)
            {
              constexpr std::uint8_t SLOT = 3;

              Elite::Bubble bubble;
              for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
              {
                for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
                {
                  bubble.blocks[slot][byte] = static_cast<std::uint8_t>(0x11u + slot * 7u + byte * 3u);
                }
              }
              bubble.blocks[SLOT][32] = ai;
              bubble.blocks[SLOT][36] = newb;

              /*
               * THE STATION'S HOSTILE BIT STARTS CLEAR, and the first version of this sweep did
               * not clear it. `SeedBubble`'s ramp gives slot 1 byte 36 the value &84, which
               * already has bit 2 set, so `AN2`'s `ORA #%00000100` changed nothing and a mutation
               * that skipped `AN2` altogether agreed on every case (§6.124).
               */
              bubble.blocks[1][36] = static_cast<std::uint8_t>(bubble.blocks[1][36] & ~Elite::NEWB_HOSTILE);

              for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
              {
                for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = bubble.blocks[slot][byte];
                }
              }

              const std::uint16_t block = static_cast<std::uint16_t>(at.kPercent + SLOT * Elite::SHIP_BLOCK_SIZE);
              cpu.memory[inf] = static_cast<std::uint8_t>(block);
              cpu.memory[static_cast<std::uint16_t>(inf + 1)] = static_cast<std::uint8_t>(block >> 8u);
              cpu.memory[typeAt] = loopType;

              cpu.a = called;
              const Elite::Testing::RunResult run = cpu.CallSubroutine(angry, 20'000);
              Assert::IsTrue(run.completed, L"ANGRY returned");

              Elite::FlightState flight;
              flight.type = loopType;
              Elite::Anger(bubble, flight, SLOT, called);

              const std::wstring where = WidenText("ANGRY NEWB " + std::to_string(newb) + " AI " + std::to_string(ai) + " TYPE " +
                                                   std::to_string(loopType) + " called " + std::to_string(called));
              for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
              {
                for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)],
                                   bubble.blocks[slot][byte],
                                   (where + L": K%+" + std::to_wstring(slot) + L"." + std::to_wstring(byte)).c_str());
                }
              }
              angered += ((bubble.blocks[1][36] & Elite::NEWB_HOSTILE) != 0u) ? 1u : 0u;
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(5u * 5u * 6u * 3u, compared, L"the whole sweep ran");

      // 6502: AN2 -- both answers, so a routine that never angered the station would be visible.
      Assert::IsTrue(angered > 0u, L"the station was angered on some cases");
      Assert::IsTrue(angered < compared, L"and left alone on others");
    }
  };

  /*
   * Slice 4a-c: the AI and the autopilot, which are one routine (§6.122).
   *
   * `TACTICS` is 330 instructions of branching over state that lives in nine different places, and
   * the only comparison worth making is the whole of it: the ship block, the bubble, the generator,
   * the flight state, the commander and the screen, after running the shipped routine and the port
   * from IDENTICAL starting conditions. Anything narrower would pass on the branch it happened to
   * take.
   *
   * THE SEAMS ARE TRAPPED RATHER THAN IMPLEMENTED. `OOPS`, `EXNO2`, `EXNO3`, `ECBLB2`, `NOISE` and
   * `MESS` are ported and reachable, but they touch the dashboard, the sound and the screen, and
   * running them inside a 6502 interpreter that has no dashboard would compare the wrong thing.
   * Each is trapped and COUNTED, and the counts are compared -- so a port that took the same branch
   * for a different reason still fails.
   */
  namespace
  {
    /*
     * The seams `TACTICS` reaches, counted rather than run.
     *
     * `OOPS`, `EXNO2`, `EXNO3`, `ECBLB2`, `NOISE` and `MESS` are all ported, and all of them draw
     * or make a noise. Running them inside the interpreter would compare a dashboard the fixture
     * does not have, so both sides are trapped and counted and the COUNTS are what agree.
     */
    struct CountingEffects final : Elite::FlightLoopEffects, Elite::ShipEffects, Elite::ShipDrawEffects
    {
      std::vector<std::uint8_t> sounds;
      std::vector<std::uint8_t> spawned;
      std::uint32_t trumbleMoves = 0;

      bool PlaySound(std::uint8_t _effect, bool) override
      {
        sounds.push_back(_effect);
        return true;
      }
      bool PlaySoundPitched(std::uint8_t _effect, std::uint8_t, std::uint8_t) override
      {
        sounds.push_back(_effect);
        return true;
      }
      void StopSound(std::uint8_t) override {}
      void MoveTrumbles() override
      {
        ++trumbleMoves;
      }
      void StartDockingMusic() override {}
      void StopDockingMusic() override {}
      bool SpawnAhead(std::uint8_t) override
      {
        return false;
      }
      void Anger(std::uint8_t) override {}
      bool SpawnChild(std::uint8_t, std::uint8_t _type) override
      {
        spawned.push_back(_type);
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

    /// Everything a `TACTICS` case has to put into both machines before it can be compared.
    struct Universe
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
      CountingEffects effects;

      std::array<std::uint8_t, 4> seed{};
      std::uint8_t ecm = 0;
      std::uint8_t legal = 0;
      std::uint8_t slot = 2;
    };

    /*
     * A bubble the AI can think about: the planet in slot 0, the station or the sun in slot 1, the
     * ship under test in slot 2, and a Cobra in slot 3 for a missile to chase.
     *
     * The positions are close enough that `MAS4` does not fold everything into "too far away",
     * because a sweep in which every ship is out of range compares one branch twenty times.
     */
    /*
     * The geometries a case is run through.
     *
     * ONE POSITION IS NOT ENOUGH and a mutation said so: `CNT2` is the cone `TACTICS` throttles
     * back inside, and with a single relative position the nose dot product never landed on it, so
     * changing 22 to 23 agreed everywhere (§6.126). These five put the ship in front, behind,
     * above, off to one side and nearly touching.
     */
    struct Geometry
    {
      const char* what;
      std::uint8_t x, xSign, y, ySign, z, zSign;

      /*
       * AND THE SHIP'S NOSE.
       *
       * `CNT` is the ship's nose dotted with the direction to it, and every decision in parts 6 and
       * 7 is made on that byte: whether it fires (160), whether it hits (163) and whether it
       * throttles back (`CNT2`). With one fixed orientation the sweep produced TWO values of `CNT`
       * in five hundred cases, so no ship ever fired its laser and three mutations in those
       * thresholds agreed everywhere (§6.126).
       */
      std::uint8_t noseX, noseY, noseZ;

      /*
       * AND THE ROLL COUNTER AND FLAGS, because zero makes two different operations agree.
       *
       * `TA11` skips the roll when `INWK+29` doubled is 32 or more, and `TN13` sets bit 7 of
       * `NEWB` with `ASL / SEC / ROR` -- a rotate, not an `ORA`. With both bytes zero the shift is
       * invisible (`(0 << 1) | 0x80` and `0 | 0x80` are the same answer) and the roll test only
       * ever sees one side, which is §6.124's "a bit that was already set" in a third form.
       */
      std::uint8_t roll, flags;
    };

    constexpr Geometry GEOMETRIES[] = {
      {"ahead", 0x20u, 0x00u, 0x10u, 0x00u, 0x28u, 0x00u, 0x60u, 0x10u, 0xE0u, 0x00u, 0x00u},
      {"behind", 0x20u, 0x80u, 0x10u, 0x00u, 0x28u, 0x80u, 0x60u, 0x10u, 0xE0u, 0x0Fu, 0x24u},
      {"above", 0x08u, 0x00u, 0x60u, 0x00u, 0x18u, 0x00u, 0x60u, 0x10u, 0xE0u, 0x10u, 0x41u},
      {"beside", 0x70u, 0x80u, 0x04u, 0x00u, 0x0Cu, 0x00u, 0x60u, 0x10u, 0xE0u, 0x1Fu, 0x12u},
      {"nearly touching", 0x01u, 0x00u, 0x01u, 0x80u, 0x02u, 0x00u, 0x60u, 0x10u, 0xE0u, 0x40u, 0x08u},

      // Aimed AT us, which is the only way a ship's laser ever fires: `CNT` has to pass 160, and
      // to HIT, 163.
      {"ahead and aiming at us", 0x20u, 0x00u, 0x10u, 0x00u, 0x28u, 0x00u, 0xE0u, 0x90u, 0x60u, 0x81u, 0x60u},
      {"close and aiming at us", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0xE0u, 0x90u, 0x60u, 0x2Au, 0x03u},
      {"close and aiming just off", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0xD0u, 0xA0u, 0x50u, 0x7Fu, 0x55u},
      {"close and nearly aimed", 0x06u, 0x00u, 0x03u, 0x00u, 0x08u, 0x00u, 0xE8u, 0x88u, 0x68u, 0x00u, 0x00u},

      /*
         * ANTI-PARALLEL, computed rather than guessed.
         *
         * `CNT` passes 160 only when the nose is opposite the direction to the ship on ALL THREE
         * axes in proportion, not merely opposite on one. For a ship at (4, 2, 6) the unit vector
         * is about (0.53, 0.27, 0.80), so the nose that points straight back at us is that times
         * 96 with every sign set: (&B3, &9A, &CD). The rows either side of it are the same
         * direction nudged, so the sweep straddles both 160 and 163 (§6.126).
         */
      {"close and pointing straight at us", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0xB3u, 0x9Au, 0xCDu, 0x0Fu, 0x24u},
      {"close and pointing nearly straight", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0xB0u, 0x9Au, 0xC8u, 0x10u, 0x41u},
      {"close and pointing a little wide", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0xA8u, 0x9Au, 0xC0u, 0x1Fu, 0x12u},
      {"close and pointing wider", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0xA0u, 0x98u, 0xB8u, 0x40u, 0x08u},
      {"very close and pointing at us", 0x02u, 0x00u, 0x01u, 0x00u, 0x03u, 0x00u, 0xB3u, 0x9Au, 0xCDu, 0x81u, 0x60u},

      /*
         * THREE ORIENTATIONS THAT LAND ON A THRESHOLD EXACTLY, found by search rather than by eye.
         *
         * `CNT` is compared against three constants -- 160 to fire, 163 to hit, and `CNT2` (22) to
         * throttle back -- and moving any of them by one is only observable when `CNT` is on the
         * boundary. The map from a nose vector to `CNT` runs through two dot products and a
         * normalisation, so it cannot be inverted by hand: these were measured by sweeping the nose
         * one step at a time against a fixed position and reading the answer (§6.126).
         */
      {"aimed so CNT is 159, one below firing", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0x9Du, 0x9Au, 0xCDu, 0x2Au, 0x03u},
      {"aimed so CNT is 162, one below hitting", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0xADu, 0x9Au, 0xCDu, 0x7Fu, 0x55u},
      {"aimed so CNT is 22, the throttle cone", 0x04u, 0x00u, 0x02u, 0x00u, 0x06u, 0x00u, 0x10u, 0x1Au, 0x38u, 0x00u, 0x00u},

      /*
         * ALL THREE HIGH BYTES ZERO, which is what `MAS4` means by "it has arrived".
         *
         * `TA34` reaches the fatal path only when the OR of the high bytes is zero, so once the
         * sweep moved its positions into the high bytes -- which is where the geometry belongs --
         * no missile ever landed and the death contract stopped being exercised. This row is the
         * case the others cannot reach (§6.126).
         */
      {"touching", 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x60u, 0x10u, 0xE0u, 0x0Fu, 0x24u}};

    void SeedTacticsUniverse(Cpu6502& _cpu, Universe& _world, const Labels& _at, std::uint8_t _type, std::uint8_t _stations,
                             std::uint8_t _thargoids, const Geometry& _where)
    {
      (void)_cpu;
      const std::uint8_t FLEET[] = {128u, 2u, 0u, 11u};

      for (std::size_t slot = 0; slot < 4u; ++slot)
      {
        const std::uint8_t type = (slot == 2u) ? _type : FLEET[slot];
        _world.world.bubble.slots[slot] = type;

        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          _world.world.bubble.blocks[slot][byte] = static_cast<std::uint8_t>(0x09u + slot * 5u + byte * 3u);
        }

        // The ship under test goes where the case asks; the others are spread around it so that a
        // missile's target and the station are somewhere distinct.
        const bool subject = (slot == 2u);

        // (low, high, sign) per axis, and the HIGH byte is the one that matters: see the comment on
        // `Geometry` above. The low bytes are varied too, so a routine reading the wrong one does
        // not agree by accident.
        _world.world.bubble.blocks[slot][0] = static_cast<std::uint8_t>(0x40u + slot * 11u);
        _world.world.bubble.blocks[slot][1] = subject ? _where.x : static_cast<std::uint8_t>(0x20u + slot);
        _world.world.bubble.blocks[slot][2] = subject ? _where.xSign : static_cast<std::uint8_t>((slot & 1u) != 0u ? 0x80u : 0x00u);
        _world.world.bubble.blocks[slot][3] = static_cast<std::uint8_t>(0x60u + slot * 7u);
        _world.world.bubble.blocks[slot][4] = subject ? _where.y : static_cast<std::uint8_t>(0x30u + slot);
        _world.world.bubble.blocks[slot][5] = subject ? _where.ySign : std::uint8_t{0u};
        _world.world.bubble.blocks[slot][6] = static_cast<std::uint8_t>(0x18u + slot * 13u);
        _world.world.bubble.blocks[slot][7] = subject ? _where.z : static_cast<std::uint8_t>(0x28u + slot);
        _world.world.bubble.blocks[slot][8] = subject ? _where.zSign : std::uint8_t{0u};

        // A believable orientation: nose along z, roof along y, side along x, with signs mixed.
        _world.world.bubble.blocks[slot][10] = 0x60u;
        _world.world.bubble.blocks[slot][12] = 0x10u;
        _world.world.bubble.blocks[slot][14] = 0xE0u;
        _world.world.bubble.blocks[slot][16] = 0x20u;
        _world.world.bubble.blocks[slot][18] = 0x60u;
        _world.world.bubble.blocks[slot][20] = 0x08u;
        _world.world.bubble.blocks[slot][22] = 0xE0u;
        _world.world.bubble.blocks[slot][24] = 0x08u;
        _world.world.bubble.blocks[slot][26] = 0x60u;

        if (subject)
        {
          _world.world.bubble.blocks[slot][10] = _where.noseX;
          _world.world.bubble.blocks[slot][12] = _where.noseY;
          _world.world.bubble.blocks[slot][14] = _where.noseZ;
        }

        _world.world.bubble.blocks[slot][27] = 12u;                        // speed
        _world.world.bubble.blocks[slot][28] = 0u;                         // acceleration
        _world.world.bubble.blocks[slot][29] = subject ? _where.roll : 0u; // roll
        _world.world.bubble.blocks[slot][30] = 0u;                         // pitch
        _world.world.bubble.blocks[slot][31] = 0u;
        _world.world.bubble.blocks[slot][33] = 0u;
        _world.world.bubble.blocks[slot][34] = 0u;
        _world.world.bubble.blocks[slot][35] = 20u;
        _world.world.bubble.blocks[slot][36] = subject ? _where.flags : 0u;
      }

      _world.world.bubble.counts[Elite::SHIP_TYPE_STATION] = _stations;
      _world.world.bubble.counts[Elite::SHIP_TYPE_THARGOID] = _thargoids;
      _world.world.bubble.counts[Elite::SHIP_TYPE_COBRA_MK3] = 1u;
      _world.world.bubble.heapBottom = Elite::SHIP_HEAP_TOP;

      _world.world.work = _world.world.bubble.blocks[2];
      _world.world.flight.type = _type;
      _world.world.flight.slot = 2u;
      _world.world.flight.blueprint = Elite::BlueprintAddress(_type == 0u ? std::uint8_t{11u} : _type);
      _world.world.flight.mainLoopCounter = 0u;

      // 6502: XX2 -- the face visibility of the last ship drawn, which `DOCKIT` reads as `K3+10`.
      for (std::size_t face = 0; face < _world.world.geometry.xx2.size(); ++face)
      {
        _world.world.geometry.xx2[face] = 0u;
      }
    }

    /// The same bytes into the interpreter's memory, at the addresses the original uses.
    void PushTacticsUniverse(Cpu6502& _cpu, Universe& _world, const Labels& _at)
    {
      for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
      {
        _cpu.memory[static_cast<std::uint16_t>(_at.frin + slot)] = _world.world.bubble.slots[slot];
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          _cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] =
            _world.world.bubble.blocks[slot][byte];
        }
      }
      for (std::size_t type = 0; type < _world.world.bubble.counts.size(); ++type)
      {
        _cpu.memory[static_cast<std::uint16_t>(_at.many + type)] = _world.world.bubble.counts[type];
      }
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        _cpu.memory[static_cast<std::uint16_t>(_at.inwk + byte)] = _world.world.work[byte];
      }
      // ONE PLACE sets the generator on both sides, because a sweep whose two machines start from
      // different random state compares nothing at all -- which is how this test first failed.
      _world.world.rng.SetState(_world.seed);
      for (std::size_t byte = 0; byte < 4u; ++byte)
      {
        _cpu.memory[static_cast<std::uint16_t>(_at.rand + byte)] = _world.seed[byte];
      }
      for (std::size_t face = 0; face < _world.world.geometry.xx2.size() && face < 14u; ++face)
      {
        _cpu.memory[static_cast<std::uint16_t>(_at.k3 + face)] = _world.world.geometry.xx2[face];
      }

      const std::uint16_t block = static_cast<std::uint16_t>(_at.kPercent + _world.slot * Elite::SHIP_BLOCK_SIZE);
      _cpu.memory[_at.inf] = static_cast<std::uint8_t>(block);
      _cpu.memory[static_cast<std::uint16_t>(_at.inf + 1)] = static_cast<std::uint8_t>(block >> 8u);
      _cpu.memory[_at.xx0] = static_cast<std::uint8_t>(_world.world.flight.blueprint);
      _cpu.memory[static_cast<std::uint16_t>(_at.xx0 + 1)] = static_cast<std::uint8_t>(_world.world.flight.blueprint >> 8u);
      _cpu.memory[_at.type] = _world.world.flight.type;
      _cpu.memory[_at.ecma] = _world.ecm;
      _cpu.memory[_at.fist] = _world.legal;
      _cpu.memory[_at.energy] = _world.world.status.energy;
      _cpu.memory[_at.fsh] = _world.world.status.forwardShield;
      _cpu.memory[_at.ash] = _world.world.status.aftShield;
      _cpu.memory[_at.dly] = 0u;
      _cpu.memory[_at.slsp] = static_cast<std::uint8_t>(Elite::SHIP_HEAP_TOP);
      _cpu.memory[static_cast<std::uint16_t>(_at.slsp + 1)] = static_cast<std::uint8_t>(Elite::SHIP_HEAP_TOP >> 8u);
    }

    /*
     * Everything either machine could have touched, compared.
     *
     * The ship block is the obvious half and the rest is the half that catches a port which took
     * the right branch for the wrong reason: the whole bubble (a spawn), the generator (how many
     * rolls were consumed and with which carry), the four steering bytes, and the seam counts.
     */
    void CompareTacticsUniverse(const Cpu6502& _cpu, const Universe& _world, const Labels& _at, const std::wstring& _where)
    {
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.inwk + byte)], _world.world.work[byte],
                         (_where + L": INWK+" + std::to_wstring(byte)).c_str());
      }
      for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.frin + slot)], _world.world.bubble.slots[slot],
                         (_where + L": FRIN+" + std::to_wstring(slot)).c_str());
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)],
                           _world.world.bubble.blocks[slot][byte],
                           (_where + L": K%+" + std::to_wstring(slot) + L"." + std::to_wstring(byte)).c_str());
        }
      }
      for (std::size_t type = 0; type < _world.world.bubble.counts.size(); ++type)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.many + type)], _world.world.bubble.counts[type],
                         (_where + L": MANY+" + std::to_wstring(type)).c_str());
      }
      for (std::size_t byte = 0; byte < 4u; ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.rand + byte)], _world.world.rng.State()[byte],
                         (_where + L": RAND+" + std::to_wstring(byte)).c_str());
      }

      Assert::AreEqual(_cpu.memory[_at.rat], _world.world.flight.rat, (_where + L": RAT").c_str());
      Assert::AreEqual(_cpu.memory[_at.rat2], _world.world.flight.rat2, (_where + L": RAT2").c_str());
      Assert::AreEqual(_cpu.memory[_at.junk], _world.world.bubble.junk, (_where + L": JUNK").c_str());

      // What `OOPS` spends, which is the half of a collision that a seam count cannot show.
      Assert::AreEqual(_cpu.memory[_at.energy], _world.world.status.energy, (_where + L": ENERGY").c_str());
      Assert::AreEqual(_cpu.memory[_at.fsh], _world.world.status.forwardShield, (_where + L": FSH").c_str());
      Assert::AreEqual(_cpu.memory[_at.ash], _world.world.status.aftShield, (_where + L": ASH").c_str());
    }
  } // namespace

  TEST_CLASS(TheShipAi)
  {
  public:
    /*
     * 6502: TACTICS, over the branches a sweep can reach without a screen.
     *
     * The cases are chosen by BRANCH rather than by volume: a missile with each of its three
     * outcomes, a station with and without its hostile bit, a rock hermit on both sides of its
     * roll, a Thargon with and without its Thargoid, a trader, a bounty hunter under and over the
     * legal threshold, a docking ship, and an ordinary pirate at four energy levels. Each runs the
     * shipped routine and the port from the same bytes and compares everything either could touch.
     */
    TEST_METHOD(TheAiMatchesTACTICS)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t tactics = oracle.Label("TACTICS");

      struct Case
      {
        const char* what;
        std::uint8_t type;
        std::uint8_t ai;    ///< 6502: INWK+32
        std::uint8_t newb;  ///< 6502: INWK+36
        std::uint8_t state; ///< 6502: INWK+31
        std::uint8_t energy;
        std::uint8_t ecm;
        std::uint8_t legal;
        std::uint8_t stationCount;
        std::uint8_t thargoidCount;

        /// The PLAYER's banks, not the ship's. Low values are how `OOPS` reaches `JMP DEATH`, which
        /// is the whole reason `RunTactics` answers a `bool` (§6.122).
        std::uint8_t banks;
      };

      const Case CASES[] = {
        {"a missile chasing us", 1u, 0xC0u, 0u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a missile chasing us with an ECM running", 1u, 0xC0u, 0u, 0u, 20u, 1u, 0u, 0u, 0u, 255u},
        {"a missile chasing a ship", 1u, 0x86u, 0u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a missile chasing the station", 1u, 0x82u, 0u, 0u, 20u, 0u, 0u, 1u, 0u, 255u},
        {"a calm station", 2u, 0xFFu, 0u, 0u, 20u, 0u, 0u, 1u, 0u, 255u},
        {"an angry station", 2u, 0xFFu, 0x04u, 0u, 20u, 0u, 0u, 1u, 0u, 255u},
        {"a rock hermit", 15u, 0xFFu, 0u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a lone Thargon", 30u, 0xFFu, 0u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a Thargon with its Thargoid", 30u, 0xFFu, 0u, 0u, 20u, 0u, 0u, 0u, 1u, 255u},
        {"a trader", 11u, 0xC1u, 0x01u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a clean bounty hunter", 11u, 0xC1u, 0x02u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a bounty hunter and an offender", 11u, 0xC1u, 0x02u, 0u, 20u, 0u, 60u, 0u, 0u, 255u},

        // Exactly on the boundary, because `CPX #40 / BCC TN2` is a `>=` and the only way to tell
        // it from a `>` is to stand on 40 (§6.126).
        {"a bounty hunter and a fresh offender", 11u, 0xC1u, 0x02u, 0u, 20u, 0u, 40u, 0u, 0u, 255u},
        {"a bounty hunter one short of it", 11u, 0xC1u, 0x02u, 0u, 20u, 0u, 39u, 0u, 0u, 255u},
        {"a ship on its way in to dock", 11u, 0xC1u, 0x10u, 0u, 20u, 0u, 0u, 1u, 0u, 255u},
        {"a ship docking with no station", 11u, 0xC1u, 0x10u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a hostile pirate", 11u, 0xC1u, 0x04u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a hostile pirate with missiles", 11u, 0xC1u, 0x04u, 0x03u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a wounded pirate", 11u, 0xC1u, 0x04u, 0u, 2u, 0u, 0u, 0u, 0u, 255u},
        {"a pirate near a station", 11u, 0xC9u, 0x0Cu, 0u, 20u, 0u, 0u, 1u, 0u, 255u},
        {"an Anaconda", 14u, 0xC1u, 0x04u, 0u, 20u, 0u, 0u, 0u, 0u, 255u},
        {"a Thargoid with Thargons to launch", 29u, 0xC1u, 0x04u, 0x03u, 20u, 0u, 0u, 0u, 0u, 255u},

        // The fatal cases: a missile arriving on empty banks, and a collision on nearly empty
        // ones. Without these the `bool` §6.122 added is never once observed to be false.
        {"a missile arriving on empty banks", 1u, 0xC0u, 0u, 0u, 20u, 0u, 0u, 0u, 0u, 0u},
        {"a missile arriving on a sliver", 1u, 0xC0u, 0u, 0u, 20u, 0u, 0u, 0u, 0u, 4u},
        {"a missile arriving on half banks", 1u, 0xC0u, 0u, 0u, 20u, 0u, 0u, 0u, 0u, 128u},
      };

      // Four generator states, because half of `TACTICS` is `DORND` and one seed reaches one
      // branch of each roll (§6.124).
      const std::array<std::array<std::uint8_t, 4>, 4> SEEDS = {
        {{0x31u, 0xF5u, 0x7Au, 0x0Cu}, {0x11u, 0x22u, 0x33u, 0x44u}, {0xFEu, 0xC3u, 0x09u, 0x5Du}, {0x80u, 0x7Fu, 0xFFu, 0x01u}}};

      std::uint32_t compared = 0;
      std::uint32_t died = 0;
      std::set<std::string> reached;

      for (const Case& one : CASES)
      {
        for (const std::array<std::uint8_t, 4>& seed : SEEDS)
        {
          for (const Geometry& where : GEOMETRIES)
          {
            Cpu6502 cpu = oracle.Fresh();

            /*
           * WHAT IS TRAPPED AND WHAT IS NOT.
           *
           * The sound and the two drawing routines are trapped, because the port reaches them
           * through seams and a 6502 interpreter with no dashboard would compare the wrong thing:
           * `NOISE`, `NOISE2`, `MESS` and `ECBLB2`. Their effects -- the message state, the ECM
           * countdown and the canvas -- are NAMED EXCLUSIONS from the comparison below.
           *
           * `OOPS`, `EXNO2` and `EXNO3` are NOT trapped. They are ported, they are arithmetic on
           * the shields and the energy banks, and running them on both sides is what makes a
           * collision comparable rather than merely counted.
           *
           * `DEATH` is trapped and is the point of the case: §6.122 gave `RunTactics` a `bool` so
           * that `JMP DEATH` could come back out, and the assertion below is that the port answers
           * false exactly when the shipped routine reaches that label.
           */
            const std::uint16_t death = oracle.Label("DEATH");
            for (const std::uint16_t seam :
                 {oracle.Label("NOISE"), oracle.Label("NOISE2"), oracle.Label("MESS"), oracle.Label("ECBLB2"), death})
            {
              cpu.AddTrap(seam);
            }

            Universe world;
            SeedTacticsUniverse(cpu, world, at, one.type, one.stationCount, one.thargoidCount, where);

            // The player's banks, which decide whether `OOPS` returns or jumps to `DEATH`.
            world.world.status.energy = one.banks;
            world.world.status.forwardShield = one.banks;
            world.world.status.aftShield = one.banks;

            world.world.work[31] = one.state;
            world.world.work[32] = one.ai;
            world.world.work[35] = one.energy;
            world.world.work[36] = one.newb;
            world.world.bubble.blocks[2] = world.world.work;
            world.seed = seed;
            world.ecm = one.ecm;
            world.legal = one.legal;

            PushTacticsUniverse(cpu, world, at);

            cpu.x = one.type;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(tactics, 400'000);
            Assert::IsTrue(run.completed, L"TACTICS returned");

            // The port, from the same bytes, through the same seams.
            world.world.status.ecmCountdown = one.ecm;
            world.world.commander.At(Elite::Field::LegalStatus) = one.legal;

            Elite::FlightScreen screen = world.world.Screen();
            Elite::FlightLoop loop{screen,     world.keys,       world.control, world.options, world.burst,   world.heap,
                                   world.clip, world.projection, world.axes,    world.effects, world.effects, world.effects};
            const bool survived = Elite::RunTactics(loop, world.slot);

            const std::wstring context =
              WidenText(std::string("TACTICS: ") + one.what + " " + where.what + " seed " + std::to_string(seed[0]));

            // 6502: JMP DEATH, which never returns -- so the shipped routine reaching that label and
            // the port answering false are the same event (§6.122).
            bool reachedDeath = false;
            for (const Cpu6502::TrapHit& hit : cpu.trapHits)
            {
              reachedDeath = reachedDeath || (hit.address == death);
            }
            Assert::AreEqual(!reachedDeath, survived, (context + L": the player lived or did not").c_str());
            died += reachedDeath ? 1u : 0u;

            CompareTacticsUniverse(cpu, world, at, context);
            reached.insert(std::string(one.what) + "/" + where.what);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(25u * 4u * 18u, compared, L"the whole sweep ran");
      Assert::AreEqual<std::size_t>(25u * 18u, reached.size(), L"and every case is distinct");
      Assert::IsTrue(died > 0u, L"and the player died on some of them, so the bool is observed false");
      Logger::WriteMessage(("TACTICS: " + std::to_string(compared) + " cases, " + std::to_string(died) + " of them fatal").c_str());
    }
  };

  /*
   * 6502: DOCKIT on its own, which is the only way to reach its four approaches.
   *
   * `TACTICS` gets to the autopilot down one narrow path -- a ship with `NEWB` bit 4, near a
   * station -- and that path reaches ONE of the four cases. Four mutations in the approach
   * thresholds walked through the AI sweep for that reason (§6.126), so the approach is swept here
   * instead: the ship is put in front of the slot, behind it, off to the side and right on top of
   * it, at four distances, and as both an NPC and the PLAYER's own computer -- which `auton` marks
   * by storing a NEGATIVE type, the branch `PH3` reads.
   */
  TEST_CLASS(TheDockingComputer)
  {
  public:
    TEST_METHOD(TheAutopilotMatchesDOCKIT)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t dockit = oracle.Label("DOCKIT");

      struct Approach
      {
        const char* what;
        std::uint8_t x, xSign, y, ySign, z, zSign;
        std::uint8_t nose;  ///< the station's nose vector x high byte, which points the slot
        std::uint8_t noseY; ///< and its y and z, because a slot pointing down one axis cannot be
        std::uint8_t noseZ; ///< both far away and lined up at once -- see `far along a diagonal slot`

        /*
         * AND THE SHIP'S OWN NOSE, which decides `TAS3` and therefore `CMP #&A2`.
         *
         * With one fixed orientation the ship's nose points at the station only by accident, so
         * that dot product stayed under 0x26 across fifty thousand positions and the threshold was
         * unreachable (§6.126). A ship on a real approach is pointing AT the slot: these rows say
         * so, by giving the nose the opposite sign to the position.
         */
        std::uint8_t shipNoseX, shipNoseY, shipNoseZ;
      };

      const Approach APPROACHES[] = {
        // The station's slot points along its NOSE vector, which these put on +x, so "in front of
        // the slot" is a large positive x with small y and z.
        {"straight in front of the slot", 0x40u, 0u, 0x02u, 0u, 0x02u, 0u, 0x60u, 0x10u, 0x20u, 0x60u, 0x10u, 0xE0u},
        {"in front and further out", 0x70u, 0u, 0x08u, 0u, 0x08u, 0u, 0x60u, 0x10u, 0x20u, 0x60u, 0x10u, 0xE0u},
        {"in front and close", 0x10u, 0u, 0x01u, 0u, 0x01u, 0u, 0x60u, 0x10u, 0x20u, 0x60u, 0x10u, 0xE0u},
        {"round the back", 0x40u, 0x80u, 0x02u, 0u, 0x02u, 0u, 0x60u, 0x10u, 0x20u, 0x60u, 0x10u, 0xE0u},
        {"off to one side", 0x04u, 0u, 0x50u, 0u, 0x04u, 0u, 0x60u, 0x10u, 0x20u, 0x60u, 0x10u, 0xE0u},
        {"above the slot", 0x20u, 0u, 0x30u, 0u, 0x04u, 0u, 0x60u, 0x10u, 0x20u, 0x60u, 0x10u, 0xE0u},
        {"with the slot turned away", 0x40u, 0u, 0x02u, 0u, 0x02u, 0u, 0xE0u, 0x10u, 0x20u, 0x60u, 0x10u, 0xE0u},

        /*
         * FAR ALONG A DIAGONAL SLOT, which is the only shape that reaches `CMP #157`.
         *
         * `K` is the length of the vector before `NORM` scales it, and each component of that
         * vector is seven bits, so the length only passes 157 when all three are large. But the
         * distance is only tested at all when `TAS4` says the ship is in front of the slot, which
         * needs the vector ALIGNED with the station's nose. With a nose pointing mostly along one
         * axis the two conditions exclude each other and the branch is unreachable, which is why
         * two mutations in it survived a sweep of two hundred positions (§6.126).
         */
        {"far along a diagonal slot", 0xFEu, 0u, 0xFEu, 0u, 0xFEu, 0u, 0x60u, 0x60u, 0x60u, 0x60u, 0x10u, 0xE0u},
        {"close along a diagonal slot", 0x30u, 0u, 0x30u, 0u, 0x30u, 0u, 0x60u, 0x60u, 0x60u, 0x60u, 0x10u, 0xE0u},
      };

      /*
       * A LADDER OF DIAGONAL MAGNITUDES, one step apart through the interesting part.
       *
       * `DOCKIT` compares the distance against 157 and the grid above cannot reach past about 135,
       * because its y and z are small and the length is built from three seven-bit components. A
       * uniform diagonal can: it runs from about 90 at &60 to 219 at &FE. Two apart steps over the
       * threshold -- &B4 gives 155 and &B6 gives 157 -- so the range that matters is walked one at
       * a time, which is what makes moving the constant by one observable (§6.126).
       */
      std::vector<Approach> ladder;
      for (int magnitude = 0x60; magnitude <= 0xFE; magnitude += 8)
      {
        const std::uint8_t m = static_cast<std::uint8_t>(magnitude);
        ladder.push_back({"diagonal ladder", m, 0u, m, 0u, m, 0u, 0x60u, 0x60u, 0x60u, 0x60u, 0x10u, 0xE0u});
      }
      /*
       * SKEWED, because a uniform diagonal cannot produce every length. `TA2` halves each component
       * before squaring it, so the three equal values quantise the answer: &B4 gives 155, &B6 gives
       * 157, and 156 is not on the line at all. Pulling one axis away from the other two fills the
       * gaps -- (&B4, &B6, &B4) is 156 exactly, which is the value that makes moving the constant
       * from 157 to 156 observable (§6.126).
       */
      for (int magnitude = 0xA8; magnitude <= 0xC4; magnitude += 4)
      {
        for (int skew = -4; skew <= 4; skew += 2)
        {
          const std::uint8_t m = static_cast<std::uint8_t>(magnitude);
          const std::uint8_t skewed = static_cast<std::uint8_t>(magnitude + skew);
          ladder.push_back({"diagonal ladder", m, 0u, skewed, 0u, m, 0u, 0x60u, 0x60u, 0x60u, 0x60u, 0x10u, 0xE0u});
        }
      }

      const std::uint8_t TYPES[] = {11u, 0xE0u}; // an NPC, and `auton`'s negative type

      /*
       * A GRID AS WELL AS THE NAMED APPROACHES, and the reason is four surviving mutations.
       *
       * `DOCKIT` decides by three measured quantities -- the station's nose against the vector to
       * the ship (35), the ship's own nose against it (&A2), and the distance (157) -- and a
       * handful of hand-placed positions lands near none of the three thresholds, so moving each of
       * them by one agreed everywhere (§6.126). Hand-picking the exact values is not possible from
       * the inputs, because each is a dot product of a normalised vector; a grid dense enough to
       * straddle them is, and it costs a few hundred fast cases.
       */
      /*
       * The grid, kept coarse now that the named rows and the ladder carry the thresholds. &4A with
       * small y and z is in it deliberately: with the ship aimed back at the station that position
       * puts `TAS3`'s dot product on &A1, one below the constant `DOCKIT` compares it against.
       */
      const std::uint8_t GRID_X[] = {0x08u, 0x18u, 0x28u, 0x40u, 0x4Au, 0x60u, 0x90u, 0xC0u, 0xFEu};
      const std::uint8_t GRID_Y[] = {0x00u, 0x08u, 0x18u, 0x38u};
      const std::uint8_t GRID_Z[] = {0x00u, 0x08u, 0x20u, 0x40u};

      std::uint32_t compared = 0;
      std::set<std::string> reached;
      std::set<std::string> outcomes;

      std::vector<Approach> approaches(std::begin(APPROACHES), std::end(APPROACHES));
      approaches.insert(approaches.end(), ladder.begin(), ladder.end());
      for (const std::uint8_t x : GRID_X)
      {
        for (const std::uint8_t y : GRID_Y)
        {
          for (const std::uint8_t z : GRID_Z)
          {
            approaches.push_back({"grid", x, 0u, y, 0u, z, 0u, 0x60u, 0x10u, 0x20u, 0x60u, 0x10u, 0xE0u});

            // The same position with the nose reversed, so the ship faces the station rather than
            // away from it. `TAS3` is a dot product: the sign of the nose is most of its answer.
            approaches.push_back({"aimed", x, 0u, y, 0u, z, 0u, 0x60u, 0x10u, 0x20u, 0xE0u, 0x90u, 0x60u});
          }
        }
      }

      for (const Approach& approach : approaches)
      {
        for (const std::uint8_t type : TYPES)
        {
          // The face byte only decides `TN13`, so both answers are swept on the NAMED approaches
          // and one is enough on the grid, which is there for the three thresholds above.
          const bool named = std::string(approach.what) != "grid";
          for (const std::uint8_t faces : named ? std::vector<std::uint8_t>{0u, 0xFFu} : std::vector<std::uint8_t>{0u})
          {
            Cpu6502 cpu = oracle.Fresh();
            for (const std::uint16_t seam : {oracle.Label("NOISE"), oracle.Label("MESS")})
            {
              cpu.AddTrap(seam);
            }

            Universe world;
            SeedTacticsUniverse(cpu, world, at, 11u, 1u, 0u, GEOMETRIES[0]);

            // The ship where the case asks, relative to a station that is at the origin of `K3`.
            /*
             * THE STATION GOES TO THE ORIGIN, and the ship's position is a HIGH byte.
             *
             * `VCSU1` computes `K3` = ship minus station and every one of `DOCKIT`'s three
             * decisions is a dot product of the unit vector along it, which `TAS2` builds from
             * bytes 1, 4 and 7. Zeroing the station makes `K3` the ship's own position, so the
             * sweep can aim at the slot rather than at wherever the ramp happened to put it -- and
             * before both of those, every case in this test went down one branch (§6.126).
             */
            for (std::size_t byte = 0; byte < 9u; ++byte)
            {
              world.world.bubble.blocks[1][byte] = 0u;
            }

            world.world.work[0] = 0u;
            world.world.work[1] = approach.x;
            world.world.work[2] = approach.xSign;
            world.world.work[3] = 0u;
            world.world.work[4] = approach.y;
            world.world.work[5] = approach.ySign;
            world.world.work[6] = 0u;
            world.world.work[7] = approach.z;
            world.world.work[8] = approach.zSign;
            world.world.work[27] = 12u;

            /*
             * A NON-ZERO `NEWB`, because `TN13` sets its top bit with `ASL / SEC / ROR`.
             *
             * That is a ROTATE and not an `ORA #128`: it also shifts bits 0 to 5 up one and drops
             * bit 6. With the byte zero the two are the same answer, and the mutation that made it
             * an `ORA` survived a thousand cases (§6.126). It varies per case so the shift shows.
             */
            world.world.work[36] = static_cast<std::uint8_t>(0x24u + (compared & 0x1Fu));

            world.world.bubble.blocks[1][10] = approach.nose;
            world.world.bubble.blocks[1][12] = approach.noseY;
            world.world.bubble.blocks[1][14] = approach.noseZ;

            world.world.work[10] = approach.shipNoseX;
            world.world.work[12] = approach.shipNoseY;
            world.world.work[14] = approach.shipNoseZ;
            world.world.bubble.blocks[1][16] = 0x08u;
            world.world.bubble.blocks[1][18] = 0x60u;

            world.world.flight.type = type;

            /*
             * 6502: XX2, which `DOCKIT` reads as `K3+10` -- the eleventh face of the last ship
             * drawn, and the only thing standing between an NPC and a completed docking (§6.125).
             * Both answers are swept, because a port that ignored the byte would agree on one.
             */
            world.world.geometry.xx2[10] = faces;

            PushTacticsUniverse(cpu, world, at);
            cpu.memory[static_cast<std::uint16_t>(at.k3 + 10u)] = faces;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(dockit, 400'000);
            Assert::IsTrue(run.completed, L"DOCKIT returned");

            Elite::FlightScreen screen = world.world.Screen();
            Elite::FlightLoop loop{screen,     world.keys,       world.control, world.options, world.burst,   world.heap,
                                   world.clip, world.projection, world.axes,    world.effects, world.effects, world.effects};
            Assert::IsTrue(Elite::RunDockingComputer(loop, world.slot), L"DOCKIT does not kill anybody");

            const std::wstring context = WidenText(std::string("DOCKIT: ") + approach.what + (type == 0xE0u ? " (ours)" : " (theirs)") +
                                                   (faces != 0u ? " faces" : " no faces"));
            CompareTacticsUniverse(cpu, world, at, context);

            // 6502: K3 itself, which the AI comparison does not reach because `TACTICS` rebuilds it.
            for (std::size_t byte = 0; byte < 10u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.k3 + byte)], world.axes[byte],
                               (context + L": K3+" + std::to_wstring(byte)).c_str());
            }

            reached.insert(std::string(approach.what) + std::to_string(approach.x) + "," + std::to_string(approach.y) + "," +
                           std::to_string(approach.z) + (type == 0xE0u ? "/ours" : "/theirs"));

            /*
             * WHAT THE SHIP WAS TOLD TO DO, measured from the block rather than from the inputs.
             *
             * `PH22` stops it dead (speed 1, no acceleration); `TN11` speeds it up and rolls it
             * hard; the two steering paths leave a pitch and a roll from the dot products. A sweep
             * whose cases all end the same way reaches one approach however many rows it has, so
             * this counts the distinct answers and the assertion below is on that count.
             */
            outcomes.insert(std::to_string(world.world.work[27]) + "," + std::to_string(world.world.work[28]) + "," +
                            std::to_string(world.world.work[29]) + "," + std::to_string(world.world.work[30]) + "," +
                            std::to_string(world.world.work[36]));
            ++compared;
          }
        }
      }

      Assert::IsTrue(compared > 500u, L"the whole sweep ran");

      // A lower bound rather than an exact count, because a few named approaches share coordinates
      // with the grid and the set collapses them -- the number that matters is `compared`.
      Assert::IsTrue(reached.size() >= 300u, L"and the approaches are nearly all distinct");
      Assert::IsTrue(outcomes.size() >= 8u, L"and the sweep reached at least eight different answers");
      Logger::WriteMessage(
        ("DOCKIT: " + std::to_string(compared) + " cases, " + std::to_string(outcomes.size()) + " distinct answers").c_str());
    }
  };

} // namespace GameLogicTests
