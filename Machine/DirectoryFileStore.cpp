#include "pch.h"

#include "DirectoryFileStore.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <utility>

namespace Machine
{

namespace
{

// A host file name as ASCII, or nothing when it has any other character: those have no DOS spelling.
std::optional<std::string> AsciiName(const std::filesystem::path& _path)
{
  const std::u8string name = _path.filename().u8string();
  std::string ascii;
  for (const char8_t character : name)
  {
    if (character >= 0x80)
    {
      return std::nullopt;
    }
    ascii.push_back(static_cast<char>(character));
  }
  return ascii;
}

// The DOS name a host file name stands for, or nothing when it is not a valid 8.3 name.
std::optional<std::string> DosNameOf(const std::string& _hostName)
{
  std::string canonical;
  if (FileStore::CanonicalName(_hostName, canonical) != DosError::None)
  {
    return std::nullopt;
  }
  return canonical;
}

} // namespace

// ── The open file ─────────────────────────────────────────────────────────────────────────────

class DirectoryFileStore::HostFile final : public OpenFile
{
public:
  HostFile(DirectoryFileStore& _store, std::string _canonical, std::fstream _stream, FileAccess _access)
    : m_store(_store),
      m_canonical(std::move(_canonical)),
      m_stream(std::move(_stream)),
      m_access(_access)
  {
  }

  [[nodiscard]] DosError Read(std::span<std::uint8_t> _buffer, std::uint32_t& _count) override
  {
    if (m_access == FileAccess::Write)
    {
      return DosError::AccessDenied;
    }
    m_stream.clear();
    m_stream.seekg(static_cast<std::streamoff>(m_position));
    m_stream.read(reinterpret_cast<char*>(_buffer.data()), static_cast<std::streamsize>(_buffer.size()));
    const std::streamsize count = m_stream.gcount();
    if (m_stream.bad())
    {
      m_stream.clear();
      return DosError::AccessDenied;
    }
    m_stream.clear();
    m_position += static_cast<std::uint64_t>(count);
    _count = static_cast<std::uint32_t>(count);
    return DosError::None;
  }

