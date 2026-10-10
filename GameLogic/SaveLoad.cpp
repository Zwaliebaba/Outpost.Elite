#include "pch.h"

#include "SaveLoad.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::uint8_t DOS_VECTOR = 0x21;
constexpr std::uint8_t DOS_SET_TRANSFER_AREA = 0x1A;
constexpr std::uint8_t DOS_CREATE = 0x3C;
constexpr std::uint8_t DOS_OPEN = 0x3D;
constexpr std::uint8_t DOS_CLOSE = 0x3E;
constexpr std::uint8_t DOS_READ = 0x3F;
constexpr std::uint8_t DOS_WRITE = 0x40;
constexpr std::uint8_t DOS_DELETE = 0x41;
constexpr std::uint8_t DOS_ATTRIBUTES = 0x43; // AL=0 get, 1 set
constexpr std::uint8_t DOS_FIND_FIRST = 0x4E;
constexpr std::uint8_t DOS_FIND_NEXT = 0x4F;
constexpr std::uint16_t DOS_NO_MORE_FILES = 0x12;

constexpr std::uint16_t FOUND_ATTRIBUTE = 0x15; // in the transfer area, after find-first or find-next
constexpr std::uint16_t FOUND_NAME = 0x1E;
constexpr std::uint8_t MOST_COMMANDER_FILES = 0x28;

[[nodiscard]] std::uint16_t WithLow(std::uint16_t _word, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_word & 0xFF00) | _low);
}

[[nodiscard]] std::uint16_t WithHigh(std::uint16_t _word, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>((_word & 0x00FF) | (_high << 8));
}

// INT 21h with AH=_function; true when DOS reports an error (CF).
[[nodiscard]] bool CallDos(Guest& _guest, std::uint8_t _function)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = WithHigh(regs.ax, _function);
  _guest.Interrupt(DOS_VECTOR);
  return (regs.flags & Machine::FLAG_CARRY) != 0;
}

// The common failure (0x039D): DOS's error in AX, which is no failure when it only ran out of files.
void Fail(Guest& _guest)
{
  if (_guest.Regs().ax != DOS_NO_MORE_FILES)
  {
    _guest.Set(DS.diskError, 1);
  }
}

// A failed read or write (0x0393): the file is closed, keeping the error.
void CloseAndFail(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t error = regs.ax;
  regs.bx = _guest.Get(DS.fileHandle);
  static_cast<void>(CallDos(_guest, DOS_CLOSE));
  regs.ax = error;
  Fail(_guest);
}

// LoadCommanderFile (0x031A): only a file with no attributes, read whole into commanderBlock.
void LoadCommanderFile(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = DS.commanderFileName.offset;
  regs.ax = WithLow(regs.ax, 0);
  if (CallDos(_guest, DOS_ATTRIBUTES))
  {
    Fail(_guest);
    return;
  }
  if (regs.cx != 0)
  {
    _guest.Set(DS.diskError, 1);
    return;
  }
  regs.dx = DS.commanderFileName.offset;
  regs.ax = WithLow(regs.ax, 0);
  if (CallDos(_guest, DOS_OPEN))
  {
    Fail(_guest);
    return;
  }
  _guest.Set(DS.fileHandle, regs.ax);
  regs.bx = _guest.Get(DS.fileHandle);
  regs.cx = _guest.Get(DS.commanderFileBytes);
  regs.dx = DS.commanderBlock.offset;
  if (CallDos(_guest, DOS_READ))
  {
    CloseAndFail(_guest);
    return;
  }
  regs.bx = _guest.Get(DS.fileHandle);
  if (CallDos(_guest, DOS_CLOSE))
  {
    Fail(_guest);
  }
}

// SaveCommanderFile (0x0358): commanderBlock written to a new file, whose attributes are then cleared.
void SaveCommanderFile(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = DS.commanderFileName.offset;
  regs.cx = 0;
  if (CallDos(_guest, DOS_CREATE))
  {
    Fail(_guest);
    return;
  }
  _guest.Set(DS.fileHandle, regs.ax);
  regs.bx = _guest.Get(DS.fileHandle);
  regs.cx = _guest.Get(DS.commanderFileBytes);
  regs.dx = DS.commanderBlock.offset;
  if (CallDos(_guest, DOS_WRITE))
  {
    CloseAndFail(_guest);
    return;
  }
  regs.bx = _guest.Get(DS.fileHandle);
  if (CallDos(_guest, DOS_CLOSE))
  {
    Fail(_guest);
    return;
  }
  regs.dx = DS.commanderFileName.offset;
  regs.cx = 0;
  regs.ax = WithLow(regs.ax, 1);
  if (CallDos(_guest, DOS_ATTRIBUTES))
  {
    Fail(_guest);
  }
}

