#include "pch.h"

#include "DirectoryFileStore.h"
#include "DosFiles.h"
#include "GameData.h"
#include "Platform.h"
#include "ReferenceRig.h"
#include "Text.h"

#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t BLOCK_BYTES = 208;
// A name of 130 bytes with no NUL in DOS's 128.
constexpr std::size_t LONG_NAME_BYTES = 130;

// The transfer area, in DOS's layout.
constexpr std::uint16_t AREA_TEMPLATE = 0x01;
constexpr std::uint16_t AREA_SEARCH_ATTRIBUTE = 0x0C;
constexpr std::uint16_t AREA_NEXT_ENTRY = 0x0D;
constexpr std::uint16_t AREA_ATTRIBUTE = 0x15;
constexpr std::uint16_t AREA_TIME = 0x16;
constexpr std::uint16_t AREA_DATE = 0x18;
constexpr std::uint16_t AREA_SIZE = 0x1A;
constexpr std::uint16_t AREA_NAME = 0x1E;
constexpr std::uint16_t AREA_NAME_BYTES = 13;

// DOS's errors, as AX gives them.
constexpr std::uint32_t FILE_NOT_FOUND = 0x02;
constexpr std::uint32_t PATH_NOT_FOUND = 0x03;
constexpr std::uint32_t TOO_MANY_OPEN_FILES = 0x04;
constexpr std::uint32_t ACCESS_DENIED = 0x05;
constexpr std::uint32_t INVALID_HANDLE = 0x06;
constexpr std::uint32_t INVALID_ACCESS_CODE = 0x0C;
constexpr std::uint32_t NO_MORE_FILES = 0x12;

constexpr std::uint8_t READ_ONLY = Elite::FileStore::ATTRIBUTE_READ_ONLY;
constexpr std::uint8_t HIDDEN = Elite::FileStore::ATTRIBUTE_HIDDEN;
constexpr std::uint8_t SYSTEM = Elite::FileStore::ATTRIBUTE_SYSTEM;
constexpr std::uint8_t ARCHIVE = Elite::FileStore::ATTRIBUTE_ARCHIVE;

// A start moment with every field of a stamp set: 12:34:56 on 1 October 1987.
constexpr Elite::DosDateTime START = {1987, 10, 1, 12, 34, 56, 0};
constexpr std::uint32_t START_DATE = 0x0F41; // (1987 - 1980) << 9 | 10 << 5 | 1
constexpr std::uint32_t START_TIME = 0x645C; // 12 << 11 | 34 << 5 | 56 / 2
// Two seconds of the machine's clock, the 8088's 4.77 MHz, and the time a stamp then shows: 12:34:58.
constexpr Elite::Cycles TWO_SECONDS = 9'545'454;
constexpr std::uint32_t TWO_SECONDS_LATER_TIME = 0x645D;

using Bytes = std::span<Elite::Counted<std::uint8_t>>;
using Snapshot = std::vector<std::uint8_t>;

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// DOS's file services natively (DosFiles), on the platform of a Game loaded from the reference, called as the game calls
/// them: on its own members of the game's state, a name up to its NUL in commanderFileName, its commander's bytes and its
/// transfer area (D38). The bytes of the game's memory each call changes are counted (LastChanges), and AX is checked to be 0 where DOS
/// left it as it went in; what a test expects of the answers, it states outright. Until D33 each call was also made on the
/// emulated DOS it replaced (Machine::Dos), and checked to answer alike, to change the same bytes as many times and to leave all
/// of memory and the store alike: what the test states outright is what both gave.
class DosRig
{
public:
  DosRig(std::string_view _name, const Elite::DosDateTime& _start)
    : m_rig(_name, _start)
  {
  }

  /// The game's own places: the transfer area it sets, where it keeps a file's name, and its commander's bytes.
  [[nodiscard]] Bytes Area() noexcept
  {
    return m_rig.State().Data().saveLoad.diskTransferArea;
  }

  [[nodiscard]] Bytes NameBuffer() noexcept
  {
    return m_rig.State().Data().saveLoad.commanderFileName;
  }

  [[nodiscard]] Bytes Block()
  {
    return Elite::BytesWithin(Elite::BytesOf(m_rig.State().Data().commander), 0, BLOCK_BYTES);
  }

  /// A second transfer area, for a search that is not the game's.
  [[nodiscard]] Bytes OtherArea() noexcept
  {
    return m_rig.State().Data().saveLoad.commanderFileList;
  }

