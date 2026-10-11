#include "pch.h"

#include "AnswerFile.h"
#include "Maths.h"
#include "ReferenceRig.h"

#include <array>
#include <format>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// The maths' known answers, from the repository's root. The file's header documents its format and states the count.
constexpr std::string_view MATHS_ANSWERS_FILE = "Tests/GameLogicTests/MathsAnswers.txt";

// Pairs of inputs, as aggregates: a std::pair's converting constructor would narrow each literal inside <utility>.
struct UnsignedDivide
{
  std::uint32_t dividend;
  std::uint16_t divisor;
};

struct Ratio
{
  std::uint16_t numerator;
  std::uint16_t denominator;
};

// The dividends and divisors of the divides: zero, one, the halves' edges and the signs' edges, and a few between.
constexpr auto BYTE_DIVIDENDS =
  std::to_array<std::uint16_t>({0x0000, 0x0001, 0x007F, 0x00FF, 0x0100, 0x1234, 0x7EFF, 0x7F00, 0x7FFF, 0x8000, 0xFE01, 0xFF00, 0xFFFF});
constexpr auto BYTE_DIVISORS = std::to_array<std::uint8_t>({0x00, 0x01, 0x02, 0x07, 0x7F, 0x80, 0xFE, 0xFF});
constexpr auto WORD_DIVIDENDS = std::to_array<std::uint32_t>({0x00000000, 0x00000001, 0x0000FFFF, 0x00010000, 0x00012345, 0x12345678,
                                                              0x7FFF0000, 0x7FFFFFFF, 0x80000000, 0xFFFE0001, 0xFFFF0000, 0xFFFFFFFF});
constexpr auto WORD_DIVISORS = std::to_array<std::uint16_t>({0x0000, 0x0001, 0x0002, 0x0010, 0x1234, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF});
constexpr auto SIGNED_DIVIDENDS =
  std::to_array<std::uint32_t>({0x00000000, 0x00000001, 0xFFFFFFFF, 0x00007FFF, 0x00008000, 0xFFFF8000, 0xFFFF7FFF, 0x3FFF8000, 0x3FFFC000,
                                0xC0004000, 0xC0008000, 0x7FFFFFFF, 0x80000000, 0x12345678, 0xEDCBA988});
constexpr auto SIGNED_DIVISORS =
  std::to_array<std::uint16_t>({0x0000, 0x0001, 0xFFFF, 0x0002, 0xFFFE, 0x0100, 0x7FFF, 0x8000, 0x8001, 0x1234});
// The instruction after each divide DivideUnsigned serves, by which each answer names it, and what the trap makes of the divide
// there: ProjectVertices' two, whose memory operand the trap misreads, ProjectDiscCoordinate's two, ScaleByInverseDistance's and
// ProjectToScreen's two.
struct DivideReturn
{
  std::uint16_t offset;
  Elite::DivideTrap trap;
};
constexpr auto DIVIDE_RETURNS = std::to_array<DivideReturn>({{0x236C, Elite::DivideTrap::LowByte},
                                                             {0x2395, Elite::DivideTrap::LowByte},
                                                             {0x4044, Elite::DivideTrap::Word},
                                                             {0x4064, Elite::DivideTrap::Word},
                                                             {0x40E2, Elite::DivideTrap::Word},
                                                             {0x8D4B, Elite::DivideTrap::Word},
                                                             {0x8D56, Elite::DivideTrap::Word}});
constexpr auto UNSIGNED_DIVIDES =
  std::to_array<UnsignedDivide>({{0x00012345, 0x0010}, {0x0000FFFF, 0x0001}, {0x00100000, 0x0010}, {0x00000001, 0x0000}});

// Angles and signed words for the rotations and the arc tangents: zero, one, the quarter turns, the edges of a word.
constexpr auto WORDS =
  std::to_array<std::int16_t>({0, 1, -1, 2, 100, -100, 1000, -1000, 0x2000, 0x4000, -0x4000, 0x7FFF, -0x7FFF, -0x8000});
