// GameLogic/ReferenceImage.h
// The reference's load image, checked in (D36). Tools/GenerateReference.py generated it from ELITES.EXE and
// Design/Symbols.tsv until change 2 of D33's piece 3 deleted the tool, whose generators read this image instead
// (Tools/GenerateTables.py). Since D57 nothing holds it to the file's bytes (ADR-028).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Elite
{

/// The load image's size: the code segment, CS:0000-8F3F, then the data segment's image, DS:0000-EFDF. The game's state takes
/// the data segment's (ReferenceLoadImage); of the code segment, Tools/GenerateTables.py reads the bytes native code needs.
inline constexpr std::size_t REFERENCE_IMAGE_BYTES = 98'080;

/// The load image, as ELITES.EXE holds it, though since D57 it need not stay byte for byte the file's (ADR-028): DS:25E4
/// holds 00, and the host writes D5's byte after the load (ADR-001). Defined in ReferenceImage.cpp, so that its bytes are
/// compiled once.
extern const std::array<std::uint8_t, REFERENCE_IMAGE_BYTES> REFERENCE_IMAGE;

} // namespace Elite
