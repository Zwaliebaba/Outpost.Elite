#include "pch.h"

#include "FileStore.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace Elite
{

namespace
{

constexpr std::size_t BASE_CHARACTERS = 8;
constexpr std::size_t EXTENSION_CHARACTERS = 3;
constexpr std::uint64_t CENTISECONDS_PER_DAY = 8'640'000;
constexpr int FIRST_STAMP_YEAR = 1980;
constexpr int LAST_STAMP_YEAR = 2107;

constexpr char ToUpper(char _character) noexcept
{
  return _character >= 'a' && _character <= 'z' ? static_cast<char>(_character - 'a' + 'A') : _character;
}

} // namespace

DosDateTime MomentAfter(const DosDateTime& _start, std::uint64_t _centiseconds)
{
  const std::uint64_t startOfDay = ((std::uint64_t{_start.hour} * 60u + _start.minute) * 60u + _start.second) * 100u + _start.hundredths;
  const std::uint64_t total = startOfDay + _centiseconds;
  std::uint64_t timeOfDay = total % CENTISECONDS_PER_DAY;

  const std::chrono::sys_days startDate{std::chrono::year{_start.year} / std::chrono::month{_start.month} / std::chrono::day{_start.day}};
  const auto days = static_cast<std::chrono::days::rep>(total / CENTISECONDS_PER_DAY);
  const std::chrono::year_month_day date{startDate + std::chrono::days{days}};

  DosDateTime now;
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

FileStamp StampOf(const DosDateTime& _moment) noexcept
{
  const int year = std::clamp(static_cast<int>(_moment.year), FIRST_STAMP_YEAR, LAST_STAMP_YEAR);
  FileStamp stamp;
  stamp.date = static_cast<std::uint16_t>(((year - FIRST_STAMP_YEAR) << 9) | (_moment.month << 5) | _moment.day);
  stamp.time = static_cast<std::uint16_t>((_moment.hour << 11) | (_moment.minute << 5) | (_moment.second / 2));
  return stamp;
}

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

} // namespace Elite
