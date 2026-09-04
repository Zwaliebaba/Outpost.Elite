#include "pch.h"

#include "OracleImage.h"

#include "LookupTables.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::OracleImage;

/*
 * The generated tables against the game they came from (slice 1a).
 *
 * tools/extract_tables.py pulls these arrays out of the assembled binaries by label. That is a
 * one-way copy, and a one-way copy with no check is a copy that goes stale. So every array is
 * compared here, byte for byte, against the same address range in the oracle's own image.
 *
 * This catches the three things that actually happen: someone edits a generated file by hand,
 * someone regenerates against a different build, and someone changes a table's length in the
 * extractor without changing its declaration.
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

/// Compares one generated array against the bytes at its label in the loaded game.
void CompareAgainstImage(const char* _label, std::span<const std::uint8_t> _generated)
{
  const OracleImage& oracle = OracleImage::Instance();

  std::uint16_t address = 0;
  Assert::IsTrue(oracle.TryLabel(_label, address), (L"missing label " + Widen(_label)).c_str());

  const Elite::Testing::Cpu6502 cpu = oracle.Fresh();

  for (std::size_t index = 0; index < _generated.size(); ++index)
  {
    const std::uint8_t expected = cpu.memory[static_cast<std::uint16_t>(address + index)];
    if (expected != _generated[index])
    {
      const std::wstring message = L"table " + Widen(_label) + L" differs at offset " + std::to_wstring(index)
                                   + L": image has " + std::to_wstring(expected) + L", generated file has "
                                   + std::to_wstring(_generated[index])
                                   + L"\nRegenerate with: python tools/extract_tables.py";
      Assert::Fail(message.c_str());
    }
  }
}

} // namespace

TEST_CLASS(GeneratedTablesMatchTheGame)
{
public:
  /*
   * 6502: TRANTABLE -- the keyboard's matrix position to a character.
   *
   * Extracted rather than written out, because the constants the port compares against are the
   * other end of this table: `f8` is internal key 37 and `TRANTABLE[37]` is `'8'`. A hand-typed
   * copy that agreed with the source's comments and not with its bytes would pass every test in
   * the suite except this one.
   */
  TEST_METHOD(TheKeyTranslationTableMatches)
  {
    if (OracleMissing())
    {
      return;
    }
    CompareAgainstImage("TRANTABLE", Elite::KEY_TRANSLATION);
  }

  TEST_METHOD(LogarithmTablesMatch)
  {
    if (OracleMissing())
    {
      return;
    }
    CompareAgainstImage("log", Elite::LOG_TABLE);
    CompareAgainstImage("logL", Elite::LOG_LOW_TABLE);
    CompareAgainstImage("antilog", Elite::ANTILOG_TABLE);
    CompareAgainstImage("antilogODD", Elite::ANTILOG_ODD_TABLE);
  }

  TEST_METHOD(TrigonometryTablesMatch)
  {
    if (OracleMissing())
    {
      return;
    }
    CompareAgainstImage("SNE", Elite::SINE_TABLE);
    CompareAgainstImage("ACT", Elite::ARCTAN_TABLE);
  }

  /// A cheap sanity check that does not consult the oracle at all: the sine table should climb
  /// to its peak and come back down. If the extractor ever grabbed the wrong address range,
  /// this says so in a way that reads as an obvious wrongness rather than a byte mismatch.
  TEST_METHOD(ScreenTablesMatch)
  {
    if (OracleMissing())
    {
      return;
    }
    CompareAgainstImage("TWOS", Elite::PIXEL_MASK_TABLE);
    CompareAgainstImage("TWOS2", Elite::DASH_MASK_TABLE);
    CompareAgainstImage("CTWOS2", Elite::MULTICOLOUR_MASK_TABLE);
    CompareAgainstImage("DTWOS", Elite::DASHBOARD_MASK_TABLE);
    CompareAgainstImage("TWFR", Elite::LINE_RIGHT_MASK_TABLE);
    CompareAgainstImage("TWFL", Elite::LINE_LEFT_MASK_TABLE);
    CompareAgainstImage("ylookupl", Elite::ROW_ADDRESS_LOW);
    CompareAgainstImage("ylookuph", Elite::ROW_ADDRESS_HIGH);
    CompareAgainstImage("celllookl", Elite::CELL_ADDRESS_LOW);
    CompareAgainstImage("celllookh", Elite::CELL_ADDRESS_HIGH);
    CompareAgainstImage("FONT", Elite::FONT_DATA);
  }

  TEST_METHOD(TheSineTableLooksLikeASineCurve)
  {
    Assert::AreEqual<std::uint32_t>(0, Elite::SINE_TABLE[0], L"a quarter turn starts at zero");

    std::size_t peak = 0;
    for (std::size_t index = 1; index < Elite::SINE_TABLE.size(); ++index)
    {
      if (Elite::SINE_TABLE[index] > Elite::SINE_TABLE[peak])
      {
        peak = index;
      }
    }

    Assert::IsTrue(Elite::SINE_TABLE[peak] == 0xFF, L"it should reach full scale");
    Assert::IsTrue(peak > 10 && peak < 22, L"and reach it somewhere near the middle");

    for (std::size_t index = 1; index <= peak; ++index)
    {
      Assert::IsTrue(Elite::SINE_TABLE[index] >= Elite::SINE_TABLE[index - 1], L"it should climb to the peak");
    }
  }
};

} // namespace GameLogicTests