  /// _bytes into _to from its first, and a NUL after them when _terminate.
  static void Put(Bytes _to, std::string_view _bytes, bool _terminate = true)
  {
    for (std::size_t index = 0; index < _bytes.size(); ++index)
    {
      _to[index] = static_cast<std::uint8_t>(_bytes[index]);
    }
    if (_terminate)
    {
      _to[_bytes.size()] = 0;
    }
  }

  /// _name as the game hands DOS one: in commanderFileName, up to the NUL after it.
  std::string_view Name(std::string_view _name)
  {
    Put(NameBuffer(), _name);
    return Elite::TextIn(NameBuffer());
  }

  /// The name the game's buffer holds now, up to its NUL.
  [[nodiscard]] std::string_view Name() noexcept
  {
    return Elite::TextIn(NameBuffer());
  }

  /// 130 'N's in the commander's bytes, with no NUL in DOS's 128: a name that never ends.
  std::string_view LongName()
  {
    const Bytes bytes = Block().first(LONG_NAME_BYTES);
    Put(bytes, std::string(LONG_NAME_BYTES, 'N'), false);
    return Elite::TextIn(bytes);
  }

  /// The commander's first _bytes as DOS writes them out.
  [[nodiscard]] std::span<const std::uint8_t> BlockBytes(std::size_t _bytes)
  {
    const Bytes block = Block();
    return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(block.data()), _bytes);
  }

  /// The clock moved on to _clock, from device event to device event, as a wait moves it.
  void AdvanceClock(Elite::Cycles _clock)
  {
    const Snapshot before = Memory();
    while (m_rig.Host().Clock() < _clock)
    {
      m_rig.Host().Idle(_clock);
    }
    Assert::IsTrue(m_rig.Host().Clock() == _clock, L"the clock where it was sent");
    Assert::IsTrue(Memory() == before, L"moving the clock changes no memory");
  }

  /// A directory in the store's directory, which the store does not show.
  void MakeDirectory(std::string_view _name)
  {
    std::filesystem::create_directories(m_rig.Files() / _name);
  }

  void SetArea(Bytes _area)
  {
    const Snapshot before = Memory();
    Files().SetTransferArea(_area);
    Done(before);
    Assert::AreEqual(std::uint64_t{0}, m_lastChanges, L"setting the transfer area changes no memory");
  }

  Elite::DosFileAttributes ReadAttributes(std::string_view _name)
  {
    const Snapshot before = Memory();
    const Elite::DosFileAttributes answer = Files().ReadAttributes(_name);
    Done(before);
    if (!answer.answer.failed)
    {
      Assert::AreEqual(std::uint32_t{answer.answer.value}, std::uint32_t{answer.attributes}, L"read attributes: AX = CX");
    }
    else
    {
      Assert::AreEqual(0u, std::uint32_t{answer.attributes}, L"read attributes: CX 0 on a failure, where DOS left it");
    }
    return answer;
  }

  Elite::DosAnswer SetAttributes(std::string_view _name, std::uint16_t _attributes)
  {
    const Snapshot before = Memory();
    return LeftOnSuccess(Files().SetAttributes(_name, _attributes), before, L"set attributes");
  }

  Elite::DosAnswer Create(std::string_view _name, std::uint16_t _attributes)
  {
    const Snapshot before = Memory();
    const Elite::DosAnswer answer = Files().Create(_name, _attributes, m_rig.Host().Clock());
    Done(before);
    return answer;
  }

  Elite::DosAnswer Open(std::string_view _name, std::uint8_t _access)
  {
    const Snapshot before = Memory();
    const Elite::DosAnswer answer = Files().Open(_name, _access);
    Done(before);
    return answer;
  }

  Elite::DosAnswer Read(std::uint16_t _handle, Bytes _buffer)
  {
    const Snapshot before = Memory();
    const Elite::DosAnswer answer = Files().Read(_handle, _buffer);
    Done(before);
    return answer;
  }

  Elite::DosAnswer Write(std::uint16_t _handle, std::span<const std::uint8_t> _buffer)
  {
    const Snapshot before = Memory();
    const Elite::DosAnswer answer = Files().Write(_handle, _buffer, m_rig.Host().Clock());
    Done(before);
    return answer;
  }

  Elite::DosAnswer Close(std::uint16_t _handle)
  {
    const Snapshot before = Memory();
    return LeftOnSuccess(Files().Close(_handle), before, L"close");
  }

  Elite::DosAnswer Delete(std::string_view _name)
  {
    const Snapshot before = Memory();
    return LeftOnSuccess(Files().Delete(_name), before, L"delete");
  }

  Elite::DosAnswer FindFirst(std::string_view _pattern, std::uint16_t _attributes)
  {
    const Snapshot before = Memory();
    const Elite::DosAnswer answer = Files().FindFirst(_pattern, _attributes);
    Done(before);
    return answer;
  }

  Elite::DosAnswer FindNext()
  {
    const Snapshot before = Memory();
    const Elite::DosAnswer answer = Files().FindNext();
    Done(before);
    return answer;
  }

  /// AH=_function on the standard handle or with the byte count the emulated DOS refused as a fault, stopping the run there and
  /// changing nothing: the native service throws std::logic_error and changes nothing.
  void Refused(std::uint8_t _function, std::uint16_t _handle, std::uint16_t _bytes)
  {
    const Snapshot before = Memory();
    Elite::DosFiles& files = Files();
    const Bytes block = Block().first(_bytes);
    const std::span<const std::uint8_t> written = BlockBytes(_bytes);
    Assert::ExpectException<std::logic_error>(
      [&]
      {
        if (_function == 0x3E)
        {
          (void)files.Close(_handle);
        }
        else if (_function == 0x3F)
        {
          (void)files.Read(_handle, block);
        }
        else
        {
          (void)files.Write(_handle, written, 0);
        }
      },
      L"the native service refuses it");
    Assert::IsTrue(Memory() == before, L"the native service changes nothing");
  }

  /// The store's files.
  [[nodiscard]] std::vector<Elite::FileEntry> StoredFiles()
  {
    return m_rig.Store().List();
  }

  /// The game's memory's byte and word _at bytes into _bytes.
  [[nodiscard]] static std::uint32_t Byte(Bytes _bytes, std::size_t _at)
  {
    return _bytes[_at];
  }

  [[nodiscard]] static std::uint32_t Word(Bytes _bytes, std::size_t _at)
  {
    return Elite::WordIn(_bytes, _at);
  }

  /// _count of _bytes from _at, as text.
  [[nodiscard]] static std::string Text(Bytes _bytes, std::size_t _at, std::size_t _count)
  {
    std::string text;
    for (std::size_t index = 0; index < _count; ++index)
    {
      text.push_back(static_cast<char>(_bytes[_at + index]));
    }
    return text;
  }

  /// The bytes the last call changed.
  [[nodiscard]] std::uint64_t LastChanges() const noexcept
  {
    return m_lastChanges;
  }

  /// The game's memory now: GameData's bytes, then video memory's.
  [[nodiscard]] Snapshot Memory() const
  {
    const Elite::GameState& state = m_rig.State();
    Snapshot bytes(state.DataBytes().begin(), state.DataBytes().end());
    bytes.insert(bytes.end(), state.VideoBytes().begin(), state.VideoBytes().end());
    return bytes;
  }

  [[nodiscard]] Elite::DosFiles& Files() noexcept
  {
    return m_rig.Host().Files();
  }

