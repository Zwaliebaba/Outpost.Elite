#include "pch.h"

#include "Dos.h"

#include "Bios.h"
#include "Memory.h"

#include <algorithm>
#include <chrono>
#include <span>
#include <string_view>
#include <vector>

namespace Machine
{

namespace
{

constexpr std::uint8_t VERSION_MAJOR = 3;
constexpr std::uint8_t VERSION_MINOR = 30;

constexpr std::size_t MAX_NAME_BYTES = 128;
constexpr std::uint32_t MAX_STRING_BYTES = 0x10000;
constexpr std::uint8_t STRING_TERMINATOR = '$';
constexpr std::uint8_t CHARACTER_TAB = 0x09;
constexpr std::uint8_t CHARACTER_BELL = 0x07;
constexpr std::uint8_t TAB_COLUMNS = 8;

constexpr std::uint64_t CENTISECONDS_PER_DAY = 8'640'000;

// The PSP: the terminate, Ctrl-Break and critical-error addresses DOS restores on exit, and the command tail, whose
// bytes are also the initial DTA.
constexpr std::uint16_t PSP_SAVED_VECTORS = 0x0A;
constexpr std::uint8_t FIRST_SAVED_VECTOR = 0x22;
constexpr std::uint8_t SAVED_VECTOR_COUNT = 3;
constexpr std::uint16_t PSP_DEFAULT_DTA = 0x80;

// The DTA as 4Eh and 4Fh fill it.
constexpr std::uint16_t DTA_DRIVE = 0x00;
constexpr std::uint16_t DTA_TEMPLATE = 0x01;
constexpr std::uint16_t DTA_SEARCH_ATTRIBUTE = 0x0C;
constexpr std::uint16_t DTA_NEXT_ENTRY = 0x0D;
constexpr std::uint16_t DTA_ATTRIBUTE = 0x15;
constexpr std::uint16_t DTA_TIME = 0x16;
constexpr std::uint16_t DTA_DATE = 0x18;
constexpr std::uint16_t DTA_SIZE = 0x1A;
constexpr std::uint16_t DTA_NAME = 0x1E;
constexpr std::uint16_t DTA_NAME_BYTES = 13;

constexpr std::size_t TEMPLATE_BYTES = 11;
constexpr std::size_t BASE_CHARACTERS = 8;
constexpr std::size_t EXTENSION_CHARACTERS = 3;
// What a file's hidden, system and directory bits must all be found in the search attribute for it to match.
constexpr std::uint8_t SEARCH_RESTRICTED = FileStore::ATTRIBUTE_HIDDEN | FileStore::ATTRIBUTE_SYSTEM | FileStore::ATTRIBUTE_DIRECTORY;

using Template = std::array<char, TEMPLATE_BYTES>;

constexpr std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

constexpr std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word & 0xFF);
}

constexpr std::uint16_t Word(std::uint8_t _high, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_high << 8) | _low);
}

constexpr char ToUpper(char _character) noexcept
{
  return _character >= 'a' && _character <= 'z' ? static_cast<char>(_character - 'a' + 'A') : _character;
}

void Succeed(Registers& _regs) noexcept
{
  _regs.flags = static_cast<std::uint16_t>(_regs.flags & ~FLAG_CARRY);
}

void Fail(Registers& _regs, DosError _error) noexcept
{
  _regs.flags = static_cast<std::uint16_t>(_regs.flags | FLAG_CARRY);
  _regs.ax = static_cast<std::uint16_t>(_error);
}

// A canonical name, "NAME.EXT", as the 11 blank-padded characters of a directory entry.
Template EntryName(std::string_view _name)
{
  Template name;
  name.fill(' ');
  const std::size_t dot = _name.find('.');
  const std::string_view base = _name.substr(0, std::min(dot, BASE_CHARACTERS));
  std::copy(base.begin(), base.end(), name.begin());
  if (dot != std::string_view::npos)
  {
    const std::string_view extension = _name.substr(dot + 1, EXTENSION_CHARACTERS);
    std::copy(extension.begin(), extension.end(), name.begin() + BASE_CHARACTERS);
  }
  return name;
}

