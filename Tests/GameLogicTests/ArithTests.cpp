#include "pch.h"

#include "OracleImage.h"

#include "Arith.h"

#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::AddSignedResult;
using Elite::MathWorkspace;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

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
 */
namespace GameLogicTests
{

  namespace
  {

    /// Zero-page addresses, resolved once from the label table.
    struct Scratch
    {
      std::uint16_t p = 0;
      std::uint16_t q = 0;
      std::uint16_t r = 0;
      std::uint16_t s = 0;
      std::uint16_t t = 0;
      std::uint16_t t1 = 0;
      std::uint16_t u = 0;

      explicit Scratch(const OracleImage& _oracle)
        : p(_oracle.Label("P")),
          q(_oracle.Label("Q")),
          r(_oracle.Label("R")),
          s(_oracle.Label("S")),
          t(_oracle.Label("T")),
          t1(_oracle.Label("T1")),
          u(_oracle.Label("U"))
      {
      }
    };

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

    std::wstring Context(const wchar_t* _what, std::uint32_t _a, std::uint32_t _b)
    {
      return std::wstring(_what) + L" with inputs " + std::to_wstring(_a) + L" and " + std::to_wstring(_b);
    }

    /// A small deterministic generator, so a failure is reproducible from its iteration number.
    std::uint32_t NextSample(std::uint32_t& _state) noexcept
    {
      _state = _state * 1664525u + 1013904223u;
      return _state;
    }

  } // namespace

