#include "pch.h"

#include "OracleImage.h"

#include "TextPrint.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::NumberWorkspace;
using Elite::TextSink;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The number printer against the game (slice 1c-c, the numbers half).
 *
 * Compared as a sequence of characters, not as a value, through a trap on DASC -- the same
 * mechanism slice 1c-a built for the token printer (plan section 6.7). What BPRNT does is not
 * "produce a number", it is "hand these characters over in this order, including the spaces it
 * pads with and the point it may or may not print", and only a character list captures that.
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

/// Collects what the printer hands over, which is the whole of its observable behaviour.
class Collector : public TextSink
{
public:
  void Put(std::uint8_t _character) override { characters.push_back(_character); }
  std::vector<std::uint8_t> characters;
};

std::string Show(const std::vector<std::uint8_t>& _characters)
{
  std::string text = "\"";
  for (const std::uint8_t character : _characters)
  {
    text += (character >= 32 && character < 127) ? static_cast<char>(character) : '?';
  }
  return text + "\"";
}

std::wstring Widen(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}
} // namespace

TEST_CLASS(NumberPrinterAgainstTheShippedGame)
{
public:
  /*
   * BPRNT over a wide spread of values, widths and both settings of the carry that decides
   * whether a decimal point appears at all.
   *
   * The values are chosen for the boundaries the routine has rather than for size: zero, the
   * powers of ten either side of each digit position, values that fill the width exactly and
   * values that overflow it, and the largest thing a 32-bit K can hold.
   */
  TEST_METHOD(NumbersMatchTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t routine = oracle.Label("BPRNT");
    const std::uint16_t k = oracle.Label("K");
    const std::uint16_t u = oracle.Label("U");

    const std::uint32_t values[] = { 0u,         1u,         9u,         10u,        99u,        100u,
                                     999u,       1000u,      1234u,      9999u,      10000u,     65535u,
                                     65536u,     123456u,    999999u,    1000000u,   16777215u,  99999999u,
                                     100000000u, 999999999u, 1000000000u, 4294967295u };

    std::uint32_t compared = 0;

    for (const bool withPoint : { false, true })
    {
      for (std::uint32_t digits = 0; digits <= 6; ++digits)
      {
        for (const std::uint32_t value : values)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(oracle.Label("DASC"));

          cpu.memory[k] = static_cast<std::uint8_t>(value >> 24);
          cpu.memory[k + 1] = static_cast<std::uint8_t>(value >> 16);
          cpu.memory[k + 2] = static_cast<std::uint8_t>(value >> 8);
          cpu.memory[k + 3] = static_cast<std::uint8_t>(value);
          cpu.memory[u] = static_cast<std::uint8_t>(digits);
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.c = withPoint;

          const auto run = cpu.CallSubroutine(routine, 500'000);
          Assert::IsTrue(run.completed && !run.illegalOpcode, L"BPRNT should return");

          std::vector<std::uint8_t> expected;
          for (const auto& hit : cpu.trapHits)
          {
            expected.push_back(hit.a);
          }

          Collector collector;
          NumberWorkspace work;
          work.k[0] = static_cast<std::uint8_t>(value >> 24);
          work.k[1] = static_cast<std::uint8_t>(value >> 16);
          work.k[2] = static_cast<std::uint8_t>(value >> 8);
          work.k[3] = static_cast<std::uint8_t>(value);
          work.u = static_cast<std::uint8_t>(digits);
          Elite::PrintNumber(collector, work, withPoint);

          const std::string context = "BPRNT " + std::to_string(value) + " with " + std::to_string(digits)
                                      + " decimals, point " + (withPoint ? "on" : "off") + ": game "
                                      + Show(expected) + ", port " + Show(collector.characters);
          Assert::IsTrue(expected == collector.characters, Widen(context).c_str());
          ++compared;
        }
      }
    }

    Logger::WriteMessage(("BPRNT: " + std::to_string(compared) + " numbers compared character for character\n").c_str());
  }

  /// 6502: TT11 and pr2 -- the entry points nearly every caller actually uses, which differ only
  /// in how they load K and what they leave the carry at.
  TEST_METHOD(EntryPointsMatchTheShippedRoutines)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();

    for (std::uint32_t value = 0; value < 65536u; value += 337u)
    {
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("DASC"));
      cpu.a = 5;
      cpu.y = static_cast<std::uint8_t>(value >> 8);
      cpu.x = static_cast<std::uint8_t>(value);
      cpu.sp = 0xFD;
      cpu.c = true;

      const auto run = cpu.CallSubroutine(oracle.Label("TT11"), 500'000);
      Assert::IsTrue(run.completed && !run.illegalOpcode, L"TT11 should return");

      std::vector<std::uint8_t> expected;
      for (const auto& hit : cpu.trapHits)
      {
        expected.push_back(hit.a);
      }

      Collector collector;
      Elite::PrintValue(collector, static_cast<std::uint16_t>(value), 5, true);

      Assert::IsTrue(expected == collector.characters,
                     Widen("TT11 " + std::to_string(value) + ": game " + Show(expected) + ", port "
                           + Show(collector.characters))
                       .c_str());
    }

    for (std::uint32_t value = 0; value < 256u; ++value)
    {
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("DASC"));
      cpu.a = cpu.y = 0;
      cpu.x = static_cast<std::uint8_t>(value);
      cpu.sp = 0xFD;
      cpu.c = false;

      const auto run = cpu.CallSubroutine(oracle.Label("pr2"), 500'000);
      Assert::IsTrue(run.completed && !run.illegalOpcode, L"pr2 should return");

      std::vector<std::uint8_t> expected;
      for (const auto& hit : cpu.trapHits)
      {
        expected.push_back(hit.a);
      }

      Collector collector;
      Elite::PrintByteValue(collector, static_cast<std::uint8_t>(value), false);

      Assert::IsTrue(expected == collector.characters,
                     Widen("pr2 " + std::to_string(value) + ": game " + Show(expected) + ", port "
                           + Show(collector.characters))
                       .c_str());
    }
  }
};

} // namespace GameLogicTests
