#include "pch.h"

#include "Reference.h"

#include "GameData.h"
#include "ReferenceImage.h"

#include <array>
#include <fstream>
#include <iterator>
#include <source_location>
#include <system_error>

namespace Elite
{

namespace
{

/// A word the image holds as an address where the game's state holds what it means (D33, piece 3): at offset in the data
/// segment, the address the image holds, and what the load puts there in its place.
struct LoadedWord
{
  std::uint16_t offset;
  std::uint16_t image;
  std::uint16_t loaded;
};

/// The image's stored addresses that are not 0: the message line's message, Front View, and keyBuffer's ring, at its start.
/// The rest are 0, which each field reads as its own 0, or are written before they are read.
constexpr std::array<LoadedWord, 3> LOADED_WORDS = {
  LoadedWord{static_cast<std::uint16_t>(GAME_DATA_OFFSET(messagePointer)), static_cast<std::uint16_t>(GAME_DATA_OFFSET(frontViewText)),
             static_cast<std::uint16_t>(Message::FrontView)},
  LoadedWord{static_cast<std::uint16_t>(GAME_DATA_OFFSET(keyBufferWrite)), static_cast<std::uint16_t>(GAME_DATA_OFFSET(keyBuffer)), 0},
  LoadedWord{static_cast<std::uint16_t>(GAME_DATA_OFFSET(keyBufferRead)), static_cast<std::uint16_t>(GAME_DATA_OFFSET(keyBuffer)), 0},
};

} // namespace

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
  // The stored addresses, each loaded as what it means, and each holding the address the file's does.
  for (const LoadedWord& word : LOADED_WORDS)
  {
    const std::size_t at = std::size_t{DATA_SEGMENT_PARAGRAPH} * PARAGRAPH_BYTES + word.offset;
    if ((image[at] | (image[at + 1] << 8)) != word.image)
    {
      return std::nullopt;
    }
    image[at] = static_cast<std::uint8_t>(word.loaded & 0xFF);
    image[at + 1] = static_cast<std::uint8_t>(word.loaded >> 8);
  }
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
