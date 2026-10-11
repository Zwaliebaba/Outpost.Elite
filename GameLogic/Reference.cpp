#include "pch.h"

#include "Reference.h"

#include "ReferenceImage.h"

#include <fstream>
#include <iterator>
#include <source_location>
#include <system_error>

namespace Elite
{

std::optional<std::vector<std::uint8_t>> ReferenceLoadImage(std::uint16_t _loadSegment)
{
  constexpr std::size_t PARAGRAPH_BYTES = 16;
  std::vector<std::uint8_t> image(REFERENCE_IMAGE.begin(), REFERENCE_IMAGE.end());
  // Each relocation's word, at its segment and offset from the image's start, holds a segment number the load adds the load
  // segment to, as EXEC does.
  for (const Relocation& relocation : REFERENCE_RELOCATIONS)
  {
    const std::size_t at = std::size_t{relocation.segment} * PARAGRAPH_BYTES + relocation.offset;
    const auto word = static_cast<std::uint16_t>((image[at] | (image[at + 1] << 8)) + _loadSegment);
    image[at] = static_cast<std::uint8_t>(word & 0xFF);
    image[at + 1] = static_cast<std::uint8_t>(word >> 8);
  }
  // D5 (ADR-001): the protection's "already shown" flag, written over the 00 the file holds.
  const std::size_t d5 = std::size_t{DATA_SEGMENT_PARAGRAPH} * PARAGRAPH_BYTES + D5_OFFSET;
  if (image[d5] != 0x00)
  {
    return std::nullopt;
  }
  image[d5] = 0x01;
  return image;
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
