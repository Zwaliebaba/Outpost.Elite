#include "pch.h"

#include "DataOverlay.h"
#include "DirectoryFileStore.h"
#include "DosFiles.h"
#include "Platform.h"
#include "ReferenceRig.h"
#include "Registers.h"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint8_t DOS_VECTOR = 0x21;

// The game's own places in the data segment: where it points the transfer area, keeps a file's name and a commander's bytes.
constexpr std::uint16_t AREA = Elite::DS.diskTransferArea.offset;
constexpr std::uint16_t NAME = Elite::DS.commanderFileName.offset;
constexpr std::uint16_t BLOCK = Elite::DS.commanderBlock.offset;
constexpr std::uint16_t BLOCK_BYTES = 208;
// A second transfer area, for a search that is not the game's, and a place for a name of 130 bytes with no NUL in reach.
constexpr std::uint16_t OTHER_AREA = Elite::DS.commanderFileList.offset;
constexpr std::uint16_t LONG_NAME = Elite::DS.commanderBlock.offset;
constexpr std::size_t LONG_NAME_BYTES = 130;

// What a call is made with on the emulated DOS where the service does not read the register, so that what DOS leaves there
// shows: AL, and CX.
constexpr std::uint8_t UNREAD_AL = 0x5A;
constexpr std::uint16_t UNREAD_CX = 0xC5C5;

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

constexpr std::uint8_t READ_ONLY = Machine::FileStore::ATTRIBUTE_READ_ONLY;
constexpr std::uint8_t HIDDEN = Machine::FileStore::ATTRIBUTE_HIDDEN;
constexpr std::uint8_t SYSTEM = Machine::FileStore::ATTRIBUTE_SYSTEM;
constexpr std::uint8_t ARCHIVE = Machine::FileStore::ATTRIBUTE_ARCHIVE;

// A start moment with every field of a stamp set: 12:34:56 on 1 October 1987.
constexpr Machine::Dos::DateTime START = {1987, 10, 1, 12, 34, 56, 0};
constexpr std::uint32_t START_DATE = 0x0F41; // (1987 - 1980) << 9 | 10 << 5 | 1
constexpr std::uint32_t START_TIME = 0x645C; // 12 << 11 | 34 << 5 | 56 / 2
// Two seconds of the machine's clock, the 8088's 4.77 MHz, and the time a stamp then shows: 12:34:58.
constexpr Machine::Cycles TWO_SECONDS = 9'545'454;
constexpr std::uint32_t TWO_SECONDS_LATER_TIME = 0x645D;

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return std::wstring(_text.begin(), _text.end());
}

// What a service answered: CF, AX and CX, and the bytes of memory it changed.
struct Answer
{
  bool failed = false;
  std::uint16_t ax = 0;
  std::uint16_t cx = 0;
  std::uint64_t changes = 0;
};

// How a service's AX is defined: always, or, on success, left as it went in by the emulated DOS and 0 natively.
enum class Ax : std::uint8_t
{
  Defined,
  LeftOnSuccess
};

/// The same calls on the emulated DOS, through int 21h on a Pc, and on the native one, DosFiles on the platform of a second
/// Game: each with its own memory and its own store, both loaded alike from the reference. Every call is checked to answer
/// alike, to change the same bytes of memory as many times as each other, and to leave all of both memories equal; and the
/// stores hold the same files when CheckStores is asked. Each returns the native answer, for what a test states outright.
class SideBySide
{
public:
  SideBySide(std::string_view _name, const Machine::Dos::DateTime& _start)
    : m_emulated(std::string(_name) + "Emulated", _start),
      m_native(std::string(_name) + "Native", _start),
      m_name(_name)
  {
    Assert::IsTrue(Elite::DataSegment(m_emulated.Program()) == Elite::DataSegment(m_native.Program()), L"one layout");
    Assert::IsTrue(m_emulated.Program().pspSegment == m_native.Program().pspSegment, L"one PSP");
    Assert::IsTrue(SameMemory(), L"the two machines start alike");
  }

  [[nodiscard]] std::uint16_t Data() const noexcept
  {
    return Elite::DataSegment(m_native.Program());
  }

  [[nodiscard]] std::uint16_t Psp() const noexcept
  {
    return m_native.Program().pspSegment;
  }

  /// _bytes at _segment:_offset in both memories, and a NUL after them when _terminate.
  void Put(std::uint16_t _segment, std::uint16_t _offset, std::string_view _bytes, bool _terminate = true)
  {
    for (ReferenceRig* const rig : {&m_emulated, &m_native})
    {
      for (std::size_t index = 0; index < _bytes.size(); ++index)
      {
        rig->State().SetFarByte(_segment, static_cast<std::uint16_t>(_offset + index), static_cast<std::uint8_t>(_bytes[index]));
      }
      if (_terminate)
      {
        rig->State().SetFarByte(_segment, static_cast<std::uint16_t>(_offset + _bytes.size()), 0);
      }
    }
  }

  /// _name at the game's NAME, in the data segment; returns where.
  std::uint16_t Name(std::string_view _name)
  {
    Put(Data(), NAME, _name);
    return NAME;
  }

