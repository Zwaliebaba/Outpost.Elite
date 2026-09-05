#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "LookupTables.h"
#include "Trumbles.h"

#include <array>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

namespace GameLogicTests
{

  /*
   * Slice 4d-a: `MVTRIBS`, and the six sprites the Trumbles are.
   *
   * WHAT THIS SUITE EXISTS TO CATCH is the two carries and the nine-bit coordinate. The routine
   * takes two random numbers and the second one runs with the carry SET -- so a port that passed
   * the same flag to both agrees with the game on the x axis and disagrees on the y axis about
   * half the time, which is a difference you would never see by watching the screen. And the x
   * coordinate is nine bits split across a per-sprite register and one bit of a register shared by
   * all eight sprites, so every move is a read-modify-write of a byte two other things live in.
   *
   * THE SPRITE REGISTERS ARE THE SHIP BLUEPRINT TABLE in a flat image. `VIC` is &D000 and so is
   * `XX21` (§6.108), so `MVTRIBS` writing sprite 2's x coordinate lands on the blueprint pointer
   * for ship type 3. That is harmless here -- nothing in this suite reads a blueprint -- and it is
   * why `FlightLoopTests` may only give a frame Trumbles when the frame stops before the ships.
   */
  TEST_CLASS(TheTrumbles)
  {
  public:
    /*
     * 6502: LDA MCNT / AND #7 / CMP TRIBCT / BCC P%+5 / JMP NOMVETR -- which sprite, and whether.
     *
     * 1,792 cases: every main loop counter against every count from none to six.
     *
     * SIX IS THE TOP AND SEVEN IS OFF THE END OF THE GAME. `TRIBCT` is written only by `SIGHT`,
     * from `TRIBTA`, whose last two entries are both 6 -- so the count saturates and seven cannot
     * happen. `MVTRIBS` does not know that: with `TRIBCT` at seven and the counter at six it would
     * index `SPMASK+12`, and the twelve-byte table is immediately followed by `MVTRIBS` itself, so
     * the mask it would AND into the register is the opcode of its own first instruction (&A5). It
     * would then write "sprite 8's" x coordinate, which is `VIC+&10` -- the register it had just
     * masked. The port's arrays stop where the game's reachable indices do, so the sweep stops
     * there too rather than comparing two different kinds of nonsense.
     *
     * The comparison is the WHOLE bank -- all sixteen bytes of each of the three tables, the twelve
     * coordinate registers, the shared bit register and the generator -- so a pass that should have
     * done nothing and moved something fails here, and so does one that moved the wrong sprite.
     */
    TEST_METHOD(TheTurnChoiceMatchesMVTRIBS)
    {
      if (OracleMissing())
      {
        return;
      }

      std::uint32_t compared = 0;
      std::uint32_t moved = 0;

      for (std::uint32_t counter = 0; counter < 256u; ++counter)
      {
        for (std::uint8_t count = 0; count <= Elite::TRUMBLE_SPRITE_MAX; ++count)
        {
          Bank bank = Seeded(static_cast<std::uint8_t>(counter * 3u + count));
          bank.count = count;

          const std::wstring where = WidenText("MVTRIBS (MCNT " + std::to_string(counter) + ", TRIBCT " + std::to_string(count) + ")");
          Compare(bank, static_cast<std::uint8_t>(counter), where);

          if ((counter & 7u) < count)
          {
            ++moved;
          }
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(256u * 7u, compared, L"the whole sweep ran");

      // Eight counters against seven counts is a triangle: 0 + 1 + ... + 6 per eight counters, and
      // thirty-two passes of that. A sweep that never moved anything would agree with a port that
      // returns immediately, so the number is asserted rather than assumed.
      Assert::AreEqual<std::uint32_t>(21u * 32u, moved, L"and it moved on the passes it should have");
    }

    /*
     * 6502: the x calculation, from `CLC / LDA VIC+4,Y` to `.oktrib STA TRIBXH,Y`.
     *
     * Every low byte against both values the ninth bit can hold, for each of the three velocities
     * the direction table can produce -- 1,536 cases, with the direction-change path held shut so
     * the velocity under test is the one that gets applied.
     *
     * THE TWO EDGES ARE NOT SYMMETRICAL and this is where that shows. Off the left is detected on
     * the SIGN of the high byte and puts the sprite at &148; off the right is detected by comparing
     * the low byte against &50 with the ninth bit already known to be set, and puts it at 0. So
     * &147 and &150 are both legal positions and &14F is not, and only the second edge has a
     * constant in it that a mutation could move.
     */
    TEST_METHOD(TheXWrapMatchesMVTRIBS)
    {
      if (OracleMissing())
      {
        return;
      }

      // 6502: TRIBDIR and TRIBDIRH -- the three distinct 16-bit velocities in the four entries.
      const std::array<std::pair<std::uint8_t, std::uint8_t>, 3> VELOCITIES = {
        std::pair<std::uint8_t, std::uint8_t>{0x00u, 0x00u},
        std::pair<std::uint8_t, std::uint8_t>{0x01u, 0x00u},
        std::pair<std::uint8_t, std::uint8_t>{0xFFu, 0xFFu},
      };

      const std::array<std::uint8_t, 4> quiet = QuietSeed();

      std::uint32_t compared = 0;
      std::uint32_t wrappedLeft = 0;
      std::uint32_t wrappedRight = 0;

      for (const std::pair<std::uint8_t, std::uint8_t>& velocity : VELOCITIES)
      {
        for (std::uint8_t high = 0; high <= 1u; ++high)
        {
          for (std::uint32_t low = 0; low < 256u; ++low)
          {
            Bank bank;
            bank.count = 1u;
            bank.sprites.velocityX[0] = velocity.first;
            bank.sprites.velocityXHigh[0] = velocity.second;
            bank.sprites.coordinateXHigh[0] = high;
            bank.sprites.coordinates[0] = static_cast<std::uint8_t>(low);
            bank.sprites.coordinates[1] = 0x80u;
            bank.sprites.coordinateMsb = static_cast<std::uint8_t>(high != 0u ? 0x04u : 0x00u);
            bank.seed = quiet;

            const std::wstring where = WidenText("MVTRIBS x " + std::to_string(high) + ":" + std::to_string(low) + " by " +
                                                 std::to_string(velocity.second) + ":" + std::to_string(velocity.first));
            Compare(bank, 0u, where);

            if (high == 0u && low == 0u && velocity.second == 0xFFu)
            {
              ++wrappedLeft;
            }
            if (high == 1u && low >= Elite::TRUMBLE_RIGHT_EDGE_LIMIT - 1u && velocity.first == 0x01u)
            {
              ++wrappedRight;
            }
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(3u * 2u * 256u, compared, L"the whole sweep ran");
      Assert::IsTrue(wrappedLeft > 0u, L"the left edge was crossed");
      Assert::IsTrue(wrappedRight > 0u, L"and so was the right one");
    }

    /*
     * 6502: the y calculation, `LDA VIC+5,Y / CLC / ADC TRIBVX+1,Y / STA VIC+5,Y`.
     *
     * Every starting row against the three velocities, and the point of the sweep is what is NOT
     * here: there is no edge test on this axis at all. A Trumble at row 255 moving down arrives at
     * row 0, and one at row 0 moving up arrives at row 255, because eight-bit addition is the whole
     * of the code. The y velocity is also read from the LOW direction table only, so its -1 is &FF
     * where the x axis spells the same direction &FFFF.
     */
    TEST_METHOD(TheYWrapMatchesMVTRIBS)
    {
      if (OracleMissing())
      {
        return;
      }

      const std::array<std::uint8_t, 4> quiet = QuietSeed();
      std::uint32_t compared = 0;

      for (const std::uint8_t velocity : {std::uint8_t{0x00u}, std::uint8_t{0x01u}, std::uint8_t{0xFFu}})
      {
        for (std::uint32_t row = 0; row < 256u; ++row)
        {
          Bank bank;
          bank.count = 1u;
          bank.sprites.velocityX[1] = velocity;
          bank.sprites.coordinates[0] = 0x40u;
          bank.sprites.coordinates[1] = static_cast<std::uint8_t>(row);
          bank.seed = quiet;

          const std::wstring where = WidenText("MVTRIBS y " + std::to_string(row) + " by " + std::to_string(velocity));
          Compare(bank, 0u, where);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(3u * 256u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: JSR DORND / CMP #235 ... JSR DORND -- the direction change, and the two carries.
     *
     * Every seed that opens the path, for every one of the six sprites, and the comparison
     * includes `RAND`: getting the second call's carry wrong changes the y velocity AND leaves the
     * generator one state adrift, so the next frame's roll is wrong as well. Two hundred and
     * fifty-six seeds are tried and the ones that roll below 235 are counted rather than skipped
     * silently -- a sweep that opened the path zero times would pass while proving nothing.
     */
    TEST_METHOD(TheDirectionChoiceMatchesMVTRIBS)
    {
      if (OracleMissing())
      {
        return;
      }

      std::uint32_t compared = 0;
      std::uint32_t turned = 0;

      for (std::uint8_t sprite = 0; sprite < Elite::TRUMBLE_SPRITE_MAX; ++sprite)
      {
        for (std::uint32_t seed = 0; seed < 256u; ++seed)
        {
          Bank bank = Seeded(static_cast<std::uint8_t>(seed));
          bank.count = Elite::TRUMBLE_SPRITE_MAX;

          Elite::Rng probe;
          probe.SetState(bank.seed);
          if (probe.Next(false).value >= Elite::TRUMBLE_TURN_ROLL)
          {
            ++turned;
          }

          const std::wstring where = WidenText("MVTRIBS turn (sprite " + std::to_string(sprite) + ", seed " + std::to_string(seed) + ")");
          Compare(bank, sprite, where);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(Elite::TRUMBLE_SPRITE_MAX * 256u, compared, L"the whole sweep ran");
      Assert::IsTrue(turned > 0u, L"and some of those seeds changed direction");
    }

    /*
     * 6502: LDA SPMASK,Y / AND VIC+&10 ... LDA SPMASK+1,Y / ORA VIC+&10 -- one sprite's bit, and
     * the seven it must not touch.
     *
     * The register is entered with EVERY bit set, so a mask read one entry out of step clears
     * somebody else's ninth x bit and is visible immediately. Bits 0 and 1 are the laser sights and
     * the explosion, which are two other slices' sprites, and both are in the sweep for that
     * reason: `SIGHT` and `DOEXP` write them and this routine has to leave them alone.
     */
    TEST_METHOD(TheSpriteBitsAreThisSpritesOnly)
    {
      if (OracleMissing())
      {
        return;
      }

      const std::array<std::uint8_t, 4> quiet = QuietSeed();
      std::uint32_t compared = 0;

      for (std::uint8_t sprite = 0; sprite < Elite::TRUMBLE_SPRITE_MAX; ++sprite)
      {
        for (const std::uint8_t entry : {std::uint8_t{0x00u}, std::uint8_t{0xFFu}})
        {
          for (std::uint8_t high = 0; high <= 1u; ++high)
          {
            Bank bank;
            bank.count = Elite::TRUMBLE_SPRITE_MAX;
            bank.sprites.coordinateMsb = entry;
            bank.sprites.coordinateXHigh[sprite * 2u] = high;
            bank.sprites.coordinates[sprite * 2u] = 0x30u;
            bank.sprites.velocityX[sprite * 2u] = 0x01u;
            bank.seed = quiet;

            const std::wstring where = WidenText("MVTRIBS VIC+&10 (sprite " + std::to_string(sprite) + ", entry " + std::to_string(entry) +
                                                 ", bit " + std::to_string(high) + ")");
            Compare(bank, sprite, where);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(Elite::TRUMBLE_SPRITE_MAX * 2u * 2u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: the two `JSR SETL1` calls, and what they are called with.
     *
     * They bracket everything the routine does to the video chip and they are the only thing in it
     * that is not memory, so a port that dropped them would compare perfectly and leave the raster
     * handler in the wrong mode for the rest of the frame. The pass that returns early makes
     * neither call, which is what the second half of this asserts.
     */
    TEST_METHOD(TheRasterModesBracketTheMove)
    {
      if (OracleMissing())
      {
        return;
      }

      const std::array<std::uint8_t, 4> quiet = QuietSeed();

      for (std::uint8_t count = 0; count <= 1u; ++count)
      {
        Bank bank;
        bank.count = count;
        bank.seed = quiet;

        const std::wstring where = WidenText("MVTRIBS SETL1 (TRIBCT " + std::to_string(count) + ")");
        const std::vector<std::uint8_t> modes = Compare(bank, 0u, where);

        if (count == 0u)
        {
          Assert::AreEqual<std::size_t>(0u, modes.size(), (where + L": no calls").c_str());
        }
        else
        {
          Assert::AreEqual<std::size_t>(2u, modes.size(), (where + L": two calls").c_str());
          Assert::AreEqual<std::uint8_t>(Elite::TRUMBLE_RASTER_IO, modes[0], (where + L": in").c_str());
          Assert::AreEqual<std::uint8_t>(Elite::TRUMBLE_RASTER_RAM, modes[1], (where + L": out").c_str());
        }
      }
    }

  private:
    /// One starting state for both sides: the sprite bank, the generator, and nothing else.
    struct Bank
    {
      Elite::TrumbleSprites sprites;
      std::uint8_t count = 0;
      std::array<std::uint8_t, 4> seed{0x11u, 0x22u, 0x33u, 0x44u};
    };

    /// Records `SETL1`, which is the routine's only reach outside memory.
    struct RasterLog final : Elite::SightEffects
    {
      std::vector<std::uint8_t> modes;

      void SetRasterMode(std::uint8_t _mode) override
      {
        modes.push_back(_mode);
      }
      void SetSightColour(std::uint8_t) override {}
      void SetSpritesEnabled(std::uint8_t) override {}
      void MaskSprites(std::uint8_t) override {}
    };

    /*
     * A seed whose first roll is below 235, so the direction under test survives the pass.
     *
     * Found by asking the port's own generator rather than by picking a number that looked right:
     * the threshold is 235 of 256, so a seed chosen at random opens the direction-change path one
     * time in twelve, and a sweep with one such case in it would be a sweep whose failures nobody
     * could read.
     */
    [[nodiscard]] static std::array<std::uint8_t, 4> QuietSeed()
    {
      for (std::uint32_t candidate = 1; candidate < 256u; ++candidate)
      {
        const std::array<std::uint8_t, 4> seed = {static_cast<std::uint8_t>(candidate), 0x5Au, 0xA5u, 0x3Cu};
        Elite::Rng probe;
        probe.SetState(seed);
        if (probe.Next(false).value < Elite::TRUMBLE_TURN_ROLL)
        {
          return seed;
        }
      }

      Assert::Fail(L"no seed rolls below the direction threshold");
      return {};
    }

    /// A bank whose every byte is different from its neighbours, so a one-index slip shows.
    [[nodiscard]] static Bank Seeded(std::uint8_t _salt)
    {
      Bank bank;
      for (std::size_t index = 0; index < Elite::TRUMBLE_VELOCITY_COUNT; ++index)
      {
        const std::uint8_t at = static_cast<std::uint8_t>(index);
        bank.sprites.velocityX[index] = static_cast<std::uint8_t>((at & 1u) != 0u ? 0x01u : 0xFFu);
        bank.sprites.velocityXHigh[index] = static_cast<std::uint8_t>((at & 1u) != 0u ? 0x00u : 0xFFu);
        bank.sprites.coordinateXHigh[index] = static_cast<std::uint8_t>(at & 1u);
      }
      for (std::size_t index = 0; index < Elite::TRUMBLE_COORDINATE_COUNT; ++index)
      {
        bank.sprites.coordinates[index] = static_cast<std::uint8_t>(0x11u * (index + 1u) + _salt);
      }
      bank.sprites.coordinateMsb = static_cast<std::uint8_t>(0xA5u ^ _salt);
      bank.seed = {static_cast<std::uint8_t>(_salt + 1u), static_cast<std::uint8_t>(_salt * 7u), 0x9Cu,
                   static_cast<std::uint8_t>(_salt ^ 0x5Au)};
      return bank;
    }

    /*
     * One case, both sides, everything compared -- and it returns the port's `SETL1` calls so a
     * test that cares about them does not have to run the case twice.
     *
     * `MVTRIBS` is entered by `JMP` and leaves by `JMP NOMVETR`, so the oracle is stopped at
     * `NOMVETR` rather than on an `RTS`. `SETL1` is trapped on that side: it is self-modifying code
     * inside the raster handler (§6.59) and running it would rewrite an instruction the image needs
     * to keep for the next case.
     */
    static std::vector<std::uint8_t> Compare(Bank& _bank, std::uint8_t _counter, const std::wstring& _where)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t mvtribs = oracle.Label("MVTRIBS");
      const std::uint16_t nomvetr = oracle.Label("NOMVETR");
      const std::uint16_t tribct = oracle.Label("TRIBCT");
      const std::uint16_t tribvx = oracle.Label("TRIBVX");
      const std::uint16_t tribvxh = oracle.Label("TRIBVXH");
      const std::uint16_t tribxh = oracle.Label("TRIBXH");
      const std::uint16_t mcnt = oracle.Label("MCNT");
      const std::uint16_t rand = oracle.Label("RAND");
      const std::uint16_t setl1 = oracle.Label("SETL1");

      // 6502: VIC -- and in a flat image the label at that address is `XX21` (§6.108).
      const std::uint16_t vic = oracle.Label("XX21");

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(setl1);

      cpu.memory[tribct] = _bank.count;
      cpu.memory[mcnt] = _counter;
      for (std::size_t index = 0; index < Elite::TRUMBLE_VELOCITY_COUNT; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(tribvx + index)] = _bank.sprites.velocityX[index];
        cpu.memory[static_cast<std::uint16_t>(tribvxh + index)] = _bank.sprites.velocityXHigh[index];
        cpu.memory[static_cast<std::uint16_t>(tribxh + index)] = _bank.sprites.coordinateXHigh[index];
      }
      for (std::size_t index = 0; index < Elite::TRUMBLE_COORDINATE_COUNT; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(vic + 4u + index)] = _bank.sprites.coordinates[index];
      }
      cpu.memory[static_cast<std::uint16_t>(vic + 0x10u)] = _bank.sprites.coordinateMsb;
      for (std::size_t index = 0; index < 4u; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(rand + index)] = _bank.seed[index];
      }

      const Elite::Testing::RunResult run = cpu.CallSubroutine(mvtribs, 20'000, nomvetr);
      Assert::IsTrue(run.completed, (_where + L": MVTRIBS reached NOMVETR").c_str());

      Elite::Rng rng;
      rng.SetState(_bank.seed);
      _bank.sprites.count = _bank.count;

      RasterLog log;
      Elite::MoveTrumbleSprites(_bank.sprites, rng, _counter, log);

      for (std::size_t index = 0; index < Elite::TRUMBLE_VELOCITY_COUNT; ++index)
      {
        const std::wstring at = _where + L" [" + std::to_wstring(index) + L"]";
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(tribvx + index)], _bank.sprites.velocityX[index],
                         (at + L": TRIBVX").c_str());
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(tribvxh + index)], _bank.sprites.velocityXHigh[index],
                         (at + L": TRIBVXH").c_str());
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(tribxh + index)], _bank.sprites.coordinateXHigh[index],
                         (at + L": TRIBXH").c_str());
      }

      for (std::size_t index = 0; index < Elite::TRUMBLE_COORDINATE_COUNT; ++index)
      {
        const std::wstring at = _where + L" [" + std::to_wstring(index) + L"]";
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(vic + 4u + index)], _bank.sprites.coordinates[index],
                         (at + L": VIC+&04").c_str());
      }

      Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(vic + 0x10u)], _bank.sprites.coordinateMsb, (_where + L": VIC+&10").c_str());
      Assert::AreEqual(cpu.memory[tribct], _bank.sprites.count, (_where + L": TRIBCT").c_str());

      // 6502: RAND -- the generator, which is the only thing this routine changes that is not a
      // sprite. Two calls with different carries leave a different state from two with the same.
      const std::array<std::uint8_t, 4> state = rng.State();
      for (std::size_t index = 0; index < state.size(); ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(rand + index)], state[index],
                         (_where + L": RAND+" + std::to_wstring(index)).c_str());
      }

      // The oracle's `SETL1` calls, counted from the traps, against the port's.
      std::vector<std::uint8_t> theirs;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == setl1)
        {
          theirs.push_back(hit.a);
        }
      }

      Assert::AreEqual(theirs.size(), log.modes.size(), (_where + L": SETL1 calls").c_str());
      for (std::size_t index = 0; index < theirs.size(); ++index)
      {
        Assert::AreEqual(theirs[index], log.modes[index], (_where + L": SETL1 mode").c_str());
      }

      return log.modes;
    }
  };

} // namespace GameLogicTests
