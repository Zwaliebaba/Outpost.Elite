#pragma once

#include "SaveGame.h"

#include <filesystem>

namespace Outpost
{

/*
 * Where a commander file actually goes (slice 2d).
 *
 * 6502: KERNALSVE and KERNALLOAD. The C64 handed the Kernal a filename and an address range; this
 * writes the same eighty-five bytes to a file under LocalAppData, and the bytes are byte-for-byte
 * what the original wrote -- `GameLogic` builds the image and compares it against the shipped
 * routine, so a commander saved here is one an emulator running the original would load.
 *
 * IT LIVES HERE AND NOT IN `GameLogic` for a reason the project enforces rather than intends:
 * `tools/check_gamelogic.py` fails the build if `GameLogic` grows file access, because a routine
 * that reached for a disk could not be compared against the original inside an interpreter that
 * has none. ADR-004 §1 names this file as the executable's from the start.
 *
 * NOTHING IN THIS FILE IS COVERED BY THE SUITE. It is the one part of the save flow with no
 * oracle: the format, the checksums, the competition number and the failure paths are all tested
 * against the shipped routines in `GameLogicTests`, and what is left here is a path and two
 * streams. That is the whole reason the seam is drawn where it is.
 */
class SaveStore : public Elite::CommanderStore
{
public:
  /// Finds the folder commanders are kept in. Does not create it; the first write does.
  SaveStore();

  bool Write(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
             std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE> _file) override;

  bool Read(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
            std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE> _outFile) override;

  /// Where the files live. Empty when the folder could not be determined, in which case every
  /// call fails rather than falling back to somewhere the player would not think to look.
  [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_root; }

private:
  /*
   * The commander's name as a filename, or an empty path if it cannot be one.
   *
   * The name comes out of a FILE as well as off the keyboard -- `DFAULT` loads eight bytes and
   * the game trusts them -- so this is the one place in the port where untrusted bytes would
   * become a path. Only letters and digits are accepted and the rest is refused outright: no
   * separators, no dots, no traversal, and nothing that could name a device. The line editor
   * already limits what can be typed, and this does not rely on that.
   */
  [[nodiscard]] std::filesystem::path PathFor(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name) const;

  std::filesystem::path m_root;
};

} // namespace Outpost