  /// Both clocks moved on to _clock, from device event to device event, as a wait moves them.
  void AdvanceClocks(Machine::Cycles _clock)
  {
    for (ReferenceRig* const rig : {&m_emulated, &m_native})
    {
      while (rig->Host().Clock() < _clock)
      {
        rig->Host().Idle(_clock);
      }
    }
    Assert::IsTrue(m_emulated.Host().Clock() == _clock && m_native.Host().Clock() == _clock, L"both clocks where they were sent");
    Assert::IsTrue(SameMemory(), L"moving the clocks changes no memory");
  }

  /// A directory in both stores' directories, which a store does not show.
  void MakeDirectory(std::string_view _name)
  {
    for (ReferenceRig* const rig : {&m_emulated, &m_native})
    {
      std::filesystem::create_directories(rig->Files() / _name);
    }
  }

  void SetArea(std::uint16_t _segment, std::uint16_t _offset)
  {
    const std::uint64_t changes = NativeChanges();
    m_native.NativePlatform().Files().SetTransferArea(_segment, _offset);
    const Answer native{false, 0, 0, NativeChanges() - changes};
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = Join(0x1A, UNREAD_AL);
        _regs.ds = _segment;
        _regs.dx = _offset;
      });
    Check(L"set the transfer area", emulated, native, false);
  }

  Elite::DosFileAttributes ReadAttributes(std::uint16_t _name)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosFileAttributes native = m_native.NativePlatform().Files().ReadAttributes(Data(), _name);
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = 0x4300;
        _regs.cx = UNREAD_CX;
        _regs.ds = Data();
        _regs.dx = _name;
      });
    Check(L"read attributes", emulated, Answer{native.answer.failed, native.answer.value, 0, NativeChanges() - changes}, false);
    if (!native.answer.failed)
    {
      Assert::AreEqual(std::uint32_t{emulated.cx}, std::uint32_t{native.attributes}, L"read attributes: CX");
      Assert::AreEqual(std::uint32_t{emulated.ax}, std::uint32_t{emulated.cx}, L"read attributes: AX = CX");
    }
    else
    {
      Assert::AreEqual(std::uint32_t{UNREAD_CX}, std::uint32_t{emulated.cx}, L"read attributes: DOS leaves CX on a failure");
      Assert::AreEqual(0u, std::uint32_t{native.attributes}, L"read attributes: 0 natively on a failure");
    }
    return native;
  }

  Elite::DosAnswer SetAttributes(std::uint16_t _name, std::uint16_t _attributes)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().SetAttributes(Data(), _name, _attributes);
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = 0x4301;
        _regs.cx = _attributes;
        _regs.ds = Data();
        _regs.dx = _name;
      });
    Check(L"set attributes", emulated, Native(native, changes), true, Ax::LeftOnSuccess, 0x4301);
    return native;
  }

  Elite::DosAnswer Create(std::uint16_t _name, std::uint16_t _attributes)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().Create(Data(), _name, _attributes, m_native.Host().Clock());
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = Join(0x3C, UNREAD_AL);
        _regs.cx = _attributes;
        _regs.ds = Data();
        _regs.dx = _name;
      });
    Check(L"create", emulated, Native(native, changes), true);
    return native;
  }

  Elite::DosAnswer Open(std::uint16_t _name, std::uint8_t _access)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().Open(Data(), _name, _access);
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = Join(0x3D, _access);
        _regs.cx = UNREAD_CX;
        _regs.ds = Data();
        _regs.dx = _name;
      });
    Check(L"open", emulated, Native(native, changes), true);
    return native;
  }

  Elite::DosAnswer Read(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().Read(_handle, _segment, _buffer, _bytes);
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = Join(0x3F, UNREAD_AL);
        _regs.bx = _handle;
        _regs.cx = _bytes;
        _regs.ds = _segment;
        _regs.dx = _buffer;
      });
    Check(L"read", emulated, Native(native, changes), true);
    return native;
  }

  Elite::DosAnswer Write(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().Write(_handle, _segment, _buffer, _bytes, m_native.Host().Clock());
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = Join(0x40, UNREAD_AL);
        _regs.bx = _handle;
        _regs.cx = _bytes;
        _regs.ds = _segment;
        _regs.dx = _buffer;
      });
    Check(L"write", emulated, Native(native, changes), true);
    return native;
  }

  Elite::DosAnswer Close(std::uint16_t _handle)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().Close(_handle);
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = Join(0x3E, UNREAD_AL);
        _regs.bx = _handle;
      });
    Check(L"close", emulated, Native(native, changes), true, Ax::LeftOnSuccess, Join(0x3E, UNREAD_AL));
    return native;
  }

  Elite::DosAnswer Delete(std::uint16_t _name)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().Delete(Data(), _name);
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = Join(0x41, UNREAD_AL);
        _regs.ds = Data();
        _regs.dx = _name;
      });
    Check(L"delete", emulated, Native(native, changes), true, Ax::LeftOnSuccess, Join(0x41, UNREAD_AL));
    return native;
  }

  Elite::DosAnswer FindFirst(std::uint16_t _pattern, std::uint16_t _attributes)
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().FindFirst(Data(), _pattern, _attributes);
    const Answer emulated = Emulated(
      [&](Machine::Registers& _regs)
      {
        _regs.ax = Join(0x4E, UNREAD_AL);
        _regs.cx = _attributes;
        _regs.ds = Data();
        _regs.dx = _pattern;
      });
    Check(L"find first", emulated, Native(native, changes), true);
    return native;
  }

  Elite::DosAnswer FindNext()
  {
    const std::uint64_t changes = NativeChanges();
    const Elite::DosAnswer native = m_native.NativePlatform().Files().FindNext();
    const Answer emulated = Emulated([&](Machine::Registers& _regs) { _regs.ax = Join(0x4F, UNREAD_AL); });
    Check(L"find next", emulated, Native(native, changes), true);
    return native;
  }

  /// AH=_function on the standard handle or with the byte count the emulated DOS refuses as a fault: it stops the run there
  /// and changes nothing, and the native service throws std::logic_error and changes nothing.
  void Refused(std::uint8_t _function, std::uint16_t _handle, std::uint16_t _bytes)
  {
    const std::uint64_t changes = NativeChanges();
    Elite::DosFiles& files = m_native.NativePlatform().Files();
    Assert::ExpectException<std::logic_error>(
      [&]
      {
        const std::uint16_t data = Data();
        if (_function == 0x3E)
        {
          (void)files.Close(_handle);
        }
        else if (_function == 0x3F)
        {
          (void)files.Read(_handle, data, BLOCK, _bytes);
        }
        else
        {
          (void)files.Write(_handle, data, BLOCK, _bytes, 0);
        }
      },
      L"the native service refuses it");
    Assert::IsTrue(NativeChanges() == changes, L"the native service changes nothing");

    Machine::Pc& pc = m_emulated.Host();
    const std::uint64_t before = pc.Ram().ChangeCount();
    Machine::Registers regs = pc.Processor().Regs();
    regs.ax = Join(_function, UNREAD_AL);
    regs.bx = _handle;
    regs.cx = _bytes;
    regs.ds = Data();
    regs.dx = BLOCK;
    const Machine::Registers sent = regs;
    Assert::IsTrue(pc.Services().ServiceInterrupt(regs, DOS_VECTOR), L"DOS takes the call");
    const std::optional<Machine::ServiceFault>& fault = pc.Services().Fault();
    Assert::IsTrue(fault.has_value() && fault->kind == Machine::FaultKind::UnsupportedRequest, L"the emulated DOS refuses it as a fault");
    Assert::IsTrue(regs == sent && pc.Ram().ChangeCount() == before, L"the emulated DOS changes nothing");
    pc.Services().ClearFault();
    Assert::IsTrue(SameMemory(), L"after a refusal, the memories are alike");
  }

  /// Both stores hold the same files: names, attributes, stamps, sizes and bytes.
  void CheckStores()
  {
    const std::vector<Machine::FileEntry> emulated = m_emulated.Store().List();
    const std::vector<Machine::FileEntry> native = m_native.Store().List();
    Assert::AreEqual(static_cast<std::uint32_t>(emulated.size()), static_cast<std::uint32_t>(native.size()), L"files in the stores");
    for (std::size_t index = 0; index < native.size(); ++index)
    {
      const Machine::FileEntry& left = emulated[index];
      const Machine::FileEntry& right = native[index];
      const std::wstring what = Widen(right.name);
      Assert::IsTrue(left.name == right.name, (L"a file's name: " + what).c_str());
      Assert::AreEqual(std::uint32_t{left.attribute}, std::uint32_t{right.attribute}, (what + L": attribute").c_str());
      Assert::AreEqual(std::uint32_t{left.stamp.date}, std::uint32_t{right.stamp.date}, (what + L": date").c_str());
      Assert::AreEqual(std::uint32_t{left.stamp.time}, std::uint32_t{right.stamp.time}, (what + L": time").c_str());
      Assert::AreEqual(left.sizeBytes, right.sizeBytes, (what + L": size").c_str());
      Assert::IsTrue(Elite::ReadWholeFile(m_emulated.Files() / left.name) == Elite::ReadWholeFile(m_native.Files() / right.name),
                     (what + L": bytes").c_str());
    }
  }

  /// The native store's files.
  [[nodiscard]] std::vector<Machine::FileEntry> NativeFiles()
  {
    return m_native.Store().List();
  }

  /// The native memory's byte and word at _segment:_offset.
  [[nodiscard]] std::uint32_t Byte(std::uint16_t _segment, std::uint16_t _offset) const
  {
    return m_native.State().FarByte(_segment, _offset);
  }

  [[nodiscard]] std::uint32_t Word(std::uint16_t _segment, std::uint16_t _offset) const
  {
    return m_native.State().FarWord(_segment, _offset);
  }

  /// The native memory's _bytes at _segment:_offset, as text.
  [[nodiscard]] std::string Text(std::uint16_t _segment, std::uint16_t _offset, std::size_t _bytes) const
  {
    std::string text;
    for (std::size_t index = 0; index < _bytes; ++index)
    {
      text.push_back(static_cast<char>(Byte(_segment, static_cast<std::uint16_t>(_offset + index))));
    }
    return text;
  }

  /// The bytes the last native call changed.
  [[nodiscard]] std::uint64_t LastChanges() const noexcept
  {
    return m_lastChanges;
  }

