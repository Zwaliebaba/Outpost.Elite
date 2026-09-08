#include "pch.h"


#include "Arith.h"

#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::AddSignedResult;
using Elite::Product;
using Elite::SignMag16;

/*
 * That the scaled divide really divides.
 *
 * The exhaustive sweeps against the shipped routines were the whole of this file and went with
 * the oracle (M6-b-5). What is left is the one assertion that did not depend on it and could not
 * have been made by comparison at all: `DivideAndScale` agrees with real arithmetic, not just
 * with the original's answer for the same inputs. Both being wrong the same way is the failure a
 * comparison cannot see.
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

  TEST_CLASS(TheScaledDivide)
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
     * The comparison against the original proved the port agreed with it; this proves they were
     * both RIGHT, which is the failure a comparison cannot see. Only the whole part is checked --
     * the fraction is a logarithm approximation, not an exact one, and holding it to real
     * arithmetic would fail on rounding rather than on error.
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
