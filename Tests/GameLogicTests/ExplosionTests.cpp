#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "Explosion.h"
#include "LineHeap.h"
#include "Rng.h"
#include "ShipDraw.h"
#include "ShipSlot.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The explosion cloud (slice 4b-b).
 *
 * `DOEXP` is a drawing routine whose every pixel comes out of the random generator, so there is
 * nothing statistical about this comparison: the port either walks the same generator in the same
 * order and puts the same marks in the same places, or the screens differ on the first particle.
 * That makes the whole canvas the assertion, exactly as it is for `LL9`.
 *
 * WHAT IS DELIBERATELY NOT COMPARED, and why:
 *
 *   `frump`, `sprx`, `spry` -- RAM in the original, locals here. Each is written and read inside
 *   one call and no other routine in the shipped build touches any of them (grepped, not assumed).
 *
 *   `K3+0` to `K3+3` -- the four bytes `PTCLS` scatters a vertex through. They are `XX2+0` to
 *   `XX2+3` as well, the aliasing `GeometryWorkspace::xx2` already warns about, and the port keeps
 *   them as locals: `DOEXP` is the last thing that happens to a ship in a frame and every reader
 *   of `XX2`, `K3` and `K4` writes them before reading them.
 *
 *   `SC` -- `PTCLS2` stores the sprite's low x byte there and so does every `PIXEL` call. The port
 *   has diverged on `SC` since slice 1d-a because `PlotPixel` computes a flat offset instead, and
 *   that is recorded on `DrawWorkspace::sc` rather than being new here.
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

    /// Somewhere in the arena between `K%` and `LS%` for the exploding ship's line heap, with room
    /// for the longest explosion count any blueprint asks for (54, the Anaconda).
    constexpr std::uint16_t HEAP_AT = 0xFE00;

    /// 6502: K%, the first slot -- the PLANET's, which is where `PTCLS` gets the byte it drops into
    /// `RAND+3` on the way out.
    constexpr std::uint16_t SLOT_AT = Elite::SHIP_BLOCK_BASE;

    /// 6502: VIC = &D000 and l1 = &0001, from the constants block at the top of the C64 source.
    /// Neither is a label, so neither is in `Labels.txt` and both are written out here.
    constexpr std::uint16_t VIC = 0xD000;
    constexpr std::uint16_t IO_PORT = 0x0001;

    /// Seeds for the two registers `PTCLS2` READS before it writes them, chosen so that a port
    /// which overwrote the whole byte instead of one bit would fail.
    constexpr std::uint8_t SPRITE_X_HIGH_SEED = 0xA5;
    constexpr std::uint8_t SPRITE_ENABLE_SEED = 0x51;
    constexpr std::uint8_t IO_PORT_SEED = 0xE7;

    /// And the four it only writes, seeded too so that "PTCLS wrote nothing" is an assertion
    /// rather than two untouched bytes of the assembled image happening to agree.
    constexpr std::uint8_t SPRITE_EXPAND_SEED = 0x3C;
    constexpr std::uint8_t SPRITE_X_SEED = 0x77;
    constexpr std::uint8_t SPRITE_Y_SEED = 0x99;

    /// Put all seven where both sides can see them.
    void SeedVideoRegisters(Cpu6502& _cpu)
    {
      _cpu.memory[static_cast<std::uint16_t>(VIC + 0x02u)] = SPRITE_X_SEED;
      _cpu.memory[static_cast<std::uint16_t>(VIC + 0x03u)] = SPRITE_Y_SEED;
      _cpu.memory[static_cast<std::uint16_t>(VIC + 0x10u)] = SPRITE_X_HIGH_SEED;
      _cpu.memory[static_cast<std::uint16_t>(VIC + 0x15u)] = SPRITE_ENABLE_SEED;
      _cpu.memory[static_cast<std::uint16_t>(VIC + 0x17u)] = SPRITE_EXPAND_SEED;
      _cpu.memory[static_cast<std::uint16_t>(VIC + 0x1Du)] = SPRITE_EXPAND_SEED;
      _cpu.memory[IO_PORT] = IO_PORT_SEED;
    }

    /// 6502: SCBASE, derived as `CanvasTests.cpp` derives it -- from ylookup's first entry less the
    /// space view's four-cell left margin, because it is an assembler constant and not a label.
    std::uint16_t ScreenBase(const OracleImage& _oracle)
    {
      const Cpu6502 cpu = _oracle.Fresh();
      const std::uint16_t low = _oracle.Label("ylookupl");
      const std::uint16_t high = _oracle.Label("ylookuph");
      return static_cast<std::uint16_t>((cpu.memory[low] | (cpu.memory[high] << 8)) - 0x20u);
    }

    void CompareScreens(const Cpu6502& _cpu, std::uint16_t _screenBase, const Elite::Canvas& _canvas, const std::wstring& _context)
    {
      const std::span<const std::uint8_t> ours = _canvas.Screen();
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_screenBase + offset)];
        if (expected != ours[offset])
        {
          Assert::Fail((_context + L": the screen differs at offset " + std::to_wstring(offset) + L" -- game has " +
                        std::to_wstring(expected) + L", port has " + std::to_wstring(ours[offset]))
                         .c_str());
        }
      }
    }

    void CompareHeaps(const Cpu6502& _cpu, const Elite::LineHeap& _heap, const std::wstring& _context)
    {
      for (std::uint16_t offset = 0; offset < 256u; ++offset)
      {
        const std::uint16_t address = static_cast<std::uint16_t>(HEAP_AT + offset);
        Assert::AreEqual(_cpu.memory[address], _heap.Read(address), (_context + L": heap byte " + std::to_wstring(offset)).c_str());
      }
    }

    /// The four generator bytes, which every routine in this file destroys on purpose.
    void CompareSeeds(const Cpu6502& _cpu, const OracleImage& _oracle, const Elite::Rng& _rng, const std::wstring& _context)
    {
      const std::uint16_t rand = _oracle.Label("RAND");
      for (std::size_t byte = 0; byte < 4u; ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(rand + byte)], _rng.State()[byte],
                         (_context + L": RAND+" + std::to_wstring(byte)).c_str());
      }
    }

    /// The workspace bytes `DOEXP`, `PTCLS` and `EXS1` leave behind between them.
    void CompareWorkspace(const Cpu6502& _cpu, const OracleImage& _oracle, const Elite::MathWorkspace& _math,
                          const Elite::DrawWorkspace& _draw, const std::wstring& _context)
    {
      const auto same = [&](const char* _name, std::uint8_t _ours)
      {
        const std::uint16_t at = _oracle.Label(_name);
        Assert::AreEqual(_cpu.memory[at], _ours, (_context + L": " + Widen(_name)).c_str());
      };

      same("Q", _math.q);
      same("R", _math.r);
      same("P", _math.p);
      same("S", _math.s);
      same("T", _math.t);
      same("U", _math.u);
      same("CNT", _math.cnt);
      same("TGT", _math.tgt);
      same("ZZ", _draw.zz);
      same("Y1", _draw.y1);
    }

    /*
     * The seam `PTCLS2` places its burst through, recorded rather than acted on.
     *
     * The test turns what it records back into the six VIC-II registers the original writes and
     * compares those against the shipped code's memory, so the ARGUMENTS are checked and not just
     * the fact that a call happened. Doing it here rather than in `GameLogic` is the point of the
     * seam: the read-modify-writes belong to whoever owns the hardware.
     */
    class RecordingBurst final : public Elite::ExplosionEffects
    {
    public:
      std::vector<std::uint8_t> rasterModes;
      std::vector<std::uint8_t> expansions;
      std::vector<std::uint16_t> spriteX;
      std::vector<std::uint8_t> spriteY;

      void SetRasterMode(std::uint8_t _mode) override
      {
        rasterModes.push_back(_mode);
      }
      void SetSpriteExpansion(std::uint8_t _mask) override
      {
        expansions.push_back(_mask);
      }
      void ShowExplosionSprite(std::uint16_t _x, std::uint8_t _y) override
      {
        spriteX.push_back(_x);
        spriteY.push_back(_y);
      }
    };

    /// Nothing is expected to reach the seam -- `PTCLS` has no sprite in it, and any call is a
    /// failure rather than something to record.
    class NoBurst final : public Elite::ExplosionEffects
    {
    public:
      std::uint32_t calls = 0;

      void SetRasterMode(std::uint8_t) override
      {
        ++calls;
      }
      void SetSpriteExpansion(std::uint8_t) override
      {
        ++calls;
      }
      void ShowExplosionSprite(std::uint16_t, std::uint8_t) override
      {
        ++calls;
      }
    };

    /*
     * Turn what the seam recorded into the registers the original writes, and compare.
     *
     * VIC+&17 and VIC+&1D take the expansion byte whole; VIC+&2 and VIC+&3 take the last sprite
     * placed; VIC+&10 keeps its other seven bits and takes the ninth x bit in bit 1; VIC+&15 keeps
     * its other seven and has bit 1 set. `l1` keeps its top five bits and takes the raster mode in
     * its bottom three, which is what `SETL1` does.
     */
    void CompareBurstRegisters(const Cpu6502& _cpu, const RecordingBurst& _burst, const std::wstring& _context)
    {
      std::uint8_t xHigh = SPRITE_X_HIGH_SEED;
      std::uint8_t enable = SPRITE_ENABLE_SEED;
      std::uint8_t port = IO_PORT_SEED;
      std::uint8_t expansion = SPRITE_EXPAND_SEED;
      std::uint8_t lowX = SPRITE_X_SEED;
      std::uint8_t row = SPRITE_Y_SEED;

      for (const std::uint8_t mode : _burst.rasterModes)
      {
        port = static_cast<std::uint8_t>((port & 0xF8u) | mode);
      }
      if (!_burst.expansions.empty())
      {
        expansion = _burst.expansions.back();
      }
      for (std::size_t placed = 0; placed < _burst.spriteX.size(); ++placed)
      {
        lowX = static_cast<std::uint8_t>(_burst.spriteX[placed] & 0xFFu);
        row = _burst.spriteY[placed];
        xHigh = static_cast<std::uint8_t>((xHigh & 0xFDu) | ((_burst.spriteX[placed] >> 8) << 1));
        enable = static_cast<std::uint8_t>(enable | 0x02u);
      }

      Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(VIC + 0x17u)], expansion, (_context + L": VIC+&17").c_str());
      Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(VIC + 0x1Du)], expansion, (_context + L": VIC+&1D").c_str());
      Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(VIC + 0x02u)], lowX, (_context + L": VIC+&2").c_str());
      Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(VIC + 0x03u)], row, (_context + L": VIC+&3").c_str());
      Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(VIC + 0x10u)], xHigh, (_context + L": VIC+&10").c_str());
      Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(VIC + 0x15u)], enable, (_context + L": VIC+&15").c_str());
      Assert::AreEqual(_cpu.memory[IO_PORT], port, (_context + L": the 6510 port register").c_str());
    }

    /*
     * Where a ship's visible vertices sit on the screen, as `LL9` part 8 would have left them in
     * `XX3`: x as sixteen bits, then y.
     *
     * There are six of them because the burst sprite has four ways of being refused and the
     * particles have two, and every one of those is a comparison against a coordinate. A layout
     * that kept the cloud in the middle of the screen would leave all six unreached and still
     * compare ten thousand canvas bytes per case, which is the shape §6.132 named.
     */
    struct Layout
    {
      const wchar_t* what;
      std::vector<int> xs;
      std::vector<int> ys;
    };

    const std::vector<Layout> LAYOUTS = {
      {L"around the middle of the view", {128, 100, 156, 118, 140}, {72, 60, 84, 50, 96}},
      {L"out to the edges of the view", {8, 248, 128, 200, 40}, {8, 130, 72, 100, 20}},
      {L"off the left edge", {-100, -60, -20, 30, 128}, {72, 40, 100, 60, 80}},
      {L"past the right edge of a nine-bit coordinate", {480, 500, 460, 300, 128}, {72, 40, 100, 60, 80}},
      {L"low enough that the burst is behind the dashboard", {128, 100, 156, 118, 140}, {160, 170, 150, 180, 165}},
      {L"below the screen entirely", {128, 100, 156, 118, 140}, {300, 320, 280, 400, 260}},
      // 194 is `2*Y+50` and the offset is 40 or 30 depending on the burst's size, so these five
      // rows straddle the comparison for both: 153/154 for the small sprite, 163/164 for the big.
      {L"either side of the row the burst is refused at", {128, 100, 156, 118, 140}, {153, 154, 163, 164, 72}},
    };

    /// The bytes those vertices become. The explosion count is `4 * n + 6`, so this produces n of
    /// them and the heap copy in `DOEXP` walks back down to byte 7.
    std::vector<std::uint8_t> Vertices(std::uint8_t _count, const Layout& _layout)
    {
      std::vector<std::uint8_t> bytes;
      const std::size_t vertices = (_count > 6u) ? static_cast<std::size_t>((_count - 6u) / 4u) : 0u;

      for (std::size_t vertex = 0; vertex < vertices; ++vertex)
      {
        const int x = _layout.xs[vertex % _layout.xs.size()];
        const int y = _layout.ys[vertex % _layout.ys.size()];
        bytes.push_back(static_cast<std::uint8_t>(x & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(y & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((y >> 8) & 0xFF));
      }
      return bytes;
    }

    /// One exploding ship, set up identically on both sides.
    struct Scene
    {
      std::uint8_t zLow;
      std::uint8_t zHigh;
      std::uint8_t state;   ///< 6502: INWK+31
      std::uint8_t counter; ///< 6502: byte 1 of the heap -- the cloud counter
      std::uint8_t count;   ///< 6502: byte 2 -- the explosion count, 4n + 6
      const wchar_t* what;
    };

    /*
     * 6502: INWK+31 -- bit 3 is "on the screen", bit 5 "exploding", bit 6 "the cloud is drawn".
     *
     * `EE55` leaves the counter at 18, so a scene with 18 is the frame that draws the burst sprite
     * and a scene with anything else is a later one. The counters at 250 and above are the ones
     * that overflow, and they differ by distance because the addition takes the distance test's
     * carry: near ships age by four and far ones by five.
     */
    const std::vector<Scene> SCENES = {
      {0x00, 0x02, 0x28, 18, 22, L"the first frame, with nothing to rub out"},
      {0x00, 0x02, 0x68, 18, 22, L"the first frame over a cloud that is already there"},
      {0x00, 0x02, 0x68, 22, 22, L"the second frame"},
      {0x00, 0x08, 0x68, 90, 34, L"halfway, further out"},
      {0x00, 0x08, 0x68, 130, 34, L"past halfway, so the cloud is thinning again"},
      {0x00, 0x06, 0x68, 18, 22, L"close enough for a double-size burst"},
      {0x00, 0x07, 0x68, 18, 22, L"one unit further, so a single-size burst"},
      {0xFF, 0x1F, 0x68, 42, 54, L"just inside the distance cap, with twelve vertices"},
      {0x00, 0x20, 0x68, 42, 54, L"one unit past it, so the cloud ages by five"},
      {0x00, 0x40, 0x68, 18, 22, L"a long way off"},
      {0x00, 0x02, 0x68, 250, 22, L"the last frame a near ship gets"},
      {0x00, 0x02, 0x68, 254, 22, L"the frame that ends a near explosion"},
      {0x00, 0x40, 0x68, 251, 22, L"the frame that ends a far one, one step sooner"},
      {0x00, 0x02, 0x20, 18, 22, L"exploding with nothing on the screen"},
      {0x00, 0x02, 0x60, 34, 22, L"a cloud to rub out and nothing on the screen"},
      {0x00, 0x02, 0x68, 18, 10, L"the missile, whose cloud has one vertex"},
      {0x00, 0x01, 0x68, 62, 22, L"almost touching, so the distance floor applies"},
      {0x00, 0x00, 0x68, 62, 22, L"z_hi of zero, where the ORA #1 is all that saves it"},
    };

  } // namespace

  TEST_CLASS(TheExplosionCloud)
  {
  public:
    /*
     * 6502: EXS1 -- the arithmetic every particle goes through, twice.
     *
     * Swept on its own because three of the routine's borrowed flags are in it: the carry the
     * generator leaves feeds the `ROL A` that doubles the multiplier, that rotate's carry chooses
     * which half of the cloud the particle lands in, and `FMLTU`'s exit carry feeds the `ADC R` or
     * the `SBC T` with no `CLC` or `SEC` between. Getting any of the three wrong moves particles
     * by one pixel, which a whole-screen comparison would find and a spot check would not.
     */
    TEST_METHOD(TheParticleOffsetMatchesEXS1)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t exs1 = oracle.Label("EXS1");
      const std::uint16_t rand = oracle.Label("RAND");
      const std::uint16_t qq = oracle.Label("Q");
      const std::uint16_t rr = oracle.Label("R");
      const std::uint16_t ss = oracle.Label("S");
      const std::uint16_t tt = oracle.Label("T");

      // The cloud sizes are the interesting ones: zero is `FMLTU`'s second early exit, and the
      // rest span the range `DOEXP` can produce (it caps at 254).
      const std::vector<std::uint8_t> SIZES = {0, 1, 8, 64, 127, 128, 200, 254};
      const std::vector<std::uint8_t> HIGH = {0, 1, 0x7F, 0x80, 0xFF};
      const std::vector<std::uint8_t> LOW = {0, 1, 72, 128, 200, 255};
      /*
       * The generator's four bytes, and there are twelve of them because the SEED alone decides
       * which half of the cloud a particle lands in -- `Q`, `R` and the coordinate do not reach
       * that branch. Five seeds gave 960 cases down one side and 240 down the other, which is one
       * answer per seed dressed up as a sweep (§6.132's lesson, met again).
       */
      const std::vector<std::array<std::uint8_t, 4>> SEEDS = {{0, 0, 0, 0},
                                                              {1, 2, 3, 4},
                                                              {0x5A, 0xA5, 0x3C, 0xC3},
                                                              {0xFF, 0xFF, 0xFF, 0xFF},
                                                              {0x80, 0x01, 0x40, 0x02},
                                                              {0x11, 0x22, 0x33, 0x44},
                                                              {0x7F, 0x80, 0x7F, 0x80},
                                                              {0xC0, 0x30, 0x0C, 0x03},
                                                              {0x2A, 0x55, 0xAA, 0xD5},
                                                              {0x00, 0xFF, 0x00, 0xFF},
                                                              {0x9E, 0x4D, 0xE1, 0x76},
                                                              {0x08, 0x91, 0x27, 0xBA}};

      std::uint32_t compared = 0;
      std::uint32_t added = 0;
      std::uint32_t subtracted = 0;

      for (const std::uint8_t size : SIZES)
      {
        for (const std::uint8_t high : HIGH)
        {
          for (const std::uint8_t low : LOW)
          {
            for (const std::array<std::uint8_t, 4>& seed : SEEDS)
            {
              Cpu6502 cpu = oracle.Fresh();
              cpu.memory[qq] = size;
              cpu.memory[rr] = low;
              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(rand + byte)] = seed[byte];
              }
              cpu.a = high;

              // A sentinel in `T`, because only the subtracting half writes it -- so this is what
              // tells the two halves apart exactly rather than by guessing from the answer.
              cpu.memory[tt] = 0x5Cu;

              // The entry carry is the caller's `LDA K3` and cannot be set, so it is not swept:
              // `EXS1` opens `STA S` and then runs the generator with the carry forced clear.
              const Elite::Testing::RunResult run = cpu.CallSubroutine(exs1, 20'000);
              Assert::IsTrue(run.completed, L"EXS1 returned");

              Elite::MathWorkspace math;
              Elite::Rng rng;
              math.q = size;
              math.r = low;
              math.t = 0x5Cu;
              rng.SetState(seed);

              const Elite::ExplosionOffset offset = Elite::OffsetByCloud(math, rng, high);

              const std::wstring where = Widen("EXS1(Q=" + std::to_string(size) + ", A=" + std::to_string(high) +
                                               ", R=" + std::to_string(low) + ", seed=" + std::to_string(seed[0]) + "): ");

              Assert::AreEqual(cpu.a, offset.high, (where + L"the high byte").c_str());
              Assert::AreEqual(cpu.x, offset.low, (where + L"the low byte").c_str());
              Assert::AreEqual(cpu.memory[ss], math.s, (where + L"S").c_str());
              Assert::AreEqual(cpu.memory[tt], math.t, (where + L"T").c_str());
              Assert::AreEqual(cpu.memory[rr], math.r, (where + L"R").c_str());
              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(rand + byte)], rng.State()[byte],
                                 (where + L"RAND+" + std::to_wstring(byte)).c_str());
              }

              if (math.t != 0x5Cu)
              {
                ++subtracted;
              }
              else
              {
                ++added;
              }
              ++compared;
            }
          }
        }
      }

      Logger::WriteMessage(("EXS1: " + std::to_string(compared) + " offsets compared, " + std::to_string(added) + " added and " +
                            std::to_string(subtracted) + " subtracted")
                             .c_str());

      Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(SIZES.size() * HIGH.size() * LOW.size() * SEEDS.size()), compared,
                                      L"every combination was compared");
      Assert::IsTrue(added > 0u, L"the adding half ran");
      Assert::IsTrue(subtracted > 0u, L"the subtracting half ran");
    }

    /*
     * 6502: DOEXP with PTCLS and PTCLS2 under it -- the whole of the explosion.
     *
     * The acceptance criterion for the slice, and it is compared on everything the routine can
     * touch: the entire canvas, the entire line heap, all of `INWK`, the four generator bytes, the
     * ten workspace bytes, and -- through the seam's recorded arguments -- the six VIC-II registers
     * and the 6510 port register that `PTCLS2` writes.
     *
     * Every scene runs against every vertex layout rather than one apiece, because which of the
     * routine's rejections fire is decided by where the vertices are and how big the cloud is, and
     * those are independent.
     */
    TEST_METHOD(TheCloudMatchesDOEXP)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t xx3 = oracle.Label("XX3");
      const std::uint16_t rand = oracle.Label("RAND");
      const std::uint16_t doexp = oracle.Label("DOEXP");
      const std::uint16_t screenBase = ScreenBase(oracle);

      constexpr std::uint8_t PLANET_Z_LOW = 0x39; // 6502: K%+6, which lands in RAND+3 on the way out
      constexpr std::array<std::uint8_t, 4> CLOUD_SEEDS = {0x4B, 0x1D, 0xF2, 0x86};
      constexpr std::array<std::uint8_t, 4> START_SEEDS = {0x9C, 0x27, 0x63, 0xB1};

      std::uint32_t compared = 0;
      std::uint32_t drawn = 0;
      std::uint32_t finished = 0;
      std::uint32_t offScreen = 0;
      std::uint32_t bursts = 0;
      std::uint32_t burstsRefused = 0;
      std::uint32_t agedByFive = 0;
      std::uint32_t cappedSize = 0;
      std::uint32_t scaledSize = 0;

      for (const Scene& scene : SCENES)
      {
        for (const Layout& layout : LAYOUTS)
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::Canvas canvas;
          Elite::DrawWorkspace draw;
          Elite::MathWorkspace math;
          Elite::GeometryWorkspace geometry;
          Elite::Rng rng;
          Elite::ShipBlock work{};
          Elite::LineHeap heap;
          Elite::Bubble bubble;
          RecordingBurst burst;

          // The heap: a stale cloud size, the counter, the explosion count, four seed bytes, and a
          // recognisable pattern above them so that a copy which did not happen is visible.
          std::vector<std::uint8_t> bytes = {0x2C, scene.counter, scene.count};
          bytes.insert(bytes.end(), CLOUD_SEEDS.begin(), CLOUD_SEEDS.end());
          for (std::uint16_t byte = 7; byte < 128u; ++byte)
          {
            bytes.push_back(static_cast<std::uint8_t>(byte ^ 0x5Au));
          }
          for (std::uint16_t offset = 0; offset < 256u; ++offset)
          {
            const std::uint16_t address = static_cast<std::uint16_t>(HEAP_AT + offset);
            const std::uint8_t value = (offset < bytes.size()) ? bytes[offset] : std::uint8_t{0};
            cpu.memory[address] = value;
            heap.Write(address, value);
          }

          const std::vector<std::uint8_t> vertices = Vertices(scene.count, layout);
          for (std::uint16_t byte = 0; byte < 192u; ++byte)
          {
            const std::uint8_t value = (byte < vertices.size()) ? vertices[byte] : std::uint8_t{0};
            cpu.memory[static_cast<std::uint16_t>(xx3 + byte)] = value;
            geometry.xx3[byte] = value;
          }

          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = 0;
            work[byte] = 0;
          }
          const auto poke = [&](std::size_t _offset, std::uint8_t _value)
          {
            cpu.memory[static_cast<std::uint16_t>(inwk + _offset)] = _value;
            work[_offset] = _value;
          };
          poke(Elite::SHIP_Z_OFFSET, scene.zLow);
          poke(Elite::SHIP_Z_OFFSET + 1u, scene.zHigh);
          poke(Elite::SHIP_STATE_OFFSET, scene.state);
          poke(Elite::SHIP_HEAP_LOW_OFFSET, HEAP_AT & 0xFFu);
          poke(Elite::SHIP_HEAP_HIGH_OFFSET, HEAP_AT >> 8);

          cpu.memory[static_cast<std::uint16_t>(SLOT_AT + 6u)] = PLANET_Z_LOW;
          bubble.blocks[0][6] = PLANET_Z_LOW;

          for (std::size_t byte = 0; byte < 4u; ++byte)
          {
            cpu.memory[static_cast<std::uint16_t>(rand + byte)] = START_SEEDS[byte];
          }
          rng.SetState(START_SEEDS);

          SeedVideoRegisters(cpu);

          const Elite::Testing::RunResult run = cpu.CallSubroutine(doexp);
          Assert::IsTrue(run.completed, L"DOEXP returned");

          Elite::DrawExplosionCloud(canvas, draw, math, rng, work, heap, geometry, bubble, burst);

          const std::wstring where = std::wstring(scene.what) + L", " + layout.what;

          CompareScreens(cpu, screenBase, canvas, where);
          CompareHeaps(cpu, heap, where);
          CompareSeeds(cpu, oracle, rng, where);
          CompareWorkspace(cpu, oracle, math, draw, where);
          CompareBurstRegisters(cpu, burst, where);

          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + byte)], work[byte],
                             (where + L": INWK+" + std::to_wstring(byte)).c_str());
          }

          // Which of the routine's four endings this scene reached, counted so that a sweep which
          // stopped exercising one of them says so.
          if (heap.Read(static_cast<std::uint16_t>(HEAP_AT + 1u)) == scene.counter)
          {
            ++finished;
          }
          else if ((scene.state & Elite::SHIP_STATE_DRAWN) == 0u)
          {
            ++offScreen;
          }
          else
          {
            ++drawn;
          }

          if (!burst.expansions.empty())
          {
            ++bursts;
            if (burst.spriteX.empty())
            {
              ++burstsRefused;
            }
          }
          if (scene.zHigh >= 32u && heap.Read(static_cast<std::uint16_t>(HEAP_AT + 1u)) != scene.counter)
          {
            ++agedByFive;
          }

          /*
           * Did the cloud size cap at 254, or come out of the three shifts?
           *
           * The two are told apart exactly rather than approximately: the shifting path is
           * `8 * P + bits` for a P below 28, so it cannot exceed 223, and 254 in byte 0 of the heap
           * can only be the `LDA #&FE`.
           */
          if (heap.Read(static_cast<std::uint16_t>(HEAP_AT + 1u)) != scene.counter)
          {
            if (heap.Read(HEAP_AT) == 0xFEu)
            {
              ++cappedSize;
            }
            else
            {
              ++scaledSize;
            }
          }
          ++compared;
        }
      }

      Logger::WriteMessage(("DOEXP: " + std::to_string(compared) + " frames compared -- " + std::to_string(drawn) + " drawn, " +
                            std::to_string(finished) + " ending the explosion, " + std::to_string(offScreen) + " with nothing shown, " +
                            std::to_string(bursts) + " placing a burst sprite (" + std::to_string(burstsRefused) + " of them refused), " +
                            std::to_string(agedByFive) + " ageing by five rather than four, " + std::to_string(cappedSize) +
                            " with the cloud size capped and " + std::to_string(scaledSize) + " with it scaled")
                             .c_str());

      Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(SCENES.size() * LAYOUTS.size()), compared, L"every scene and layout");
      Assert::IsTrue(drawn > 0u, L"a cloud was drawn");
      Assert::IsTrue(finished > 0u, L"an explosion ran out");
      Assert::IsTrue(offScreen > 0u, L"a ship with nothing on the screen returned early");
      Assert::IsTrue(bursts > 0u, L"the burst sprite was sized");
      Assert::IsTrue(burstsRefused > 0u, L"and refused a position at least once");
      Assert::IsTrue(agedByFive > 0u, L"a distant cloud aged by five");
      Assert::IsTrue(cappedSize > 0u, L"a cloud was big enough to cap");
      Assert::IsTrue(scaledSize > 0u, L"and one was not");
    }

    /*
     * 6502: PTCLS and PTCLS2 called directly, which is how the two entry points are told apart.
     *
     * `DOEXP` reaches `PTCLS2` on exactly one frame of an explosion and `PTCLS` on all the others,
     * so a comparison that only went through `DOEXP` would exercise the burst once per scene and
     * would never show that the two share a body. This calls each of them on the same heap and
     * asserts what the difference is: the same canvas, the same generator, and a sprite from one
     * of them.
     */
    TEST_METHOD(TheTwoEntryPointsMatchPTCLSAndPTCLS2)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t rand = oracle.Label("RAND");
      const std::uint16_t ptcls = oracle.Label("PTCLS");
      const std::uint16_t ptcls2 = oracle.Label("PTCLS2");
      const std::uint16_t screenBase = ScreenBase(oracle);

      constexpr std::uint8_t PLANET_Z_LOW = 0x71;
      constexpr std::array<std::uint8_t, 4> CLOUD_SEEDS = {0x33, 0xEE, 0x07, 0x9A};
      constexpr std::array<std::uint8_t, 4> START_SEEDS = {0x12, 0xAB, 0x5F, 0x60};

      // The sizes are what `DOEXP` can leave in byte 0, and the counters walk both halves of the
      // cloud's life -- under 128 it grows, over 128 the complement makes it shrink.
      const std::vector<std::uint8_t> SIZES = {1, 8, 40, 128, 254};
      const std::vector<std::uint8_t> COUNTERS = {18, 66, 127, 128, 200, 250};

      std::uint32_t compared = 0;
      std::uint32_t plotted = 0;
      std::uint32_t placed = 0;

      // Per layout, so that "the burst was refused" is a statement about each of the four ways of
      // being refused rather than about the total. Every layout is designed for one of them.
      std::vector<std::uint32_t> placedBy(LAYOUTS.size(), 0u);
      std::vector<std::uint32_t> offeredBy(LAYOUTS.size(), 0u);

      for (const std::uint8_t size : SIZES)
      {
        for (const std::uint8_t counter : COUNTERS)
        {
          for (const Layout& layout : LAYOUTS)
          {
            for (int withSprite = 0; withSprite < 2; ++withSprite)
            {
              Cpu6502 cpu = oracle.Fresh();
              Elite::Canvas canvas;
              Elite::DrawWorkspace draw;
              Elite::MathWorkspace math;
              Elite::Rng rng;
              Elite::ShipBlock work{};
              Elite::LineHeap heap;
              Elite::Bubble bubble;
              RecordingBurst burst;
              NoBurst refused;

              constexpr std::uint8_t COUNT = 30; // six vertices
              std::vector<std::uint8_t> bytes = {size, counter, COUNT};
              bytes.insert(bytes.end(), CLOUD_SEEDS.begin(), CLOUD_SEEDS.end());
              const std::vector<std::uint8_t> vertices = Vertices(COUNT, layout);
              bytes.insert(bytes.end(), vertices.begin(), vertices.end());

              for (std::uint16_t offset = 0; offset < 256u; ++offset)
              {
                const std::uint16_t address = static_cast<std::uint16_t>(HEAP_AT + offset);
                const std::uint8_t value = (offset < bytes.size()) ? bytes[offset] : std::uint8_t{0};
                cpu.memory[address] = value;
                heap.Write(address, value);
              }

              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = 0;
                work[byte] = 0;
              }
              // z_hi decides the burst's size, and it is read by `PTCLS2` alone.
              const std::uint8_t zHigh = static_cast<std::uint8_t>((size & 1u) != 0u ? 4u : 12u);
              cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_Z_OFFSET + 1u)] = zHigh;
              work[Elite::SHIP_Z_OFFSET + 1u] = zHigh;
              cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_LOW_OFFSET)] = HEAP_AT & 0xFFu;
              cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_HIGH_OFFSET)] = HEAP_AT >> 8;
              work[Elite::SHIP_HEAP_LOW_OFFSET] = HEAP_AT & 0xFFu;
              work[Elite::SHIP_HEAP_HIGH_OFFSET] = HEAP_AT >> 8;

              cpu.memory[static_cast<std::uint16_t>(SLOT_AT + 6u)] = PLANET_Z_LOW;
              bubble.blocks[0][6] = PLANET_Z_LOW;

              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(rand + byte)] = START_SEEDS[byte];
              }
              rng.SetState(START_SEEDS);

              SeedVideoRegisters(cpu);

              const Elite::Testing::RunResult run = cpu.CallSubroutine(withSprite != 0 ? ptcls2 : ptcls);
              Assert::IsTrue(run.completed, L"the routine returned");

              if (withSprite != 0)
              {
                Elite::DrawExplosionParticlesWithSprite(canvas, draw, math, rng, work, heap, bubble, burst);
              }
              else
              {
                Elite::DrawExplosionParticles(canvas, draw, math, rng, work, heap, bubble);
              }

              const std::wstring where =
                Widen((withSprite != 0 ? "PTCLS2(" : "PTCLS(") + std::to_string(size) + ", " + std::to_string(counter) + "): ") +
                layout.what;

              CompareScreens(cpu, screenBase, canvas, where);
              CompareHeaps(cpu, heap, where);
              CompareSeeds(cpu, oracle, rng, where);
              CompareWorkspace(cpu, oracle, math, draw, where);
              CompareBurstRegisters(cpu, burst, where);
              Assert::AreEqual<std::uint32_t>(0u, refused.calls, (where + L": PTCLS has no sprite in it").c_str());

              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + byte)], work[byte],
                                 (where + L": INWK+" + std::to_wstring(byte)).c_str());
              }

              if (canvas.Screen()[0x1000] != 0u || draw.zz != 0u)
              {
                ++plotted;
              }
              placed += static_cast<std::uint32_t>(burst.spriteX.size());
              if (withSprite != 0)
              {
                const std::size_t which = static_cast<std::size_t>(&layout - LAYOUTS.data());
                placedBy[which] += static_cast<std::uint32_t>(burst.spriteX.size());
                offeredBy[which] += 6u; // six vertices per cloud, at an explosion count of 30
              }
              ++compared;
            }
          }
        }
      }

      Logger::WriteMessage(
        ("PTCLS and PTCLS2: " + std::to_string(compared) + " clouds compared, " + std::to_string(placed) + " burst sprites placed")
          .c_str());

      Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(SIZES.size() * COUNTERS.size() * LAYOUTS.size() * 2u), compared,
                                      L"every size, counter, layout and entry point");
      Assert::IsTrue(plotted > 0u, L"particles were drawn");
      Assert::IsTrue(placed > 0u, L"and a burst sprite was placed");

      for (std::size_t layout = 0; layout < LAYOUTS.size(); ++layout)
      {
        Logger::WriteMessage(("  layout " + std::to_string(layout) + ": " + std::to_string(placedBy[layout]) + " of " +
                              std::to_string(offeredBy[layout]) + " bursts placed")
                               .c_str());
      }

      Assert::AreEqual(offeredBy[0], placedBy[0], L"a cloud in the middle of the view places every burst");
      for (std::size_t layout = 2; layout < LAYOUTS.size(); ++layout)
      {
        Assert::IsTrue(placedBy[layout] < offeredBy[layout],
                       (L"the burst was refused for the layout that exists to refuse it: " + std::wstring(LAYOUTS[layout].what)).c_str());
      }
    }
  };

} // namespace GameLogicTests