private:
  [[nodiscard]] static std::uint16_t Join(std::uint8_t _high, std::uint8_t _low) noexcept
  {
    return static_cast<std::uint16_t>((_high << 8) | _low);
  }

  [[nodiscard]] std::uint64_t NativeChanges() const noexcept
  {
    return m_native.Host().Ram().ChangeCount();
  }

  [[nodiscard]] Answer Native(const Elite::DosAnswer& _answer, std::uint64_t _changesBefore) const noexcept
  {
    return Answer{_answer.failed, _answer.value, 0, NativeChanges() - _changesBefore};
  }

  // Int 21h on the emulated DOS as Hardware made it before D27: the processor's registers with what _set sets, the call as INT
  // makes it (Pc::CallInterrupt), and the registers DOS left; then the processor's registers put back.
  template <typename Set> Answer Emulated(Set _set)
  {
    Machine::Pc& pc = m_emulated.Host();
    Machine::Registers& regs = pc.Processor().Regs();
    const Machine::Registers kept = regs;
    _set(regs);
    const std::uint64_t changes = pc.Ram().ChangeCount();
    pc.CallInterrupt(DOS_VECTOR);
    const Answer answer{(regs.flags & Machine::FLAG_CARRY) != 0, regs.ax, regs.cx, pc.Ram().ChangeCount() - changes};
    regs = kept;
    Assert::IsFalse(pc.Services().Fault().has_value(), L"the emulated DOS took the call");
    return answer;
  }

  // The two answers to one call agree as _ax says, and they changed the same bytes, leaving the memories alike.
  void Check(const wchar_t* _call, const Answer& _emulated, const Answer& _native, bool _answers, Ax _ax = Ax::Defined,
             std::uint16_t _axSent = 0)
  {
    const std::wstring call = Widen(m_name) + L", call " + std::to_wstring(++m_calls) + L" (" + _call + L")";
    if (_answers)
    {
      Assert::IsTrue(_emulated.failed == _native.failed, (call + L": CF").c_str());
      if (_ax == Ax::Defined || _native.failed)
      {
        Assert::AreEqual(std::uint32_t{_emulated.ax}, std::uint32_t{_native.ax}, (call + L": AX").c_str());
      }
      else
      {
        Assert::AreEqual(std::uint32_t{_axSent}, std::uint32_t{_emulated.ax}, (call + L": DOS leaves AX as it went in").c_str());
        Assert::AreEqual(0u, std::uint32_t{_native.ax}, (call + L": 0 natively").c_str());
      }
    }
    Assert::AreEqual(_emulated.changes, _native.changes, (call + L": the bytes of memory changed").c_str());
    Assert::IsTrue(SameMemory(), (call + L": the memories are alike").c_str());
    m_lastChanges = _native.changes;
  }

  // Every byte of both machines' 1 MiB alike: Memory's own bytes, and the GameState's three regions, which Memory maps.
  [[nodiscard]] bool SameMemory() const
  {
    const Elite::GameState& left = m_emulated.State();
    const Elite::GameState& right = m_native.State();
    if (!std::ranges::equal(m_emulated.Host().Ram().Bytes(), m_native.Host().Ram().Bytes()) ||
        !std::ranges::equal(left.DataBytes(), right.DataBytes()) || !std::ranges::equal(left.VideoBytes(), right.VideoBytes()))
    {
      return false;
    }
    for (std::uint16_t offset = 0; offset < Elite::CODE_SEGMENT_BYTES; ++offset)
    {
      if (left.CodeByte(offset) != right.CodeByte(offset))
      {
        return false;
      }
    }
    return true;
  }

  ReferenceRig m_emulated;
  ReferenceRig m_native;
  std::string m_name;
  std::size_t m_calls = 0;
  std::uint64_t m_lastChanges = 0;
};

