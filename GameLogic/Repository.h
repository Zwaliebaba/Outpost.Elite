// GameLogic/Repository.h
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Elite
{

/// The first of _name found walking up from the working directory, and then from the repository this
/// file was compiled in; empty if there is none. How the tests and tools find Replays/ and the known answers.
[[nodiscard]] std::filesystem::path FindInRepository(const std::filesystem::path& _name);

/// A whole file, or nothing if it cannot be read.
[[nodiscard]] std::vector<std::uint8_t> ReadWholeFile(const std::filesystem::path& _path);

} // namespace Elite