constexpr auto ROTATION_ANGLES = std::to_array<std::uint16_t>({0x000, 0x080, 0x100, 0x200, 0x2AB, 0x400, 0x555, 0x600, 0x7FF});
constexpr auto ROTATION_POINTS = std::to_array<Elite::Pair>(
  {{0, 0}, {1, 0}, {0, 1}, {100, -200}, {0x4000, -0x4000}, {0x7FFF, 0}, {-0x8000, 0}, {0x7FFF, 0x7FFF}, {-0x8000, -0x8000}});
// Sines and cosines no table gives, at the edges of a word.
constexpr auto RAW_ROTATIONS = std::to_array<Elite::SinCos>({{0x7FFF, 0x7FFF}, {-0x8000, -0x8000}, {0x7FFF, -0x8000}});
constexpr auto RATIOS = std::to_array<Ratio>({{0x0000, 0x0000},
                                              {0x0000, 0x0001},
                                              {0x0001, 0x0001},
                                              {0x0001, 0x0002},
                                              {0x0001, 0x7FFF},
                                              {0x00FF, 0x0100},
                                              {0x3FFF, 0x8000},
                                              {0x7FFE, 0x7FFF},
                                              {0x7FFF, 0x7FFF},
                                              {0x8000, 0xFFFF},
                                              {0xFFFF, 0xFFFF},
                                              {0x0064, 0x012C}});
constexpr auto VECTORS = std::to_array<Elite::Vector>({{0, 0, 0},
                                                       {1, 0, 0},
                                                       {3, 4, 0},
                                                       {-5, 0, 12},
                                                       {0, 0xFF, 0},
                                                       {0, 0x100, 0},
                                                       {100, 200, 300},
                                                       {0x7FFF, 0, 0},
                                                       {-0x8000, 0, 0},
                                                       {0x7FFF, 0x7FFF, 0x7FFF},
                                                       {-0x8000, -0x8000, -0x8000},
                                                       {0x1234, -0x2345, 0x0456}});
constexpr auto BOX_HALF_SIZES = std::to_array<std::uint16_t>({0, 1, 100, 0x8000, 0xFFFF});
constexpr auto ANGLES = std::to_array<std::uint16_t>({0x000, 0x001, 0x0FF, 0x3FF, 0x400, 0x401, 0x7FF, 0x800, 0xFFFF});
constexpr auto TOLERANCES = std::to_array<std::uint16_t>({0, 1, 0x10, 0x400});
constexpr auto SHIFTED = std::to_array<std::uint32_t>({0x000000, 0x000001, 0x123456, 0x7FFFFF, 0x800000, 0x876543, 0xFFFFFF});
constexpr auto SHIFTS = std::to_array<std::uint8_t>({0, 1, 4, 8, 16, 23, 24, 31});
constexpr std::size_t RANDOM_STEPS = 64;
constexpr std::size_t SINE_ANGLES = 0x800; // every angle, 2048 to a turn, then two past the mask
constexpr auto ANGLES_PAST_THE_MASK = std::to_array<std::uint16_t>({0x0800, 0xFFFF});

// What every answer below adds up to: the count MATHS_ANSWERS_FILE's header states.
constexpr std::size_t MATHS_ANSWERS = BYTE_DIVIDENDS.size() * BYTE_DIVISORS.size() + WORD_DIVIDENDS.size() * WORD_DIVISORS.size() +
                                      SIGNED_DIVIDENDS.size() * SIGNED_DIVISORS.size() + DIVIDE_RETURNS.size() * UNSIGNED_DIVIDES.size() +
                                      2 * RANDOM_STEPS + SINE_ANGLES + ANGLES_PAST_THE_MASK.size() +
                                      ROTATION_POINTS.size() * (ROTATION_ANGLES.size() + RAW_ROTATIONS.size()) +
                                      WORDS.size() * WORDS.size() + 2 * RATIOS.size() + VECTORS.size() * (1 + BOX_HALF_SIZES.size()) +
                                      ANGLES.size() * ANGLES.size() * TOLERANCES.size() + SHIFTED.size() * SHIFTS.size();