// A file made through both DOSes: created, _bytes written from the block, closed, and its attributes set.
void MakeFile(SideBySide& _twin, std::string_view _name, std::uint16_t _bytes, std::uint8_t _attributes = 0)
{
  const Elite::DosAnswer created = _twin.Create(_twin.Name(_name), 0);
  Assert::IsFalse(created.failed, L"made: created");
  Assert::IsFalse(_twin.Write(created.value, _twin.Data(), BLOCK, _bytes).failed, L"made: written");
  Assert::IsFalse(_twin.Close(created.value).failed, L"made: closed");
  Assert::IsFalse(_twin.SetAttributes(_twin.Name(_name), _attributes).failed, L"made: its attributes set");
}

// A name of 130 'N's at LONG_NAME, with no NUL in DOS's 128 bytes.
void Put130(SideBySide& _twin)
{
  _twin.Put(_twin.Data(), LONG_NAME, std::string(LONG_NAME_BYTES, 'N'), false);
}

// The transfer area at DS:_area holds the search for _template with _attribute, at entry _next.
void CheckSearch(const SideBySide& _twin, std::uint16_t _area, std::string_view _template, std::uint32_t _attribute, std::uint32_t _next)
{
  const std::uint16_t data = _twin.Data();
  Assert::AreEqual(3u, _twin.Byte(data, _area), L"the drive, C:");
  Assert::IsTrue(_twin.Text(data, static_cast<std::uint16_t>(_area + AREA_TEMPLATE), 11) == _template, L"the search's template");
  Assert::AreEqual(_attribute, _twin.Byte(data, static_cast<std::uint16_t>(_area + AREA_SEARCH_ATTRIBUTE)), L"the search's attribute");
  Assert::AreEqual(_next, _twin.Word(data, static_cast<std::uint16_t>(_area + AREA_NEXT_ENTRY)), L"the next entry");
  for (std::uint16_t offset = AREA_NEXT_ENTRY + 2; offset < AREA_ATTRIBUTE; ++offset)
  {
    Assert::AreEqual(0u, _twin.Byte(data, static_cast<std::uint16_t>(_area + offset)), L"the reserved bytes, zero");
  }
}

