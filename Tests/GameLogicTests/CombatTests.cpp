#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "Combat.h"
#include "Commander.h"
#include "ShipBlueprint.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * What happens after something is hit (slice 3d-d-iii-b's prerequisites).
 *
 * Five routines the flight loop's per-ship half needs before it can be written. All five are
 * small enough to sweep properly: the two explosions are exhaustive in the byte that picks their
 * volume, `OOPS` over every shield and bank state that changes its answer, and `OUCH` over every
 * slot in the block it can empty.
 */
namespace GameLogicTests
{

namespace
{
struct RecordingCombat final : Elite::DashboardEffects
{
  struct Pitched
  {
    std::uint8_t effect, sustain, frequency;
  };

  std::vector<std::uint8_t> sounds;
  std::vector<Pitched> pitched;

  bool PlaySound(std::uint8_t _effect) override { sounds.push_back(_effect); return true; }
  bool PlaySoundPitched(std::uint8_t _effect, std::uint8_t _sustain,
                        std::uint8_t _frequency) override
  {
    pitched.push_back({ _effect, _sustain, _frequency });
    return true;
  }
  void StopSound(std::uint8_t) override {}
};
} // namespace

TEST_CLASS(TheExplosions)
{
public:
  /*
   * 6502: EXNO and EXNO2's volume, over every byte that can pick it.
   *
   * Five levels chosen by four compares, and the two routines differ only in where the compares
   * sit -- 8/4/3/2 for a hit and 16/8/6/3 for a kill. Exhaustive in the distance byte because
   * that is the whole input, and the boundaries are exactly what a port gets wrong.
   */
  TEST_METHOD(TheExplosionVolumesMatchEXNOAndEXNO2)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t exno = oracle.Label("EXNO");
    const std::uint16_t noise2 = oracle.Label("NOISE2");
    const std::uint16_t inwk = oracle.Label("INWK");

    std::uint32_t compared = 0;
    std::set<std::uint8_t> levels;

    for (std::uint16_t distance = 0; distance < 256u; ++distance)
    {
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(noise2);
      cpu.memory[static_cast<std::uint16_t>(inwk + 7u)] = static_cast<std::uint8_t>(distance);

      const Elite::Testing::RunResult run = cpu.CallSubroutine(exno, 400'000);
      Assert::IsTrue(run.completed, L"EXNO returned");
      Assert::AreEqual<std::size_t>(1u, cpu.trapHits.size(), L"one NOISE2 call");

      const std::wstring where = WidenText("EXNO(" + std::to_string(distance) + ")");
      const std::uint8_t ours = Elite::ExplosionVolume(static_cast<std::uint8_t>(distance));

      Assert::AreEqual(cpu.trapHits[0].a, ours, (where + L": sustain").c_str());
      Assert::AreEqual<std::uint8_t>(Elite::EXPLOSION_PITCH_HIT, cpu.trapHits[0].x,
                                     (where + L": frequency").c_str());
      Assert::AreEqual<std::uint8_t>(Elite::SOUND_SHIP_EXPLODING, cpu.trapHits[0].y,
                                     (where + L": effect").c_str());

      levels.insert(ours);
      ++compared;
    }

    Assert::AreEqual<std::uint32_t>(256u, compared, L"the byte was swept");
    Assert::AreEqual<std::size_t>(5u, levels.size(), L"all five volumes were reached");
  }

  /*
   * 6502: EXNO2 -- the kill tally as well as the noise.
   *
   * Every ship type against tally states chosen to straddle both carries: the fraction's, which
   * is what makes most kills add nothing the player can see, and the whole byte's, which is what
   * prints "RIGHT ON COMMANDER" once every 256 kills.
   */
  TEST_METHOD(TheKillTallyMatchesEXNO2)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Where at(oracle);
    const std::uint16_t exno2 = oracle.Label("EXNO2");
    const std::uint16_t noise2 = oracle.Label("NOISE2");
    const std::uint16_t tallyl = oracle.Label("TALLYL");
    const std::uint16_t tally = oracle.Label("TALLY");
    const std::uint16_t inwk = oracle.Label("INWK");

    struct Start
    {
      std::uint8_t fraction, whole, high;
    };

    const Start STARTS[] = {
      { 0u, 0u, 0u }, { 200u, 0u, 0u }, { 255u, 0u, 0u },
      { 0u, 255u, 0u }, { 250u, 255u, 3u }, { 128u, 200u, 0u },
    };

    std::uint32_t compared = 0;
    std::uint32_t announced = 0;

    for (std::uint8_t type = 1u; type <= Elite::SHIP_TYPE_COUNT; ++type)
    {
      for (const Start& start : STARTS)
      {
        World world;
        Seed(world, type * 11u + start.whole);
        world.message.token = 101u;
        world.message.column = 9u;
        world.message.append = 1u;
        world.message.delay = 0u;
        world.commander.At(Elite::Field::KillsLow) = start.fraction;
        world.commander.At(Elite::Field::Kills) = start.whole;
        world.commander.bytes[static_cast<std::size_t>(Elite::Field::Kills) + 1u] = start.high;
        world.work[7] = static_cast<std::uint8_t>(type * 7u);

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(noise2);
        FillScreens(cpu, world.canvas, at.screen, 0x1Du);
        Mirror(world, cpu, at);
        cpu.memory[tallyl] = start.fraction;
        cpu.memory[tally] = start.whole;
        cpu.memory[static_cast<std::uint16_t>(tally + 1u)] = start.high;
        cpu.memory[at.mch] = world.message.token;
        cpu.memory[at.messxc] = world.message.column;
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = world.work[byte];
        }

        cpu.x = type;
        const Elite::Testing::RunResult run = cpu.CallSubroutine(exno2, 400'000);
        Assert::IsTrue(run.completed, L"EXNO2 returned");

        RecordingCombat effects;
        Elite::FlightScreen screen = world.Screen();
        const std::uint8_t ours = Elite::RecordKill(screen, effects, type);

        const std::wstring where =
          WidenText("EXNO2(type " + std::to_string(type) + ", tally "
                    + std::to_string(start.high) + "." + std::to_string(start.whole) + "."
                    + std::to_string(start.fraction) + ")");

        Assert::AreEqual<std::size_t>(1u, cpu.trapHits.size(), (where + L": one NOISE2").c_str());
        Assert::AreEqual(cpu.trapHits[0].a, ours, (where + L": sustain").c_str());
        Assert::AreEqual<std::uint8_t>(Elite::EXPLOSION_PITCH_KILL, cpu.trapHits[0].x,
                                       (where + L": frequency").c_str());
        Assert::AreEqual<std::uint8_t>(Elite::SOUND_EXPLOSION, cpu.trapHits[0].y,
                                       (where + L": effect").c_str());

        Assert::AreEqual(cpu.memory[tallyl], world.commander.At(Elite::Field::KillsLow),
                         (where + L": TALLYL").c_str());
        Assert::AreEqual(cpu.memory[tally], world.commander.At(Elite::Field::Kills),
                         (where + L": TALLY").c_str());
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(tally + 1u)],
                         world.commander.bytes[static_cast<std::size_t>(Elite::Field::Kills) + 1u],
                         (where + L": TALLY+1").c_str());

        CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where);
        Assert::AreEqual(cpu.memory[at.dly], world.message.delay, (where + L": DLY").c_str());
        Assert::AreEqual(cpu.memory[at.mch], world.message.token, (where + L": MCH").c_str());

