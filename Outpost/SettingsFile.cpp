#include "pch.h"

#include "SettingsFile.h"

#include "Universe.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Outpost
{

  namespace
  {
    constexpr const char* FILE_NAME = "Settings.txt";

    /// The two values `DKS3` left in a byte -- it flips every bit, so it is 0 or 255.
    constexpr std::uint8_t SET = 0xFF;
    constexpr std::uint8_t CLEAR = 0x00;

    /*
     * The thirteen, in the assembler's order (`DAMP` to `MUSILLY`, §6.139), and `DNOIZ`. Which
     * value means ON is each byte's own: `DAMP`, `DJD` and `MUTOK` read backwards (non-zero means
     * the thing is OFF), the rest read forwards, and `DNOIZ` non-zero is sound OFF. `JSTK` is
     * position seven of the block and is not here -- see the header.
     */
    constexpr SettingKey KEYS[] = {
      {"damping", "DAMP", CLEAR, SET, "the flight controls creep back to centre when released"},
      {"auto-recentre", "DJD", CLEAR, SET, "pushing back through the middle recentres the control at once"},
      {"author-names", "PATG", SET, CLEAR, "the credits on the title screen -- and the spawner reads this byte too"},
      {"flashing-bars", "FLH", SET, CLEAR, "the dashboard's danger bars flash rather than change colour"},
      {"joystick-reverse-y", "JSTGY", SET, CLEAR, "a controller's pitch axis inverted; nothing until a controller exists"},
      {"joystick-reverse-both", "JSTE", SET, CLEAR, "a controller's both axes inverted; nothing until a controller exists"},
      {"docking-music", "MUTOK", CLEAR, SET, "the Blue Danube while the docking computer flies"},
      {"disk", "DISK", SET, CLEAR, "the disk menu names disk rather than tape as the media"},
      {"planet-detail", "PLTOG", SET, CLEAR, "meridians and a crater on the planet rather than a plain circle"},
      {"docking-music-forced", "MUFOR", SET, CLEAR, "the docking music cannot be stopped once started"},
      {"docking-plays-theme", "MUDOCK", SET, CLEAR, "the theme in place of the Blue Danube when the computer engages"},
      {"effects-during-music", "MUSILLY", SET, CLEAR, "sound effects play while music plays"},
      {"sound", "DNOIZ", CLEAR, SET, "the sound effects at all"},
    };

    constexpr int KEY_COUNT = static_cast<int>(sizeof(KEYS) / sizeof(KEYS[0]));

    /// Recognised so the report can say why it is refused (InputTimer.md §5.1).
    constexpr std::string_view JOYSTICK_KEY = "joystick";

    /// The machine, which is the platform's rather than the game's (Design/Platform.md T-1).
    constexpr std::string_view MACHINE_KEY = "machine";

    [[nodiscard]] std::uint8_t* ByteFor(int _index, Elite::Universe& _universe) noexcept
    {
      switch (_index)
      {
      case 0:
        return &_universe.options.dampingDisabled;
      case 1:
        return &_universe.options.recentreDisabled;
      case 2:
        return &_universe.options.authorNames;
      case 3:
        return &_universe.status.damageFlash;
      case 4:
        return &_universe.joystickGeometry;
      case 5:
        return &_universe.joystickEnabled;
      case 6:
        return &_universe.music.options.dockingMusicOff;
      case 7:
        return &_universe.useDisk;
      case 8:
        return &_universe.heaps.planetDetail;
      case 9:
        return &_universe.music.options.dockingMusicForced;
      case 10:
        return &_universe.music.options.dockingPlaysTheme;
      case 11:
        return &_universe.music.options.effectsDuringMusic;
      case 12:
        return &_universe.sound.soundOff;
      default:
        return nullptr;
      }
    }

    [[nodiscard]] std::string_view Trim(std::string_view _text) noexcept
    {
      while (!_text.empty() && std::isspace(static_cast<unsigned char>(_text.front())) != 0)
      {
        _text.remove_prefix(1);
      }
      while (!_text.empty() && std::isspace(static_cast<unsigned char>(_text.back())) != 0)
      {
        _text.remove_suffix(1);
      }
      return _text;
    }

    [[nodiscard]] std::string Lower(std::string_view _text)
    {
      std::string lower(_text);
      for (char& character : lower)
      {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
      }
      return lower;
    }

    [[nodiscard]] int IndexOf(std::string_view _key) noexcept
    {
      for (int index = 0; index < KEY_COUNT; ++index)
      {
        if (_key == KEYS[index].key)
        {
          return index;
        }
      }
      return -1;
    }

    /// `on`, `off`, and the spellings a person reaches for instead. Anything else is nothing.
    [[nodiscard]] std::optional<bool> Truth(std::string_view _value) noexcept
    {
      if (_value == "on" || _value == "yes" || _value == "true" || _value == "1")
      {
        return true;
      }
      if (_value == "off" || _value == "no" || _value == "false" || _value == "0")
      {
        return false;
      }
      return std::nullopt;
    }
  } // namespace

  const SettingKey* SettingKeys() noexcept
  {
    return KEYS;
  }

  int SettingKeyCount() noexcept
  {
    return KEY_COUNT;
  }

  std::optional<std::uint8_t> ParsedSettings::ValueOf(std::string_view _key) const noexcept
  {
    const int index = IndexOf(_key);
    if (index < 0 || static_cast<std::size_t>(index) >= values.size())
    {
      return std::nullopt;
    }
    return values[static_cast<std::size_t>(index)];
  }

  ParsedSettings ParseSettings(std::string_view _text)
  {
    ParsedSettings parsed;
    parsed.values.resize(static_cast<std::size_t>(KEY_COUNT));

    int lineNumber = 0;
    while (!_text.empty())
    {
      ++lineNumber;
      const std::size_t end = _text.find('\n');
      std::string_view line = _text.substr(0, end);
      _text = (end == std::string_view::npos) ? std::string_view{} : _text.substr(end + 1);

      // A comment runs to the end of the line, and a line that is only one is nothing.
      const std::size_t hash = line.find('#');
      if (hash != std::string_view::npos)
      {
        line = line.substr(0, hash);
      }
      line = Trim(line);
      if (line.empty())
      {
        continue;
      }

      const std::size_t equals = line.find('=');
      if (equals == std::string_view::npos)
      {
        parsed.problems.push_back("line " + std::to_string(lineNumber) + ": expected `key = on` or `key = off`");
        continue;
      }

      const std::string key = Lower(Trim(line.substr(0, equals)));
      const std::string value = Lower(Trim(line.substr(equals + 1)));

      if (key == MACHINE_KEY)
      {
        if (value == "ntsc")
        {
          parsed.machine = MachineTiming::Ntsc();
        }
        else if (value == "pal")
        {
          parsed.machine = MachineTiming::Pal();
        }
        else
        {
          parsed.problems.push_back("line " + std::to_string(lineNumber) + ": `machine` wants ntsc or pal, not `" + value + "`");
        }
        continue;
      }

      if (key == JOYSTICK_KEY)
      {
        parsed.problems.push_back("line " + std::to_string(lineNumber) +
                                  ": `joystick` is not honoured until a controller exists (Design/InputTimer.md 5.1)");
        continue;
      }

      const int index = IndexOf(key);
      if (index < 0)
      {
        parsed.problems.push_back("line " + std::to_string(lineNumber) + ": unknown key `" + key + "`");
        continue;
      }

      const std::optional<bool> truth = Truth(value);
      if (!truth.has_value())
      {
        parsed.problems.push_back("line " + std::to_string(lineNumber) + ": `" + key + "` wants on or off, not `" + value + "`");
        continue;
      }

      parsed.values[static_cast<std::size_t>(index)] = *truth ? KEYS[index].onValue : KEYS[index].offValue;
    }

    return parsed;
  }

  void ApplySettings(const ParsedSettings& _settings, Elite::Universe& _universe) noexcept
  {
    for (int index = 0; index < KEY_COUNT; ++index)
    {
      const std::size_t at = static_cast<std::size_t>(index);
      if (at >= _settings.values.size() || !_settings.values[at].has_value())
      {
        continue;
      }
      std::uint8_t* const byte = ByteFor(index, _universe);
      if (byte != nullptr)
      {
        *byte = *_settings.values[at];
      }
    }
  }

  std::string DefaultSettingsText()
  {
    /*
     * The boot value of every key is what a fresh `Universe` holds, so the default file is the game
     * as it starts: each key at the value a zero byte means. Written out in full rather than as an
     * empty file, because a person who opens it should see what there is to change.
     */
    Elite::Universe fresh{};
    std::ostringstream out;
    out << "# Outpost: Elite -- settings. Every key below is `on` or `off`; a line that cannot be read\n"
           "# is reported when the game starts and ignored, and a key left out keeps the game's own\n"
           "# value. These were the pause screen's thirteen toggles on the Commodore 64.\n\n"
           "# The machine the game keeps time against: `ntsc` or `pal`. It decides how fast a flight\n"
           "# frame runs, how long every docked pause lasts, and the sound interrupt's rate. NTSC is\n"
           "# the variant these masters were built as; PAL is 3.8% slower and its frame is 17% longer.\n"
           "machine = ntsc\n\n";
    for (const SettingKey& key : KEYS)
    {
      const int index = static_cast<int>(&key - KEYS);
      const std::uint8_t* const byte = ByteFor(index, fresh);
      const bool on = (byte != nullptr) && (*byte == key.onValue);
      out << "# " << key.what << " (" << key.label << ")\n" << key.key << " = " << (on ? "on" : "off") << "\n\n";
    }
    return out.str();
  }

  std::string SettingsReport::Summary() const
  {
    if (problems.empty())
    {
      return {};
    }
    std::string text = path.string() + " has " + std::to_string(problems.size()) +
                       (problems.size() == 1 ? " line" : " lines") + " the game could not use, and ignored:\n";
    for (const std::string& problem : problems)
    {
      text += "\n  " + problem;
    }
    return text;
  }

  SettingsReport ReadSettingsFile(const std::filesystem::path& _commanders)
  {
    SettingsReport report;
    if (_commanders.empty())
    {
      return report; // no LocalAppData: `SaveStore` has already decided this machine keeps nothing
    }
    report.path = _commanders.parent_path() / FILE_NAME;

    std::error_code error;
    if (!std::filesystem::exists(report.path, error))
    {
      // Absent, so the player gets the default to edit. A failure to write it is not a problem
      // worth a box: the game runs on its boot values either way.
      std::filesystem::create_directories(report.path.parent_path(), error);
      std::ofstream out(report.path, std::ios::trunc);
      if (out)
      {
        out << DefaultSettingsText();
        report.created = true;
      }
      return report;
    }

    std::ifstream in(report.path);
    if (!in)
    {
      report.problems.push_back("the file exists and could not be read");
      return report;
    }
    std::stringstream whole;
    whole << in.rdbuf();

    ParsedSettings parsed = ParseSettings(whole.str());
    report.problems = parsed.problems;
    if (parsed.machine.has_value())
    {
      report.timing = *parsed.machine;
    }
    report.parsed = std::move(parsed);
    return report;
  }

} // namespace Outpost
