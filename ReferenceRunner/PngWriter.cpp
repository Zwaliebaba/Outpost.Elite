#include "pch.h"

#include "PngWriter.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <string_view>
#include <vector>

namespace ReferenceRunner
{

namespace
{

constexpr std::size_t STORED_BLOCK_BYTES = 0xFFFF;

constexpr std::array<std::uint32_t, 256> MakeCrcTable() noexcept
{
  std::array<std::uint32_t, 256> table = {};
  for (std::uint32_t entry = 0; entry < table.size(); ++entry)
  {
    std::uint32_t crc = entry;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 1) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    table[entry] = crc;
  }
  return table;
}

constexpr std::array<std::uint32_t, 256> CRC_TABLE = MakeCrcTable();

void AppendBigEndian32(std::vector<std::uint8_t>& _out, std::uint32_t _value)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    _out.push_back(static_cast<std::uint8_t>(_value >> shift));
}

// One chunk: length, type, data, and the CRC-32 of type and data (PNG §5.3).
void AppendChunk(std::vector<std::uint8_t>& _out, std::string_view _type, std::span<const std::uint8_t> _data)
{
  AppendBigEndian32(_out, static_cast<std::uint32_t>(_data.size()));
  std::uint32_t crc = 0xFFFFFFFFu;
  const auto feed = [&](std::uint8_t _byte)
  {
    _out.push_back(_byte);
    crc = CRC_TABLE[(crc ^ _byte) & 0xFF] ^ (crc >> 8);
  };
  for (const char letter : _type)
    feed(static_cast<std::uint8_t>(letter));
  for (const std::uint8_t byte : _data)
    feed(byte);
  AppendBigEndian32(_out, crc ^ 0xFFFFFFFFu);
}

// A zlib stream (RFC 1950) of stored deflate blocks (RFC 1951 §3.2.4) and its Adler-32.
std::vector<std::uint8_t> StoredZlib(std::span<const std::uint8_t> _data)
{
  std::vector<std::uint8_t> stream = {0x78, 0x01};
  std::uint32_t adlerLow = 1;
  std::uint32_t adlerHigh = 0;
  std::size_t offset = 0;
  do
  {
    const std::size_t length = std::min(STORED_BLOCK_BYTES, _data.size() - offset);
    const bool last = offset + length == _data.size();
    stream.push_back(last ? 1 : 0);
    stream.push_back(static_cast<std::uint8_t>(length & 0xFF));
    stream.push_back(static_cast<std::uint8_t>(length >> 8));
    stream.push_back(static_cast<std::uint8_t>(~length & 0xFF));
    stream.push_back(static_cast<std::uint8_t>((~length >> 8) & 0xFF));
    for (const std::uint8_t byte : _data.subspan(offset, length))
    {
      stream.push_back(byte);
      adlerLow = (adlerLow + byte) % 65521;
      adlerHigh = (adlerHigh + adlerLow) % 65521;
    }
    offset += length;
  } while (offset < _data.size());
  AppendBigEndian32(stream, (adlerHigh << 16) | adlerLow);
  return stream;
}

} // namespace

bool WritePalettePng(const std::filesystem::path& _path, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                     std::span<const std::uint8_t> _pixels, std::span<const std::uint32_t> _paletteRgb)
{
  if (_widthPixels == 0 || _heightPixels == 0 || _pixels.size() != std::size_t{_widthPixels} * _heightPixels || _paletteRgb.empty() ||
      _paletteRgb.size() > 256)
    return false;

  std::vector<std::uint8_t> header;
  AppendBigEndian32(header, _widthPixels);
  AppendBigEndian32(header, _heightPixels);
  header.insert(header.end(), {8, 3, 0, 0, 0}); // 8 bits, palette, deflate, adaptive filtering, no interlace

  std::vector<std::uint8_t> palette;
  for (const std::uint32_t rgb : _paletteRgb)
    palette.insert(palette.end(),
                   {static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb)});

  // Every row starts with its filter type; 0 leaves the bytes as they are.
  std::vector<std::uint8_t> rows;
  rows.reserve((std::size_t{_widthPixels} + 1) * _heightPixels);
  for (std::uint32_t row = 0; row < _heightPixels; ++row)
  {
    rows.push_back(0);
    const auto line = _pixels.subspan(std::size_t{row} * _widthPixels, _widthPixels);
    rows.insert(rows.end(), line.begin(), line.end());
  }

  std::vector<std::uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  AppendChunk(file, "IHDR", header);
  AppendChunk(file, "PLTE", palette);
  AppendChunk(file, "IDAT", StoredZlib(rows));
  AppendChunk(file, "IEND", {});

  std::ofstream out(_path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  return static_cast<bool>(out);
}

} // namespace ReferenceRunner