// A search pattern as DOS's template: '*' fills the rest of its field with '?', and what follows a '*' in the same
// field is ignored, as DOS does.
DosError SearchTemplate(std::string_view _pattern, Template& _template)
{
  if (_pattern.find_first_of("\\/:") != std::string_view::npos || _pattern == "." || _pattern == "..")
  {
    return DosError::PathNotFound;
  }
  Template result;
  result.fill(' ');
  std::size_t start = 0;
  std::size_t limit = BASE_CHARACTERS;
  std::size_t position = 0;
  bool starred = false;
  bool inExtension = false;
  for (const char character : _pattern)
  {
    if (character == '.')
    {
      if (inExtension)
      {
        return DosError::FileNotFound;
      }
      inExtension = true;
      start = BASE_CHARACTERS;
      limit = EXTENSION_CHARACTERS;
      position = 0;
      starred = false;
      continue;
    }
    if (starred)
    {
      continue;
    }
    if (character == '*')
    {
      std::fill(result.begin() + static_cast<std::ptrdiff_t>(start + position), result.begin() + static_cast<std::ptrdiff_t>(start + limit),
                '?');
      starred = true;
      continue;
    }
    if ((character != '?' && !FileStore::IsNameCharacter(character)) || position >= limit)
    {
      return DosError::FileNotFound;
    }
    result[start + position] = ToUpper(character);
    ++position;
  }
  if (result[0] == ' ')
  {
    return DosError::FileNotFound;
  }
  _template = result;
  return DosError::None;
}

bool Matches(const Template& _pattern, const Template& _name) noexcept
{
  for (std::size_t index = 0; index < TEMPLATE_BYTES; ++index)
  {
    if (_pattern[index] != '?' && _pattern[index] != _name[index])
    {
      return false;
    }
  }
  return true;
}

} // namespace

Dos::Dos(Memory& _memory, Bios& _bios, FileStore& _files, const Cycles& _clock, const DateTime& _start)
  : m_memory(_memory),
    m_bios(_bios),
    m_files(_files),
    m_clock(_clock),
    m_start(_start)
{
}

void Dos::StartProgram(std::uint16_t _pspSegment)
{
  m_pspSegment = _pspSegment;
  m_dtaSegment = _pspSegment;
  m_dtaOffset = PSP_DEFAULT_DTA;
  for (std::unique_ptr<OpenFile>& file : m_handles)
  {
    file.reset();
  }
  m_terminated = false;
  m_exitCode = 0;
}

std::optional<FaultKind> Dos::Call(Registers& _regs)
{
  const std::uint8_t function = High(_regs.ax);
  std::optional<FaultKind> fault;
  switch (function)
  {
  case 0x09:
    PrintString(_regs, fault);
    break;
  case 0x0C:
    return Low(_regs.ax) == 0x0A ? FaultKind::WouldBlock : FaultKind::UnknownSubfunction;
  case 0x1A:
    m_dtaSegment = _regs.ds;
    m_dtaOffset = _regs.dx;
    break;
  case 0x2C:
  {
    const DateTime now = Now();
    _regs.cx = Word(now.hour, now.minute);
    _regs.dx = Word(now.second, now.hundredths);
    break;
  }
  case 0x30:
    _regs.ax = Word(VERSION_MINOR, VERSION_MAJOR);
    _regs.bx = 0;
    _regs.cx = 0;
    break;
  case 0x3C:
    Create(_regs);
    break;
  case 0x3D:
    Open(_regs);
    break;
  case 0x3E:
  case 0x3F:
  case 0x40:
    if (_regs.bx < FIRST_FILE_HANDLE || (function == 0x40 && _regs.cx == 0))
    {
      return FaultKind::UnsupportedRequest;
    }
    if (function == 0x3E)
    {
      Close(_regs);
    }
    else if (function == 0x3F)
    {
      Read(_regs);
    }
    else
    {
      Write(_regs);
    }
    break;
  case 0x41:
    Delete(_regs);
    break;
  case 0x43:
    Attributes(_regs, fault);
    break;
  case 0x4E:
    FindFirst(_regs);
    break;
  case 0x4F:
    FindNext(_regs);
    break;
  default:
    return FaultKind::UnknownFunction;
  }
  return fault;
}

void Dos::Terminate(Registers& _regs)
{
  m_terminated = true;
  m_exitCode = 0;
  for (std::unique_ptr<OpenFile>& file : m_handles)
  {
    file.reset();
  }
  for (std::uint8_t index = 0; index < SAVED_VECTOR_COUNT; ++index)
  {
    const auto saved = static_cast<std::uint16_t>(PSP_SAVED_VECTORS + index * 4);
    const std::uint32_t entry = static_cast<std::uint32_t>(FIRST_SAVED_VECTOR + index) * 4u;
    m_memory.Write16(entry, m_memory.Read16(m_pspSegment, saved));
    m_memory.Write16(entry + 2u, m_memory.Read16(m_pspSegment, static_cast<std::uint16_t>(saved + 2)));
  }
  _regs.ip = m_memory.Read16(m_pspSegment, PSP_SAVED_VECTORS);
  _regs.cs = m_memory.Read16(m_pspSegment, PSP_SAVED_VECTORS + 2);
}

