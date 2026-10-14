// GameLogic/Reference.h
#pragma once

#include "FileStore.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Elite
{

// Where the 1987 program's data segment began, in paragraphs past its code segment's (Reverse-Engineering-Plan.md §1): the
// segment values the game's state still carries until step 6 of Phase 7.
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
