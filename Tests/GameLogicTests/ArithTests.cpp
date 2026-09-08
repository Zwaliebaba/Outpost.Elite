#include "pch.h"


#include "Arith.h"

#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::AddSignedResult;
using Elite::Product;
using Elite::SignMag16;

/*
 * The arithmetic kernel against the shipped routines (slice 1b, ADR-003).
 *
 * Where the whole input space is 16 bits these run exhaustively -- 65,536 comparisons is a
 * fraction of a second and it removes the question of whether the interesting case was the one
 * nobody sampled. ADD takes four bytes of input, so it gets a deterministic sweep plus the
 * edges that actually break sign-magnitude arithmetic: zero, negative zero, and equal
 * magnitudes with opposite signs.
 *
 * One shortcut worth naming: these routines touch only zero page, so a single loaded image is
 * reused across iterations and just the scratch bytes are reset. Copying 64 KB per call would
 * turn an exhaustive sweep into gigabytes of memcpy for no extra confidence.
 *
 * Since M2-b the kernel takes its operands as values and hands back structs, so each comparison
 * here reads the oracle's zero page on one side and a returned field on the other: the low byte
 * the original left in `P` is `Product::low`, the quotient it left in `R` is `Quotient::value`,
 * and so on. What the original leaves in its scratch bytes -- `T`, `T1`, `U`, `widget` -- is not
 * compared, because the port no longer has them.
 */
namespace GameLogicTests
{

  namespace
  {

    std::wstring Context(const wchar_t* _what, std::uint32_t _first, std::uint32_t _second)
    {
      return std::wstring(_what) + L" with inputs " + std::to_wstring(_first) + L" and " + std::to_wstring(_second);
    }

  } // namespace

  TEST_CLASS(LogarithmRoutinesAgainstTheShippedGame)
  {
  public:
    /*
     * The division half really divides, and what it is dividing is worth stating.
     *
     * DVID4 is an 8.8 fixed-point divide: P comes out as the whole part of A / Q and R as the
     * fraction, scaled to a byte. The ASL A / STA P at the top is what makes that work -- it puts
     * A's top bit into the remainder before the first step and leaves the rest of A in P, where
     * each ROL P hands over the next bit while shifting a quotient bit in behind it. One register
     * being both the dividend and the quotient is the trick the whole routine turns on.
     *
     * An oracle comparison alone proves the port agrees with the game; this also confirms they are
     * both right. Only P is checked -- R is a logarithm approximation of the fraction, not an
     * exact one, and comparing it to real arithmetic would fail on rounding rather than on error.
     */
    TEST_METHOD(TheRestoringDivideReallyDivides)
    {
      for (std::uint32_t divisor = 1; divisor < 256; ++divisor)
      {
        for (std::uint32_t a = 0; a < 256; ++a)
        {
          const Elite::ScaledDivision result = Elite::DivideAndScale(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(divisor));

          Assert::AreEqual<std::uint32_t>(a / divisor, result.whole, Context(L"whole part", a, divisor).c_str());
        }
      }
    }
  };

} // namespace GameLogicTests