Dos::DateTime Dos::Now() const
{
  const std::uint64_t elapsed = m_clock * 100u * CPU_CLOCK_DIVISOR / CRYSTAL_HZ;
  const std::uint64_t startOfDay =
    ((std::uint64_t{m_start.hour} * 60u + m_start.minute) * 60u + m_start.second) * 100u + m_start.hundredths;
  const std::uint64_t total = startOfDay + elapsed;
  std::uint64_t timeOfDay = total % CENTISECONDS_PER_DAY;

  const std::chrono::sys_days startDate{std::chrono::year{m_start.year} / std::chrono::month{m_start.month} /
                                        std::chrono::day{m_start.day}};
  const auto days = static_cast<std::chrono::days::rep>(total / CENTISECONDS_PER_DAY);
  const std::chrono::year_month_day date{startDate + std::chrono::days{days}};

  DateTime now;
  now.year = static_cast<std::uint16_t>(static_cast<int>(date.year()));
  now.month = static_cast<std::uint8_t>(static_cast<unsigned>(date.month()));
  now.day = static_cast<std::uint8_t>(static_cast<unsigned>(date.day()));
  now.hundredths = static_cast<std::uint8_t>(timeOfDay % 100u);
  timeOfDay /= 100u;
  now.second = static_cast<std::uint8_t>(timeOfDay % 60u);
  timeOfDay /= 60u;
  now.minute = static_cast<std::uint8_t>(timeOfDay % 60u);
  now.hour = static_cast<std::uint8_t>(timeOfDay / 60u);
  return now;
}

FileStamp Dos::Stamp(const DateTime& _moment) noexcept
{
  const int year = std::clamp(static_cast<int>(_moment.year), 1980, 2107);
  FileStamp stamp;
  stamp.date = static_cast<std::uint16_t>(((year - 1980) << 9) | (_moment.month << 5) | _moment.day);
  stamp.time = static_cast<std::uint16_t>((_moment.hour << 11) | (_moment.minute << 5) | (_moment.second / 2));
  return stamp;
}

void Dos::PrintString(Registers& _regs, std::optional<FaultKind>& _fault)
{
  if (!m_bios.TeletypeAvailable())
  {
    _fault = FaultKind::UnsupportedRequest;
    return;
  }
  std::vector<std::uint8_t> text;
  for (std::uint32_t index = 0;; ++index)
  {
    if (index >= MAX_STRING_BYTES)
    {
      _fault = FaultKind::UnsupportedRequest;
      return;
    }
    const std::uint8_t character = m_memory.Read8(_regs.ds, static_cast<std::uint16_t>(_regs.dx + index));
    if (character == STRING_TERMINATOR)
    {
      break;
    }
    if (character == CHARACTER_BELL)
    {
      _fault = FaultKind::UnsupportedRequest;
      return;
    }
    text.push_back(character);
  }
  for (const std::uint8_t character : text)
  {
    if (character != CHARACTER_TAB)
    {
      m_bios.WriteTeletype(character);
      continue;
    }
    do
    {
      m_bios.WriteTeletype(' ');
    } while (m_bios.CursorColumn() % TAB_COLUMNS != 0);
  }
  _regs.ax = Word(High(_regs.ax), STRING_TERMINATOR);
}

void Dos::Create(Registers& _regs)
{
  const int handle = FreeHandle();
  const std::optional<std::string> name = NameAt(_regs);
  if (handle < 0)
  {
    Fail(_regs, DosError::TooManyOpenFiles);
    return;
  }
  if (!name)
  {
    Fail(_regs, DosError::PathNotFound);
    return;
  }
  std::unique_ptr<OpenFile> file;
  const DosError error = m_files.Create(*name, Low(_regs.cx), Stamp(Now()), file);
  if (error != DosError::None)
  {
    Fail(_regs, error);
    return;
  }
  m_handles[static_cast<std::size_t>(handle)] = std::move(file);
  _regs.ax = static_cast<std::uint16_t>(handle);
  Succeed(_regs);
}

