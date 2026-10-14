// GameLogic/Reference.h
#pragma once

#include "DosFiles.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Elite
{

// The image's layout: one code segment at paragraph 0 of the image, 0x8F40 bytes, and the data segment
// at image paragraph 0x08F4 (Reverse-Engineering-Plan.md §1).
inline constexpr std::uint16_t CODE_SEGMENT_BYTES = 0x8F40;
inline constexpr std::uint16_t DATA_SEGMENT_PARAGRAPH = 0x08F4;

// Where DOSBox-X puts the PSP (measured from its trace), which the host copied so the two boot traces
// compared register for register (ADR-003). Replays use it too: one load address for everything.
inline constexpr std::uint16_t PSP_SEGMENT = 0x0813;

// A PSP's size in paragraphs: DOS loads the image at the paragraph after it.
inline constexpr std::uint16_t PSP_PARAGRAPHS = 0x10;

// The moment DOS's clock starts from: midnight on 1 January 1980, as on a PC/XT without a clock card.
inline constexpr DosDateTime START_MOMENT = {1980, 1, 1, 0, 0, 0, 0};

/// The first of _name found walking up from the working directory, and then from the repository this
/// file was compiled in; empty if there is none. How the tests and tools find Replays/ and the known answers.
[[nodiscard]] std::filesystem::path FindInRepository(const std::filesystem::path& _name);

/// A whole file, or nothing if it cannot be read.
[[nodiscard]] std::vector<std::uint8_t> ReadWholeFile(const std::filesystem::path& _path);

} // namespace Elite
