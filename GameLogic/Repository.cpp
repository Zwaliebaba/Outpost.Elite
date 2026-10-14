#include "pch.h"

#include "Repository.h"

#include <fstream>
#include <iterator>
#include <source_location>
#include <system_error>

namespace Elite
{

std::filesystem::path FindInRepository(const std::filesystem::path& _name)
{
  std::vector<std::filesystem::path> roots;
  std::error_code error;
  for (std::filesystem::path directory = std::filesystem::current_path(error); !error && !directory.empty();
       directory = directory.parent_path())
  {
    roots.push_back(directory);
    if (directory == directory.parent_path())
    {
      break;
    }
  }
  const std::filesystem::path source = std::source_location::current().file_name();
  roots.push_back(source.parent_path().parent_path());
  for (const std::filesystem::path& root : roots)
  {
    std::error_code found;
    if (std::filesystem::exists(root / _name, found))
    {
      return root / _name;
    }
  }
  return {};
}

std::vector<std::uint8_t> ReadWholeFile(const std::filesystem::path& _path)
{
  std::ifstream stream(_path, std::ios::binary);
  if (!stream)
  {
    return {};
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace Elite