// ListCommanderFiles (0x03A8): the names of up to 40 *.cdr files with no attributes, without their extension, into
// commanderFileList. CX, the attributes searched for, is the caller's.
void ListCommanderFiles(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.commanderFileCount, 0);
  _guest.Set(DS.commanderFileListNext, DS.commanderFileList.offset);
  regs.dx = DS.commanderFilePattern.offset;
  if (CallDos(_guest, DOS_FIND_FIRST))
  {
    Fail(_guest);
    return;
  }
  for (;;)
  {
    regs.di = DS.diskTransferArea.At(FOUND_ATTRIBUTE);
    if (_guest.Byte(regs.di) == 0)
    {
      regs.di = DS.diskTransferArea.At(FOUND_NAME);
      regs.si = _guest.Get(DS.commanderFileListNext);
      for (;;)
      {
        regs.ax = WithLow(regs.ax, _guest.Byte(regs.di));
        ++regs.di;
        if (static_cast<std::uint8_t>(regs.ax) == '.')
        {
          break;
        }
        _guest.SetByte(regs.si, static_cast<std::uint8_t>(regs.ax));
        ++regs.si;
      }
      _guest.SetByte(regs.si, 0);
      ++regs.si;
      _guest.Set(DS.commanderFileListNext, regs.si);
      const auto count = static_cast<std::uint8_t>(_guest.Get(DS.commanderFileCount) + 1);
      _guest.Set(DS.commanderFileCount, count);
      if (count == MOST_COMMANDER_FILES)
      {
        return;
      }
    }
    if (CallDos(_guest, DOS_FIND_NEXT))
    {
      Fail(_guest);
      return;
    }
  }
}

// DeleteCommanderFile (0x03F2).
void DeleteCommanderFile(Guest& _guest)
{
  _guest.Regs().dx = DS.commanderFileName.offset;
  if (CallDos(_guest, DOS_DELETE))
  {
    Fail(_guest);
  }
}

} // namespace

void PerformDiskRequest(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t request = regs.ax;
  regs.dx = DS.diskTransferArea.offset;
  static_cast<void>(CallDos(_guest, DOS_SET_TRANSFER_AREA));
  regs.ax = request;
  // DEC AL until it reaches 0: 1 load, 2 save, 3 delete.
  for (std::uint8_t step = 1; step <= 3; ++step)
  {
    regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(static_cast<std::uint8_t>(regs.ax) - 1));
    if (static_cast<std::uint8_t>(regs.ax) == 0)
    {
      if (step == 1)
      {
        LoadCommanderFile(_guest);
      }
      else if (step == 2)
      {
        SaveCommanderFile(_guest);
      }
      else
      {
        DeleteCommanderFile(_guest);
      }
      return;
    }
  }
  ListCommanderFiles(_guest);
}

void SaveStartupCommander(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = regs.ds;
  regs.es = regs.ds;
  regs.si = DS.commanderBlock.offset;
  regs.di = DS.startupCommander.offset;
  regs.cx = _guest.Get(DS.commanderFileBytes);
  // REP MOVSB, forwards or, with DF set, backwards.
  const auto step = static_cast<std::uint16_t>((regs.flags & Machine::FLAG_DIRECTION) != 0 ? 0xFFFF : 1);
  for (; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarByte(regs.es, regs.di, _guest.FarByte(regs.ds, regs.si));
    regs.si = static_cast<std::uint16_t>(regs.si + step);
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
}

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract DISK_REQUEST{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI, 0};
constexpr Machine::NativeContract COPY{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI | REGISTER_ES, 0};

constexpr std::array ENTRIES = {
  NativeEntry{0x02FF, "PerformDiskRequest", &PerformDiskRequest, DISK_REQUEST},
  NativeEntry{0x4660, "SaveStartupCommander", &SaveStartupCommander, COPY},
};

} // namespace

std::span<const NativeEntry> SaveLoadEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
