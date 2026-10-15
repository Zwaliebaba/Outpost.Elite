// Tests/OutpostTests/GoldenImage.h
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OutpostTests
{

/// A screen's golden image (§6.5, D94): what a screen drawn on WARP from a prepared view is compared with. The test writes what
/// it drew to RENDERED_DIRECTORY, which CI keeps as an artifact; the owner looks at it and commits it to GOLDEN_DIRECTORY as the
/// golden. A missing golden or a difference is reported, not failed: WARP's output can change with the Windows build, and a
/// new screen has no golden until the owner has looked at it (D94). A golden that cannot be read fails.
class GoldenImage
{
public:
  /// From the repository's root: where the goldens are, and where a run writes what it drew.
  static constexpr std::string_view GOLDEN_DIRECTORY = "Tests/OutpostTests/Golden";
  static constexpr std::string_view RENDERED_DIRECTORY = "x64/Rendered";

  /// A picture of 0x00RRGGBB pixels, row after row.
  struct Picture
  {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> pixels;
  };

  /// _picture as a PNG at _path, through WIC. Returns false, with why in _error, when it cannot.
  [[nodiscard]] static bool Save(const std::filesystem::path& _path, const Picture& _picture, std::wstring& _error);

  /// The PNG at _path, or nothing, with why in _error.
  [[nodiscard]] static std::optional<Picture> Load(const std::filesystem::path& _path, std::wstring& _error);

  /// The pixels of _found more than _tolerance from _golden in any channel; every pixel when the sizes differ.
  [[nodiscard]] static std::size_t Differences(const Picture& _found, const Picture& _golden, int _tolerance);

  /// _picture, named _name, written to RENDERED_DIRECTORY and compared with its golden: the test fails when the golden
  /// cannot be read, and writes a missing golden or a difference to its log.
  static void Check(std::string_view _name, const Picture& _picture);
};

} // namespace OutpostTests