private:
  void Done(const Snapshot& _before)
  {
    const Snapshot after = Memory();
    std::uint64_t changed = 0;
    for (std::size_t at = 0; at < after.size(); ++at)
    {
      changed += after[at] != _before[at] ? 1 : 0;
    }
    m_lastChanges = changed;
  }

  // A service that succeeds leaves AX as it went in on DOS, and 0 natively: no caller reads it.
  Elite::DosAnswer LeftOnSuccess(const Elite::DosAnswer& _answer, const Snapshot& _before, const wchar_t* _call)
  {
    Done(_before);
    if (!_answer.failed)
    {
      Assert::AreEqual(0u, std::uint32_t{_answer.value}, (std::wstring(_call) + L": AX 0, where DOS left it as it went in").c_str());
    }
    return _answer;
  }

  ReferenceRig m_rig;
  std::uint64_t m_lastChanges = 0;
};

// A file made through DOS: created, _bytes written from the block, closed, and its attributes set.
void MakeFile(DosRig& _twin, std::string_view _name, std::uint16_t _bytes, std::uint8_t _attributes = 0)
{
  const Elite::DosAnswer created = _twin.Create(_twin.Name(_name), 0);
  Assert::IsFalse(created.failed, L"made: created");
  Assert::IsFalse(_twin.Write(created.value, _twin.BlockBytes(_bytes)).failed, L"made: written");
  Assert::IsFalse(_twin.Close(created.value).failed, L"made: closed");
  Assert::IsFalse(_twin.SetAttributes(_twin.Name(_name), _attributes).failed, L"made: its attributes set");
}

