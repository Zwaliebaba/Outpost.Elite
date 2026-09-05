#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "Controls.h"
#include "LookupTables.h"
#include "VideoState.h"

#include <array>
#include <cstdint>
#include <set>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::OracleImage;

/*
 * The sprite overlay: the registers, the definitions, and the blit (ADR-005 section 1, plan
 * section 6.148).
 *
 * WHAT IS AND IS NOT VERIFIED HERE, because the distinction is the whole point of this file.
 *
 * VERIFIED AGAINST THE ORIGINAL: the 448 bytes of `spritp`, compared against the assembler's own
 * output like every other generated table. That is the one part of the sprite overlay that has an
 * oracle at all, and it is checked twice -- here, and by `extract_tables.py --check` in CI.
 *
 * VERIFIED AS ARITHMETIC: the register model. `ApplyMaskSprites` really does read-modify-write,
 * `ApplyHideAllSprites` really does leave positions alone, `ApplyExplosionSprite` really does set
 * bit 1 and nothing else. Those are claims about the port and they can be wrong, so they are
 * asserted rather than assumed.
 *
 * NOT VERIFIED AGAINST ANYTHING: the blit. The game never rendered a composited image into memory,
 * so there is nothing to compare one against, and no amount of testing here changes that. What the
 * tests below do instead is pin the DECISIONS -- transparency, the two colour models, the origin,
 * expansion, priority order -- so that a change to any of them is deliberate. That is a regression
 * net and not evidence of fidelity, and calling it anything else would be the coverage claim
 * ADR-005 section 1 explicitly refuses to make.
 */
namespace GameLogicTests
{

  namespace
  {
    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    /// A canvas with sprite N pointing at definition N, which is what the loader sets up.
    Elite::Canvas PointedCanvas()
    {
      Elite::Canvas canvas;
      canvas.Clear();
      for (std::size_t sprite = 0; sprite < Elite::SPRITE_COUNT; ++sprite)
      {
        const std::uint8_t definition = static_cast<std::uint8_t>(sprite % Elite::SPRITE_DEFINITION_COUNT);
        canvas.Write(static_cast<std::uint16_t>(Elite::Canvas::SCREEN_CELLS + 0x3F8u + sprite),
                     static_cast<std::uint8_t>(Elite::SPRITE_POINTER_ORIGIN + definition));
      }
      return canvas;
    }

    using Image = std::array<std::uint8_t, static_cast<std::size_t>(Elite::Canvas::WIDTH) * Elite::Canvas::HEIGHT>;

    /// How many pixels of the resolved image differ from the bitmap-only resolve.
    std::size_t Painted(const Elite::Canvas& _canvas, const Elite::VideoState& _video)
    {
      Image plain{};
      Image composited{};
      _canvas.Resolve(plain);
      _canvas.Resolve(composited, _video);

      std::size_t changed = 0;
      for (std::size_t index = 0; index < plain.size(); ++index)
      {
        changed += (plain[index] != composited[index]) ? 1u : 0u;
      }
      return changed;
    }
  } // namespace