void Dos::Open(Registers& _regs)
{
  const auto mode = static_cast<std::uint8_t>(Low(_regs.ax) & 0x07);
  if (mode > 2)
  {
    Fail(_regs, DosError::InvalidAccessCode);
    return;
  }
  const int handle = FreeHandle();
  const std::optional<std::string> name = NameAt(_regs);
  if (handle < 0)
  {
    Fail(_regs, DosError::TooManyOpenFiles);
    return;
  }
  if (!name)
  {
    Fail(_regs, DosError::PathNotFound);
    return;
  }
  const FileAccess access = mode == 0 ? FileAccess::Read : (mode == 1 ? FileAccess::Write : FileAccess::ReadWrite);
  std::unique_ptr<OpenFile> file;
  const DosError error = m_files.Open(*name, access, file);
  if (error != DosError::None)
  {
    Fail(_regs, error);
    return;
  }
  m_handles[static_cast<std::size_t>(handle)] = std::move(file);
  _regs.ax = static_cast<std::uint16_t>(handle);
  Succeed(_regs);
}

void Dos::Close(Registers& _regs)
{
  if (HandleFile(_regs.bx) == nullptr)
  {
    Fail(_regs, DosError::InvalidHandle);
    return;
  }
  m_handles[_regs.bx].reset();
  Succeed(_regs);
}

void Dos::Read(Registers& _regs)
{
  OpenFile* const file = HandleFile(_regs.bx);
  if (file == nullptr)
  {
    Fail(_regs, DosError::InvalidHandle);
    return;
  }
  std::vector<std::uint8_t> buffer(_regs.cx);
  std::uint32_t count = 0;
  const DosError error = file->Read(buffer, count);
  if (error != DosError::None)
  {
    Fail(_regs, error);
    return;
  }
  m_memory.Load(Memory::Linear(_regs.ds, _regs.dx), std::span<const std::uint8_t>(buffer.data(), count));
  _regs.ax = static_cast<std::uint16_t>(count);
  Succeed(_regs);
}

void Dos::Write(Registers& _regs)
{
  OpenFile* const file = HandleFile(_regs.bx);
  if (file == nullptr)
  {
    Fail(_regs, DosError::InvalidHandle);
    return;
  }
  std::vector<std::uint8_t> buffer(_regs.cx);
  const std::uint32_t source = Memory::Linear(_regs.ds, _regs.dx);
  for (std::uint32_t index = 0; index < buffer.size(); ++index)
  {
    buffer[index] = m_memory.Read8(source + index);
  }
  std::uint32_t count = 0;
  const DosError error = file->Write(buffer, Stamp(Now()), count);
  if (error != DosError::None)
  {
    Fail(_regs, error);
    return;
  }
  _regs.ax = static_cast<std::uint16_t>(count);
  Succeed(_regs);
}

void Dos::Delete(Registers& _regs)
{
  const std::optional<std::string> name = NameAt(_regs);
  const DosError error = name ? m_files.Delete(*name) : DosError::PathNotFound;
  if (error != DosError::None)
  {
    Fail(_regs, error);
    return;
  }
  Succeed(_regs);
}

void Dos::Attributes(Registers& _regs, std::optional<FaultKind>& _fault)
{
  const std::uint8_t subfunction = Low(_regs.ax);
  if (subfunction > 1)
  {
    _fault = FaultKind::UnknownSubfunction;
    return;
  }
  const std::optional<std::string> name = NameAt(_regs);
  if (!name)
  {
    Fail(_regs, DosError::PathNotFound);
    return;
  }
  if (subfunction == 0)
  {
    std::uint8_t attribute = 0;
    const DosError error = m_files.Attribute(*name, attribute);
    if (error != DosError::None)
    {
      Fail(_regs, error);
      return;
    }
    _regs.cx = attribute;
    _regs.ax = attribute;
    Succeed(_regs);
    return;
  }
  const DosError error = _regs.cx > 0xFF ? DosError::AccessDenied : m_files.SetAttribute(*name, Low(_regs.cx));
  if (error != DosError::None)
  {
    Fail(_regs, error);
    return;
  }
  Succeed(_regs);
}

void Dos::FindFirst(Registers& _regs)
{
  const std::optional<std::string> pattern = NameAt(_regs);
  if (!pattern)
  {
    Fail(_regs, DosError::PathNotFound);
    return;
  }
  Template search;
  const DosError patternError = SearchTemplate(*pattern, search);
  if (patternError != DosError::None)
  {
    Fail(_regs, patternError);
    return;
  }
  const DosError error = Find(search, Low(_regs.cx), 0);
  if (error != DosError::None)
  {
    Fail(_regs, error);
    return;
  }
  _regs.ax = 0;
  Succeed(_regs);
}

