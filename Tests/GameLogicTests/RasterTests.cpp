#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"
#include "Canvas.h"
#include "Raster.h"
#include "VideoState.h"

#include <array>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * 6502: COMIRQ1's VIC-II half (slice 4f).
 *
 * The same shape as the sound half's comparison and for the same reason (§6.129): what a VIC-II
 * sees is a SEQUENCE of register writes, so the interpreter logs the writes in order and the port's
 * are compared against them one at a time. A comparison of the registers' final state would agree
 * with a port that wrote them in any order, and the order is the whole of a raster split -- the
 * screen changes where the writes land, not where they finish.
 *
 * The handler is entered as the interrupt it is, with a return address and a status byte on the
 * stack, and runs to its `RTI`.
 *
 * TWO WRITES ARE NAMED EXCLUSIONS AND THEY ARE THE SAME KIND OF THING. `VIC+&19` is the interrupt
 * latch, acknowledged with `LDA / ORA #%10000000 / STA` on the way in; the port has no interrupt to
 * acknowledge, and in the flat oracle image that address is inside `XX21` so the value read back is
 * a ship blueprint pointer rather than a VIC-II flag. `l1` is the 6510 port register, which is the
 * `SetRasterMode` seam already. Neither reaches the screen.
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

    /// 6502: VIC -- the chip's registers, which the interpreter treats as memory.
    constexpr std::uint16_t VIC_BASE = 0xD000;

    /// 6502: VIC+&19, the interrupt latch -- acknowledged, never displayed. See the file comment.
    constexpr std::uint16_t VIC_INTERRUPT_LATCH = VIC_BASE + 0x19;

    struct RasterLabels
    {
      std::uint16_t comirq1, rastct, bomb, zebop, abraxas, moonflower, caravanserai, welcome, mupla;

      explicit RasterLabels(const OracleImage& _oracle)
        : comirq1(_oracle.Label("COMIRQ1")),
          rastct(_oracle.Label("RASTCT")),
          bomb(_oracle.Label("BOMB")),
          zebop(_oracle.Label("zebop")),
          abraxas(_oracle.Label("abraxas")),
          moonflower(_oracle.Label("moonflower")),
          caravanserai(_oracle.Label("caravanserai")),
          welcome(_oracle.Label("welcome")),
          mupla(_oracle.Label("MUPLA"))
      {
      }
    };

    void RunInterrupt(Cpu6502& _cpu, std::uint16_t _handler, const std::wstring& _where)
    {
      constexpr std::uint16_t STOP = 0xFFF9;
      _cpu.Push(static_cast<std::uint8_t>(STOP >> 8));
      _cpu.Push(static_cast<std::uint8_t>(STOP & 0xFFu));
      _cpu.Push(0x24u); // the flags the main loop would have: interrupts enabled, nothing else set
      _cpu.pc = _handler;

      for (std::uint32_t steps = 0; steps < 200'000u; ++steps)
      {
        if (_cpu.pc == STOP)
        {
          return;
        }
        Assert::IsTrue(_cpu.Step(), (_where + L": the handler hit an opcode the interpreter does not have").c_str());
      }
      Assert::Fail((_where + L": the handler did not return").c_str());
    }

    /// One pass of the port's model, flattened into the six writes the handler makes, in its order.
    struct Write
    {
      std::uint16_t address;
      std::uint8_t value;
    };

    std::array<Write, 6> WritesOf(const Elite::RasterRegisters& _registers)
    {
      return {{
        {VIC_BASE + 0x18, _registers.memoryPointers},
        {VIC_BASE + 0x16, _registers.control2},
        {VIC_BASE + 0x12, _registers.nextRasterLine},
        {VIC_BASE + 0x1C, _registers.spriteMulticolour},
        {VIC_BASE + 0x28, _registers.explosionColour},
        {VIC_BASE + 0x21, _registers.background},
      }};
    }
  } // namespace

  TEST_CLASS(TheRasterInterrupt)
  {
  public:
    /*
     * 6502: COMIRQ1 over both halves of the split, with and without the energy bomb.
     *
     * The state the handler reads is five bytes -- `RASTCT`, `BOMB`, `abraxas`, `caravanserai` and
     * `moonflower` -- plus `welcome`, which it also WRITES. The sweep crosses all of them: both
     * passes, the bomb on and off, the dashboard's two set-ups and the space view's two bitmap
     * modes, and four starting values of `welcome` including the one that wraps.
     */
    TEST_METHOD(TheRasterInterruptMatchesCOMIRQ1)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const RasterLabels at(oracle);

      // 6502: LDA #%11000000 is the default and #%11010000 is what the energy bomb stores, so the
      // pair is the whole range this byte takes.
      constexpr std::uint8_t MODES[] = {0xC0u, 0xD0u};

      // 6502: abraxas is &81 for a text view and &91 with the dashboard on it; caravanserai
      // follows it. Crossed rather than paired, because the handler reads them independently.
      constexpr std::uint8_t BANKS[] = {0x81u, 0x91u};

      // 255 is the one that matters: `INC welcome` wraps it to zero, and a port that saturated
      // would agree everywhere else.
      constexpr std::uint8_t FLASHES[] = {0x00u, 0x07u, 0x9Cu, 0xFFu};

      std::uint32_t compared = 0;

      for (const std::uint8_t counter : {0u, 1u})
      {
        for (const std::uint8_t bomb : {0x00u, 0x80u, 0x7Fu, 0xFFu})
        {
          for (const std::uint8_t upperMode : MODES)
          {
            for (const std::uint8_t lowerMode : MODES)
            {
              for (const std::uint8_t bank : BANKS)
              {
                for (const std::uint8_t flash : FLASHES)
                {
                  Cpu6502 cpu = oracle.Fresh();

                  /*
                   * The music is trapped and the sound is not reached.
                   *
                   * `COMIRQ1` falls into `SOINT` on the pass that leaves `RASTCT` at zero, and
                   * `SOINT` is slice 5a's and is compared there. `MUPLA` clear is what keeps the
                   * music player out of it; the sound writes that follow are outside the logged
                   * range, so they cannot reach this comparison either way.
                   */
                  cpu.memory[at.mupla] = 0u;

                  cpu.memory[at.rastct] = counter;
                  cpu.memory[at.bomb] = bomb;
                  cpu.memory[at.zebop] = Elite::RASTER_MEMORY_SPACE_VIEW;
                  cpu.memory[at.abraxas] = bank;
                  cpu.memory[at.moonflower] = upperMode;
                  cpu.memory[at.caravanserai] = lowerMode;
                  cpu.memory[at.welcome] = flash;
                  cpu.memory[static_cast<std::uint16_t>(at.welcome + 1)] = Elite::RASTER_BACKGROUND_DASHBOARD;

                  // The latch read is a blueprint pointer in a flat image, so it is pinned rather
                  // than left to whatever `XX21` holds -- see the file comment.
                  cpu.Io(VIC_INTERRUPT_LATCH) = 0u;

                  cpu.LogStores(VIC_BASE, static_cast<std::uint16_t>(VIC_BASE + 0x2E));
                  const std::wstring where =
                    Widen("COMIRQ1: RASTCT " + std::to_string(counter) + " BOMB " + std::to_string(bomb) + " upper "
                          + std::to_string(upperMode) + " lower " + std::to_string(lowerMode) + " bank " + std::to_string(bank)
                          + " welcome " + std::to_string(flash));
                  RunInterrupt(cpu, at.comirq1, where);

                  Elite::ScreenState screen{};
                  screen.rasterCounter = counter;
                  screen.colourBank = bank;
                  screen.bitmapMode = lowerMode;
                  screen.upperBitmapMode = upperMode;
                  screen.backgroundFlash = flash;

                  const Elite::RasterRegisters registers = Elite::TickRasterInterrupt(screen, bomb);
                  const std::array<Write, 6> want = WritesOf(registers);

                  // Every VIC write the handler made, in order, minus the latch acknowledge.
                  std::size_t index = 0;
                  for (const Cpu6502::StoreHit& store : cpu.stores)
                  {
                    if (store.address == VIC_INTERRUPT_LATCH)
                    {
                      continue;
                    }
                    Assert::IsTrue(index < want.size(), (where + L": the handler wrote more registers than the port").c_str());
                    const std::wstring at_write = where + L" write " + std::to_wstring(index);
                    Assert::AreEqual<int>(want[index].address, store.address, (at_write + L": register").c_str());
                    Assert::AreEqual<int>(want[index].value, store.value, (at_write + L": value").c_str());
                    ++index;
                  }
                  Assert::AreEqual<std::size_t>(want.size(), index, (where + L": how many VIC writes").c_str());

                  Assert::AreEqual<int>(cpu.memory[at.rastct], screen.rasterCounter, (where + L": RASTCT").c_str());
                  Assert::AreEqual<int>(cpu.memory[at.welcome], screen.backgroundFlash, (where + L": welcome").c_str());
                  Assert::AreEqual(counter == 0u, registers.spaceView, (where + L": which half").c_str());
                  ++compared;
                }
              }
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(2u * 4u * 2u * 2u * 2u * 4u, compared, L"the whole sweep ran");
      Logger::WriteMessage(("COMIRQ1: " + std::to_string(compared) + " interrupts, six VIC writes each").c_str());
    }

    /*
     * The split alternates, and it does so from either end.
     *
     * `innersec` is the only thing that moves `RASTCT`, so two passes must return it to where it
     * started -- and a frame is exactly two passes. This is the assertion that a port which
     * hardcoded "space view then dashboard" would pass and one that dropped `innersec` would not.
     */
    TEST_METHOD(TheSplitAlternates)
    {
      Elite::ScreenState screen{};

      screen.rasterCounter = 0u;
      Assert::IsTrue(Elite::TickRasterInterrupt(screen, 0u).spaceView, L"pass one is the space view");
      Assert::AreEqual<int>(1, screen.rasterCounter, L"and leaves the dashboard next");
      Assert::IsFalse(Elite::TickRasterInterrupt(screen, 0u).spaceView, L"pass two is the dashboard");
      Assert::AreEqual<int>(0, screen.rasterCounter, L"and comes back round");
    }

    /*
     * 6502: moonflower's bit 4 reaching the screen -- the energy bomb, resolved.
     *
     * The complement of `ResolveDrawsTheGameScreenInStandardBitmapMode`, on the SAME asymmetric
     * byte, because that is what makes the two decodes distinguishable: %10110001 is eight one-bit
     * pixels one way and four two-bit codes the other, and a symmetric byte would let either pass.
     *
     * What the bomb does is not a tint. The bytes on the screen do not change at all -- the VIC-II
     * is told to read them differently, so a ship's outline becomes half-width blocks in four
     * colours, one of which (%00) is the background the interrupt is busy incrementing. That is the
     * whole effect, and no colour-per-pixel canvas could have expressed it (ADR-002 §7).
     */
    TEST_METHOD(TheBombPutsTheSpaceViewIntoMulticolour)
    {
      constexpr int Y = 60;
      constexpr int CELL_COLUMN = (Elite::Canvas::SPACE_VIEW_MARGIN + 64) / 8;
      constexpr std::uint8_t BITS = 0xB1u; // %10110001 -- as codes: %10, %11, %00, %01

      Elite::Canvas canvas;
      canvas.Write(static_cast<std::uint16_t>((Y / 8) * Elite::Canvas::ROW_BYTES + CELL_COLUMN * 8 + (Y % 8)), BITS);

      const int cell = (Y / 8) * Elite::Canvas::CELL_COLUMNS + CELL_COLUMN;
      canvas.Screen()[Elite::Canvas::SCREEN_CELLS + cell] = 0x27; // red for %01, yellow for %10
      canvas.SetCellColour(cell, 5);                             // green for %11
      canvas.SetSpaceViewBackground(13);                         // and the flashing colour for %00

      std::array<std::uint8_t, Elite::Canvas::WIDTH * Elite::Canvas::HEIGHT> image{};

      // Bomb off: the space view is standard, and neither colour RAM nor the background is read.
      canvas.Resolve(image);
      const std::uint8_t* quiet = image.data() + Y * Elite::Canvas::WIDTH + CELL_COLUMN * 8;
      for (int pixel = 0; pixel < 8; ++pixel)
      {
        const std::uint32_t expected = (((BITS >> (7 - pixel)) & 1u) != 0u) ? 2u : 7u;
        Assert::AreEqual<std::uint32_t>(expected, quiet[pixel], L"standard mode: one bit is one pixel");
      }

      // 6502: LDY #%11010000 / STY moonflower.
      canvas.SetSpaceViewMulticolour(true);
      canvas.Resolve(image);
      const std::uint8_t* loud = image.data() + Y * Elite::Canvas::WIDTH + CELL_COLUMN * 8;

      Assert::AreEqual<std::uint32_t>(7, loud[0], L"%10 takes the cell's low nibble");
      Assert::AreEqual<std::uint32_t>(7, loud[1], L"and is two columns wide");
      Assert::AreEqual<std::uint32_t>(5, loud[2], L"%11 takes colour RAM, which standard mode never reads");
      Assert::AreEqual<std::uint32_t>(5, loud[3], L"and is two columns wide");
      Assert::AreEqual<std::uint32_t>(13, loud[4], L"%00 takes the space view's own background");
      Assert::AreEqual<std::uint32_t>(13, loud[5], L"and is two columns wide");
      Assert::AreEqual<std::uint32_t>(2, loud[6], L"%01 takes the cell's high nibble");
      Assert::AreEqual<std::uint32_t>(2, loud[7], L"and is two columns wide");
    }

    /*
     * 6502: santana and lotus -- how the original keeps explosions out of the dashboard.
     *
     * NOT A CLIP RECTANGLE. `COMIRQ1` writes %11111110 to VIC+&1C above the split, which makes
     * sprite 1 multicolour, and 2 to VIC+&28, which is red; below the split it writes %11111100 and
     * 0, which makes the same sprite single-colour in colour 0. A single-colour sprite in colour 0
     * paints nothing, so the burst simply stops existing at row 144 -- and the upstream comment
     * says that is the point.
     *
     * The port read the mode off the DEFINITION until slice 4f, on the stated grounds that the game
     * never writes VIC+&1C. It writes it twice a frame. So the explosion was drawn hi-res in colour
     * 0 -- invisible everywhere -- and would have drawn over the dashboard the moment a colour was
     * given to it (§6.155).
     */
    TEST_METHOD(TheExplosionIsMulticolourAboveTheSplitAndGoneBelowIt)
    {
      Elite::Canvas canvas;

      // 6502: SPOFF% + 4 -- the explosion cloud is the fifth of the seven definitions.
      canvas.Write(static_cast<std::uint16_t>(Elite::Canvas::SCREEN_CELLS + 0x3F8u + Elite::EXPLOSION_SPRITE),
                   static_cast<std::uint8_t>(Elite::SPRITE_POINTER_ORIGIN + 4u));

      Elite::VideoState video{};
      Elite::ApplySpritesEnabled(video, 1u << Elite::EXPLOSION_SPRITE);

      std::array<std::uint8_t, Elite::Canvas::WIDTH * Elite::Canvas::HEIGHT> image{};

      // High in the space view: multicolour, and `lotus`'s red is what %10 takes.
      video.x[Elite::EXPLOSION_SPRITE] = Elite::SPRITE_ORIGIN_X + 100u;
      video.y[Elite::EXPLOSION_SPRITE] = Elite::SPRITE_ORIGIN_Y + 40u;
      canvas.Resolve(image, video);

      std::size_t painted = 0;
      bool red = false;
      for (const std::uint8_t pixel : image)
      {
        painted += (pixel != 0u) ? 1u : 0u;
        red = red || (pixel == 2u);
      }
      Assert::IsTrue(painted > 0u, L"the burst is drawn in the space view");
      Assert::IsTrue(red, L"and lotus's red is on the screen");

      // Wholly below the split: single colour, and that colour is zero.
      video.y[Elite::EXPLOSION_SPRITE] =
        static_cast<std::uint8_t>(Elite::SPRITE_ORIGIN_Y + Elite::Canvas::SPACE_VIEW_HEIGHT + 4);
      image.fill(0u);
      canvas.Resolve(image, video);

      for (const std::uint8_t pixel : image)
      {
        Assert::AreEqual<std::uint32_t>(0u, pixel, L"below the split the burst paints nothing at all");
      }
    }

    /*
     * And a burst that straddles the split is drawn on one side of it only.
     *
     * The VIC-II decides the mode as it scans, so one sprite is two sprites when it crosses row
     * 144. The port decides per screen row for that reason, and this is the assertion that a model
     * deciding once per sprite would fail whichever way it decided.
     */
    TEST_METHOD(ABurstAcrossTheSplitIsCutOffAtIt)
    {
      Elite::Canvas canvas;
      canvas.Write(static_cast<std::uint16_t>(Elite::Canvas::SCREEN_CELLS + 0x3F8u + Elite::EXPLOSION_SPRITE),
                   static_cast<std::uint8_t>(Elite::SPRITE_POINTER_ORIGIN + 4u));

      Elite::VideoState video{};
      Elite::ApplySpritesEnabled(video, 1u << Elite::EXPLOSION_SPRITE);
      video.x[Elite::EXPLOSION_SPRITE] = Elite::SPRITE_ORIGIN_X + 100u;

      // Ten rows above the split, so eleven of the sprite's twenty-one land below it.
      video.y[Elite::EXPLOSION_SPRITE] = static_cast<std::uint8_t>(Elite::SPRITE_ORIGIN_Y + Elite::Canvas::SPACE_VIEW_HEIGHT - 10);

      std::array<std::uint8_t, Elite::Canvas::WIDTH * Elite::Canvas::HEIGHT> image{};
      canvas.Resolve(image, video);

      std::size_t above = 0;
      std::size_t below = 0;
      for (int y = 0; y < Elite::Canvas::HEIGHT; ++y)
      {
        for (int x = 0; x < Elite::Canvas::WIDTH; ++x)
        {
          if (image[static_cast<std::size_t>(y) * Elite::Canvas::WIDTH + x] == 0u)
          {
            continue;
          }
          ((y < Elite::Canvas::SPACE_VIEW_HEIGHT) ? above : below) += 1u;
        }
      }

      Assert::IsTrue(above > 0u, L"the rows in the space view are drawn");
      Assert::AreEqual<std::size_t>(0u, below, L"and the rows over the dashboard are not");
    }

    /*
     * And the dashboard keeps its own background, which is the reason the port holds two.
     *
     * `welcome` and `welcome+1` are one VIC-II register written twice a frame, so a port with one
     * background byte would flash the dashboard along with the space view. Nothing in the game ever
     * writes `welcome+1`, and the upstream comment says so.
     */
    TEST_METHOD(TheDashboardDoesNotFlashWithTheSpaceView)
    {
      Elite::Canvas canvas;
      canvas.SetDashboardShown(true);
      canvas.SetBackground(Elite::RASTER_BACKGROUND_DASHBOARD);
      canvas.SetSpaceViewMulticolour(true);
      canvas.SetSpaceViewBackground(13);

      std::array<std::uint8_t, Elite::Canvas::WIDTH * Elite::Canvas::HEIGHT> image{};
      canvas.Resolve(image);

      // An untouched cell is all %00, so each half shows its own background and nothing else.
      const int spaceRow = 60;
      const int dashRow = Elite::Canvas::DASHBOARD_CELL_ROW * 8 + 4;
      Assert::AreEqual<std::uint32_t>(13, image[static_cast<std::size_t>(spaceRow) * Elite::Canvas::WIDTH + 8],
                                      L"the space view is flashing");
      Assert::AreEqual<std::uint32_t>(0, image[static_cast<std::size_t>(dashRow) * Elite::Canvas::WIDTH + 8],
                                      L"and the dashboard is not");
    }

    /*
     * 6502: BIT BOMB / BPL nobombef / INC welcome -- twice a frame, not once.
     *
     * The increment is ABOVE the split test, so both passes reach it and the space view's
     * background moves two steps per frame. Nothing else in the game writes `welcome` except
     * `BOMBOFF`, which zeroes it.
     */
    TEST_METHOD(TheBombFlashesTwicePerFrame)
    {
      Elite::ScreenState screen{};
      screen.backgroundFlash = 0u;

      const Elite::RasterRegisters upper = Elite::TickRasterInterrupt(screen, Elite::BOMB_RUNNING);
      Assert::AreEqual<int>(1, upper.background, L"the space view takes the colour just incremented");

      const Elite::RasterRegisters lower = Elite::TickRasterInterrupt(screen, Elite::BOMB_RUNNING);
      Assert::AreEqual<int>(2, screen.backgroundFlash, L"and the dashboard pass increments it too");
      Assert::AreEqual<int>(Elite::RASTER_BACKGROUND_DASHBOARD, lower.background, L"but shows its own byte, which is fixed");

      // Bit 7 and nothing else: 0x7F is every other bit set and must not flash.
      screen.backgroundFlash = 0u;
      (void)Elite::TickRasterInterrupt(screen, 0x7Fu);
      Assert::AreEqual<int>(0, screen.backgroundFlash, L"a bomb byte without bit 7 is not a bomb");
    }

    /*
     * THE BYTE THE BOMB PUTS ON THE BUS IS NOT A COLOUR INDEX, and this is the test that says so.
     *
     * `welcome` is a counter and `COMIRQ1` stores it whole into VIC+&21, so eight frames of bomb
     * carry it past 15 and it keeps going to 255 and wraps. The VIC-II keeps four bits of it; a
     * port that resolves its canvas into indices for a sixteen-entry palette must do the same, and
     * this one did not until slice 5a. `TheRasterInterruptMatchesCOMIRQ1` already proves the STORE
     * is right at &9C and &FF -- the defect was one step further on, where nothing looked.
     */
    TEST_METHOD(TheBombFlashCountsPastEveryColourTheChipHas)
    {
      Elite::ScreenState screen{};
      screen.backgroundFlash = 0u;

      Elite::RasterRegisters space{};
      for (int pass = 0; pass < 40; ++pass)
      {
        const Elite::RasterRegisters registers = Elite::TickRasterInterrupt(screen, Elite::BOMB_RUNNING);
        if (registers.spaceView)
        {
          space = registers;
        }
      }

      Assert::AreEqual<int>(40, screen.backgroundFlash, L"forty passes, forty increments");
      Assert::IsTrue(space.background > 15u, L"and the byte VIC+&21 is given is past every colour there is");

      // The canvas is the chip: what it holds is four bits of what it was given, whatever it was
      // given, so no resolved pixel can index past the palette.
      Elite::Canvas canvas;
      canvas.SetSpaceViewBackground(space.background);
      Assert::AreEqual<int>(space.background & 0x0F, Elite::ColourIndex(canvas.SpaceViewBackground()), L"the register latched it");

      canvas.SetSpaceViewMulticolour(true);
      std::array<std::uint8_t, Elite::Canvas::WIDTH * Elite::Canvas::HEIGHT> image{};
      canvas.Resolve(image);
      for (std::size_t pixel = 0; pixel < image.size(); ++pixel)
      {
        Assert::IsTrue(image[pixel] < 16u, L"every resolved index is a colour the palette has");
      }
    }
  };

} // namespace GameLogicTests
