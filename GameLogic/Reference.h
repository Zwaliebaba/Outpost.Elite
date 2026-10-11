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

// ADR-001 and ADR-007: the reference binary, byte for byte, and the one byte the host changes in memory
// (D5), the protection's "already shown" flag at DS:25E4. Nothing reads the file (D3, D36), and since D28 it is not in the tree
// (ADR-020): its load image is checked in (ReferenceImage.h), and since D57 nothing holds it to the file's bytes (ADR-028).
inline constexpr std::uint16_t D5_OFFSET = 0x25E4;

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

/// The game's state as DOS's EXEC leaves the reference's data segment, GameData's bytes: each member the state holds from its
/// 1987 offset in the load image (ReferenceImage.h) past its code segment, zeros past the image's end, at its place in GameData
/// (DATA_PLACES, D38); the D5 byte written; and the words the image holds as addresses where the game's state holds a message
/// or an index loaded as what they mean (D33, piece 3). The code segment's relocations change only its own words, which the
/// game's state does not hold. Nothing if the D5 byte did not hold 00, as the file's does, or one of those words did not hold
/// its address.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReferenceLoadImage();

/// The first of _name found walking up from the working directory, and then from the repository this
/// file was compiled in; empty if there is none. How the tests find the subsystems' known answers.
[[nodiscard]] std::filesystem::path FindInRepository(const std::filesystem::path& _name);

/// A whole file, or nothing if it cannot be read.
[[nodiscard]] std::vector<std::uint8_t> ReadWholeFile(const std::filesystem::path& _path);

} // namespace Elite