// The transfer area at DS:_area holds the file found: _attribute, _time, _date, _sizeBytes and _name, zero-padded to 13 bytes.
void CheckFound(const SideBySide& _twin, std::uint16_t _area, std::uint32_t _attribute, std::uint32_t _time, std::uint32_t _date,
                std::uint32_t _sizeBytes, std::string_view _name)
{
  const std::uint16_t data = _twin.Data();
  const auto at = [_area](std::uint16_t _offset) { return static_cast<std::uint16_t>(_area + _offset); };
  Assert::AreEqual(_attribute, _twin.Byte(data, at(AREA_ATTRIBUTE)), L"the file's attribute");
  Assert::AreEqual(_time, _twin.Word(data, at(AREA_TIME)), L"the file's time");
  Assert::AreEqual(_date, _twin.Word(data, at(AREA_DATE)), L"the file's date");
  Assert::AreEqual(_sizeBytes, _twin.Word(data, at(AREA_SIZE)) | (_twin.Word(data, at(AREA_SIZE + 2)) << 16), L"the file's size");
  std::string name(_name);
  name.resize(AREA_NAME_BYTES, '\0');
  Assert::IsTrue(_twin.Text(data, at(AREA_NAME), AREA_NAME_BYTES) == name, (L"the file's name, zero-padded: " + Widen(_name)).c_str());
}

} // namespace

