#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "Scanner.h"
#include "ShipMove.h"
#include "ShipSlot.h"
#include "Tactics.h"

#include <array>
#include <cstdint>
#include <string>

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

    struct Labels
    {
      std::uint16_t inwk = 0, k3 = 0, kPercent = 0, v = 0, x1 = 0, y1 = 0, x2 = 0;
      std::uint16_t q = 0, r = 0, s = 0, u = 0, k = 0;

      explicit Labels(const OracleImage& _oracle)
      {
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

              const std::wstring where = Widen("VCSUB mine " + std::to_string(mineHigh) + "/" + std::to_string(mineSign) + " theirs " +
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

                const std::wstring where = Widen(std::string(station != 0 ? "TAS4" : "TAS3") + " vector " + std::to_string(which) + " (" +
                                                 std::to_string(vx) + "," + std::to_string(vy) + "," + std::to_string(sx) + ")");
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

            const std::wstring where = Widen("TAS6 " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
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

              const std::wstring where = Widen("DCS1 nose " + std::to_string(nose) + " K3 " + std::to_string(low) + "/" +
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

              const std::wstring where = Widen("ANGRY NEWB " + std::to_string(newb) + " AI " + std::to_string(ai) + " TYPE " +
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

} // namespace GameLogicTests