// The transfer area _area holds the search for _template with _attribute, at entry _next.
void CheckSearch(Bytes _area, std::string_view _template, std::uint32_t _attribute, std::uint32_t _next)
{
  Assert::AreEqual(3u, DosRig::Byte(_area, 0), L"the drive, C:");
  Assert::IsTrue(DosRig::Text(_area, AREA_TEMPLATE, 11) == _template, L"the search's template");
  Assert::AreEqual(_attribute, DosRig::Byte(_area, AREA_SEARCH_ATTRIBUTE), L"the search's attribute");
  Assert::AreEqual(_next, DosRig::Word(_area, AREA_NEXT_ENTRY), L"the next entry");
  for (std::uint16_t offset = AREA_NEXT_ENTRY + 2; offset < AREA_ATTRIBUTE; ++offset)
  {
    Assert::AreEqual(0u, DosRig::Byte(_area, offset), L"the reserved bytes, zero");
  }
}

// The transfer area _area holds the file found: _attribute, _time, _date, _sizeBytes and _name, zero-padded to 13 bytes.
void CheckFound(Bytes _area, std::uint32_t _attribute, std::uint32_t _time, std::uint32_t _date, std::uint32_t _sizeBytes,
                std::string_view _name)
{
  Assert::AreEqual(_attribute, DosRig::Byte(_area, AREA_ATTRIBUTE), L"the file's attribute");
  Assert::AreEqual(_time, DosRig::Word(_area, AREA_TIME), L"the file's time");
  Assert::AreEqual(_date, DosRig::Word(_area, AREA_DATE), L"the file's date");
  Assert::AreEqual(_sizeBytes, DosRig::Word(_area, AREA_SIZE) | (DosRig::Word(_area, AREA_SIZE + 2) << 16), L"the file's size");
  std::string name(_name);
  name.resize(AREA_NAME_BYTES, '\0');
  Assert::IsTrue(DosRig::Text(_area, AREA_NAME, AREA_NAME_BYTES) == name, (L"the file's name, zero-padded: " + Widen(_name)).c_str());
}

} // namespace

