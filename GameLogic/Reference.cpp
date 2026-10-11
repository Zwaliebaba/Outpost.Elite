#include "pch.h"

#include "Reference.h"

#include "DataPlaces.h"
#include "GameData.h"
#include "Message.h"
#include "ReferenceImage.h"

#include <array>
#include <fstream>
#include <iterator>
#include <source_location>
#include <span>
#include <system_error>

namespace Elite
{

namespace
{

/// A word the image holds as an address where the game's state holds what it means (D33, piece 3): its offset in the 1987 data
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
  LoadedWord{OriginalOffset("messagePointer"), OriginalOffset("frontViewText"), static_cast<std::uint16_t>(Message::FrontView)},
  LoadedWord{OriginalOffset("keyBufferWrite"), OriginalOffset("keyBuffer"), 0},
  LoadedWord{OriginalOffset("keyBufferRead"), OriginalOffset("keyBuffer"), 0},
};

/// The place in GameData of the byte at the 1987 data segment's _offset, which the state must hold (DATA_PLACES).
[[nodiscard]] std::optional<std::size_t> PlaceOf(std::uint16_t _offset) noexcept
{
  for (const DataPlace& place : DATA_PLACES)
  {
    if (place.place != NO_PLACE && _offset >= place.original && _offset - place.original < place.bytes)
    {
      return std::size_t{place.place} + (_offset - place.original);
    }
  }
  return std::nullopt;
}

} // namespace

std::optional<std::vector<std::uint8_t>> ReferenceLoadImage()
{
  const std::span<const std::uint8_t> image = std::span<const std::uint8_t>(REFERENCE_IMAGE).subspan(CODE_SEGMENT_BYTES);
  // D5 (ADR-001) is written over the 00 the file holds, and each stored address must hold the address the file's does.
  if (image[D5_OFFSET] != 0x00)
  {
    return std::nullopt;
  }
  for (const LoadedWord& word : LOADED_WORDS)
  {
    if ((image[word.offset] | (image[word.offset + 1] << 8)) != word.image)
    {
      return std::nullopt;
    }
  }
  // Each member the state holds, from its 1987 offset in the image, zeros past the image's end, to its place (DATA_PLACES).
  std::vector<std::uint8_t> state(sizeof(GameData), 0);
  for (const DataPlace& place : DATA_PLACES)
  {
    if (place.place == NO_PLACE)
    {
      continue;
    }
    for (std::size_t index = 0; index < place.bytes && place.original + index < image.size(); ++index)
    {
      state[place.place + index] = image[place.original + index];
    }
  }
  // D5's byte, the protection's "already shown" flag; and the stored addresses, each loaded as what it means.
  const std::optional<std::size_t> d5 = PlaceOf(D5_OFFSET);
  if (!d5)
  {
    return std::nullopt;
  }
  state[*d5] = 0x01;
  for (const LoadedWord& word : LOADED_WORDS)
  {
    const std::optional<std::size_t> low = PlaceOf(word.offset);
    const std::optional<std::size_t> high = PlaceOf(static_cast<std::uint16_t>(word.offset + 1));
    if (!low || !high)
    {
      return std::nullopt;
    }
    state[*low] = static_cast<std::uint8_t>(word.loaded & 0xFF);
    state[*high] = static_cast<std::uint8_t>(word.loaded >> 8);
  }
  return state;
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