  [[nodiscard]] DosError Write(std::span<const std::uint8_t> _bytes, const FileStamp& _stamp, std::uint32_t& _count) override
  {
    if (m_access == FileAccess::Read)
    {
      return DosError::AccessDenied;
    }
    m_stream.clear();
    m_stream.seekp(static_cast<std::streamoff>(m_position));
    m_stream.write(reinterpret_cast<const char*>(_bytes.data()), static_cast<std::streamsize>(_bytes.size()));
    m_stream.flush();
    if (!m_stream)
    {
      m_stream.clear();
      return DosError::AccessDenied;
    }
    m_position += _bytes.size();
    m_store.RecordWrite(m_canonical, _stamp);
    _count = static_cast<std::uint32_t>(_bytes.size());
    return DosError::None;
  }

private:
  DirectoryFileStore& m_store;
  std::string m_canonical;
  std::fstream m_stream;
  FileAccess m_access;
  std::uint64_t m_position = 0;
};

// ── The store ─────────────────────────────────────────────────────────────────────────────────

DirectoryFileStore::DirectoryFileStore(std::filesystem::path _directory)
  : m_directory(std::move(_directory))
{
}

DosError DirectoryFileStore::Open(std::string_view _name, FileAccess _access, std::unique_ptr<OpenFile>& _file)
{
  std::string canonical;
  std::filesystem::path path;
  const DosError error = ExistingFile(_name, canonical, path);
  if (error != DosError::None)
  {
    return error;
  }
  if (_access != FileAccess::Read && (RecordOf(canonical).attribute & ATTRIBUTE_READ_ONLY) != 0)
  {
    return DosError::AccessDenied;
  }
  // Write-only still opens for input too: output alone would truncate the file.
  const std::ios::openmode mode = _access == FileAccess::Read ? std::ios::in : (std::ios::in | std::ios::out);
  std::fstream stream(path, mode | std::ios::binary);
  if (!stream.is_open())
  {
    return DosError::AccessDenied;
  }
  _file = std::make_unique<HostFile>(*this, std::move(canonical), std::move(stream), _access);
  return DosError::None;
}

DosError DirectoryFileStore::Create(std::string_view _name, std::uint8_t _attribute, const FileStamp& _stamp,
                                    std::unique_ptr<OpenFile>& _file)
{
  std::string canonical;
  const DosError nameError = CanonicalName(_name, canonical);
  if (nameError != DosError::None)
  {
    return nameError;
  }
  if ((_attribute & (ATTRIBUTE_VOLUME | ATTRIBUTE_DIRECTORY)) != 0)
  {
    return DosError::AccessDenied;
  }
  std::error_code error;
  if (!std::filesystem::is_directory(m_directory, error))
  {
    return DosError::PathNotFound;
  }
  const std::filesystem::path path = HostPath(canonical);
  if (std::filesystem::exists(path, error))
  {
    if (!std::filesystem::is_regular_file(path, error) || (RecordOf(canonical).attribute & ATTRIBUTE_READ_ONLY) != 0)
    {
      return DosError::AccessDenied;
    }
  }
  std::fstream stream(path, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
  if (!stream.is_open())
  {
    return DosError::AccessDenied;
  }
  const auto kept = static_cast<std::uint8_t>(_attribute & (ATTRIBUTE_READ_ONLY | ATTRIBUTE_HIDDEN | ATTRIBUTE_SYSTEM));
  m_records[canonical] = Record{static_cast<std::uint8_t>(kept | ATTRIBUTE_ARCHIVE), _stamp};
  _file = std::make_unique<HostFile>(*this, std::move(canonical), std::move(stream), FileAccess::ReadWrite);
  return DosError::None;
}

DosError DirectoryFileStore::Delete(std::string_view _name)
{
  std::string canonical;
  std::filesystem::path path;
  const DosError found = ExistingFile(_name, canonical, path);
  if (found != DosError::None)
  {
    return found;
  }
  if ((RecordOf(canonical).attribute & ATTRIBUTE_READ_ONLY) != 0)
  {
    return DosError::AccessDenied;
  }
  std::error_code error;
  if (!std::filesystem::remove(path, error) || error)
  {
    return DosError::AccessDenied;
  }
  m_records.erase(canonical);
  return DosError::None;
}

DosError DirectoryFileStore::Attribute(std::string_view _name, std::uint8_t& _attribute)
{
  std::string canonical;
  std::filesystem::path path;
  const DosError error = ExistingFile(_name, canonical, path);
  if (error != DosError::None)
  {
    return error;
  }
  _attribute = RecordOf(canonical).attribute;
  return DosError::None;
}

DosError DirectoryFileStore::SetAttribute(std::string_view _name, std::uint8_t _attribute)
{
  std::string canonical;
  std::filesystem::path path;
  const DosError error = ExistingFile(_name, canonical, path);
  if (error != DosError::None)
  {
    return error;
  }
  if ((_attribute & ~ATTRIBUTES_SETTABLE) != 0)
  {
    return DosError::AccessDenied;
  }
  m_records[canonical].attribute = _attribute;
  return DosError::None;
}

std::vector<FileEntry> DirectoryFileStore::List()
{
  struct Found
  {
    std::string hostName;
    std::string name;
    std::filesystem::path path;
  };

  // Host files in host-name order, so that two spellings of one DOS name always resolve the same way.
  std::vector<Found> found;
  std::error_code error;
  for (auto entry = std::filesystem::directory_iterator(m_directory, error); !error && entry != std::filesystem::directory_iterator();
       entry.increment(error))
  {
    std::error_code kind;
    if (!entry->is_regular_file(kind))
    {
      continue;
    }
    std::optional<std::string> hostName = AsciiName(entry->path());
    std::optional<std::string> name = hostName ? DosNameOf(*hostName) : std::nullopt;
    if (hostName && name)
    {
      found.push_back(Found{std::move(*hostName), std::move(*name), entry->path()});
    }
  }
  std::sort(found.begin(), found.end(), [](const Found& _left, const Found& _right) { return _left.hostName < _right.hostName; });

  std::vector<FileEntry> entries;
  std::set<std::string> seen;
  for (Found& file : found)
  {
    if (!seen.insert(file.name).second)
    {
      continue;
    }
    std::error_code sizeError;
    const std::uintmax_t sizeBytes = std::filesystem::file_size(file.path, sizeError);
    const Record record = RecordOf(file.name);
    FileEntry fileEntry;
    fileEntry.name = std::move(file.name);
    fileEntry.attribute = record.attribute;
    fileEntry.stamp = record.stamp;
    fileEntry.sizeBytes =
      sizeError ? 0 : static_cast<std::uint32_t>(std::min<std::uintmax_t>(sizeBytes, std::numeric_limits<std::uint32_t>::max()));
    entries.push_back(std::move(fileEntry));
  }
  std::sort(entries.begin(), entries.end(), [](const FileEntry& _left, const FileEntry& _right) { return _left.name < _right.name; });
  return entries;
}

std::filesystem::path DirectoryFileStore::HostPath(const std::string& _canonical) const
{
  std::filesystem::path exact = m_directory / _canonical;
  std::error_code error;
  if (std::filesystem::exists(exact, error))
  {
    return exact;
  }
  std::filesystem::path match;
  std::string matchName;
  for (auto entry = std::filesystem::directory_iterator(m_directory, error); !error && entry != std::filesystem::directory_iterator();
       entry.increment(error))
  {
    const std::optional<std::string> hostName = AsciiName(entry->path());
    if (!hostName || DosNameOf(*hostName) != _canonical)
    {
      continue;
    }
    if (match.empty() || *hostName < matchName)
    {
      match = entry->path();
      matchName = *hostName;
    }
  }
  return match.empty() ? exact : match;
}

DosError DirectoryFileStore::ExistingFile(std::string_view _name, std::string& _canonical, std::filesystem::path& _path) const
{
  const DosError nameError = CanonicalName(_name, _canonical);
  if (nameError != DosError::None)
  {
    return nameError;
  }
  std::error_code error;
  if (!std::filesystem::is_directory(m_directory, error))
  {
    return DosError::PathNotFound;
  }
  _path = HostPath(_canonical);
  if (!std::filesystem::is_regular_file(_path, error))
  {
    return DosError::FileNotFound;
  }
  return DosError::None;
}

DirectoryFileStore::Record DirectoryFileStore::RecordOf(const std::string& _canonical) const
{
  const auto found = m_records.find(_canonical);
  return found == m_records.end() ? Record{} : found->second;
}

void DirectoryFileStore::RecordWrite(const std::string& _canonical, const FileStamp& _stamp)
{
  Record& record = m_records[_canonical];
  record.attribute = static_cast<std::uint8_t>(record.attribute | ATTRIBUTE_ARCHIVE);
  record.stamp = _stamp;
}

} // namespace Machine