// DOS's file services natively (DosFiles, D27). Until D33 each ran beside the emulated DOS it replaced (Machine::Dos), call for
// call, and gave the same answers, changed the same bytes of memory and left the same files; each states what both gave
// outright, so that it outlived the emulated DOS.
TEST_CLASS(DosFilesTests)
{
public:
  // SaveCommanderFile then LoadCommanderFile, call for call, at the game's own places.
  TEST_METHOD(CreateWriteCloseOpenReadClose)
  {
    DosRig twin("DosFilesSaveLoad", START);
    std::string commander;
    for (std::uint16_t index = 0; index < BLOCK_BYTES; ++index)
    {
      commander.push_back(static_cast<char>(1 + index % 251)); // no zero, so that a read into zeros changes every byte
    }
    DosRig::Put(twin.Block(), commander, false);
    twin.SetArea(twin.Area());

    // A file made at the start moment, and then the commander's two seconds later.
    const Elite::DosAnswer empty = twin.Create(twin.Name("EMPTY.CDR"), 0);
    Assert::IsTrue(!empty.failed && empty.value == 5, L"handles start at 5");
    Assert::IsFalse(twin.Close(5).failed);
    twin.AdvanceClock(TWO_SECONDS);
    const Elite::DosAnswer created = twin.Create(twin.Name("JAMESON.CDR"), 0);
    Assert::IsTrue(!created.failed && created.value == 5, L"handle 5, free again");
    const Elite::DosAnswer written = twin.Write(5, twin.BlockBytes(BLOCK_BYTES));
    Assert::IsTrue(!written.failed && written.value == BLOCK_BYTES, L"every byte written");
    Assert::IsFalse(twin.Close(5).failed);
    const Elite::DosFileAttributes archived = twin.ReadAttributes(twin.Name());
    Assert::IsTrue(!archived.answer.failed && archived.attributes == ARCHIVE && archived.answer.value == ARCHIVE,
                   L"a new file has the archive bit, in CX and AX");
    Assert::IsFalse(twin.SetAttributes(twin.Name(), 0).failed, L"which the game clears");
    Assert::AreEqual(0u, std::uint32_t{twin.ReadAttributes(twin.Name()).attributes});

    // Loaded over the commander it was saved from, as the game loads one over another: a byte that is already what the file
    // holds is no change, so nothing changes.
    Assert::IsFalse(twin.Open(twin.Name(), 0).failed);
    Assert::AreEqual(std::uint32_t{BLOCK_BYTES}, std::uint32_t{twin.Read(5, twin.Block()).value}, L"read over itself");
    Assert::AreEqual(std::uint64_t{0}, twin.LastChanges(), L"a byte read over the same byte is no change");
    Assert::IsFalse(twin.Close(5).failed);

    // Loaded back over zeros.
    DosRig::Put(twin.Block(), std::string(BLOCK_BYTES, '\0'), false);
    const Elite::DosAnswer opened = twin.Open(twin.Name(), 0);
    Assert::IsTrue(!opened.failed && opened.value == 5, L"opened as handle 5");
    const Elite::DosAnswer read = twin.Read(5, twin.Block());
    Assert::IsTrue(!read.failed && read.value == BLOCK_BYTES, L"every byte read");
    Assert::AreEqual(std::uint64_t{BLOCK_BYTES}, twin.LastChanges(), L"each byte of the block changed");
    Assert::IsTrue(DosRig::Text(twin.Block(), 0, BLOCK_BYTES) == commander, L"the commander's bytes, back in the block");
    const Elite::DosAnswer end = twin.Read(5, twin.Block());
    Assert::IsTrue(!end.failed && end.value == 0, L"0 at the end of the file");
    Assert::AreEqual(std::uint64_t{0}, twin.LastChanges(), L"and nothing changed");
    Assert::IsFalse(twin.Close(5).failed);

    const std::vector<Elite::FileEntry> files = twin.StoredFiles();
    Assert::AreEqual(2u, static_cast<std::uint32_t>(files.size()));
    Assert::IsTrue(files[0].name == "EMPTY.CDR" && files[0].sizeBytes == 0 && files[0].attribute == ARCHIVE, L"EMPTY.CDR");
    Assert::AreEqual(START_DATE, std::uint32_t{files[0].stamp.date}, L"stamped on the start's date");
    Assert::AreEqual(START_TIME, std::uint32_t{files[0].stamp.time}, L"at the start's time");
    Assert::IsTrue(files[1].name == "JAMESON.CDR" && files[1].sizeBytes == BLOCK_BYTES && files[1].attribute == 0, L"JAMESON.CDR");
    Assert::AreEqual(START_DATE, std::uint32_t{files[1].stamp.date});
    Assert::AreEqual(TWO_SECONDS_LATER_TIME, std::uint32_t{files[1].stamp.time}, L"two seconds later");
  }

  // ListCommanderFiles' searches, and others: find-first and find-next over several files and attributes, to the end.
  TEST_METHOD(FindFirstAndNextToTheEnd)
  {
    DosRig twin("DosFilesFind", START);
    DosRig::Put(twin.Block(), "0123456789", false);
    twin.SetArea(twin.Area());
    MakeFile(twin, "ZED.CDR", 4);
    MakeFile(twin, "A.CDR", 3);
    MakeFile(twin, "B.CDR", 1, HIDDEN);
    MakeFile(twin, "C.CDR", 2, SYSTEM);
    MakeFile(twin, "ELITE01.HI", 5);
    MakeFile(twin, "READ.CDR", 6, READ_ONLY);
    twin.MakeDirectory("SUB.CDR");
    // In the store's order: A.CDR, B.CDR, C.CDR, ELITE01.HI, READ.CDR, ZED.CDR; no directory.

    // The game's pattern, with no hidden or system file wanted: A, READ and ZED, then no more, and no more again.
    Elite::DosAnswer found = twin.FindFirst(twin.Name("*.cdr"), 0);
    Assert::IsTrue(!found.failed && found.value == 0, L"A.CDR found");
    CheckSearch(twin.Area(), "????????CDR", 0, 1);
    CheckFound(twin.Area(), 0, START_TIME, START_DATE, 3, "A.CDR");
    found = twin.FindNext();
    Assert::IsTrue(!found.failed && found.value == 0, L"READ.CDR found");
    CheckSearch(twin.Area(), "????????CDR", 0, 5);
    CheckFound(twin.Area(), READ_ONLY, START_TIME, START_DATE, 6, "READ.CDR");
    found = twin.FindNext();
    CheckFound(twin.Area(), 0, START_TIME, START_DATE, 4, "ZED.CDR");
    found = twin.FindNext();
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"no more files");
    CheckSearch(twin.Area(), "????????CDR", 0, 6);
    CheckFound(twin.Area(), 0, START_TIME, START_DATE, 4, "ZED.CDR"); // the last found is left
    found = twin.FindNext();
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"and no more again");

    // The game's search attribute as Start leaves CX, which lets hidden and system files in: all five.
    found = twin.FindFirst(twin.Name(), 0xFFE7);
    CheckSearch(twin.Area(), "????????CDR", 0xE7, 1);
    const std::array<std::string_view, 4> rest = {"B.CDR", "C.CDR", "READ.CDR", "ZED.CDR"};
    for (const std::string_view name : rest)
    {
      found = twin.FindNext();
      Assert::IsTrue(!found.failed && DosRig::Text(twin.Area(), AREA_NAME, name.size()) == name, Widen(name).c_str());
    }
    Assert::IsTrue(twin.FindNext().failed);

    // Other patterns: a match by '?', none at all, and a volume label, which the store has none of.
    found = twin.FindFirst(twin.Name("ELITE??.*"), 0);
    CheckFound(twin.Area(), 0, START_TIME, START_DATE, 5, "ELITE01.HI");
    found = twin.FindFirst(twin.Name("*.XYZ"), 0);
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"nothing matches: no more files");
    CheckSearch(twin.Area(), "????????XYZ", 0, 6);
    found = twin.FindFirst(twin.Name("*.*"), Elite::FileStore::ATTRIBUTE_VOLUME);
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"no volume label");
    CheckSearch(twin.Area(), "???????????", Elite::FileStore::ATTRIBUTE_VOLUME, 0);

    // Patterns DOS refuses, which leave the transfer area alone.
    found = twin.FindFirst(twin.Name("..\\*.CDR"), 0);
    Assert::IsTrue(found.failed && found.value == PATH_NOT_FOUND, L"a path");
    found = twin.FindFirst(twin.Name("A.B.C"), 0);
    Assert::IsTrue(found.failed && found.value == FILE_NOT_FOUND, L"two dots");
    found = twin.FindFirst(twin.Name(""), 0);
    Assert::IsTrue(found.failed && found.value == FILE_NOT_FOUND, L"nothing");
    found = twin.FindFirst(twin.LongName(), 0);
    Assert::IsTrue(found.failed && found.value == PATH_NOT_FOUND, L"no NUL in 128 bytes");

    // A transfer area no search has filled: find-next has nothing to go on.
    twin.SetArea(twin.OtherArea());
    found = twin.FindNext();
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"no search in the transfer area");
  }

  // The transfer area before the program sets one: DOS's was PSP:0080, over the command tail, outside the game's memory, and
  // the PSP is not memory any more (D33). A search there is refused, as a fault, and changes nothing. The game always sets one
  // first.
  TEST_METHOD(FindBeforeATransferAreaIsRefused)
  {
    DosRig twin("DosFilesPspArea", START);
    DosRig::Put(twin.Block(), "x", false);
    MakeFile(twin, "ONLY.CDR", 1);
    const std::string_view pattern = twin.Name("*.CDR");
    const Snapshot named = twin.Memory();
    Assert::ExpectException<std::logic_error>([&twin, pattern] { (void)twin.Files().FindFirst(pattern, 0); }, L"find first");
    Assert::ExpectException<std::logic_error>([&twin] { (void)twin.Files().FindNext(); }, L"find next");
    Assert::IsTrue(twin.Memory() == named, L"and nothing changed");
    twin.SetArea(twin.Area());
    Assert::IsFalse(twin.FindFirst(pattern, 0).failed, L"found once there is a transfer area");
    CheckFound(twin.Area(), 0, START_TIME, START_DATE, 1, "ONLY.CDR");
  }

  // DeleteCommanderFile, and the attributes that guard a file.
  TEST_METHOD(DeleteAndAttributes)
  {
    DosRig twin("DosFilesDelete", START);
    DosRig::Put(twin.Block(), "abc", false);
    MakeFile(twin, "LOCKED.CDR", 3, READ_ONLY);
    Assert::AreEqual(std::uint32_t{READ_ONLY}, std::uint32_t{twin.ReadAttributes(twin.Name("LOCKED.CDR")).attributes});
    Elite::DosAnswer answer = twin.Delete(twin.Name());
    Assert::IsTrue(answer.failed && answer.value == ACCESS_DENIED, L"a read-only file is not deleted");
    answer = twin.Open(twin.Name(), 1);
    Assert::IsTrue(answer.failed && answer.value == ACCESS_DENIED, L"nor opened to write");
    answer = twin.Open(twin.Name(), 0);
    Assert::IsTrue(!answer.failed && answer.value == 5, L"but opened to read");
    Assert::IsFalse(twin.Close(5).failed);
    answer = twin.SetAttributes(twin.Name(), 0x0100);
    Assert::IsTrue(answer.failed && answer.value == ACCESS_DENIED, L"attributes above FFh");
    answer = twin.SetAttributes(twin.Name(), Elite::FileStore::ATTRIBUTE_DIRECTORY);
    Assert::IsTrue(answer.failed && answer.value == ACCESS_DENIED, L"the directory bit");
    Assert::IsFalse(twin.SetAttributes(twin.Name(), 0).failed);
    Assert::IsFalse(twin.Delete(twin.Name()).failed, L"deleted");
    Assert::IsTrue(twin.StoredFiles().empty(), L"gone");
    answer = twin.Delete(twin.Name());
    Assert::IsTrue(answer.failed && answer.value == FILE_NOT_FOUND, L"deleted again: not found");
    const Elite::DosFileAttributes missing = twin.ReadAttributes(twin.Name());
    Assert::IsTrue(missing.answer.failed && missing.answer.value == FILE_NOT_FOUND, L"no attributes: not found");
    answer = twin.SetAttributes(twin.Name(), 0);
    Assert::IsTrue(answer.failed && answer.value == FILE_NOT_FOUND, L"none to set");
  }

  // The errors: a missing file, a bad access code, a bad handle, a path, a name with no end, too many open handles, and the
  // calls the emulated DOS refused as faults.
  TEST_METHOD(Errors)
  {
    DosRig twin("DosFilesErrors", START);
    DosRig::Put(twin.Block(), "abcdef", false);
    Elite::DosAnswer answer = twin.Open(twin.Name("MISSING.CDR"), 0);
    Assert::IsTrue(answer.failed && answer.value == FILE_NOT_FOUND, L"a missing file");
    MakeFile(twin, "THERE.CDR", 6);
    answer = twin.Open(twin.Name("THERE.CDR"), 3);
    Assert::IsTrue(answer.failed && answer.value == INVALID_ACCESS_CODE, L"access 3");
    answer = twin.Open(twin.Name(), 0x40);
    Assert::IsTrue(!answer.failed && answer.value == 5, L"the sharing bits are ignored");
    Assert::IsFalse(twin.Close(5).failed);

    answer = twin.Close(7);
    Assert::IsTrue(answer.failed && answer.value == INVALID_HANDLE, L"close: a handle not open");
    answer = twin.Read(25, twin.Block().first(4));
    Assert::IsTrue(answer.failed && answer.value == INVALID_HANDLE, L"read: a handle past the table");
    answer = twin.Write(5, twin.BlockBytes(4));
    Assert::IsTrue(answer.failed && answer.value == INVALID_HANDLE, L"write: a closed handle");

    answer = twin.Create(twin.Name("C:X.CDR"), 0);
    Assert::IsTrue(answer.failed && answer.value == PATH_NOT_FOUND, L"a drive");
    answer = twin.Create(twin.Name("TOOLONG12.CD"), 0);
    Assert::IsTrue(answer.failed && answer.value == FILE_NOT_FOUND, L"a base of nine");
    const std::string_view longName = twin.LongName();
    answer = twin.Open(longName, 0);
    Assert::IsTrue(answer.failed && answer.value == PATH_NOT_FOUND, L"open: no NUL in 128 bytes");
    answer = twin.Create(longName, 0);
    Assert::IsTrue(answer.failed && answer.value == PATH_NOT_FOUND, L"create: no NUL in 128 bytes");
    answer = twin.Delete(longName);
    Assert::IsTrue(answer.failed && answer.value == PATH_NOT_FOUND, L"delete: no NUL in 128 bytes");
    Assert::IsTrue(twin.ReadAttributes(longName).answer.failed, L"read attributes: no NUL in 128 bytes");
    Assert::IsTrue(twin.SetAttributes(longName, 0).failed, L"set attributes: no NUL in 128 bytes");

    // Fifteen handles, 5 to 19, and no more: the game never holds two.
    for (std::uint16_t handle = 5; handle < 20; ++handle)
    {
      answer = twin.Open(twin.Name("THERE.CDR"), 0);
      Assert::IsTrue(!answer.failed && answer.value == handle, L"the next handle");
    }
    answer = twin.Open(twin.Name(), 0);
    Assert::IsTrue(answer.failed && answer.value == TOO_MANY_OPEN_FILES, L"open: too many open files");
    answer = twin.Create(twin.Name("NEW.CDR"), 0);
    Assert::IsTrue(answer.failed && answer.value == TOO_MANY_OPEN_FILES, L"create: too many open files");
    for (std::uint16_t handle = 5; handle < 20; ++handle)
    {
      Assert::IsFalse(twin.Close(handle).failed);
    }

    // What the emulated DOS refused as faults: the standard handles, and a write of nothing.
    twin.Refused(0x3E, 1, 0);
    twin.Refused(0x3F, 0, 4);
    twin.Refused(0x40, 4, 4);
    answer = twin.Create(twin.Name("TRUNC.CDR"), 0);
    Assert::IsFalse(answer.failed);
    twin.Refused(0x40, answer.value, 0);
    Assert::IsFalse(twin.Close(answer.value).failed);
  }

  // A read lands in the buffer it is given, of the game's state, and no further: a buffer shorter than the file takes its own
  // bytes, and one that is not the game's state, where its writes would not count, is refused whole, as is a transfer area
  // there or one too short for a search. The game reads and searches into its own members only.
  TEST_METHOD(BuffersAreTheGameStates)
  {
    DosRig twin("DosFilesBuffers", START);
    std::string bytes;
    for (char character = 'A'; character < 'A' + 0x10; ++character)
    {
      bytes.push_back(character);
    }
    DosRig::Put(twin.Block(), bytes, false);
    const Elite::DosAnswer created = twin.Create(twin.Name("SPAN.CDR"), 0);
    const Elite::DosAnswer written = twin.Write(created.value, twin.BlockBytes(0x10));
    Assert::IsTrue(!written.failed && written.value == 0x10, L"written from the block");
    Assert::IsFalse(twin.Close(created.value).failed);

    DosRig::Put(twin.Block(), std::string(0x10, '\0'), false);
    Assert::IsFalse(twin.Open(twin.Name(), 0).failed);
    std::array<Elite::Counted<std::uint8_t>, Elite::DosFiles::TRANSFER_AREA_BYTES> outside{};
    const Snapshot before = twin.Memory();
    Assert::ExpectException<std::out_of_range>([&] { (void)twin.Files().Read(5, outside); }, L"a buffer outside the state is refused");
    Assert::IsTrue(twin.Memory() == before, L"and nothing changed");
    const Elite::DosAnswer first = twin.Read(5, twin.Block().first(4));
    Assert::IsTrue(!first.failed && first.value == 4, L"a buffer of four takes four");
    Assert::IsTrue(DosRig::Text(twin.Block(), 0, 0x10) == std::string("ABCD") + std::string(0x0C, '\0'), L"and no more");
    const Elite::DosAnswer rest = twin.Read(5, twin.Block().subspan(4));
    Assert::IsTrue(!rest.failed && rest.value == 0x0C, L"the other twelve: the refused read moved nothing");
    Assert::IsTrue(DosRig::Text(twin.Block(), 0, 0x10) == bytes, L"every byte, once");
    Assert::IsFalse(twin.Close(5).failed);

    Assert::ExpectException<std::out_of_range>([&] { twin.Files().SetTransferArea(outside); }, L"a transfer area outside the state");
    Assert::ExpectException<std::logic_error>([&] { twin.Files().SetTransferArea(twin.Area().first(0x2A)); },
                                              L"a transfer area too short for a search");
  }

  // DOS's clock, which stamps a file: the start moment plus the machine's time, over midnight and the year's end.
  TEST_METHOD(TheClockStampsAsDosDid)
  {
    ScratchDirectory directory("DosFilesClock");
    Elite::DirectoryFileStore store(directory.Path());
    Elite::GameState state;
    Elite::DosDateTime start;
    start.year = 1987;
    start.month = 12;
    start.day = 31;
    start.hour = 23;
    start.minute = 59;
    start.second = 59;
    start.hundredths = 99;
    const Elite::DosFiles files(state, store, start);
    const Elite::DosDateTime now = files.Now(95'456); // two hundredths
    Assert::AreEqual(1988u, std::uint32_t{now.year});
    Assert::AreEqual(1u, std::uint32_t{now.month});
    Assert::AreEqual(1u, std::uint32_t{now.day});
    Assert::AreEqual(0u, std::uint32_t{now.hour} + now.minute + now.second);
    Assert::AreEqual(1u, std::uint32_t{now.hundredths});
    const Elite::FileStamp stamp = Elite::DosFiles::Stamp(now);
    Assert::AreEqual(std::uint32_t{(8u << 9) | (1u << 5) | 1u}, std::uint32_t{stamp.date});
    Assert::AreEqual(0u, std::uint32_t{stamp.time});
  }
};

} // namespace GameLogicTests