  TEST_CLASS(MultiplicationAgainstTheShippedGame)
  {
  public:
    /// (A P) = P * Q, unsigned, over every possible pair.
    TEST_METHOD(UnsignedMultiplyMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("MULTU");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t p = 0; p < 256; ++p)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.p] = static_cast<std::uint8_t>(p);
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"MULTU should return");

          MathWorkspace work;
          work.p = static_cast<std::uint8_t>(p);
          work.q = static_cast<std::uint8_t>(q);
          const Elite::WideResult product = Elite::MultiplyUnsigned(work);

          Assert::AreEqual<std::uint32_t>(cpu.a, product.high, Context(L"high byte", p, q).c_str());

          // The carry the final `ROR P` leaves, which the stardust reads in the very next
          // instruction and which this port dropped until it did (§6.42).
          Assert::AreEqual(cpu.c, product.carry, Context(L"carry", p, q).c_str());

          /*
           * And what that carry actually IS, measured on the game rather than argued from the
           * listing: always clear (§6.43).
           *
           * `LSR P` shifts a zero into bit 7, and the eight `ROR P`s that follow walk it down to
           * bit 0 and out, so the ninth shift's carry cannot be anything else whatever P and Q
           * hold. `MU1`, the Q = 0 path, has an explicit `CLC`. So every `ADC` and `SBC` in the
           * game that follows `MULTU` without a `CLC` is running on a zero it can rely on -- the
           * stardust's two are the ones that matter -- and the port threading the flag through is
           * documentation of a dependency, not a correction of a defect.
           */
          Assert::IsFalse(cpu.c, Context(L"the game's carry is always clear", p, q).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, Context(L"low byte", p, q).c_str());

          // The product really is the product -- a check the oracle cannot give us, since it
          // would be agreeing with itself.
          Assert::AreEqual<std::uint32_t>(p * q, static_cast<std::uint32_t>((product.high << 8) | work.p),
                                          Context(L"the 16-bit product", p, q).c_str());
        }
      }
    }

    /// (A P) = P * X, the entry point that takes its multiplier in a register.
    TEST_METHOD(MultiplyByRegisterMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("MU11");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t p = 0; p < 256; ++p)
      {
        // X = 0 is not a legal entry: the caller checks for it and jumps elsewhere, so the
        // routine would decrement to 255 and multiply by that. Matching the game means not
        // calling it that way either.
        for (std::uint32_t x = 1; x < 256; ++x)
        {
          cpu.memory[zp.p] = static_cast<std::uint8_t>(p);
          cpu.a = 0;
          cpu.x = static_cast<std::uint8_t>(x);
          cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"MU11 should return");

          MathWorkspace work;
          work.p = static_cast<std::uint8_t>(p);
          const Elite::WideResult product = Elite::MultiplyByX(work, static_cast<std::uint8_t>(x));

          Assert::AreEqual<std::uint32_t>(cpu.a, product.high, Context(L"high byte", p, x).c_str());

          // The carry the final `ROR P` leaves, which the stardust reads in the very next
          // instruction and which this port dropped until it did (§6.42).
          Assert::AreEqual(cpu.c, product.carry, Context(L"carry", p, x).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, Context(L"low byte", p, x).c_str());
        }
      }
    }

    /// (A P) = Q * A for sign-magnitude operands, including the sign of the product.
    TEST_METHOD(SignedMultiplyMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("MULT1");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"MULT1 should return");

          MathWorkspace work;
          work.q = static_cast<std::uint8_t>(q);
          const std::uint8_t high = Elite::MultiplySigned(work, static_cast<std::uint8_t>(a));

          Assert::AreEqual<std::uint32_t>(cpu.a, high, Context(L"high byte", a, q).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, Context(L"low byte", a, q).c_str());
        }
      }
    }

    /// The same multiplication, landing in a different pair of scratch bytes.
    TEST_METHOD(SignedMultiplyIntoScratchMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("MULT12");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"MULT12 should return");

          MathWorkspace work;
          work.q = static_cast<std::uint8_t>(q);
          Elite::MultiplySignedToSR(work, static_cast<std::uint8_t>(a));

          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.s], work.s, Context(L"S", a, q).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.r], work.r, Context(L"R", a, q).c_str());
        }
      }
    }

    /// (A P) = |A| * Q.
    TEST_METHOD(MagnitudeTimesQMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("MLU2");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"MLU2 should return");

          MathWorkspace work;
          work.q = static_cast<std::uint8_t>(q);
          const Elite::WideResult product = Elite::MultiplyMagnitudeByQ(work, static_cast<std::uint8_t>(a));

          Assert::AreEqual<std::uint32_t>(cpu.a, product.high, Context(L"high byte", a, q).c_str());

          // The carry the final `ROR P` leaves, which the stardust reads in the very next
          // instruction and which this port dropped until it did (§6.42).
          Assert::AreEqual(cpu.c, product.carry, Context(L"carry", a, q).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, Context(L"low byte", a, q).c_str());
        }
      }
    }

    TEST_METHOD(SquaringMatchesForEveryInput)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t carries = 0;

      for (std::uint32_t value = 0; value < 256; ++value)
      {
        // SQUA clears the sign bit first; SQUA2 trusts the caller to have done so.
        struct Variant
        {
          const char* label;
          bool clearsSignBit;
        };

        for (const Variant& variant : {Variant{"SQUA", true}, Variant{"SQUA2", false}})
        {
          cpu.a = static_cast<std::uint8_t>(value);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(oracle.Label(variant.label), 5'000);
          Assert::IsTrue(run.completed, L"the squaring routine should return");

          MathWorkspace work;
          const Elite::WideResult squared = variant.clearsSignBit ? Elite::Square(work, static_cast<std::uint8_t>(value))
                                                                  : Elite::SquareUnsigned(work, static_cast<std::uint8_t>(value));

          Assert::AreEqual<std::uint32_t>(cpu.a, squared.high, Context(L"high byte", value, 0).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, Context(L"low byte", value, 0).c_str());

          // 6502: the exit carry, which `MAS3` reads in an `ADC` with no `CLC` (§6.70). The sweep
          // was already exhaustive, so widening the model cost this one line -- §6.42 for the fifth
          // time.
          Assert::AreEqual(cpu.c, squared.carry, Context(L"exit carry", value, 0).c_str());
          carries += squared.carry ? 1u : 0u;
        }
      }

      /*
       * And it is NEVER SET, over every input either entry point can be given -- which is the half
       * §6.65 says decides whether a dropped carry matters, and it says this one does not.
       *
       * `MAS3` reads it in an `ADC` with no `CLC`, twice over, and an `ADC` cannot see a clear
       * carry. So the port was already right about `MAS3` before the flag was modelled at all, and
       * the widening buys the proof rather than a fix. `DVID4`'s carry (§6.60) is the same shape;
       * `DIL2`'s (§6.65) is not, because there it lands in an `SBC`.
       *
       * Asserted as zero rather than left uncounted, because "always clear" is a claim about all
       * 512 inputs and this is the sweep that can make it.
       */
      Assert::AreEqual<std::uint32_t>(0u, carries, L"the squaring routine never exits with the carry set");
    }
  };

  TEST_CLASS(SignedAdditionAgainstTheShippedGame)
  {
  public:
    /// (A X) = (A P) + (S R). Four bytes of input, so a deterministic sweep rather than an
    /// exhaustive one, with the sign-magnitude edge cases pinned separately below.
    TEST_METHOD(SignedAdditionMatchesOverASweep)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("ADD");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t state = 20260903u;

      for (std::uint32_t iteration = 0; iteration < 200'000; ++iteration)
      {
        const std::uint32_t sample = NextSample(state);
        const std::uint8_t a = static_cast<std::uint8_t>(sample);
        const std::uint8_t p = static_cast<std::uint8_t>(sample >> 8);
        const std::uint8_t s = static_cast<std::uint8_t>(sample >> 16);
        const std::uint8_t r = static_cast<std::uint8_t>(sample >> 24);

        cpu.memory[zp.p] = p;
        cpu.memory[zp.s] = s;
        cpu.memory[zp.r] = r;
        cpu.a = a;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"ADD should return");

        MathWorkspace work;
        work.p = p;
        work.s = s;
        work.r = r;
        const AddSignedResult actual = Elite::AddSigned(work, a);

        const std::wstring where = L" at iteration " + std::to_wstring(iteration);
        Assert::AreEqual<std::uint32_t>(cpu.a, actual.high, (L"high byte" + where).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.x, actual.low, (L"low byte" + where).c_str());

        // The carry too: `PLS22` reads it twice, once to place a meridian on the screen and once
        // to hand it to `BLINE` (§6.53). None of `ADD`'s three exits clears it.
        Assert::AreEqual(cpu.c, actual.carry, (L"carry" + where).c_str());
      }
    }

    /// The cases sign-magnitude arithmetic actually gets wrong: equal magnitudes cancelling,
    /// negative zero, and a borrow that has to propagate into the negation.
    TEST_METHOD(SignedAdditionMatchesAtTheAwkwardEdges)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("ADD");

      Cpu6502 cpu = oracle.Fresh();

      struct Case
      {
        std::uint8_t a;
        std::uint8_t p;
        std::uint8_t s;
        std::uint8_t r;
      };

      const Case cases[] = {
        {0x00, 0x00, 0x00, 0x00}, // zero plus zero
        {0x80, 0x00, 0x00, 0x00}, // negative zero plus zero
        {0x00, 0x00, 0x80, 0x00}, // zero plus negative zero
        {0x01, 0x00, 0x81, 0x00}, // equal magnitudes, opposite signs
        {0x81, 0x00, 0x01, 0x00}, // the same, the other way round
        {0x01, 0x00, 0x80, 0x01}, // borrow out of the low byte
        {0x80, 0x01, 0x01, 0x00}, // borrow the other way
        {0x7F, 0xFF, 0x7F, 0xFF}, // largest positive magnitudes, overflowing
        {0xFF, 0xFF, 0xFF, 0xFF}, // largest negative magnitudes
        {0x00, 0x01, 0x80, 0x02}, // small negative difference needing negation
        {0x40, 0x00, 0xC0, 0x00}, // exact cancellation with the sign flipped
      };

      for (const Case& item : cases)
      {
        cpu.memory[zp.p] = item.p;
        cpu.memory[zp.s] = item.s;
        cpu.memory[zp.r] = item.r;
        cpu.a = item.a;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed);

        MathWorkspace work;
        work.p = item.p;
        work.s = item.s;
        work.r = item.r;
        const AddSignedResult actual = Elite::AddSigned(work, item.a);

        const std::wstring where = L" for A=" + std::to_wstring(item.a) + L" P=" + std::to_wstring(item.p) + L" S=" +
                                   std::to_wstring(item.s) + L" R=" + std::to_wstring(item.r);
        Assert::AreEqual<std::uint32_t>(cpu.a, actual.high, (L"high byte" + where).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.x, actual.low, (L"low byte" + where).c_str());
      }
    }
  };

  TEST_CLASS(DivisionAndAccumulateAgainstTheShippedGame)
  {
  public:
    /// (A X) = Q * A + (S R) -- the multiply-accumulate the geometry code runs on.
    TEST_METHOD(MultiplyAndAddMatchesOverASweep)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("MAD");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t state = 7771u;

      for (std::uint32_t iteration = 0; iteration < 200'000; ++iteration)
      {
        const std::uint32_t sample = NextSample(state);
        const std::uint8_t a = static_cast<std::uint8_t>(sample);
        const std::uint8_t q = static_cast<std::uint8_t>(sample >> 8);
        const std::uint8_t s = static_cast<std::uint8_t>(sample >> 16);
        const std::uint8_t r = static_cast<std::uint8_t>(sample >> 24);

        cpu.memory[zp.q] = q;
        cpu.memory[zp.s] = s;
        cpu.memory[zp.r] = r;
        cpu.a = a;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"MAD should return");

        MathWorkspace work;
        work.q = q;
        work.s = s;
        work.r = r;
        const AddSignedResult actual = Elite::MultiplyAndAdd(work, a);

        const std::wstring where = L" at iteration " + std::to_wstring(iteration);
        Assert::AreEqual<std::uint32_t>(cpu.a, actual.high, (L"high byte" + where).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.x, actual.low, (L"low byte" + where).c_str());
      }
    }

    TEST_METHOD(BlockFillAndPairSetMatchForEveryInput)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t blockAddress = oracle.Label("K");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t value = 0; value < 256; ++value)
      {
        cpu.a = static_cast<std::uint8_t>(value);
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        Assert::IsTrue(cpu.CallSubroutine(oracle.Label("MU5"), 500).completed);

        MathWorkspace fill;
        Elite::FillK(fill, static_cast<std::uint8_t>(value));
        for (std::uint16_t byte = 0; byte < 4; ++byte)
        {
          Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(blockAddress + byte)], fill.k[byte],
                                          Context(L"K block byte", value, byte).c_str());
        }

        cpu.a = static_cast<std::uint8_t>(value);
        cpu.sp = 0xFD;
        Assert::IsTrue(cpu.CallSubroutine(oracle.Label("MU6"), 500).completed);

        MathWorkspace pair;
        Elite::SetPairP(pair, static_cast<std::uint8_t>(value));
        Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], pair.p, Context(L"P", value, 0).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(zp.p + 1)], pair.p1, Context(L"P+1", value, 0).c_str());
      }
    }

    /// The scaled multiply, over every input pair.
    TEST_METHOD(ScaledMultiplyMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("MULTS");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t p = 0; p < 256; ++p)
        {
          cpu.memory[zp.p] = static_cast<std::uint8_t>(p);
          cpu.memory[static_cast<std::uint16_t>(zp.p + 1)] = 0;
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"MULTS should return");

          MathWorkspace work;
          work.p = static_cast<std::uint8_t>(p);
          const std::uint8_t high = Elite::MultiplyScaled(work, static_cast<std::uint8_t>(a));

          Assert::AreEqual<std::uint32_t>(cpu.a, high, Context(L"high byte", a, p).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, Context(L"low byte", a, p).c_str());
        }
      }
    }

    /// The sixteen-step wide multiply.
    TEST_METHOD(WideMultiplyMatchesOverASweep)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("MLTU2");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t state = 31337u;

      for (std::uint32_t iteration = 0; iteration < 150'000; ++iteration)
      {
        const std::uint32_t sample = NextSample(state);
        const std::uint8_t a = static_cast<std::uint8_t>(sample);
        const std::uint8_t p = static_cast<std::uint8_t>(sample >> 8);
        const std::uint8_t q = static_cast<std::uint8_t>(sample >> 16);

        cpu.memory[zp.p] = p;
        cpu.memory[static_cast<std::uint16_t>(zp.p + 1)] = 0;
        cpu.memory[zp.q] = q;
        cpu.a = a;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"MLTU2 should return");

        MathWorkspace work;
        work.p = p;
        work.q = q;
        const std::uint8_t high = Elite::MultiplyWide(work, a).high;

        const std::wstring where = L" at iteration " + std::to_wstring(iteration);
        Assert::AreEqual<std::uint32_t>(cpu.a, high, (L"high byte" + where).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(zp.p + 1)], work.p1, (L"middle byte" + where).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, (L"low byte" + where).c_str());
      }
    }

    /// A = A / Q, saturating, over every input pair.
    TEST_METHOD(DivideByQMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("TIS2");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"TIS2 should return");

          MathWorkspace work;
          work.q = static_cast<std::uint8_t>(q);
          const std::uint8_t result = Elite::DivideByQ(work, static_cast<std::uint8_t>(a));

          Assert::AreEqual<std::uint32_t>(cpu.a, result, Context(L"quotient", a, q).c_str());
        }
      }
    }

    /// The shared divide-by-96 tail, over every input.
    TEST_METHOD(DivideBy96MatchesForEveryInput)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();

      std::uint16_t routine = 0;
      if (!oracle.TryLabel("DVID96", routine))
      {
        Logger::WriteMessage("SKIPPED -- this build has no DVID96 entry point");
        return;
      }

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        cpu.a = static_cast<std::uint8_t>(a);
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"DVID96 should return");

        MathWorkspace work;
        const std::uint8_t result = Elite::DivideBy96(work, static_cast<std::uint8_t>(a));

        Assert::AreEqual<std::uint32_t>(cpu.a, result, Context(L"quotient", a, 0).c_str());
      }
    }

    /// (A ?) = (-X * A + (S R)) / 96.
    TEST_METHOD(MultiplyAddDivide96MatchesOverASweep)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("TIS1");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t state = 909090u;

      for (std::uint32_t iteration = 0; iteration < 150'000; ++iteration)
      {
        const std::uint32_t sample = NextSample(state);
        const std::uint8_t a = static_cast<std::uint8_t>(sample);
        const std::uint8_t x = static_cast<std::uint8_t>(sample >> 8);
        const std::uint8_t s = static_cast<std::uint8_t>(sample >> 16);
        const std::uint8_t r = static_cast<std::uint8_t>(sample >> 24);

        cpu.memory[zp.s] = s;
        cpu.memory[zp.r] = r;
        cpu.a = a;
        cpu.x = x;
        cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"TIS1 should return");

        MathWorkspace work;
        work.s = s;
        work.r = r;
        const std::uint8_t result = Elite::MultiplyAddDivide96(work, a, x);

        const std::wstring where = L" at iteration " + std::to_wstring(iteration);
        Assert::AreEqual<std::uint32_t>(cpu.a, result, (L"result" + where).c_str());
      }
    }

    /// The sixteen-step long division.
    TEST_METHOD(WideDivideMatchesOverASweep)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("DVIDT");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t state = 246810u;

      for (std::uint32_t iteration = 0; iteration < 150'000; ++iteration)
      {
        const std::uint32_t sample = NextSample(state);
        const std::uint8_t a = static_cast<std::uint8_t>(sample);
        const std::uint8_t p = static_cast<std::uint8_t>(sample >> 8);
        const std::uint8_t q = static_cast<std::uint8_t>(sample >> 16);

        cpu.memory[zp.p] = p;
        cpu.memory[static_cast<std::uint16_t>(zp.p + 1)] = 0;
        cpu.memory[zp.q] = q;
        cpu.a = a;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"DVIDT should return");

        MathWorkspace work;
        work.p = p;
        work.q = q;
        const std::uint8_t result = Elite::DivideWide(work, a);

        const std::wstring where = L" at iteration " + std::to_wstring(iteration);
        Assert::AreEqual<std::uint32_t>(cpu.a, result, (L"result" + where).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, (L"P" + where).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(zp.p + 1)], work.p1, (L"P+1" + where).c_str());
      }
    }
  };

  TEST_CLASS(LogarithmRoutinesAgainstTheShippedGame)
  {
  public:
    /// A = A * Q / 256, through the logarithm tables, over every input pair.
    TEST_METHOD(LogMultiplyMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("FMLTU");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          // Both entry carries, because the two zero exits hand the caller's own flag straight
          // back and `DOEXP` and `CIRCLE2` read it (§6.50).
          const bool carryIn = ((a + q) & 1u) != 0u;

          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = carryIn;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"FMLTU should return");

          MathWorkspace work;
          work.q = static_cast<std::uint8_t>(q);
          const Elite::WideResult result = Elite::MultiplyByLog(work, static_cast<std::uint8_t>(a), carryIn);

          Assert::AreEqual<std::uint32_t>(cpu.a, result.high, Context(L"product", a, q).c_str());
          Assert::AreEqual(cpu.c, result.carry, Context(L"carry", a, q).c_str());
        }
      }
    }

    /// R = 256 * A / Q, over every input pair, including the carry the callers branch on.
    TEST_METHOD(LogDivideMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("LL28");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"LL28 should return");

          MathWorkspace work;
          work.q = static_cast<std::uint8_t>(q);
          const bool carry = Elite::DivideToR(work, static_cast<std::uint8_t>(a));

          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.r], work.r, Context(L"R", a, q).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.c ? 1u : 0u, carry ? 1u : 0u, Context(L"carry", a, q).c_str());
        }
      }
    }

    /// The signed combine, over a sweep of its four input bytes.
    TEST_METHOD(SignedCombineMatchesOverASweep)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("LL38");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t state = 55501u;

      for (std::uint32_t iteration = 0; iteration < 200'000; ++iteration)
      {
        const std::uint32_t sample = NextSample(state);
        const std::uint8_t a = static_cast<std::uint8_t>(sample);
        const std::uint8_t s = static_cast<std::uint8_t>(sample >> 8);
        const std::uint8_t q = static_cast<std::uint8_t>(sample >> 16);
        const std::uint8_t r = static_cast<std::uint8_t>(sample >> 24);

        cpu.memory[zp.s] = s;
        cpu.memory[zp.q] = q;
        cpu.memory[zp.r] = r;
        cpu.a = a;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"LL38 should return");

        MathWorkspace work;
        work.s = s;
        work.q = q;
        work.r = r;
        const Elite::SignedSum result = Elite::CombineSigned(work, a);

        const std::wstring where = L" at iteration " + std::to_wstring(iteration);
        Assert::AreEqual<std::uint32_t>(cpu.a, result.value, (L"result" + where).c_str());
        Assert::AreEqual<std::uint32_t>(cpu.memory[zp.s], work.s, (L"S" + where).c_str());

        // The carry is the routine's documented overflow flag and `LL9` branches on it, so it is
        // compared here rather than at the one caller that happens to read it.
        Assert::AreEqual(cpu.c, result.carry, (L"C" + where).c_str());
      }
    }

    /// The angle of P over Q, over every input pair. This one decides how the ship responds to
    /// the stick, so it is worth every one of the 65,536 comparisons.
    TEST_METHOD(ArctanMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("ARCTAN");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t p = 0; p < 256; ++p)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.p] = static_cast<std::uint8_t>(p);
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"ARCTAN should return");

          MathWorkspace work;
          work.p = static_cast<std::uint8_t>(p);
          work.q = static_cast<std::uint8_t>(q);
          const std::uint8_t angle = Elite::Arctan(work);

          Assert::AreEqual<std::uint32_t>(cpu.a, angle, Context(L"angle", p, q).c_str());
        }
      }
    }

    /// A = K * sin(A) / 256. Exhaustive: A is a byte and K is a byte, so 65,536 pairs.
    TEST_METHOD(SineMultiplyMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t routine = oracle.Label("FMLTU2");
      const std::uint16_t k = oracle.Label("K");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t kValue = 0; kValue < 256; ++kValue)
        {
          const bool carryIn = ((a + kValue) & 1u) != 0u;

          cpu.memory[k] = static_cast<std::uint8_t>(kValue);
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = carryIn;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"FMLTU2 should return");

          MathWorkspace work;
          work.k[0] = static_cast<std::uint8_t>(kValue);
          const Elite::WideResult result = Elite::MultiplyKBySine(work, static_cast<std::uint8_t>(a), carryIn);

          Assert::AreEqual<std::uint32_t>(cpu.a, result.high, Context(L"product", a, kValue).c_str());
          Assert::AreEqual(cpu.c, result.carry, Context(L"carry", a, kValue).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("Q")], work.q, Context(L"Q", a, kValue).c_str());
        }
      }
    }

    /*
     * DVID4 over every input pair: the quotient it leaves in P, the R it leaves through the code
     * it falls into, and the value in A at the return.
     *
     * Q = 0 is included deliberately. The routine has no guard for it and neither does the port,
     * so both must agree on the nonsense they produce -- a port that "helpfully" returned early
     * there would diverge from the game on an input its callers are responsible for avoiding.
     */
    TEST_METHOD(RestoringDivideMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("DVID4");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t a = 0; a < 256; ++a)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = static_cast<std::uint8_t>(a);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"DVID4 should return");

          MathWorkspace work;
          work.q = static_cast<std::uint8_t>(q);
          const Elite::ScaledDivision result = Elite::DivideAndScale(work, static_cast<std::uint8_t>(a));

          Assert::AreEqual<std::uint32_t>(cpu.a, result.r, Context(L"returned value", a, q).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.p], work.p, Context(L"quotient", a, q).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.r], work.r, Context(L"R", a, q).c_str());

          // 6502: the exit carry, which `SPS2` hands to `SP2`'s `ADC #195` and `SBC T` (§6.60).
          // The sweep was already exhaustive, so widening the model cost this one line.
          Assert::AreEqual(cpu.c, result.carry, Context(L"exit carry", a, q).c_str());
        }
      }
    }

    /*
     * 6502: LL5 -- the square root, over all 65,536 radicands.
     *
     * Also checked against real arithmetic, which earns its line here: the routine truncates
     * rather than rounds, so agreeing with the game AND with the integer square root pins both
     * that the port is right and that the original does what it looks like.
     */
    TEST_METHOD(SquareRootMatchesExhaustively)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("LL5");

      Cpu6502 cpu = oracle.Fresh();

      for (std::uint32_t r = 0; r < 256; ++r)
      {
        for (std::uint32_t q = 0; q < 256; ++q)
        {
          cpu.memory[zp.r] = static_cast<std::uint8_t>(r);
          cpu.memory[zp.q] = static_cast<std::uint8_t>(q);
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = false;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"LL5 should return");

          MathWorkspace work;
          work.r = static_cast<std::uint8_t>(r);
          work.q = static_cast<std::uint8_t>(q);
          const bool carry = Elite::SquareRoot(work);
          Assert::AreEqual(cpu.c, carry, L"LL5's exit carry, which the sun's DORND runs on");

          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.q], work.q, Context(L"root", r, q).c_str());

          // The radicand is (R Q), so the answer should be its integer square root.
          const std::uint32_t radicand = (r << 8) | q;
          std::uint32_t root = 0;
          while ((root + 1) * (root + 1) <= radicand)
          {
            ++root;
          }
          Assert::AreEqual<std::uint32_t>(root, work.q, Context(L"the real square root", r, q).c_str());
        }
      }
    }

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
      for (std::uint32_t q = 1; q < 256; ++q)
      {
        for (std::uint32_t a = 0; a < 256; ++a)
        {
          MathWorkspace work;
          work.q = static_cast<std::uint8_t>(q);
          (void)Elite::DivideAndScale(work, static_cast<std::uint8_t>(a));

          Assert::AreEqual<std::uint32_t>(a / q, work.p, Context(L"whole part", a, q).c_str());
        }
      }
    }
  };

} // namespace GameLogicTests