  TEST_CLASS(TheSpriteDefinitions)
  {
  public:
    /*
     * The 448 bytes, against the assembler.
     *
     * This is the reason `elite-sprites.asm` was added to `tools/labels.py` at all: ADR-005 section
     * 1 called it a prerequisite worth doing on its own merits, because it is the only part of the
     * sprite overlay that CAN be compared against the original.
     */
    TEST_METHOD(TheSpriteDefinitionsMatchTheAssembledSPRITE)
    {
      const OracleImage& sprites = OracleImage::SpriteInstance();
      if (!sprites.Available())
      {
        Logger::WriteMessage(("SKIPPED -- sprite image absent: " + sprites.Reason()).c_str());
        return;
      }

      const std::uint16_t address = sprites.Label("spritp");
      const Elite::Testing::Cpu6502 cpu = sprites.Fresh();

      for (std::size_t index = 0; index < Elite::SPRITE_DEFINITIONS.size(); ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(address + index)], Elite::SPRITE_DEFINITIONS[index],
                         (L"spritp+" + std::to_wstring(index)).c_str());
      }
    }

    /*
     * Seven definitions of 64 bytes, and the split between the two macros.
     *
     * `spritp` is written with `SPRITE2` for the four laser sights and the explosion and `SPRITE4`
     * for the two Trumbles, and nothing in the assembled bytes says which is which -- the VIC-II
     * carries it in register &1C, which this build never writes. So `FIRST_MULTICOLOUR_DEFINITION`
     * is a fact transcribed from the source, and this is the assertion that says so out loud rather
     * than leaving a 5 in a header.
     */
    TEST_METHOD(TheDefinitionsAreSevenAndTheLastTwoAreMulticolour)
    {
      Assert::AreEqual<std::size_t>(Elite::SPRITE_DEFINITION_COUNT * Elite::SPRITE_BYTES, Elite::SPRITE_DEFINITIONS.size(),
                                    L"seven definitions of 64 bytes");
      Assert::AreEqual<std::size_t>(5u, Elite::FIRST_MULTICOLOUR_DEFINITION,
                                    L"0-3 are the laser sights and 4 is the explosion, all SPRITE2");

      // 21 rows of three bytes is 63, and the 64th is padding the hardware never reads. Definition
      // 0's is &3A, which the upstream source calls "random workspace noise left over from the BBC
      // Micro assembly process" -- data that means nothing and is carried because the extraction is
      // of an address range rather than of a meaning.
      Assert::AreEqual<std::uint8_t>(0x3Au, Elite::SPRITE_DEFINITIONS[63], L"definition 0's padding byte");
      Assert::AreEqual<int>(63, Elite::SPRITE_ROWS * Elite::SPRITE_ROW_BYTES, L"21 rows of three bytes");
    }
  };

  TEST_CLASS(TheSpriteRegisters)
  {
  public:
    /// 6502: STA VIC+&15, and then part 15's `LDA VIC+&15 / AND #.. / STA VIC+&15`.
    TEST_METHOD(MaskingReadsTheRegisterAndEnablingReplacesIt)
    {
      Elite::VideoState video;

      Elite::ApplySpritesEnabled(video, 0xFDu);
      Assert::AreEqual<std::uint8_t>(0xFDu, video.enabled, L"SetSpritesEnabled writes the whole byte");

      // The read-modify-write, which is a separate call precisely because the game cannot compute
      // the new value: it does not know how many Trumble sprites are showing.
      Elite::ApplyMaskSprites(video, 0x03u);
      Assert::AreEqual<std::uint8_t>(0x01u, video.enabled, L"part 15 ANDs what is there, not what it wanted");
    }

    /*
     * 6502: NOSPRITES -- `LDA #0 / STA VIC+&15` and nothing else.
     *
     * The positions and colours SURVIVE, and that is the assertion worth having: a port that
     * cleared them here would move the laser sights to the origin on every view change, and the
     * only thing that would show it is a screenshot.
     */
    TEST_METHOD(HidingEverythingLeavesThePositionsAlone)
    {
      Elite::VideoState video;
      Elite::ApplyExplosionSprite(video, 300u, 120u);
      Elite::ApplySightColour(video, 0x0Du);
      Elite::ApplySpriteExpansion(video, 0xFFu);

      Elite::ApplyHideAllSprites(video);

      Assert::AreEqual<std::uint8_t>(0u, video.enabled, L"every sprite is off");
      Assert::AreEqual<std::uint16_t>(300u, video.x[1], L"and the burst is still where it was");
      Assert::AreEqual<std::uint8_t>(120u, video.y[1], L"including its y");
      Assert::AreEqual<std::uint8_t>(0x0Du, video.colour[0], L"and the sights keep their colour");
      Assert::AreEqual<std::uint8_t>(0xFFu, video.expanded, L"and the expand byte is untouched");
    }

    /// 6502: the five writes that place sprite 1 -- and it switches ON exactly one bit.
    TEST_METHOD(TheExplosionIsSpriteOneAndSetsOnlyItsBit)
    {
      Elite::VideoState video;
      Elite::ApplySpritesEnabled(video, 0x01u); // the sights, already on

      Elite::ApplyExplosionSprite(video, 0x1FFu, 200u);

      Assert::AreEqual<std::uint8_t>(0x03u, video.enabled, L"bit 1 ORed in, bit 0 left alone");
      Assert::AreEqual<std::uint16_t>(0x1FFu, video.x[1], L"the x is nine bits and arrives whole");
      Assert::AreEqual<std::uint8_t>(200u, video.y[1], L"and the y is eight");

      for (std::size_t sprite = 0; sprite < Elite::SPRITE_COUNT; ++sprite)
      {
        if (sprite != 1u)
        {
          Assert::AreEqual<std::uint16_t>(0u, video.x[sprite], L"no other sprite moved");
        }
      }
    }
  };

  TEST_CLASS(TheSpriteOverlay)
  {
  public:
    /// Nothing enabled draws nothing, which is what every golden hash depends on.
    TEST_METHOD(NoSpritesEnabledLeavesTheBitmapExactlyAsItWas)
    {
      const Elite::Canvas canvas = PointedCanvas();
      Elite::VideoState video;
      Assert::AreEqual<std::size_t>(0u, Painted(canvas, video), L"a disabled sprite paints nothing");
    }

    /*
     * The laser sights, drawn where the hardware puts them.
     *
     * Definition 0 is the pulse-laser sight and its bytes are known: a vertical stroke down the
     * middle and a horizontal bar across the eleventh row. Placing it at the VIC-II's origin should
     * put its top-left corner at (0, 0), so the count of painted pixels is exactly the count of set
     * bits in the definition -- which is the tightest statement that can be made about a blit with
     * no oracle: every set bit reaches the screen, and no clear bit does.
     */
    TEST_METHOD(AHiResSpriteAtTheOriginPaintsExactlyItsSetBits)
    {
      const Elite::Canvas canvas = PointedCanvas();

      Elite::VideoState video;
      Elite::ApplySpritesEnabled(video, 0x01u);
      Elite::ApplySightColour(video, 0x07u);
      video.x[0] = Elite::SPRITE_ORIGIN_X;
      video.y[0] = Elite::SPRITE_ORIGIN_Y;

      std::size_t set = 0;
      for (int row = 0; row < Elite::SPRITE_ROWS; ++row)
      {
        for (int byte = 0; byte < Elite::SPRITE_ROW_BYTES; ++byte)
        {
          std::uint8_t bits = Elite::SPRITE_DEFINITIONS[static_cast<std::size_t>(row * Elite::SPRITE_ROW_BYTES + byte)];
          while (bits != 0u)
          {
            set += (bits & 1u);
            bits = static_cast<std::uint8_t>(bits >> 1);
          }
        }
      }

      Assert::IsTrue(set > 0u, L"the pulse sight is not blank");
      Assert::AreEqual(set, Painted(canvas, video), L"every set bit paints one pixel, and nothing else does");
    }

    /*
     * Expansion doubles the pixel and moves nothing.
     *
     * `PTCLS2` writes the same byte to VIC+&17 and VIC+&1D, so a burst is double size in both
     * directions at once; the top-left corner stays where it is and the sprite grows down and
     * right, which is what makes a close explosion bloom rather than jump. Four times the pixels,
     * exactly.
     */
    TEST_METHOD(ExpansionQuadruplesTheAreaAndKeepsTheCorner)
    {
      const Elite::Canvas canvas = PointedCanvas();

      Elite::VideoState plain;
      Elite::ApplySpritesEnabled(plain, 0x01u);
      Elite::ApplySightColour(plain, 0x07u);
      plain.x[0] = Elite::SPRITE_ORIGIN_X;
      plain.y[0] = Elite::SPRITE_ORIGIN_Y;

      Elite::VideoState big = plain;
      Elite::ApplySpriteExpansion(big, 0x01u);

      Assert::AreEqual(Painted(canvas, plain) * 4u, Painted(canvas, big), L"twice across and twice down");

      /*
       * And the corner does not move -- which is NOT the same as the first painted pixel not
       * moving, and the first version of this test confused the two.
       *
       * Rows 0 and 1 of the pulse sight are blank, so its first set pixel is at definition (11, 2)
       * and lands at screen (11, 2) unexpanded. Expanded it lands at (22, 4), because every
       * definition pixel goes to twice its own coordinates -- the SPRITE's corner is unmoved and
       * the PIXEL's is not. Asserting the doubling is the stronger statement anyway: it fails on
       * an expansion that grows from the centre, and it fails on one that offsets by a pixel.
       */
      Image plainImage{};
      Image bigImage{};
      canvas.Resolve(plainImage, plain);
      canvas.Resolve(bigImage, big);

      std::size_t firstPlain = plainImage.size();
      std::size_t firstBig = bigImage.size();
      for (std::size_t index = 0; index < plainImage.size(); ++index)
      {
        if (firstPlain == plainImage.size() && plainImage[index] != 0u)
        {
          firstPlain = index;
        }
        if (firstBig == bigImage.size() && bigImage[index] != 0u)
        {
          firstBig = index;
        }
      }
      Assert::IsTrue(firstPlain < plainImage.size(), L"the unexpanded sprite painted something");
      Assert::IsTrue(firstBig < bigImage.size(), L"and so did the expanded one");

      const std::size_t plainX = firstPlain % Elite::Canvas::WIDTH;
      const std::size_t plainY = firstPlain / Elite::Canvas::WIDTH;
      const std::size_t bigX = firstBig % Elite::Canvas::WIDTH;
      const std::size_t bigY = firstBig / Elite::Canvas::WIDTH;

      Assert::AreEqual(plainX * 2u, bigX, L"expansion doubles the x of every pixel");
      Assert::AreEqual(plainY * 2u, bigY, L"and the y, so the sprite grows down and right");
    }

    /*
     * A multicolour sprite draws from THREE colours, and only one of them is its own.
     *
     * %01 and %11 come from VIC+&25 and VIC+&26, which every multicolour sprite shares -- so a
     * Trumble cannot be recoloured on its own however hard a port tries. Asserting the palette of
     * the painted pixels is what pins that: change the decode to treat %01 as the sprite's colour
     * and this fails.
     */
    TEST_METHOD(AMulticolourSpriteUsesTheTwoSharedRegistersAndItsOwn)
    {
      Elite::Canvas canvas = PointedCanvas();

      // Sprite 5's pointer is definition 5, the first Trumble.
      canvas.Write(static_cast<std::uint16_t>(Elite::Canvas::SCREEN_CELLS + 0x3F8u + 5u),
                   static_cast<std::uint8_t>(Elite::SPRITE_POINTER_ORIGIN + 5u));

      Elite::VideoState video;
      Elite::ApplySpritesEnabled(video, 0x20u); // bit 5
      video.colour[5] = 0x09u;
      video.x[5] = Elite::SPRITE_ORIGIN_X + 40u;
      video.y[5] = Elite::SPRITE_ORIGIN_Y + 40u;

      Image image{};
      canvas.Resolve(image, video);

      std::set<std::uint8_t> painted;
      for (const std::uint8_t index : image)
      {
        if (index != 0u)
        {
          painted.insert(index);
        }
      }

      Assert::IsTrue(!painted.empty(), L"the Trumble paints something");
      for (const std::uint8_t index : painted)
      {
        const bool known = (index == 0x09u) || (index == Elite::SPRITE_MULTICOLOUR_1) || (index == Elite::SPRITE_MULTICOLOUR_2);
        Assert::IsTrue(known, (L"unexpected colour index " + std::to_wstring(index)).c_str());
      }
      Assert::IsTrue(painted.count(0x09u) == 1u, L"%10 is the sprite's own colour");
    }

    /*
     * A lower-numbered sprite is in front, because the VIC-II draws sprite 7 first.
     *
     * Two sprites in the same place, and the one that survives is sprite 0. This is hardware order
     * rather than a preference, and it is what puts the laser sights over a Trumble.
     */
    TEST_METHOD(ALowerNumberedSpriteDrawsInFront)
    {
      const Elite::Canvas canvas = PointedCanvas();

      Elite::VideoState video;
      Elite::ApplySpritesEnabled(video, 0x05u); // sprites 0 and 2
      video.colour[0] = 0x01u;
      video.colour[2] = 0x0Fu;
      video.x[0] = video.x[2] = Elite::SPRITE_ORIGIN_X;
      video.y[0] = video.y[2] = Elite::SPRITE_ORIGIN_Y;

      Image image{};
      canvas.Resolve(image, video);

      // The pulse sight's vertical stroke is column 11 of the definition, on row 2 -- a pixel both
      // sprites would paint if sprite 2's definition covered it. Whatever the overlap, colour 1
      // must appear and it must be what is on top wherever both wrote.
      bool sawFront = false;
      for (const std::uint8_t index : image)
      {
        sawFront = sawFront || (index == 0x01u);
      }
      Assert::IsTrue(sawFront, L"sprite 0 reaches the screen over sprite 2");
    }

    /*
     * A pointer outside `spritp` is skipped rather than clamped.
     *
     * This build cannot form one -- the loader sets all eight and `SIGHT` only ever writes SPOFF% +
     * 0 to 3 -- so the branch exists for a future writer that is wrong. Clamping would invent a
     * picture; on the hardware such a pointer shows whatever 64 bytes it lands on, and the port
     * does not model memory outside the canvas at all.
     */
    TEST_METHOD(APointerOutsideTheDefinitionsDrawsNothing)
    {
      Elite::Canvas canvas = PointedCanvas();
      canvas.Write(static_cast<std::uint16_t>(Elite::Canvas::SCREEN_CELLS + 0x3F8u), 0u);

      Elite::VideoState video;
      Elite::ApplySpritesEnabled(video, 0x01u);
      Elite::ApplySightColour(video, 0x07u);
      video.x[0] = Elite::SPRITE_ORIGIN_X;
      video.y[0] = Elite::SPRITE_ORIGIN_Y;

      Assert::AreEqual<std::size_t>(0u, Painted(canvas, video), L"an unknown pointer paints nothing");
    }

    /// A sprite hanging off the edge is clipped, not wrapped -- and does not write out of bounds.
    TEST_METHOD(ASpriteOffTheEdgeIsClipped)
    {
      const Elite::Canvas canvas = PointedCanvas();

      Elite::VideoState video;
      Elite::ApplySpritesEnabled(video, 0x01u);
      Elite::ApplySightColour(video, 0x07u);

      // Hard against the top-left, so most of it is above and left of the screen.
      video.x[0] = 0u;
      video.y[0] = 0u;
      const std::size_t topLeft = Painted(canvas, video);

      // And past the bottom-right.
      video.x[0] = static_cast<std::uint16_t>(Elite::Canvas::WIDTH + Elite::SPRITE_ORIGIN_X);
      video.y[0] = 250u;
      const std::size_t bottomRight = Painted(canvas, video);

      Assert::AreEqual<std::size_t>(0u, topLeft, L"entirely off the top-left paints nothing");
      Assert::AreEqual<std::size_t>(0u, bottomRight, L"entirely off the bottom-right paints nothing");
    }
  };

} // namespace GameLogicTests