[[nodiscard]] std::uint16_t Word(std::int16_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value);
}

// The divides, through the game's trap where the quotient does not fit.
void Divides(std::vector<std::string>& _answers)
{
  for (const std::uint16_t dividend : BYTE_DIVIDENDS)
  {
    for (const std::uint8_t divisor : BYTE_DIVISORS)
    {
      const Elite::ByteQuotient result = Elite::DivideByte(dividend, divisor);
      _answers.push_back(std::format("DivideByte {:04x} {:02x} = {:02x} {:02x}", dividend, divisor, result.quotient, result.remainder));
    }
  }
  for (const std::uint32_t dividend : WORD_DIVIDENDS)
  {
    for (const std::uint16_t divisor : WORD_DIVISORS)
    {
      const Elite::WordQuotient result = Elite::DivideWord(dividend, divisor);
      _answers.push_back(std::format("DivideWord {:08x} {:04x} = {:04x} {:04x}", dividend, divisor, result.quotient, result.remainder));
    }
  }
  for (const std::uint32_t dividend : SIGNED_DIVIDENDS)
  {
    for (const std::uint16_t divisor : SIGNED_DIVISORS)
    {
      const Elite::WordQuotient result = Elite::DivideSignedWord(dividend, divisor);
      _answers.push_back(
        std::format("DivideSignedWord {:08x} {:04x} = {:04x} {:04x}", dividend, divisor, result.quotient, result.remainder));
    }
  }
  for (const DivideReturn& divideReturn : DIVIDE_RETURNS)
  {
    for (const auto& [dividend, divisor] : UNSIGNED_DIVIDES)
    {
      const Elite::WordQuotient result = Elite::DivideUnsigned(dividend, divisor, divideReturn.trap);
      _answers.push_back(std::format("DivideUnsigned {:08x} {:04x} at {:04x} = {:04x} {:04x}", dividend, divisor, divideReturn.offset,
                                     result.quotient, result.remainder));
    }
  }
}

// NextRandom's walk from the loaded commander's state, and from the all-ones state.
void RandomNumbers(Elite::GameState& _state, std::vector<std::string>& _answers)
{
  for (const bool fromOnes : {false, true})
  {
    if (fromOnes)
    {
      _state.Data().commander.randomState0 = 0xFFFF;
      _state.Data().commander.randomState1 = 0xFFFF;
      _state.Data().commander.randomState2 = 0xFFFF;
    }
    for (std::size_t step = 0; step < RANDOM_STEPS; ++step)
    {
      const std::uint16_t a = _state.Data().commander.randomState0;
      const std::uint16_t b = _state.Data().commander.randomState1;
      const std::uint16_t c = _state.Data().commander.randomState2;
      const std::uint16_t number = Elite::NextRandom(_state);
      _answers.push_back(std::format("NextRandom {:04x} {:04x} {:04x} = {:04x}", a, b, c, number));
    }
  }
}

// The sine table at every angle, and the rotations by its sines and by words at the edges.
void Rotations(std::vector<std::string>& _answers)
{
  const auto sinCos = [&](std::uint16_t _angle)
  {
    const Elite::SinCos result = Elite::SinCosOf(_angle);
    _answers.push_back(std::format("SinCosOf {:04x} = {:04x} {:04x}", _angle, Word(result.sine), Word(result.cosine)));
  };
  for (std::uint16_t angle = 0; angle < SINE_ANGLES; ++angle)
  {
    sinCos(angle);
  }
  for (const std::uint16_t angle : ANGLES_PAST_THE_MASK)
  {
    sinCos(angle);
  }
  std::vector<Elite::SinCos> rotations;
  rotations.reserve(ROTATION_ANGLES.size() + RAW_ROTATIONS.size());
  for (const std::uint16_t angle : ROTATION_ANGLES)
  {
    rotations.push_back(Elite::SinCosOf(angle));
  }
  rotations.insert(rotations.end(), RAW_ROTATIONS.begin(), RAW_ROTATIONS.end());
  for (const Elite::Pair& point : ROTATION_POINTS)
  {
    for (const Elite::SinCos& by : rotations)
    {
      const Elite::Pair turned = Elite::RotateBySinCos(point, by);
      _answers.push_back(std::format("RotateBySinCos {:04x} {:04x} by {:04x} {:04x} = {:04x} {:04x}", Word(point.first), Word(point.second),
                                     Word(by.sine), Word(by.cosine), Word(turned.first), Word(turned.second)));
    }
  }
}

