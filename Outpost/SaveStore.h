#pragma once

#include "SaveGame.h"

#include <filesystem>
#include <string_view>

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
 * THERE IS NO ORACLE FOR THIS FILE, which is not the same as there being no test -- and the
 * distinction was worth a day. The format, the checksums, the competition number and the failure
 * paths are all compared against the shipped routines in `GameLogicTests`; what is left here is a
 * path and two streams, and this header used to say that made it untestable. It did not. The
 * portable runner needed ten lines of Win32 stand-in to compile this file on a machine with no
 * Windows SDK, and the first test written against it found a defect: `PathFor` accepted "CON",
 * which Win32 resolves to the console device whatever directory and extension surround it.
 *
 * So the seam is still drawn here for the reason `check_gamelogic.py` enforces -- `GameLogic` may
 * not touch a disk -- and not because what is on this side of it cannot be checked.
 */
class SaveStore : public Elite::CommanderStore
{
public:
  /// Finds the folder commanders are kept in, from LOCALAPPDATA. Does not create it; the first
  /// write does.
  SaveStore();

  /// The same store rooted somewhere chosen by the caller, which is how the suite reaches it
  /// without writing into the machine's own LocalAppData.
  explicit SaveStore(std::filesystem::path _root) noexcept
    : m_root(std::move(_root))
  {
  }

  bool Write(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
             std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE> _file) override;

  bool Read(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
            std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE> _outFile) override;

  /// Where the files live. Empty when the folder could not be determined, in which case every
  /// call fails rather than falling back to somewhere the player would not think to look.
  [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_root; }

  /*
   * True for the stems Win32 resolves to a DEVICE rather than to a file.
   *
   * CON, PRN, AUX, NUL, COM1 to COM9 and LPT1 to LPT9, matched without regard to case and
   * whatever directory precedes them or extension follows. Public because the suite walks the
   * list rather than trusting a comment that says it is complete.
   */
  [[nodiscard]] static bool IsReservedDeviceName(std::wstring_view _stem) noexcept;

private:
  /*
   * The commander's name as a filename, or an empty path if it cannot be one.
   *
   * The name comes out of a FILE as well as off the keyboard -- `DFAULT` loads eight bytes and
   * the game trusts them -- so this is the one place in the port where untrusted bytes would
   * become a path. Only letters and digits are accepted and the rest is refused outright: no
   * separators, no dots, no traversal. The line editor already limits what can be typed, and this
   * does not rely on that.
   *
   * ALPHANUMERIC IS NOT ENOUGH, which the first test written against this found. Every reserved
   * device name is made of letters and digits, is seven characters or fewer, and is typeable at
   * the name prompt -- so a player calling themselves CON would have had their commander written
   * to the console and read back from it, with the write reporting success.
   */
  [[nodiscard]] std::filesystem::path PathFor(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name) const;

  std::filesystem::path m_root;
};

} // namespace Outpost
