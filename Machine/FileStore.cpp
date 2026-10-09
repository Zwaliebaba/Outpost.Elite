#include "pch.h"

#include "FileStore.h"

#include <utility>

namespace Machine
{

namespace
{

constexpr std::size_t BASE_CHARACTERS = 8;
constexpr std::size_t EXTENSION_CHARACTERS = 3;

constexpr char ToUpper(char _character) noexcept
{
  return _character >= 'a' && _character <= 'z' ? static_cast<char>(_character - 'a' + 'A') : _character;
}

} // namespace

bool FileStore::IsNameCharacter(char _character) noexcept
{
  const auto code = static_cast<unsigned char>(_character);
  if (code <= 0x20 || code >= 0x7F)
  {
    return false;
  }
  constexpr std::string_view RESERVED = "\"*+,./:;<=>?[\\]|";
  return RESERVED.find(_character) == std::string_view::npos;
}

DosError FileStore::CanonicalName(std::string_view _name, std::string& _canonical)
{
  if (_name.find_first_of("\\/:") != std::string_view::npos || _name == "." || _name == "..")
  {
    return DosError::PathNotFound;
  }
  const std::size_t dot = _name.find('.');
  const std::string_view base = _name.substr(0, dot);
  const std::string_view extension = dot == std::string_view::npos ? std::string_view{} : _name.substr(dot + 1);
  if (base.empty() || base.size() > BASE_CHARACTERS || extension.size() > EXTENSION_CHARACTERS)
  {
    return DosError::FileNotFound;
  }
  std::string canonical;
  for (const char character : base)
  {
    if (!IsNameCharacter(character))
    {
      return DosError::FileNotFound;
    }
    canonical.push_back(ToUpper(character));
  }
  if (!extension.empty())
  {
    canonical.push_back('.');
    for (const char character : extension)
    {
      if (!IsNameCharacter(character))
      {
        return DosError::FileNotFound;
      }
      canonical.push_back(ToUpper(character));
    }
  }
  _canonical = std::move(canonical);
  return DosError::None;
}

} // namespace Machine
