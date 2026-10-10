// GameLogic/Reference.h
#pragma once

#include "Dos.h"
#include "ExeLoader.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace Machine
{
class Pc;
}

namespace Elite
{

// ADR-001 and ADR-007: the reference binary, byte for byte, and the one byte the host changes in memory
// (D5), the protection's "already shown" flag at DS:25E4.
inline constexpr std::string_view REFERENCE_SHA256 = "18b5076a54733dea2d45b1e3b280fd1377067b6cb1b27166d3873aa7e7744363";
inline constexpr std::string_view REFERENCE_FILE_NAME = "ELITES.EXE";
inline constexpr std::uint16_t D5_OFFSET = 0x25E4;

// The image's layout: one code segment at paragraph 0 of the image, 0x8F40 bytes, and the data segment
// at image paragraph 0x08F4 (Reverse-Engineering-Plan.md §1).
inline constexpr std::uint16_t CODE_SEGMENT_BYTES = 0x8F40;
inline constexpr std::uint16_t DATA_SEGMENT_PARAGRAPH = 0x08F4;

// The stack segment, DS:AD60 (SS:0000) up to the initial SP of 03F8 and the rest of its paragraph. What
// lies below SP there is whatever the last calls and interrupts left, so the game-state digest skips it.
inline constexpr std::uint16_t STACK_FIRST_OFFSET = 0xAD60;
inline constexpr std::uint16_t STACK_END_OFFSET = 0xB160;

// Where DOSBox-X puts the PSP (measured from its trace), which the host copies so the two boot traces
// compare register for register (ADR-003). Replays use it too: one load address for everything.
inline constexpr std::uint16_t PSP_SEGMENT = 0x0813;

// The moment DOS's clock starts from: midnight on 1 January 1980, as on a PC/XT without a clock card, and
// as Tools/ReferenceScreens.py sets DOSBox-X's.
inline constexpr Machine::Dos::DateTime START_MOMENT = {1980, 1, 1, 0, 0, 0, 0};

// GetKey: the end of ADR-003's boot trace.
inline constexpr std::uint16_t GET_KEY_OFFSET = 0x7616;

/// Why LoadReference failed.
enum class ReferenceFailure : std::uint8_t
{
  None,
  NotTheReference, ///< The file's SHA-256 is not REFERENCE_SHA256.
  DidNotLoad,      ///< ExeLoader refused it, or the D5 byte did not hold 00.
};

/// Checks that _file is the reference, loads it as DOS would (ExeLoader) with the PSP at _pspSegment,
/// and writes the D5 byte. On failure nothing is started.
[[nodiscard]] ReferenceFailure LoadReference(Machine::Pc& _pc, std::span<const std::uint8_t> _file, std::uint16_t _pspSegment,
                                             Machine::LoadedProgram& _program);

/// The paragraph of the loaded program's data segment.
[[nodiscard]] constexpr std::uint16_t DataSegment(const Machine::LoadedProgram& _program) noexcept
{
  return static_cast<std::uint16_t>(_program.loadSegment + DATA_SEGMENT_PARAGRAPH);
}

/// The first of _name found walking up from the working directory, and then from the repository this
/// file was compiled in; empty if there is none. How the tests and tools find ELITES.EXE and Replays/.
[[nodiscard]] std::filesystem::path FindInRepository(const std::filesystem::path& _name);

/// A whole file, or nothing if it cannot be read.
[[nodiscard]] std::vector<std::uint8_t> ReadWholeFile(const std::filesystem::path& _path);

} // namespace Elite
