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
// (D5), the protection's "already shown" flag at DS:25E4. Nothing reads the file at runtime (D3), and since D28 it
// is not in the tree (ADR-020): the image is generated from a local copy (ReferenceImage.h), Tools/GenerateReference.py
// refuses any other file by this hash, and ReferenceImageTests holds the image to the hash measured from it.
inline constexpr std::string_view REFERENCE_SHA256 = "18b5076a54733dea2d45b1e3b280fd1377067b6cb1b27166d3873aa7e7744363";
inline constexpr std::uint16_t D5_OFFSET = 0x25E4;

// The image's layout: one code segment at paragraph 0 of the image, 0x8F40 bytes, and the data segment
// at image paragraph 0x08F4 (Reverse-Engineering-Plan.md §1).
inline constexpr std::uint16_t CODE_SEGMENT_BYTES = 0x8F40;
inline constexpr std::uint16_t DATA_SEGMENT_PARAGRAPH = 0x08F4;

// The stack segment, DS:AD60 (SS:0000) up to the initial SP of 03F8 and the rest of its paragraph. What
// lies below SP there is whatever the last calls and interrupts left, so the game-state digest skips it,
// and with it triangleEdgeMasks at its foot (DS:AD60-AD6F), which is constant.
inline constexpr std::uint16_t STACK_FIRST_OFFSET = 0xAD60;
inline constexpr std::uint16_t STACK_END_OFFSET = 0xB160;

// Where DOSBox-X puts the PSP (measured from its trace), which the host copied so the two boot traces
// compared register for register (ADR-003). Replays use it too: one load address for everything, and the
// game keeps the segment in its data (pspSegment), so it is part of the digest.
inline constexpr std::uint16_t PSP_SEGMENT = 0x0813;

// A PSP's size in paragraphs: DOS loads the image at the paragraph after it.
inline constexpr std::uint16_t PSP_PARAGRAPHS = 0x10;

// The moment DOS's clock starts from: midnight on 1 January 1980, as on a PC/XT without a clock card.
inline constexpr DosDateTime START_MOMENT = {1980, 1, 1, 0, 0, 0, 0};

// GetKey: the end of ADR-003's boot trace.
inline constexpr std::uint16_t GET_KEY_OFFSET = 0x7616;

/// The reference's load image as DOS's EXEC leaves it with the image at _loadSegment, from its generated image
/// (ReferenceImage.h): the relocations applied for that segment, and the D5 byte written. Nothing if the D5 byte
/// did not hold 00, as the file's does.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReferenceLoadImage(std::uint16_t _loadSegment);

/// The first of _name found walking up from the working directory, and then from the repository this
/// file was compiled in; empty if there is none. How the tests and tools find Replays/ and the known answers.
[[nodiscard]] std::filesystem::path FindInRepository(const std::filesystem::path& _name);

/// A whole file, or nothing if it cannot be read.
[[nodiscard]] std::vector<std::uint8_t> ReadWholeFile(const std::filesystem::path& _path);

} // namespace Elite
