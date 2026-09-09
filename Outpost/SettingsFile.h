#pragma once

#include "Scheduler.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Elite
{
  struct Universe;
}

namespace Outpost
{

  /*
   * The settings file: the thirteen configuration bytes the pause screen used to toggle, and
   * `DNOIZ` beside them (Design/InputTimer.md S-1).
   *
   * `DK4`'s pause screen was the game's only settings interface and it was removed by owner ruling
   * on 2026-09-08 (InputTimer.md §5.9, ADR-005 §4). The bytes it toggled are still `Universe`'s --
   * damping, auto-recentre, the author names that also change the spawner, the flashing bars, the
   * two joystick reversals, the docking music and its three companions, the media, the planet
   * detail -- and none of them is in the commander file, so without the screen nothing could set
   * them. This reads them from `Settings.txt` beside the commander folder once, at start-up, into
   * the same bytes `DKS3` wrote.
   *
   * IT IS `key = on|off` TEXT AND NOTHING ELSE, written by hand rather than through a library
   * (AGENTS.md §5), and a person can edit it. Every key is named for what it ENABLES, so `damping =
   * on` means damping, although the byte behind it (`DAMP`) is non-zero when damping is OFF: the
   * table below carries each byte's sense so the file does not have to. A key the file leaves out
   * keeps the game's own boot value, which is what the port already holds.
   *
   * A LINE THAT CANNOT BE USED IS A DIAGNOSTIC AND NOT A CRASH (AGENTS.md §5): it is reported with
   * its line number and skipped, and every other line is honoured. The parser is here rather than
   * in `Main.cpp` so that `ShellTests` covers it on both CI legs; only the message box that shows
   * the report is the window's.
   *
   * `machine = ntsc|pal` IS THE ONE KEY THAT IS NOT A BYTE OF THE GAME (Design/Platform.md T-1).
   * It chooses the 6510 and the VIC-II the port keeps time against: the flight loop's cost model,
   * the vertical blank `DELAY` counts and the sound interrupt's rate. NTSC is the default and the
   * default is not a preference -- ADR-001's context line records the masters as `_VARIANT=1`, the
   * GMA85 NTSC release -- but `wscan.asm` says the machine decides, so a player may say otherwise.
   * It is read BEFORE the universe exists, which is why this file has two halves: `ReadSettingsFile`
   * says what the file holds and `ApplySettings` writes the game's bytes once there are some.
   *
   * `JSTK` -- keyboard or joystick -- IS DELIBERATELY NOT A KEY. InputTimer.md §5.1 rules that the
   * game may believe a joystick is configured only when the platform has one to read, and until the
   * gamepad slice exists it has none; a `joystick = on` line would put `DOKEY` into the joystick
   * branch with nothing behind it (InputTimer.md I-4). The line is recognised so that the report
   * can say why it is ignored, rather than calling it unknown.
   */

  /// One key of the file and the byte it sets. Public aggregate, plain fields (AGENTS.md R5).
  struct SettingKey
  {
    const char* key = "";  ///< as written in the file, lower case, hyphenated
    const char* label = ""; ///< the 6502 name of the byte
    std::uint8_t onValue = 0;  ///< what the byte holds when the key is `on`
    std::uint8_t offValue = 0; ///< and when it is `off`
    const char* what = "";     ///< the comment the default file carries beside it
  };

  /// The keys, in the assembler's order of the block (`DAMP` to `MUSILLY`), then `DNOIZ`.
  [[nodiscard]] const SettingKey* SettingKeys() noexcept;
  [[nodiscard]] int SettingKeyCount() noexcept;

  /// The parsed file: a value per key where the file gave one, and every line it could not use.
  struct ParsedSettings
  {
    std::vector<std::optional<std::uint8_t>> values; ///< `SettingKeyCount()` entries, in `SettingKeys()` order
    std::vector<std::string> problems;               ///< "line N: ..." -- reported, never fatal

    /// `machine`, if the file named one. Not a `Universe` byte, so it is not in `values`: it is
    /// the platform's, and the platform is built before the game (Design/Platform.md T-1).
    std::optional<MachineTiming> machine;

    [[nodiscard]] std::optional<std::uint8_t> ValueOf(std::string_view _key) const noexcept;
  };

  /// Parse the file's text. Blank lines and `#` comments are skipped; `key = value`, case-insensitive,
  /// with `on|off`, `yes|no`, `true|false` or `1|0` as the value; a later line for the same key wins.
  [[nodiscard]] ParsedSettings ParseSettings(std::string_view _text);

  /// Write every value the file gave into the universe's own bytes. Absent keys are left alone.
  void ApplySettings(const ParsedSettings& _settings, Elite::Universe& _universe) noexcept;

  /// The file a player starts from: every key at the game's boot value, with its comment.
  [[nodiscard]] std::string DefaultSettingsText();

  /// What start-up hands the window: the problems, as one message, or an empty string for none.
  struct SettingsReport
  {
    std::filesystem::path path;        ///< where the file was looked for
    std::vector<std::string> problems; ///< `ParsedSettings::problems`, plus a read failure if there was one
    bool created = false;              ///< the file was absent and the default was written

    /// The machine to build the clock, the scheduler and the sound against -- the file's, or NTSC.
    MachineTiming timing = MachineTiming::Ntsc();

    /// What was read, for the caller to apply once it has a universe to apply it to.
    ParsedSettings parsed;

    [[nodiscard]] std::string Summary() const;
  };

  /*
   * Read `Settings.txt` beside `_commanders` (the commander folder `SaveStore::Root` names -- the
   * file sits in its parent, `Outpost.Elite`) and say what it holds.
   *
   * IT READS AND DOES NOT APPLY, since T-1, and the split is not tidiness: `machine` decides how
   * the clock, the scheduler and the sound are CONSTRUCTED, so it has to be known before any of
   * them exists -- and the universe those objects are built around does not exist yet either. The
   * caller reads first, builds the platform from `timing`, and then hands `parsed` to
   * `ApplySettings` when there is a universe. It was one call that did both until then.
   *
   * An absent file is written from `DefaultSettingsText` so that the player has something to edit,
   * and holds nothing. An empty `_commanders` -- no LocalAppData -- holds nothing and reports
   * nothing, which is `SaveStore`'s own rule for the same case. Reads through `<fstream>`, as the
   * store does, so this file compiles on the portable runner.
   */
  [[nodiscard]] SettingsReport ReadSettingsFile(const std::filesystem::path& _commanders);

} // namespace Outpost