void Dos::FindNext(Registers& _regs)
{
  const auto at = [this](std::uint16_t _offset) { return static_cast<std::uint16_t>(m_dtaOffset + _offset); };
  if (m_memory.Read8(m_dtaSegment, at(DTA_DRIVE)) != CURRENT_DRIVE)
  {
    Fail(_regs, DosError::NoMoreFiles);
    return;
  }
  Template search;
  for (std::size_t index = 0; index < TEMPLATE_BYTES; ++index)
  {
    search[index] = static_cast<char>(m_memory.Read8(m_dtaSegment, at(static_cast<std::uint16_t>(DTA_TEMPLATE + index))));
  }
  const std::uint8_t attribute = m_memory.Read8(m_dtaSegment, at(DTA_SEARCH_ATTRIBUTE));
  const std::uint16_t next = m_memory.Read16(m_dtaSegment, at(DTA_NEXT_ENTRY));
  const DosError error = Find(search, attribute, next);
  if (error != DosError::None)
  {
    Fail(_regs, error);
    return;
  }
  _regs.ax = 0;
  Succeed(_regs);
}

DosError Dos::Find(const std::array<char, 11>& _pattern, std::uint8_t _attribute, std::uint16_t _index)
{
  const std::vector<FileEntry> entries = m_files.List();
  std::size_t index = _index;
  bool found = false;
  // A search for the volume label alone finds nothing: the store has no label.
  for (; _attribute != FileStore::ATTRIBUTE_VOLUME && index < entries.size(); ++index)
  {
    const FileEntry& entry = entries[index];
    if ((entry.attribute & SEARCH_RESTRICTED & ~_attribute) == 0 && Matches(_pattern, EntryName(entry.name)))
    {
      found = true;
      break;
    }
  }

  const auto at = [this](std::size_t _offset) { return static_cast<std::uint16_t>(m_dtaOffset + _offset); };
  m_memory.Write8(m_dtaSegment, at(DTA_DRIVE), CURRENT_DRIVE);
  for (std::size_t offset = 0; offset < TEMPLATE_BYTES; ++offset)
  {
    m_memory.Write8(m_dtaSegment, at(DTA_TEMPLATE + offset), static_cast<std::uint8_t>(_pattern[offset]));
  }
  m_memory.Write8(m_dtaSegment, at(DTA_SEARCH_ATTRIBUTE), _attribute);
  const std::size_t next = found ? index + 1 : std::max<std::size_t>(index, _index);
  m_memory.Write16(m_dtaSegment, at(DTA_NEXT_ENTRY), static_cast<std::uint16_t>(std::min<std::size_t>(next, 0xFFFF)));
  for (std::size_t offset = DTA_NEXT_ENTRY + 2; offset < DTA_ATTRIBUTE; ++offset)
  {
    m_memory.Write8(m_dtaSegment, at(offset), 0);
  }
  if (!found)
  {
    return DosError::NoMoreFiles;
  }

  const FileEntry& entry = entries[index];
  m_memory.Write8(m_dtaSegment, at(DTA_ATTRIBUTE), entry.attribute);
  m_memory.Write16(m_dtaSegment, at(DTA_TIME), entry.stamp.time);
  m_memory.Write16(m_dtaSegment, at(DTA_DATE), entry.stamp.date);
  m_memory.Write16(m_dtaSegment, at(DTA_SIZE), static_cast<std::uint16_t>(entry.sizeBytes & 0xFFFF));
  m_memory.Write16(m_dtaSegment, at(DTA_SIZE + 2), static_cast<std::uint16_t>(entry.sizeBytes >> 16));
  for (std::size_t offset = 0; offset < DTA_NAME_BYTES; ++offset)
  {
    const char character = offset < entry.name.size() ? entry.name[offset] : '\0';
    m_memory.Write8(m_dtaSegment, at(DTA_NAME + offset), static_cast<std::uint8_t>(character));
  }
  return DosError::None;
}

std::optional<std::string> Dos::NameAt(const Registers& _regs) const
{
  std::string name;
  for (std::size_t index = 0; index < MAX_NAME_BYTES; ++index)
  {
    const std::uint8_t character = m_memory.Read8(_regs.ds, static_cast<std::uint16_t>(_regs.dx + index));
    if (character == 0)
    {
      return name;
    }
    name.push_back(static_cast<char>(character));
  }
  return std::nullopt;
}

OpenFile* Dos::HandleFile(std::uint16_t _handle) const noexcept
{
  if (_handle < FIRST_FILE_HANDLE || _handle >= HANDLE_COUNT)
  {
    return nullptr;
  }
  return m_handles[_handle].get();
}

int Dos::FreeHandle() const noexcept
{
  for (std::uint16_t handle = FIRST_FILE_HANDLE; handle < HANDLE_COUNT; ++handle)
  {
    if (!m_handles[handle])
    {
      return handle;
    }
  }
  return -1;
}

} // namespace Machine
