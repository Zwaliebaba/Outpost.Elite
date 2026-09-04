#include "pch.h"

#include "GoldenCanvas.h"

#include "OracleImage.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Elite::Testing
{

  namespace
  {
    /// The sixteen VIC-II colours, as 24-bit RGB, for the PNG palette. Approximate on purpose: the
    /// palette is a viewing aid for a person looking at a diff, and the comparison itself is on
    /// colour INDICES, which are exact.
    constexpr std::uint8_t PALETTE[16][3] = {
      {0x00, 0x00, 0x00}, {0xFF, 0xFF, 0xFF}, {0x88, 0x39, 0x32}, {0x67, 0xB6, 0xBD}, {0x8B, 0x3F, 0x96}, {0x55, 0xA0, 0x49},
      {0x40, 0x31, 0x8D}, {0xBF, 0xCE, 0x72}, {0x8B, 0x54, 0x29}, {0x57, 0x42, 0x00}, {0xB8, 0x69, 0x62}, {0x50, 0x50, 0x50},
      {0x78, 0x78, 0x78}, {0x94, 0xE0, 0x89}, {0x78, 0x69, 0xC4}, {0x9F, 0x9F, 0x9F},
    };

    std::uint32_t Crc32(std::span<const std::uint8_t> _bytes, std::uint32_t _seed = 0)
    {
      std::uint32_t crc = ~_seed;
      for (const std::uint8_t byte : _bytes)
      {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
        {
          crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
      }
      return ~crc;
    }

    void AppendBigEndian(std::vector<std::uint8_t>& _out, std::uint32_t _value)
    {
      _out.push_back(static_cast<std::uint8_t>(_value >> 24));
      _out.push_back(static_cast<std::uint8_t>(_value >> 16));
      _out.push_back(static_cast<std::uint8_t>(_value >> 8));
      _out.push_back(static_cast<std::uint8_t>(_value));
    }

    void AppendChunk(std::vector<std::uint8_t>& _out, const char* _type, const std::vector<std::uint8_t>& _data)
    {
      AppendBigEndian(_out, static_cast<std::uint32_t>(_data.size()));

      std::vector<std::uint8_t> body;
      body.insert(body.end(), _type, _type + 4);
      body.insert(body.end(), _data.begin(), _data.end());

      _out.insert(_out.end(), body.begin(), body.end());
      AppendBigEndian(_out, Crc32(body));
    }

    /// A zlib stream of STORED deflate blocks. No compression: this is a debugging artefact written
    /// only when a test has already failed, and a compressor would be a second thing to get wrong.
    std::vector<std::uint8_t> StoredZlib(const std::vector<std::uint8_t>& _raw)
    {
      std::vector<std::uint8_t> out{0x78, 0x01};

      std::size_t offset = 0;
      while (offset < _raw.size())
      {
        const std::size_t block = std::min<std::size_t>(65535, _raw.size() - offset);
        const bool last = (offset + block) >= _raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(block));
        out.push_back(static_cast<std::uint8_t>(block >> 8));
        out.push_back(static_cast<std::uint8_t>(~block));
        out.push_back(static_cast<std::uint8_t>((~block) >> 8));
        out.insert(out.end(), _raw.begin() + offset, _raw.begin() + offset + block);
        offset += block;
      }

      std::uint32_t a = 1;
      std::uint32_t b = 0;
      for (const std::uint8_t byte : _raw)
      {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
      }
      AppendBigEndian(out, (b << 16) | a);
      return out;
    }

    std::filesystem::path OutputDirectory()
    {
      const std::filesystem::path directory = std::filesystem::temp_directory_path() / "OutpostElite-goldens";
      std::error_code error;
      std::filesystem::create_directories(directory, error);
      return directory;
    }
  } // namespace

  void LoadScreenFromOracle(const std::array<std::uint8_t, 65536>& _memory, std::uint16_t _base, std::uint8_t _colourRam,
                            Canvas& _outCanvas)
  {
    for (std::uint16_t offset = 0; offset < Canvas::SCREEN_SIZE; ++offset)
    {
      _outCanvas.Write(offset, _memory[static_cast<std::uint16_t>(_base + offset)]);
    }
    for (int cell = 0; cell < Canvas::CELL_COLUMNS * Canvas::CELL_ROWS; ++cell)
    {
      _outCanvas.SetCellColour(cell, _colourRam);
    }
  }

  std::string WriteCanvasPng(const Canvas& _canvas, const std::string& _name)
  {
    std::array<std::uint8_t, Canvas::WIDTH * Canvas::HEIGHT> indices{};
    _canvas.Resolve(indices);

    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(Canvas::HEIGHT) * (Canvas::WIDTH + 1));
    for (int y = 0; y < Canvas::HEIGHT; ++y)
    {
      raw.push_back(0); // filter: none
      raw.insert(raw.end(), indices.begin() + y * Canvas::WIDTH, indices.begin() + (y + 1) * Canvas::WIDTH);
    }

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<std::uint8_t> header;
    AppendBigEndian(header, Canvas::WIDTH);
    AppendBigEndian(header, Canvas::HEIGHT);
    header.push_back(8); // bit depth
    header.push_back(3); // colour type: indexed
    header.push_back(0);
    header.push_back(0);
    header.push_back(0);
    AppendChunk(png, "IHDR", header);

    std::vector<std::uint8_t> palette;
    for (const auto& colour : PALETTE)
    {
      palette.insert(palette.end(), colour, colour + 3);
    }
    AppendChunk(png, "PLTE", palette);
    AppendChunk(png, "IDAT", StoredZlib(raw));
    AppendChunk(png, "IEND", {});

    const std::filesystem::path path = OutputDirectory() / (_name + ".png");
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
      return {};
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return path.string();
  }

  std::string CompareCanvasImages(const Canvas& _expected, const Canvas& _actual, const std::string& _name)
  {
    std::array<std::uint8_t, Canvas::WIDTH * Canvas::HEIGHT> want{};
    std::array<std::uint8_t, Canvas::WIDTH * Canvas::HEIGHT> got{};
    _expected.Resolve(want);
    _actual.Resolve(got);

    std::size_t differing = 0;
    std::size_t first = 0;
    for (std::size_t index = 0; index < want.size(); ++index)
    {
      if (want[index] != got[index])
      {
        if (differing == 0)
        {
          first = index;
        }
        ++differing;
      }
    }

    if (differing == 0)
    {
      return {};
    }

    const std::string expectedPath = WriteCanvasPng(_expected, _name + "-expected");
    const std::string actualPath = WriteCanvasPng(_actual, _name + "-actual");

    return _name + ": " + std::to_string(differing) + " pixel(s) differ, first at (" + std::to_string(first % Canvas::WIDTH) + ", " +
           std::to_string(first / Canvas::WIDTH) + ") -- game has " + std::to_string(want[first]) + ", port has " +
           std::to_string(got[first]) + "\n  expected: " + expectedPath + "\n  actual:   " + actualPath +
           "\n  diff:     python tools/golden_diff.py \"" + expectedPath + "\" \"" + actualPath + "\"";
  }

} // namespace Elite::Testing