// The arc tangents: of every pair of WORDS, and of each ratio directly and by its quadrant.
void ArcTangents(std::vector<std::string>& _answers)
{
  for (const std::int16_t a : WORDS)
  {
    for (const std::int16_t b : WORDS)
    {
      _answers.push_back(std::format("ArcTangent2 {:04x} {:04x} = {:04x}", Word(a), Word(b), Elite::ArcTangent2(a, b)));
    }
  }
  for (const auto& [numerator, denominator] : RATIOS)
  {
    _answers.push_back(
      std::format("RatioArcTangent {:04x} {:04x} = {:04x}", numerator, denominator, Elite::RatioArcTangent(numerator, denominator)));
    _answers.push_back(
      std::format("QuadrantArcTangent {:04x} {:04x} = {:04x}", denominator, numerator, Elite::QuadrantArcTangent(denominator, numerator)));
  }
}

// The lengths, the boxes, the angles' tolerance and the 24-bit shifts.
void Measures(std::vector<std::string>& _answers)
{
  for (const Elite::Vector& vector : VECTORS)
  {
    _answers.push_back(std::format("VectorLength {:04x} {:04x} {:04x} = {:04x}", Word(vector.x), Word(vector.y), Word(vector.z),
                                   Elite::VectorLength(vector)));
    for (const std::uint16_t halfSize : BOX_HALF_SIZES)
    {
      _answers.push_back(std::format("VectorWithinBox {:04x} {:04x} {:04x} {:04x} = {:d}", Word(vector.x), Word(vector.y), Word(vector.z),
                                     halfSize, Elite::VectorWithinBox(vector, halfSize) ? 1 : 0));
    }
  }
  for (const std::uint16_t a : ANGLES)
  {
    for (const std::uint16_t b : ANGLES)
    {
      for (const std::uint16_t tolerance : TOLERANCES)
      {
        _answers.push_back(std::format("AngleWithinTolerance {:04x} {:04x} {:04x} = {:d}", a, b, tolerance,
                                       Elite::AngleWithinTolerance(a, b, tolerance) ? 1 : 0));
      }
    }
  }
  for (const std::uint32_t value : SHIFTED)
  {
    for (const std::uint8_t count : SHIFTS)
    {
      _answers.push_back(std::format("ShiftRight24 {:06x} {:d} = {:06x}", value, count, Elite::ShiftRight24(value, count)));
    }
  }
}

} // namespace

// Known answers per subsystem (D33): the maths, from the faithful code while the corpus still vouched for it.
TEST_CLASS(MathsAnswerTests)
{
public:
  // The value routines of Maths.h, the multiplies, divides, random numbers and trigonometry the game uses, over a spread
  // of inputs that takes in the edges: the answers recorded in MATHS_ANSWERS_FILE, MATHS_ANSWERS of them.
  TEST_METHOD(EveryRoutineGivesTheOriginalsAnswers)
  {
    ReferenceRig rig("MathsAnswers");
    Elite::GameState& state = rig.State();
    std::vector<std::string> answers;
    Divides(answers);
    RandomNumbers(state, answers);
    Rotations(answers);
    ArcTangents(answers);
    Measures(answers);
    CheckSubsystemAnswers(MATHS_ANSWERS_FILE, answers, MATHS_ANSWERS);
  }
};

} // namespace GameLogicTests
