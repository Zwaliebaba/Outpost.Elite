// ReferenceRunner/PngWriter.h
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace ReferenceRunner
{

/// Writes a palette image as a PNG: 8 bits per pixel, colour type 3, one PLTE entry per palette
/// colour. The pixels go in stored (uncompressed) deflate blocks, which every PNG reader accepts and
/// which needs no compressor (R14). A 640x200 frame is 128 KB on disk.
///
/// `_pixels` holds `_widthPixels * _heightPixels` palette indices, row by row from the top, and
/// `_paletteRgb` holds up to 256 colours as 0xRRGGBB. Returns false if the file cannot be written or
/// the sizes do not agree.
[[nodiscard]] bool WritePalettePng(const std::filesystem::path& _path, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                                   std::span<const std::uint8_t> _pixels, std::span<const std::uint32_t> _paletteRgb);

} // namespace ReferenceRunner