// DOS's file services natively (DosFiles, D27) beside the emulated DOS they replace (Machine::Dos), call for call: the same
// answers, the same bytes of memory changed, the same files. Each also states what it expects outright, so that it outlives
// the emulated DOS.
TEST_CLASS(DosFilesTests)
{
public:
  // SaveCommanderFile then LoadCommanderFile, call for call, at the game's own places.
  TEST_METHOD(CreateWriteCloseOpenReadClose)
  {
    SideBySide twin("DosFilesSaveLoad", START);
    const std::uint16_t data = twin.Data();
    std::string commander;
    for (std::uint16_t index = 0; index < BLOCK_BYTES; ++index)
    {
      commander.push_back(static_cast<char>(1 + index % 251)); // no zero, so that a read into zeros changes every byte
    }
    twin.Put(data, BLOCK, commander, false);
    twin.SetArea(data, AREA);

    // A file made at the start moment, and then the commander's two seconds later.
    const Elite::DosAnswer empty = twin.Create(twin.Name("EMPTY.CDR"), 0);
    Assert::IsTrue(!empty.failed && empty.value == 5, L"handles start at 5");
    Assert::IsFalse(twin.Close(5).failed);
    twin.AdvanceClocks(TWO_SECONDS);
    const Elite::DosAnswer created = twin.Create(twin.Name("JAMESON.CDR"), 0);
    Assert::IsTrue(!created.failed && created.value == 5, L"handle 5, free again");
    const Elite::DosAnswer written = twin.Write(5, data, BLOCK, BLOCK_BYTES);
    Assert::IsTrue(!written.failed && written.value == BLOCK_BYTES, L"every byte written");
    Assert::IsFalse(twin.Close(5).failed);
    const Elite::DosFileAttributes archived = twin.ReadAttributes(NAME);
    Assert::IsTrue(!archived.answer.failed && archived.attributes == ARCHIVE && archived.answer.value == ARCHIVE,
                   L"a new file has the archive bit, in CX and AX");
    Assert::IsFalse(twin.SetAttributes(NAME, 0).failed, L"which the game clears");
    Assert::AreEqual(0u, std::uint32_t{twin.ReadAttributes(NAME).attributes});
    twin.CheckStores();

    // Loaded over the commander it was saved from, as the game loads one over another: a byte that is already what the file
    // holds is no change, so nothing changes, and every byte is written once.
    Assert::IsFalse(twin.Open(NAME, 0).failed);
    Assert::AreEqual(std::uint32_t{BLOCK_BYTES}, std::uint32_t{twin.Read(5, data, BLOCK, BLOCK_BYTES).value}, L"read over itself");
    Assert::AreEqual(std::uint64_t{0}, twin.LastChanges(), L"a byte read over the same byte is no change");
    Assert::IsFalse(twin.Close(5).failed);

    // Loaded back over zeros.
    twin.Put(data, BLOCK, std::string(BLOCK_BYTES, '\0'), false);
    const Elite::DosAnswer opened = twin.Open(NAME, 0);
    Assert::IsTrue(!opened.failed && opened.value == 5, L"opened as handle 5");
    const Elite::DosAnswer read = twin.Read(5, data, BLOCK, BLOCK_BYTES);
    Assert::IsTrue(!read.failed && read.value == BLOCK_BYTES, L"every byte read");
    Assert::AreEqual(std::uint64_t{BLOCK_BYTES}, twin.LastChanges(), L"each byte of the block changed once");
    Assert::IsTrue(twin.Text(data, BLOCK, BLOCK_BYTES) == commander, L"the commander's bytes, back in the block");
    const Elite::DosAnswer end = twin.Read(5, data, BLOCK, BLOCK_BYTES);
    Assert::IsTrue(!end.failed && end.value == 0, L"0 at the end of the file");
    Assert::AreEqual(std::uint64_t{0}, twin.LastChanges(), L"and nothing changed");
    Assert::IsFalse(twin.Close(5).failed);

    const std::vector<Machine::FileEntry> files = twin.NativeFiles();
    Assert::AreEqual(2u, static_cast<std::uint32_t>(files.size()));
    Assert::IsTrue(files[0].name == "EMPTY.CDR" && files[0].sizeBytes == 0 && files[0].attribute == ARCHIVE, L"EMPTY.CDR");
    Assert::AreEqual(START_DATE, std::uint32_t{files[0].stamp.date}, L"stamped on the start's date");
    Assert::AreEqual(START_TIME, std::uint32_t{files[0].stamp.time}, L"at the start's time");
    Assert::IsTrue(files[1].name == "JAMESON.CDR" && files[1].sizeBytes == BLOCK_BYTES && files[1].attribute == 0, L"JAMESON.CDR");
    Assert::AreEqual(START_DATE, std::uint32_t{files[1].stamp.date});
    Assert::AreEqual(TWO_SECONDS_LATER_TIME, std::uint32_t{files[1].stamp.time}, L"two seconds later");
    twin.CheckStores();
  }

  // ListCommanderFiles' searches, and others: find-first and find-next over several files and attributes, to the end.
  TEST_METHOD(FindFirstAndNextToTheEnd)
  {
    SideBySide twin("DosFilesFind", START);
    const std::uint16_t data = twin.Data();
    twin.Put(data, BLOCK, "0123456789", false);
    twin.SetArea(data, AREA);
    MakeFile(twin, "ZED.CDR", 4);
    MakeFile(twin, "A.CDR", 3);
    MakeFile(twin, "B.CDR", 1, HIDDEN);
    MakeFile(twin, "C.CDR", 2, SYSTEM);
    MakeFile(twin, "ELITE01.HI", 5);
    MakeFile(twin, "READ.CDR", 6, READ_ONLY);
    twin.MakeDirectory("SUB.CDR");
    twin.CheckStores();
    // In the store's order: A.CDR, B.CDR, C.CDR, ELITE01.HI, READ.CDR, ZED.CDR; no directory.

    // The game's pattern, with no hidden or system file wanted: A, READ and ZED, then no more, and no more again.
    twin.Put(data, NAME, "*.cdr");
    Elite::DosAnswer found = twin.FindFirst(NAME, 0);
    Assert::IsTrue(!found.failed && found.value == 0, L"A.CDR found");
    CheckSearch(twin, AREA, "????????CDR", 0, 1);
    CheckFound(twin, AREA, 0, START_TIME, START_DATE, 3, "A.CDR");
    found = twin.FindNext();
    Assert::IsTrue(!found.failed && found.value == 0, L"READ.CDR found");
    CheckSearch(twin, AREA, "????????CDR", 0, 5);
    CheckFound(twin, AREA, READ_ONLY, START_TIME, START_DATE, 6, "READ.CDR");
    found = twin.FindNext();
    CheckFound(twin, AREA, 0, START_TIME, START_DATE, 4, "ZED.CDR");
    found = twin.FindNext();
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"no more files");
    CheckSearch(twin, AREA, "????????CDR", 0, 6);
    CheckFound(twin, AREA, 0, START_TIME, START_DATE, 4, "ZED.CDR"); // the last found is left
    found = twin.FindNext();
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"and no more again");

    // The game's search attribute as Start leaves CX, which lets hidden and system files in: all five.
    found = twin.FindFirst(NAME, 0xFFE7);
    CheckSearch(twin, AREA, "????????CDR", 0xE7, 1);
    const std::array<std::string_view, 4> rest = {"B.CDR", "C.CDR", "READ.CDR", "ZED.CDR"};
    for (const std::string_view name : rest)
    {
      found = twin.FindNext();
      Assert::IsTrue(!found.failed && twin.Text(data, AREA + AREA_NAME, name.size()) == name, Widen(name).c_str());
    }
    Assert::IsTrue(twin.FindNext().failed);

    // Other patterns: a match by '?', none at all, and a volume label, which the store has none of.
    found = twin.FindFirst(twin.Name("ELITE??.*"), 0);
    CheckFound(twin, AREA, 0, START_TIME, START_DATE, 5, "ELITE01.HI");
    found = twin.FindFirst(twin.Name("*.XYZ"), 0);
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"nothing matches: no more files");
    CheckSearch(twin, AREA, "????????XYZ", 0, 6);
    found = twin.FindFirst(twin.Name("*.*"), Machine::FileStore::ATTRIBUTE_VOLUME);
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"no volume label");
    CheckSearch(twin, AREA, "???????????", Machine::FileStore::ATTRIBUTE_VOLUME, 0);

    // Patterns DOS refuses, which leave the transfer area alone.
    found = twin.FindFirst(twin.Name("..\\*.CDR"), 0);
    Assert::IsTrue(found.failed && found.value == PATH_NOT_FOUND, L"a path");
    found = twin.FindFirst(twin.Name("A.B.C"), 0);
    Assert::IsTrue(found.failed && found.value == FILE_NOT_FOUND, L"two dots");
    found = twin.FindFirst(twin.Name(""), 0);
    Assert::IsTrue(found.failed && found.value == FILE_NOT_FOUND, L"nothing");
    Put130(twin);
    found = twin.FindFirst(LONG_NAME, 0);
    Assert::IsTrue(found.failed && found.value == PATH_NOT_FOUND, L"no NUL in 128 bytes");

    // A transfer area no search has filled: find-next has nothing to go on.
    twin.SetArea(data, OTHER_AREA);
    found = twin.FindNext();
    Assert::IsTrue(found.failed && found.value == NO_MORE_FILES, L"no search in the transfer area");
    twin.CheckStores();
  }

  // The transfer area before the program sets one: PSP:0080, over the command tail, outside the game's memory.
  TEST_METHOD(FindFirstIntoThePspsTransferArea)
  {
    SideBySide twin("DosFilesPspArea", START);
    twin.Put(twin.Data(), BLOCK, "x", false);
    MakeFile(twin, "ONLY.CDR", 1);
    const Elite::DosAnswer found = twin.FindFirst(twin.Name("*.CDR"), 0);
    Assert::IsFalse(found.failed);
    Assert::AreEqual(3u, twin.Byte(twin.Psp(), Elite::DosFiles::DEFAULT_TRANSFER_AREA), L"the drive, at PSP:0080");
    Assert::IsTrue(twin.Text(twin.Psp(), Elite::DosFiles::DEFAULT_TRANSFER_AREA + AREA_NAME, 9) == std::string("ONLY.CDR\0", 9),
                   L"the name, at PSP:009E");
  }

  // DeleteCommanderFile, and the attributes that guard a file.
  TEST_METHOD(DeleteAndAttributes)
  {
    SideBySide twin("DosFilesDelete", START);
    twin.Put(twin.Data(), BLOCK, "abc", false);
    MakeFile(twin, "LOCKED.CDR", 3, READ_ONLY);
    Assert::AreEqual(std::uint32_t{READ_ONLY}, std::uint32_t{twin.ReadAttributes(twin.Name("LOCKED.CDR")).attributes});
    Elite::DosAnswer answer = twin.Delete(NAME);
    Assert::IsTrue(answer.failed && answer.value == ACCESS_DENIED, L"a read-only file is not deleted");
    answer = twin.Open(NAME, 1);
    Assert::IsTrue(answer.failed && answer.value == ACCESS_DENIED, L"nor opened to write");
    answer = twin.Open(NAME, 0);
    Assert::IsTrue(!answer.failed && answer.value == 5, L"but opened to read");
    Assert::IsFalse(twin.Close(5).failed);
    answer = twin.SetAttributes(NAME, 0x0100);
    Assert::IsTrue(answer.failed && answer.value == ACCESS_DENIED, L"attributes above FFh");
    answer = twin.SetAttributes(NAME, Machine::FileStore::ATTRIBUTE_DIRECTORY);
    Assert::IsTrue(answer.failed && answer.value == ACCESS_DENIED, L"the directory bit");
    Assert::IsFalse(twin.SetAttributes(NAME, 0).failed);
    Assert::IsFalse(twin.Delete(NAME).failed, L"deleted");
    twin.CheckStores();
    Assert::IsTrue(twin.NativeFiles().empty(), L"gone");
    answer = twin.Delete(NAME);
    Assert::IsTrue(answer.failed && answer.value == FILE_NOT_FOUND, L"deleted again: not found");
    const Elite::DosFileAttributes missing = twin.ReadAttributes(NAME);
    Assert::IsTrue(missing.answer.failed && missing.answer.value == FILE_NOT_FOUND, L"no attributes: not found");
    answer = twin.SetAttributes(NAME, 0);
    Assert::IsTrue(answer.failed && answer.value == FILE_NOT_FOUND, L"none to set");
  }

  // The errors: a missing file, a bad access code, a bad handle, a path, a name with no end, too many open handles, and the
  // calls the emulated DOS refused as faults.
  TEST_METHOD(Errors)
  {
    SideBySide twin("DosFilesErrors", START);
    const std::uint16_t data = twin.Data();
    twin.Put(data, BLOCK, "abcdef", false);
    Elite::DosAnswer answer = twin.Open(twin.Name("MISSING.CDR"), 0);
    Assert::IsTrue(answer.failed && answer.value == FILE_NOT_FOUND, L"a missing file");
    MakeFile(twin, "THERE.CDR", 6);
    answer = twin.Open(twin.Name("THERE.CDR"), 3);
    Assert::IsTrue(answer.failed && answer.value == INVALID_ACCESS_CODE, L"access 3");
    answer = twin.Open(NAME, 0x40);
    Assert::IsTrue(!answer.failed && answer.value == 5, L"the sharing bits are ignored");
    Assert::IsFalse(twin.Close(5).failed);

    answer = twin.Close(7);
    Assert::IsTrue(answer.failed && answer.value == INVALID_HANDLE, L"close: a handle not open");
    answer = twin.Read(25, data, BLOCK, 4);
    Assert::IsTrue(answer.failed && answer.value == INVALID_HANDLE, L"read: a handle past the table");
    answer = twin.Write(5, data, BLOCK, 4);
    Assert::IsTrue(answer.failed && answer.value == INVALID_HANDLE, L"write: a closed handle");

    answer = twin.Create(twin.Name("C:X.CDR"), 0);
    Assert::IsTrue(answer.failed && answer.value == PATH_NOT_FOUND, L"a drive");
    answer = twin.Create(twin.Name("TOOLONG12.CD"), 0);
    Assert::IsTrue(answer.failed && answer.value == FILE_NOT_FOUND, L"a base of nine");
    Put130(twin);
    answer = twin.Open(LONG_NAME, 0);
    Assert::IsTrue(answer.failed && answer.value == PATH_NOT_FOUND, L"open: no NUL in 128 bytes");
    answer = twin.Create(LONG_NAME, 0);
    Assert::IsTrue(answer.failed && answer.value == PATH_NOT_FOUND, L"create: no NUL in 128 bytes");
    answer = twin.Delete(LONG_NAME);
    Assert::IsTrue(answer.failed && answer.value == PATH_NOT_FOUND, L"delete: no NUL in 128 bytes");
    Assert::IsTrue(twin.ReadAttributes(LONG_NAME).answer.failed, L"read attributes: no NUL in 128 bytes");
    Assert::IsTrue(twin.SetAttributes(LONG_NAME, 0).failed, L"set attributes: no NUL in 128 bytes");

    // Fifteen handles, 5 to 19, and no more: the game never holds two.
    for (std::uint16_t handle = 5; handle < 20; ++handle)
    {
      answer = twin.Open(twin.Name("THERE.CDR"), 0);
      Assert::IsTrue(!answer.failed && answer.value == handle, L"the next handle");
    }
    answer = twin.Open(NAME, 0);
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
    twin.CheckStores();
  }

  // A read and a write by linear address, as the emulated DOS made them: past the end of the data segment, on into the
  // paragraphs after it rather than round to its start.
  TEST_METHOD(ReadAndWritePastASegmentsEnd)
  {
    SideBySide twin("DosFilesLinear", START);
    const std::uint16_t data = twin.Data();
    std::string bytes;
    for (char character = 'A'; character < 'A' + 0x20; ++character)
    {
      bytes.push_back(character);
    }
    twin.Put(data, 0xFFF0, bytes.substr(0, 0x10), false);
    twin.Put(static_cast<std::uint16_t>(data + 0x1000), 0x0000, bytes.substr(0x10), false);
    const Elite::DosAnswer created = twin.Create(twin.Name("LINEAR.CDR"), 0);
    const Elite::DosAnswer written = twin.Write(created.value, data, 0xFFF0, 0x20);
    Assert::IsTrue(!written.failed && written.value == 0x20, L"written across the segment's end");
    Assert::IsFalse(twin.Close(created.value).failed);
    Assert::IsTrue(twin.NativeFiles().front().sizeBytes == 0x20, L"all of it");

    twin.Put(data, 0xFFF0, std::string(0x10, '\0'), false);
    twin.Put(static_cast<std::uint16_t>(data + 0x1000), 0x0000, std::string(0x10, '\0'), false);
    Assert::IsFalse(twin.Open(NAME, 0).failed);
    const Elite::DosAnswer read = twin.Read(5, data, 0xFFF0, 0x20);
    Assert::IsTrue(!read.failed && read.value == 0x20, L"read across the segment's end");
    Assert::IsTrue(twin.Text(data, 0xFFF0, 0x10) == bytes.substr(0, 0x10), L"the first half, at the segment's end");
    Assert::IsTrue(twin.Text(static_cast<std::uint16_t>(data + 0x1000), 0, 0x10) == bytes.substr(0x10), L"the second, after it");
    Assert::IsFalse(twin.Close(5).failed);
    twin.CheckStores();
  }

  // DOS's clock, which stamps a file: the start moment plus the machine's time, over midnight and the year's end.
  TEST_METHOD(TheClockStampsAsDosDid)
  {
    ScratchDirectory directory("DosFilesClock");
    Machine::DirectoryFileStore store(directory.Path());
    Elite::GameState state;
    Machine::Dos::DateTime start;
    start.year = 1987;
    start.month = 12;
    start.day = 31;
    start.hour = 23;
    start.minute = 59;
    start.second = 59;
    start.hundredths = 99;
    const Elite::DosFiles files(state, store, start);
    const Machine::Dos::DateTime now = files.Now(95'456); // two hundredths
    Assert::AreEqual(1988u, std::uint32_t{now.year});
    Assert::AreEqual(1u, std::uint32_t{now.month});
    Assert::AreEqual(1u, std::uint32_t{now.day});
    Assert::AreEqual(0u, std::uint32_t{now.hour} + now.minute + now.second);
    Assert::AreEqual(1u, std::uint32_t{now.hundredths});
    const Machine::FileStamp stamp = Elite::DosFiles::Stamp(now);
    Assert::AreEqual(std::uint32_t{(8u << 9) | (1u << 5) | 1u}, std::uint32_t{stamp.date});
    Assert::AreEqual(0u, std::uint32_t{stamp.time});

    // The emulated DOS's clock, at the same moment.
    ReferenceRig rig("DosFilesClockEmulated", start);
    while (rig.Host().Clock() < 95'456)
    {
      rig.Host().Idle(95'456);
    }
    const Machine::Dos::DateTime emulated = rig.Host().Services().DosKernel().Now();
    Assert::IsTrue(emulated.year == now.year && emulated.month == now.month && emulated.day == now.day && emulated.hour == now.hour &&
                     emulated.minute == now.minute && emulated.second == now.second && emulated.hundredths == now.hundredths,
                   L"the emulated DOS's clock agrees");
    const Machine::FileStamp emulatedStamp = Machine::Dos::Stamp(emulated);
    Assert::IsTrue(emulatedStamp.date == stamp.date && emulatedStamp.time == stamp.time, L"and so do the stamps");
  }
};

} // namespace GameLogicTests
