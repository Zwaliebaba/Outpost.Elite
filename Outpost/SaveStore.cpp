#include "pch.h"

#include "SaveStore.h"

#include <fstream>

namespace Outpost
{

namespace
{
/// 6502: the name is eight bytes ending in a carriage return, which MT26 writes there (§6.19).
constexpr std::uint8_t NAME_TERMINATOR = 13;

/// One folder under LocalAppData, and one extension, so a person can find and copy them.
constexpr const wchar_t* FOLDER = L"Outpost.Elite\\Commanders";
constexpr const wchar_t* EXTENSION = L".cmdr";

/// LocalAppData, or an empty path. Read from the environment rather than through SHGetKnownFolderPath
/// so that this file needs no additional library and no COM.
[[nodiscard]] std::filesystem::path LocalAppData()
{
  const DWORD needed = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  if (needed == 0)
  {
    return {};
  }

  std::wstring value(needed, L'\0');
  const DWORD written = ::GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), needed);
  if (written == 0 || written >= needed)
  {
    return {};
  }

  value.resize(written);
  return std::filesystem::path(value);
}
} // namespace

SaveStore::SaveStore()
{
  const std::filesystem::path base = LocalAppData();
  if (!base.empty())
  {
    m_root = base / FOLDER;
  }
}

std::filesystem::path SaveStore::PathFor(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name) const
{
  if (m_root.empty())
  {
    return {};
  }

  std::wstring stem;
  for (const std::uint8_t byte : _name)
  {
    if (byte == NAME_TERMINATOR)
    {
      break;
    }

    const bool allowed = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
                         || (byte >= '0' && byte <= '9');
    if (!allowed)
    {
      // Refused rather than dropped: a name that cannot be a filename should fail visibly, not
      // silently become a different commander's file.
      return {};
    }

    stem += static_cast<wchar_t>(byte);
  }

  if (stem.empty())
  {
    return {};
  }

  return m_root / (stem + EXTENSION);
}

bool SaveStore::Write(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
                      std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE> _file)
{
  const std::filesystem::path path = PathFor(_name);
  if (path.empty())
  {
    return false;
  }

  std::error_code error;
  std::filesystem::create_directories(m_root, error);
  if (error)
  {
    return false;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out)
  {
    return false;
  }

  out.write(reinterpret_cast<const char*>(_file.data()), static_cast<std::streamsize>(_file.size()));
  out.close();
  return out.good();
}

bool SaveStore::Read(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
                     std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE> _outFile)
{
  const std::filesystem::path path = PathFor(_name);
  if (path.empty())
  {
    return false;
  }

  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size != _outFile.size())
  {
    // A file of the wrong length is refused here rather than read short and left to the
    // checksums, because a partial read would leave the caller's block half updated.
    return false;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return false;
  }

  in.read(reinterpret_cast<char*>(_outFile.data()), static_cast<std::streamsize>(_outFile.size()));
  return in.gcount() == static_cast<std::streamsize>(_outFile.size());
}

} // namespace Outpost
