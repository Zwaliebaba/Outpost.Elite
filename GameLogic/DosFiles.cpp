#include "pch.h"

#include "DosFiles.h"

#include "Arithmetic.h"
#include "GameState.h"
#include "Memory.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Elite
{

namespace
{

using Machine::DosError;
using Machine::FileStore;

constexpr std::size_t MAX_NAME_BYTES = 128;
constexpr std::uint8_t ACCESS_MODE_BITS = 0x07; // AL's bits 0-2; the sharing bits above them are ignored
constexpr std::uint8_t READ_ACCESS = 0;
constexpr std::uint8_t WRITE_ACCESS = 1;
constexpr std::uint8_t READ_WRITE_ACCESS = 2;
constexpr std::uint16_t MOST_ATTRIBUTES = 0xFF; // AX=4301h refuses a CX above it

constexpr std::uint64_t CENTISECONDS_PER_DAY = 8'640'000;
constexpr int FIRST_STAMP_YEAR = 1980;
constexpr int LAST_STAMP_YEAR = 2107;

// The transfer area as find-first and find-next fill it, in DOS's layout.
constexpr std::uint16_t AREA_DRIVE = 0x00;
constexpr std::uint16_t AREA_TEMPLATE = 0x01;
constexpr std::uint16_t AREA_SEARCH_ATTRIBUTE = 0x0C;
constexpr std::uint16_t AREA_NEXT_ENTRY = 0x0D;
constexpr std::uint16_t AREA_ATTRIBUTE = 0x15;
constexpr std::uint16_t AREA_TIME = 0x16;
constexpr std::uint16_t AREA_DATE = 0x18;
constexpr std::uint16_t AREA_SIZE = 0x1A;
constexpr std::uint16_t AREA_NAME = 0x1E;
constexpr std::uint16_t AREA_NAME_BYTES = 13;

constexpr std::size_t TEMPLATE_BYTES = 11;
constexpr std::size_t BASE_CHARACTERS = 8;
constexpr std::size_t EXTENSION_CHARACTERS = 3;
// What a file's hidden, system and directory bits must all be found in the search attribute for it to match.
constexpr std::uint8_t SEARCH_RESTRICTED = FileStore::ATTRIBUTE_HIDDEN | FileStore::ATTRIBUTE_SYSTEM | FileStore::ATTRIBUTE_DIRECTORY;

using Template = std::array<char, TEMPLATE_BYTES>;

[[nodiscard]] constexpr DosAnswer Succeed(std::uint16_t _value) noexcept
{
  return DosAnswer{false, _value};
}

[[nodiscard]] constexpr DosAnswer Fail(DosError _error) noexcept
{
  return DosAnswer{true, static_cast<std::uint16_t>(_error)};
}

[[nodiscard]] constexpr char ToUpper(char _character) noexcept
{
  return _character >= 'a' && _character <= 'z' ? static_cast<char>(_character - 'a' + 'A') : _character;
}

// The byte at linear address _linear, wrapping at 1 MiB, as segment and offset: what the emulated DOS's reads and writes by
// linear address reach.
[[nodiscard]] constexpr std::uint16_t LinearSegment(std::uint32_t _linear) noexcept
{
  return static_cast<std::uint16_t>((_linear & Machine::Memory::ADDRESS_MASK) >> 4);
}

[[nodiscard]] constexpr std::uint16_t LinearOffset(std::uint32_t _linear) noexcept
{
  return static_cast<std::uint16_t>(_linear & (Machine::Memory::PARAGRAPH_BYTES - 1));
}

// A canonical name, "NAME.EXT", as the 11 blank-padded characters of a directory entry.
[[nodiscard]] Template EntryName(std::string_view _name)
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

// A search pattern as DOS's template: '*' fills the rest of its field with '?', and what follows a '*' in the same field is
// ignored, as DOS does.
[[nodiscard]] DosError SearchTemplate(std::string_view _pattern, Template& _template)
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

[[nodiscard]] bool Matches(const Template& _pattern, const Template& _name) noexcept
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

// What the emulated DOS refused as a fault, stopping the run: a standard handle, or a write of nothing, which truncates.
void RefuseStandardHandle(std::uint16_t _handle, const char* _service)
{
  if (_handle < DosFiles::FIRST_HANDLE)
  {
    throw std::logic_error(std::string("DosFiles: ") + _service + " on a standard handle, which DOS's emulation refused as a fault");
  }
}

} // namespace

DosFiles::DosFiles(GameState& _state, Machine::FileStore& _files, const Machine::Dos::DateTime& _startMoment) noexcept
  : m_state(_state),
    m_files(_files),
    m_startMoment(_startMoment)
{
}

void DosFiles::StartProgram(std::uint16_t _pspSegment) noexcept
{
  m_transferSegment = _pspSegment;
  m_transferOffset = DEFAULT_TRANSFER_AREA;
  for (std::unique_ptr<Machine::OpenFile>& file : m_handles)
  {
    file.reset();
  }
}

void DosFiles::SetTransferArea(std::uint16_t _segment, std::uint16_t _offset) noexcept
{
  m_transferSegment = _segment;
  m_transferOffset = _offset;
}

DosFileAttributes DosFiles::ReadAttributes(std::uint16_t _segment, std::uint16_t _name)
{
  const std::optional<std::string> name = NameAt(_segment, _name);
  if (!name)
  {
    return DosFileAttributes{Fail(DosError::PathNotFound), 0};
  }
  std::uint8_t attribute = 0;
  const DosError error = m_files.Attribute(*name, attribute);
  if (error != DosError::None)
  {
    return DosFileAttributes{Fail(error), 0};
  }
  return DosFileAttributes{Succeed(attribute), attribute};
}

DosAnswer DosFiles::SetAttributes(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes)
{
  const std::optional<std::string> name = NameAt(_segment, _name);
  if (!name)
  {
    return Fail(DosError::PathNotFound);
  }
  const DosError error = _attributes > MOST_ATTRIBUTES ? DosError::AccessDenied : m_files.SetAttribute(*name, Low(_attributes));
  return error != DosError::None ? Fail(error) : Succeed(0);
}

DosAnswer DosFiles::Create(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes, Machine::Cycles _clock)
{
  const int handle = FreeHandle();
  const std::optional<std::string> name = NameAt(_segment, _name);
  if (handle < 0)
  {
    return Fail(DosError::TooManyOpenFiles);
  }
  if (!name)
  {
    return Fail(DosError::PathNotFound);
  }
  std::unique_ptr<Machine::OpenFile> file;
  const DosError error = m_files.Create(*name, Low(_attributes), Stamp(Now(_clock)), file);
  if (error != DosError::None)
  {
    return Fail(error);
  }
  m_handles[static_cast<std::size_t>(handle)] = std::move(file);
  return Succeed(static_cast<std::uint16_t>(handle));
}

DosAnswer DosFiles::Open(std::uint16_t _segment, std::uint16_t _name, std::uint8_t _access)
{
  const auto mode = static_cast<std::uint8_t>(_access & ACCESS_MODE_BITS);
  if (mode > READ_WRITE_ACCESS)
  {
    return Fail(DosError::InvalidAccessCode);
  }
  const int handle = FreeHandle();
  const std::optional<std::string> name = NameAt(_segment, _name);
  if (handle < 0)
  {
    return Fail(DosError::TooManyOpenFiles);
  }
  if (!name)
  {
    return Fail(DosError::PathNotFound);
  }
  const Machine::FileAccess access =
    mode == READ_ACCESS ? Machine::FileAccess::Read : (mode == WRITE_ACCESS ? Machine::FileAccess::Write : Machine::FileAccess::ReadWrite);
  std::unique_ptr<Machine::OpenFile> file;
  const DosError error = m_files.Open(*name, access, file);
  if (error != DosError::None)
  {
    return Fail(error);
  }
  m_handles[static_cast<std::size_t>(handle)] = std::move(file);
  return Succeed(static_cast<std::uint16_t>(handle));
}

DosAnswer DosFiles::Read(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes)
{
  RefuseStandardHandle(_handle, "a read");
  Machine::OpenFile* const file = HandleFile(_handle);
  if (file == nullptr)
  {
    return Fail(DosError::InvalidHandle);
  }
  std::vector<std::uint8_t> buffer(_bytes);
  std::uint32_t count = 0;
  const DosError error = file->Read(buffer, count);
  if (error != DosError::None)
  {
    return Fail(error);
  }
  // As Memory::Load lays them down: a byte at a time, from the linear address on, wrapping at 1 MiB.
  const std::uint32_t first = Machine::Memory::Linear(_segment, _buffer);
  for (std::uint32_t index = 0; index < count; ++index)
  {
    m_state.SetFarByte(LinearSegment(first + index), LinearOffset(first + index), buffer[index]);
  }
  return Succeed(static_cast<std::uint16_t>(count));
}

DosAnswer DosFiles::Write(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes,
                          Machine::Cycles _clock)
{
  RefuseStandardHandle(_handle, "a write");
  if (_bytes == 0)
  {
    throw std::logic_error("DosFiles: a write of no bytes, which truncates in DOS and which DOS's emulation refused as a fault");
  }
  Machine::OpenFile* const file = HandleFile(_handle);
  if (file == nullptr)
  {
    return Fail(DosError::InvalidHandle);
  }
  std::vector<std::uint8_t> buffer(_bytes);
  const std::uint32_t first = Machine::Memory::Linear(_segment, _buffer);
  for (std::uint32_t index = 0; index < buffer.size(); ++index)
  {
    buffer[index] = m_state.FarByte(LinearSegment(first + index), LinearOffset(first + index));
  }
  std::uint32_t count = 0;
  const DosError error = file->Write(buffer, Stamp(Now(_clock)), count);
  if (error != DosError::None)
  {
    return Fail(error);
  }
  return Succeed(static_cast<std::uint16_t>(count));
}

DosAnswer DosFiles::Close(std::uint16_t _handle)
{
  RefuseStandardHandle(_handle, "a close");
  if (HandleFile(_handle) == nullptr)
  {
    return Fail(DosError::InvalidHandle);
  }
  m_handles[_handle].reset();
  return Succeed(0);
}

DosAnswer DosFiles::Delete(std::uint16_t _segment, std::uint16_t _name)
{
  const std::optional<std::string> name = NameAt(_segment, _name);
  const DosError error = name ? m_files.Delete(*name) : DosError::PathNotFound;
  return error != DosError::None ? Fail(error) : Succeed(0);
}

DosAnswer DosFiles::FindFirst(std::uint16_t _segment, std::uint16_t _pattern, std::uint16_t _attributes)
{
  const std::optional<std::string> pattern = NameAt(_segment, _pattern);
  if (!pattern)
  {
    return Fail(DosError::PathNotFound);
  }
  Template search;
  const DosError patternError = SearchTemplate(*pattern, search);
  if (patternError != DosError::None)
  {
    return Fail(patternError);
  }
  const DosError error = Find(search, Low(_attributes), 0);
  return error != DosError::None ? Fail(error) : Succeed(0);
}

DosAnswer DosFiles::FindNext()
{
  const auto at = [this](std::uint16_t _offset) { return static_cast<std::uint16_t>(m_transferOffset + _offset); };
  if (m_state.FarByte(m_transferSegment, at(AREA_DRIVE)) != CURRENT_DRIVE)
  {
    return Fail(DosError::NoMoreFiles);
  }
  Template search;
  for (std::size_t index = 0; index < TEMPLATE_BYTES; ++index)
  {
    search[index] = static_cast<char>(m_state.FarByte(m_transferSegment, at(static_cast<std::uint16_t>(AREA_TEMPLATE + index))));
  }
  const std::uint8_t attribute = m_state.FarByte(m_transferSegment, at(AREA_SEARCH_ATTRIBUTE));
  const std::uint16_t next = m_state.FarWord(m_transferSegment, at(AREA_NEXT_ENTRY));
  const DosError error = Find(search, attribute, next);
  return error != DosError::None ? Fail(error) : Succeed(0);
}

Machine::Dos::DateTime DosFiles::Now(Machine::Cycles _clock) const
{
  const std::uint64_t elapsed = _clock * 100u * Machine::CPU_CLOCK_DIVISOR / Machine::CRYSTAL_HZ;
  const std::uint64_t startOfDay =
    ((std::uint64_t{m_startMoment.hour} * 60u + m_startMoment.minute) * 60u + m_startMoment.second) * 100u + m_startMoment.hundredths;
  const std::uint64_t total = startOfDay + elapsed;
  std::uint64_t timeOfDay = total % CENTISECONDS_PER_DAY;

  const std::chrono::sys_days startDate{std::chrono::year{m_startMoment.year} / std::chrono::month{m_startMoment.month} /
                                        std::chrono::day{m_startMoment.day}};
  const auto days = static_cast<std::chrono::days::rep>(total / CENTISECONDS_PER_DAY);
  const std::chrono::year_month_day date{startDate + std::chrono::days{days}};

  Machine::Dos::DateTime now;
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

Machine::FileStamp DosFiles::Stamp(const Machine::Dos::DateTime& _moment) noexcept
{
  const int year = std::clamp(static_cast<int>(_moment.year), FIRST_STAMP_YEAR, LAST_STAMP_YEAR);
  Machine::FileStamp stamp;
  stamp.date = static_cast<std::uint16_t>(((year - FIRST_STAMP_YEAR) << 9) | (_moment.month << 5) | _moment.day);
  stamp.time = static_cast<std::uint16_t>((_moment.hour << 11) | (_moment.minute << 5) | (_moment.second / 2));
  return stamp;
}

Machine::DosError DosFiles::Find(const Template& _pattern, std::uint8_t _attribute, std::uint16_t _index)
{
  const std::vector<Machine::FileEntry> entries = m_files.List();
  std::size_t index = _index;
  bool found = false;
  // A search for the volume label alone finds nothing: the store has no label.
  for (; _attribute != FileStore::ATTRIBUTE_VOLUME && index < entries.size(); ++index)
  {
    const Machine::FileEntry& entry = entries[index];
    if ((entry.attribute & SEARCH_RESTRICTED & ~_attribute) == 0 && Matches(_pattern, EntryName(entry.name)))
    {
      found = true;
      break;
    }
  }

  // The search's state, in the order the emulated DOS wrote it, each byte once.
  const auto at = [this](std::size_t _offset) { return static_cast<std::uint16_t>(m_transferOffset + _offset); };
  m_state.SetFarByte(m_transferSegment, at(AREA_DRIVE), CURRENT_DRIVE);
  for (std::size_t offset = 0; offset < TEMPLATE_BYTES; ++offset)
  {
    m_state.SetFarByte(m_transferSegment, at(AREA_TEMPLATE + offset), static_cast<std::uint8_t>(_pattern[offset]));
  }
  m_state.SetFarByte(m_transferSegment, at(AREA_SEARCH_ATTRIBUTE), _attribute);
  const std::size_t next = found ? index + 1 : std::max<std::size_t>(index, _index);
  m_state.SetFarWord(m_transferSegment, at(AREA_NEXT_ENTRY), static_cast<std::uint16_t>(std::min<std::size_t>(next, 0xFFFF)));
  for (std::size_t offset = AREA_NEXT_ENTRY + 2; offset < AREA_ATTRIBUTE; ++offset)
  {
    m_state.SetFarByte(m_transferSegment, at(offset), 0);
  }
  if (!found)
  {
    return DosError::NoMoreFiles;
  }

  // What was found.
  const Machine::FileEntry& entry = entries[index];
  m_state.SetFarByte(m_transferSegment, at(AREA_ATTRIBUTE), entry.attribute);
  m_state.SetFarWord(m_transferSegment, at(AREA_TIME), entry.stamp.time);
  m_state.SetFarWord(m_transferSegment, at(AREA_DATE), entry.stamp.date);
  m_state.SetFarWord(m_transferSegment, at(AREA_SIZE), static_cast<std::uint16_t>(entry.sizeBytes & 0xFFFF));
  m_state.SetFarWord(m_transferSegment, at(AREA_SIZE + 2), static_cast<std::uint16_t>(entry.sizeBytes >> 16));
  for (std::size_t offset = 0; offset < AREA_NAME_BYTES; ++offset)
  {
    const char character = offset < entry.name.size() ? entry.name[offset] : '\0';
    m_state.SetFarByte(m_transferSegment, at(AREA_NAME + offset), static_cast<std::uint8_t>(character));
  }
  return DosError::None;
}

std::optional<std::string> DosFiles::NameAt(std::uint16_t _segment, std::uint16_t _name) const
{
  std::string name;
  for (std::size_t index = 0; index < MAX_NAME_BYTES; ++index)
  {
    const std::uint8_t character = m_state.FarByte(_segment, static_cast<std::uint16_t>(_name + index));
    if (character == 0)
    {
      return name;
    }
    name.push_back(static_cast<char>(character));
  }
  return std::nullopt;
}

Machine::OpenFile* DosFiles::HandleFile(std::uint16_t _handle) const noexcept
{
  if (_handle < FIRST_HANDLE || _handle >= HANDLE_COUNT)
  {
    return nullptr;
  }
  return m_handles[_handle].get();
}

int DosFiles::FreeHandle() const noexcept
{
  for (std::uint16_t handle = FIRST_HANDLE; handle < HANDLE_COUNT; ++handle)
  {
    if (!m_handles[handle])
    {
      return handle;
    }
  }
  return -1;
}

} // namespace Elite
