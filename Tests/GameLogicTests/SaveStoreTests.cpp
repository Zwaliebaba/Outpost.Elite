#include "pch.h"

#include "Commander.h"
#include "SaveGame.h"
#include "SaveStore.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Outpost::SaveStore;

/*
 * Where a commander file actually goes (slice 2d).
 *
 * This is the one file in `Outpost/` the suite covers, and covering it was an argument as much as
 * a test. `SaveStore.h` used to say the file could not be tested because it has no oracle; that
 * conflates two things. There is no shipped routine to compare it against -- the C64 handed the
 * Kernal a filename and this writes a file -- but a path and two streams have properties, and the
 * first test written against them found a defect.
 *
 * It runs on both legs. MSVC compiles it against the real Win32; the portable runner compiles the
 * same file against ten lines of stand-in beside it. The store is rooted at a temporary directory
 * rather than at LocalAppData, so the tests write nowhere a person keeps anything.
 */
namespace GameLogicTests
{

namespace
{
std::wstring Widen(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// A directory of its own per test, removed on the way out however the test ends.
class ScratchRoot
{
public:
  explicit ScratchRoot(const char* _what)
  {
    m_path = std::filesystem::temp_directory_path() / ("outpost-savestore-" + std::string(_what));
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
  }

  ~ScratchRoot()
  {
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
  }

  ScratchRoot(const ScratchRoot&) = delete;
  ScratchRoot& operator=(const ScratchRoot&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

private:
  std::filesystem::path m_path;
};

/// A commander name as the game holds it: up to seven characters and a carriage return.
std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> Name(const char* _text)
{
  std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name{};
  std::size_t index = 0;
  for (; _text[index] != '\0' && index + 1 < name.size(); ++index)
  {
    name[index] = static_cast<std::uint8_t>(_text[index]);
  }
  name[index] = 13;
  return name;
}

std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> AFile(std::uint8_t _seed)
{
  std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
  for (std::size_t index = 0; index < file.size(); ++index)
  {
    file[index] = static_cast<std::uint8_t>(index * 7u + _seed);
  }
  return file;
}
} // namespace

TEST_CLASS(TheCommanderStoreOnDisk)
{
public:
  /*
   * A commander written and read back, byte for byte, and the file that results.
   *
   * The file is opened directly as well, because "the store agrees with itself" is the failure
   * mode a round-trip test has: a store that wrote the bytes reversed and read them reversed
   * would pass one.
   */
  TEST_METHOD(AWrittenCommanderComesBackByteForByte)
  {
    const ScratchRoot root("roundtrip");
    SaveStore store(root.Path());

    const auto name = Name("BELL");
    const auto file = AFile(0x11);

    Assert::IsTrue(store.Write(name, file), L"the write should succeed");

    std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> back{};
    Assert::IsTrue(store.Read(name, back), L"the read should succeed");
    for (std::size_t index = 0; index < file.size(); ++index)
    {
      Assert::AreEqual(file[index], back[index], (L"byte " + std::to_wstring(index)).c_str());
    }

    // The file itself, at the name a person would look for it under.
    const std::filesystem::path expected = root.Path() / "BELL.cmdr";
    Assert::IsTrue(std::filesystem::exists(expected), L"the file should be named after the commander");
    Assert::AreEqual<std::uintmax_t>(Elite::COMMANDER_FILE_SIZE, std::filesystem::file_size(expected),
                                     L"the file should be exactly the block and its name");

    std::ifstream in(expected, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Assert::AreEqual<std::size_t>(file.size(), raw.size(), L"the file's length");
    for (std::size_t index = 0; index < file.size(); ++index)
    {
      Assert::AreEqual(file[index], static_cast<std::uint8_t>(raw[index]),
                       (L"file byte " + std::to_wstring(index)).c_str());
    }
  }

  /*
   * The names that must not become a path.
   *
   * The commander's name reaches here from a FILE as well as from the keyboard, so this is where
   * untrusted bytes would turn into a filename. Everything below is refused, and refused means
   * the call fails rather than writing somewhere else.
   */
  TEST_METHOD(AnUnsafeNameIsRefusedRatherThanRewritten)
  {
    const ScratchRoot root("unsafe");
    SaveStore store(root.Path());
    const auto file = AFile(0x22);

    struct Case
    {
      const char* what;
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name;
    };

    const std::vector<Case> REFUSED = {
      { "empty", Name("") },
      { "a separator", Name("A/B") },
      { "a backslash", Name("A\\B") },
      { "traversal", Name("..") },
      { "a dot", Name("A.B") },
      { "a colon", Name("C:") },
      { "a space", Name("A B") },
      { "a wildcard", Name("A*") },
      /*
       * The device names, which are the reason this test exists. Every one of them is letters and
       * digits, so the alphanumeric filter this class started with let them all through -- and
       * Win32 resolves them to the DEVICE whatever directory and extension surround them.
       */
      { "CON", Name("CON") },
      { "con, lower case", Name("con") },
      { "CoN, mixed", Name("CoN") },
      { "PRN", Name("PRN") },
      { "AUX", Name("AUX") },
      { "NUL", Name("NUL") },
      { "COM1", Name("COM1") },
      { "COM9", Name("COM9") },
      { "LPT1", Name("LPT1") },
      { "LPT9", Name("LPT9") },
    };

    for (const Case& item : REFUSED)
    {
      const std::wstring where = Widen(std::string("refused: ") + item.what);
      Assert::IsFalse(store.Write(item.name, file), (where + L" -- the write").c_str());

      std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> back{};
      Assert::IsFalse(store.Read(item.name, back), (where + L" -- the read").c_str());
    }

    // Nothing was created by any of them, which is the property that matters: a refusal that
    // wrote somewhere else would pass the two assertions above.
    Assert::IsFalse(std::filesystem::exists(root.Path()), L"a refused name should create nothing at all");

    /*
     * The near misses, which must still work. COM0 and LPT0 are not devices, and neither is a
     * device name with anything appended -- so a rule that matched a PREFIX would take these too.
     */
    for (const char* allowed : { "COM0", "LPT0", "CONS", "AUXY", "NULL", "PRNT", "COM10" })
    {
      Assert::IsTrue(store.Write(Name(allowed), file),
                     (Widen(std::string("allowed: ") + allowed)).c_str());
    }
  }

  /// The list itself, walked rather than trusted.
  TEST_METHOD(TheReservedDeviceNamesAreTheOnesWindowsReserves)
  {
    for (const wchar_t* device : { L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4",
                                   L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2",
                                   L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9" })
    {
      Assert::IsTrue(SaveStore::IsReservedDeviceName(device), L"a device name should be recognised");
    }

    for (const wchar_t* file : { L"", L"C", L"CO", L"CONN", L"COM", L"COM0", L"LPT", L"LPT0", L"JAMESON",
                                 L"A", L"NULL" })
    {
      Assert::IsFalse(SaveStore::IsReservedDeviceName(file), L"an ordinary name should not be");
    }
  }

  /*
   * The failures a caller has to be able to tell apart.
   *
   * A store with no root fails everything rather than falling back to the working directory; a
   * read of a file that is not there fails; and a file of the wrong LENGTH is refused outright
   * rather than read short, because a partial read would leave the caller's commander half
   * updated and the checksums would then reject a block the file never contained.
   */
  TEST_METHOD(EveryFailurePathIsReported)
  {
    const auto name = Name("BELL");
    const auto file = AFile(0x33);

    SaveStore rootless(std::filesystem::path{});
    std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> back{};
    Assert::IsFalse(rootless.Write(name, file), L"a store with no root cannot write");
    Assert::IsFalse(rootless.Read(name, back), L"a store with no root cannot read");

    const ScratchRoot root("failures");
    SaveStore store(root.Path());
    Assert::IsFalse(store.Read(name, back), L"a commander that was never saved does not load");

    Assert::IsTrue(store.Write(name, file), L"the write should succeed");

    // One byte short, which the length check must catch.
    {
      std::ofstream out(root.Path() / "BELL.cmdr", std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size() - 1));
    }
    Assert::IsFalse(store.Read(name, back), L"a short file is refused rather than read short");

    // And one byte long, which is the same rule from the other side.
    {
      std::ofstream out(root.Path() / "BELL.cmdr", std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
      const char extra = 0;
      out.write(&extra, 1);
    }
    Assert::IsFalse(store.Read(name, back), L"a long file is refused too");
  }

  /*
   * The name is CR-terminated, and what follows the terminator is not part of it.
   *
   * `TRNME` copies eight bytes whatever was typed, so the bytes past the carriage return are
   * whatever the buffer held -- the previous commander's name, usually. A store that read all
   * eight would give two different commanders two different files for the same name.
   */
  TEST_METHOD(TheNameStopsAtItsCarriageReturn)
  {
    const ScratchRoot root("terminator");
    SaveStore store(root.Path());
    const auto file = AFile(0x44);

    std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Name("BELL");
    Assert::IsTrue(store.Write(name, file), L"the write should succeed");

    // The same name with different rubbish behind the terminator.
    name[5] = 'X';
    name[6] = 'Y';
    std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> back{};
    Assert::IsTrue(store.Read(name, back), L"the same commander should still be found");
    for (std::size_t index = 0; index < file.size(); ++index)
    {
      Assert::AreEqual(file[index], back[index], (L"byte " + std::to_wstring(index)).c_str());
    }

    // And exactly one file exists, rather than one per trailing byte pattern.
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root.Path()))
    {
      files += entry.is_regular_file() ? 1u : 0u;
    }
    Assert::AreEqual<std::size_t>(1, files, L"one commander, one file");
  }
};

} // namespace GameLogicTests