        announced += (cpu.memory[at.mch] == 101u && start.whole == 255u) ? 1u : 0u;
        ++compared;
      }
    }

    Assert::AreEqual<std::uint32_t>(Elite::SHIP_TYPE_COUNT * 6u, compared, L"the sweep ran");
    Assert::IsTrue(announced > 0u, L"the top byte was carried into on some passes");
  }
};

TEST_CLASS(TheDamage)
{
public:
  /*
   * 6502: OOPS -- and the `SBC` at the top runs on the CALLER'S carry.
   *
   * Both entries are swept from both carries, because neither `MA58` nor `MA67` sets it: one
   * arrives from a `BCC` with it clear and the other from `LDA INWK+35 / SEC / ROR A`, which
   * leaves bit 0 of the ship's own energy in it. So the same hit takes one more point off the
   * shield depending on what hit us (§6.87).
   */
  TEST_METHOD(TheDamageMatchesOOPS)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Where at(oracle);
    const std::uint16_t oops = oracle.Label("OOPS");
    const std::uint16_t inf = oracle.Label("INF");
    const std::uint16_t noise = oracle.Label("NOISE");
    const std::uint16_t death = oracle.Label("DEATH");

    const std::uint8_t DAMAGE[] = { 0u, 1u, 20u, 60u, 100u, 255u };
    const std::uint8_t SHIELDS[] = { 0u, 1u, 20u, 60u, 255u };
    const std::uint8_t BANKS[] = { 0u, 1u, 40u, 200u, 255u };

    std::uint32_t compared = 0;
    std::uint32_t died = 0;
    std::uint32_t broke = 0;

    for (const std::uint8_t damage : DAMAGE)
    {
      for (const std::uint8_t shield : SHIELDS)
      {
        for (const std::uint8_t banks : BANKS)
        {
          for (std::uint8_t shape = 0; shape < 4u; ++shape)
          {
            const bool fromBehind = (shape & 1u) != 0u;
            const bool carryIn = (shape & 2u) != 0u;

            World world;
            Seed(world, damage + shield * 3u + banks * 7u + shape);
            world.message.token = 101u;
            world.message.column = 9u;
            world.message.append = 1u;
            world.message.delay = 0u;
            world.status.forwardShield = shield;
            world.status.aftShield = static_cast<std::uint8_t>(shield ^ 0x11u);
            world.status.energy = banks;
            world.bubble.blocks[2][8] = fromBehind ? 0x80u : 0x00u;

            // A hold with something in every slot, and a generator whose X lands inside it often
            // enough that `OUCH` actually breaks things -- `Seed`'s own state always picks slot
            // 34, which is past the end of the block and so never breaks anything at all.
            for (std::size_t index = 0; index < 22u; ++index)
            {
              world.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + index] = 6u;
            }
            world.rng.SetState(
              { 0u, static_cast<std::uint8_t>((damage + shield + banks + shape) % 30u), 0u, 0u });

            Cpu6502 cpu = oracle.Fresh();
            cpu.AddTrap(noise, Cpu6502::TrapExit::SetCarry);
            cpu.AddTrap(death);
            FillScreens(cpu, world.canvas, at.screen, 0x1Du);
            Mirror(world, cpu, at);
            cpu.memory[at.mch] = world.message.token;
            cpu.memory[at.messxc] = world.message.column;

            const std::uint16_t block =
              static_cast<std::uint16_t>(at.kPercent + 2u * Elite::SHIP_BLOCK_SIZE);
            cpu.memory[inf] = static_cast<std::uint8_t>(block & 0xFFu);
            cpu.memory[static_cast<std::uint16_t>(inf + 1u)] =
              static_cast<std::uint8_t>(block >> 8);

            cpu.a = damage;
            cpu.c = carryIn;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(oops, 400'000);
            Assert::IsTrue(run.completed, L"OOPS returned");

            RecordingCombat effects;
            Elite::FlightScreen screen = world.Screen();
            const bool alive =
              Elite::TakeDamage(screen, effects, world.bubble.blocks[2], damage, carryIn);

            const std::wstring where =
              WidenText("OOPS(damage " + std::to_string(damage) + ", shield "
                        + std::to_string(shield) + ", banks " + std::to_string(banks)
                        + (fromBehind ? ", aft" : ", fore")
                        + (carryIn ? ", carry set)" : ", carry clear)"));

            bool wentToDeath = false;
            for (const Cpu6502::TrapHit& hit : cpu.trapHits)
            {
              wentToDeath = wentToDeath || (hit.address == death);
            }
            Assert::AreEqual(!wentToDeath, alive, (where + L": survived").c_str());

            Assert::AreEqual(cpu.memory[at.fsh], world.status.forwardShield,
                             (where + L": FSH").c_str());
            Assert::AreEqual(cpu.memory[at.ash], world.status.aftShield,
                             (where + L": ASH").c_str());
            Assert::AreEqual(cpu.memory[at.energy], world.status.energy,
                             (where + L": ENERGY").c_str());

            if (!wentToDeath)
            {
              CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where);
              Assert::AreEqual(cpu.memory[at.dly], world.message.delay,
                               (where + L": DLY").c_str());
              Assert::AreEqual(cpu.memory[at.de], world.message.append, (where + L": de").c_str());
              for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.tp + byte)],
                                 world.commander.bytes[byte],
                                 (where + L": commander byte " + std::to_wstring(byte)).c_str());
              }
              for (std::size_t index = 0; index < 4u; ++index)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.rand + index)],
                                 world.rng.State()[index], (where + L": RAND").c_str());
              }
            }

            died += wentToDeath ? 1u : 0u;
            for (std::size_t index = 0; index < 22u; ++index)
            {
              const std::size_t byte = static_cast<std::size_t>(Elite::Field::CargoHold) + index;
              broke += (world.commander.bytes[byte] == 0u) ? 1u : 0u;
            }
            ++compared;
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(6u * 5u * 5u * 4u, compared, L"the whole sweep ran");
    Assert::IsTrue(died > 0u, L"some hits killed us");
    Assert::IsTrue(broke > 0u, L"and some broke a piece of equipment");
  }

  /*
   * 6502: OUCH -- every slot the generator can pick, by driving the generator rather than by
   * hoping a seed lands in range.
   *
   * `DORND` leaves the OLD `RAND+1` in X and `RAND+1 + RAND+3` (plus the first addition's carry)
   * in A, so with `RAND` and `RAND+2` at zero the slot is `RAND+1` and the coin toss is
   * `RAND+3`'s top bit. That makes the sweep exhaustive in the byte the routine actually
   * branches on -- every hold slot, every fitting, and four past the end of the block -- instead
   * of a scatter of seeds of which one in fifty reaches the interesting half.
   *
   * The carry is swept too, because `OOPS` reaches here through `EXNO3`'s tail call and hands
   * over whatever `NOISE` returned (§6.88): the same seed picks a different slot depending on
   * whether the explosion was audible.
   */
  TEST_METHOD(TheEquipmentDamageMatchesOUCH)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Where at(oracle);
    const std::uint16_t ouch = oracle.Label("OUCH");

    std::uint32_t compared = 0;
    std::uint32_t emptied = 0;
    std::uint32_t suppressed = 0;

    for (std::uint8_t slot = 0; slot < 26u; ++slot)
    {
      for (const std::uint8_t toss : { std::uint8_t{ 0 }, std::uint8_t{ 0x80 } })
      {
        for (const std::uint8_t already : { std::uint8_t{ 0 }, std::uint8_t{ 7 } })
        {
          for (const std::uint8_t held : { std::uint8_t{ 0 }, std::uint8_t{ 4 } })
          {
            for (std::uint8_t carry = 0; carry < 2u; ++carry)
            {
              World world;
              Seed(world, slot * 13u + toss + already + held + carry);
              world.message.token = 101u;
              world.message.column = 9u;
              world.message.append = 1u;
              world.message.delay = already;
              world.rng.SetState({ 0u, slot, 0u, toss });

              for (std::size_t index = 0; index < 22u; ++index)
              {
                world.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + index] =
                  held;
              }

              Cpu6502 cpu = oracle.Fresh();
              FillScreens(cpu, world.canvas, at.screen, 0x1Du);
              Mirror(world, cpu, at);

              cpu.c = (carry != 0u);
              const Elite::Testing::RunResult run = cpu.CallSubroutine(ouch, 400'000);
              Assert::IsTrue(run.completed, L"OUCH returned");

              Elite::FlightScreen screen = world.Screen();
              Elite::DamageEquipment(screen, carry != 0u);

              const std::wstring where =
                WidenText("OUCH(slot " + std::to_string(slot) + ", toss " + std::to_string(toss)
                          + ", DLY " + std::to_string(already) + ", held " + std::to_string(held)
                          + (carry != 0u ? ", carry set)" : ", carry clear)"));

              CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where);
              CompareState(cpu, world, at, where);
              Assert::AreEqual(cpu.memory[at.dly], world.message.delay, (where + L": DLY").c_str());
              Assert::AreEqual(cpu.memory[at.de], world.message.append, (where + L": de").c_str());
              Assert::AreEqual(cpu.memory[at.mch], world.message.token, (where + L": MCH").c_str());
              Assert::AreEqual(cpu.memory[at.messxc], world.message.column,
                               (where + L": messXC").c_str());
              for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.tp + byte)],
                                 world.commander.bytes[byte],
                                 (where + L": commander byte " + std::to_wstring(byte)).c_str());
              }

              const std::uint8_t after =
                (slot < 22u)
                  ? world.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + slot]
                  : held;
              emptied += (held != 0u && after == 0u) ? 1u : 0u;
              suppressed += (already != 0u && after == held) ? 1u : 0u;
              ++compared;
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(26u * 2u * 2u * 2u * 2u, compared, L"the whole sweep ran");
    Assert::IsTrue(emptied > 20u, L"every hold slot and fitting was broken at least once");
    Assert::IsTrue(suppressed > 0u, L"and a message already up suppressed the whole routine");
  }

  /// 6502: BOMBOFF -- five instructions, and both of the bytes they write are outside the game's
  /// own workspace: one is an operand and the other is a table the raster handler indexes.
  TEST_METHOD(TheBombEndingMatchesBOMBOFF)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t bomboff = oracle.Label("BOMBOFF");
    const std::uint16_t moonflower = oracle.Label("moonflower");
    const std::uint16_t welcome = oracle.Label("welcome");

    for (const std::uint8_t mode : { std::uint8_t{ 0xC0 }, std::uint8_t{ 0xD0 } })
    {
      for (const std::uint8_t flash : { std::uint8_t{ 0 }, std::uint8_t{ 5 } })
      {
        Cpu6502 cpu = oracle.Fresh();
        cpu.memory[moonflower] = mode;
        cpu.memory[welcome] = flash;

        const Elite::Testing::RunResult run = cpu.CallSubroutine(bomboff, 4000);
        Assert::IsTrue(run.completed, L"BOMBOFF returned");

        Elite::ScreenState screen;
        screen.upperBitmapMode = mode;
        screen.backgroundFlash = flash;
        Elite::StopEnergyBomb(screen);

        Assert::AreEqual(cpu.memory[moonflower], screen.upperBitmapMode, L"moonflower");
        Assert::AreEqual(cpu.memory[welcome], screen.backgroundFlash, L"welcome");
      }
    }
  }
};

} // namespace GameLogicTests
