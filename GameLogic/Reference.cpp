#include "pch.h"

#include "Reference.h"

#include "Pacing.h"
#include "Pc.h"
#include "Sha256.h"

#include <fstream>
#include <iterator>
#include <source_location>
#include <system_error>

namespace Elite
{

ReferenceFailure LoadReference(Machine::Pc& _pc, std::span<const std::uint8_t> _file, std::uint16_t _pspSegment,
                               Machine::LoadedProgram& _program)
{
  if (Machine::Sha256::ToHex(Machine::Sha256::Of(_file)) != REFERENCE_SHA256)
  {
    return ReferenceFailure::NotTheReference;
  }
  Machine::ExeLoader::Desc load;
  load.pspSegment = _pspSegment;
  if (_pc.Load(_file, load, _program) != Machine::LoadError::None)
  {
    return ReferenceFailure::DidNotLoad;
  }
  if (!Machine::ExeLoader::PatchByte(_pc.Ram(), _program, DATA_SEGMENT_PARAGRAPH, D5_OFFSET, 0x00, 0x01))
  {
    return ReferenceFailure::DidNotLoad;
  }
  InstallPacing(_pc, _program);
  return ReferenceFailure::None;
}

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
