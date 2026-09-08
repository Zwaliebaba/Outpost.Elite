#include "pch.h"

#include "Music.h"

#include "LookupTables.h"

/*
 * The music (slice 5b).
 */

namespace Elite
{

  namespace
  {
    /// 6502: BIT / BMI -- every music flag is read from its top bit.
    [[nodiscard]] bool IsSet(std::uint8_t _flag) noexcept
    {
      return (_flag & 0x80u) != 0u;
    }

    /// 6502: the volume register's low nibble -- full volume, no filter.
    constexpr std::uint8_t FULL_VOLUME = 0x0Fu;

    /// 6502: the vibrato periods, in interrupts, as the GMA release has them.
    constexpr std::uint8_t VIBRATO_PERIOD_VOICE_3 = 5;
    constexpr std::uint8_t VIBRATO_PERIOD_VOICE_2 = 4;

    /// 6502: how far above the note each voice's vibrato frequency sits.
    constexpr std::uint8_t VIBRATO_RISE_VOICE_2 = 32;
    constexpr std::uint8_t VIBRATO_RISE_VOICE_3 = 37;

    /// A pass that never reaches a rest would loop for ever on the 6502 too. This is the ceiling
    /// the port puts under that, well above the longest chain in either shipped tune.
    constexpr std::uint32_t COMMANDS_PER_PASS = 4096;

    /// 6502: SID+&7, SID+&8 and SID+&E, SID+&F -- voice 2 and 3's frequency registers.
    constexpr std::uint8_t VOICE_2_FREQUENCY_LOW = 0x07;
    constexpr std::uint8_t VOICE_2_FREQUENCY_HIGH = 0x08;
    constexpr std::uint8_t VOICE_3_FREQUENCY_LOW = 0x0E;
    constexpr std::uint8_t VOICE_3_FREQUENCY_HIGH = 0x0F;

    /// 6502: SID+&4, SID+&B and SID+&12 -- the three control registers.
    constexpr std::uint8_t VOICE_1_CONTROL = 0x04;
    constexpr std::uint8_t VOICE_2_CONTROL = 0x0B;
    constexpr std::uint8_t VOICE_3_CONTROL = 0x12;

    /*
     * 6502: BDlab19 -- the data pointer stepped, carrying into its high byte, and then read.
     *
     * Pre-increment, then read. The pointer is sixteen bits and wraps as the original's does; a read
     * past the extracted region answers zero, which is `LineHeap::Read`'s rule and never a read the
     * shipped tunes make -- both loop through command 9 long before their last byte.
     */
    [[nodiscard]] std::uint8_t FetchByte(MusicPlayer& _music) noexcept
    {
      _music.pointer = static_cast<std::uint16_t>(_music.pointer + 1u);
      return (_music.pointer < MUSIC_DATA.size()) ? MUSIC_DATA[_music.pointer] : std::uint8_t{0};
    }

    /// 6502: BDlab3 -- two bytes into voice 1's frequency, high register first.
    void SetVoice1Frequency(MusicPlayer& _music, SidWriteLog& _log) noexcept
    {
      _log.Add(SID_FREQUENCY_HIGH, FetchByte(_music));
      _log.Add(SID_FREQUENCY_LOW, FetchByte(_music));
    }

    /*
     * 6502: BDlab5 -- voice 2's frequency, and the two copies the vibrato alternates between.
     *
     * The second copy is the first plus 32, the addition carrying into the other byte: a sixteen-bit
     * number whose low byte is the one called "hi".
     */
    void SetVoice2Frequency(MusicPlayer& _music, SidWriteLog& _log) noexcept
    {
      const std::uint8_t high = FetchByte(_music);
      _log.Add(VOICE_2_FREQUENCY_HIGH, high);
      _music.voice2NoteHigh = high;
      _music.voice2RaisedHigh = high;

      const std::uint8_t low = FetchByte(_music);
      _log.Add(VOICE_2_FREQUENCY_LOW, low);
      _music.voice2NoteLow = low;
      _music.voice2RaisedLow = low;

      const std::uint16_t raised = static_cast<std::uint16_t>(VIBRATO_RISE_VOICE_2 + _music.voice2RaisedLow);
      _music.voice2RaisedLow = static_cast<std::uint8_t>(raised);
      if (raised > 0xFFu)
      {
        _music.voice2RaisedHigh = static_cast<std::uint8_t>(_music.voice2RaisedHigh + 1u);
      }
    }

    /// 6502: BDlab7 -- the same for voice 3, and 37 rather than 32 in this release.
    void SetVoice3Frequency(MusicPlayer& _music, SidWriteLog& _log) noexcept
    {
      const std::uint8_t high = FetchByte(_music);
      _log.Add(VOICE_3_FREQUENCY_HIGH, high);
      _music.voice3NoteHigh = high;
      _music.voice3RaisedHigh = high;

      const std::uint8_t low = FetchByte(_music);
      _log.Add(VOICE_3_FREQUENCY_LOW, low);
      _music.voice3NoteLow = low;
      _music.voice3RaisedLow = low;

      const std::uint16_t raised = static_cast<std::uint16_t>(VIBRATO_RISE_VOICE_3 + _music.voice3RaisedLow);
      _music.voice3RaisedLow = static_cast<std::uint8_t>(raised);
      if (raised > 0xFFu)
      {
        _music.voice3RaisedHigh = static_cast<std::uint8_t>(_music.voice3RaisedHigh + 1u);
      }
    }

    /// 6502: BDlab4, BDlab6, BDlab8 -- a zero into the control register and then the value, so the
    /// gate goes down before it goes up, which is what re-triggers the envelope.
    void GateVoice(SidWriteLog& _log, std::uint8_t _register, std::uint8_t _control) noexcept
    {
      _log.Add(_register, 0u);
      _log.Add(_register, _control);
    }

    /*
     * 6502: BDlab21 -- the end of every pass.
     *
     * On the pass where the rest counter has just reached zero, every voice's control register is
     * written with its value minus one -- bit 0 off, the gate down -- so the notes release before
     * the next command sounds. Every other pass ends here doing nothing.
     */
    void EndPass(const MusicPlayer& _music, SidWriteLog& _log) noexcept
    {
      if (_music.counter != 0u)
      {
        return;
      }
      _log.Add(VOICE_1_CONTROL, static_cast<std::uint8_t>(_music.voice1Control - 1u));
      _log.Add(VOICE_2_CONTROL, static_cast<std::uint8_t>(_music.voice2Control - 1u));
      _log.Add(VOICE_3_CONTROL, static_cast<std::uint8_t>(_music.voice3Control - 1u));
    }

    /*
     * 6502: BDlab1, BDlab23, BDlab24 -- the vibrato, and then BDlab21.
     *
     * Voice 3's counter is stepped and tested first, voice 2's second, and only one of them fires on
     * any pass: the first that reaches its period jumps to its routine, which resets that counter,
     * flips the self-modified branch, writes the other frequency and goes to BDlab21. The other
     * counter is not stepped on that pass.
     *
     * The two halves of each routine are the same code with the other frequency and the other
     * operand, so the port has one branch on the bit the operand stands for.
     */
    void Vibrato(MusicPlayer& _music, SidWriteLog& _log) noexcept
    {
      // 6502: BDbeqmod2 -- voice 3's counter stepped and compared against its period.
      _music.vibrato3Count = static_cast<std::uint8_t>(_music.vibrato3Count + 1u);
      if (_music.vibrato3Count == VIBRATO_PERIOD_VOICE_3)
      {
        _music.vibrato3Count = 0u;
        if (_music.vibrato3Raised)
        {
          // The unlabelled half: the raised copy, and the operand back to the labelled one.
          _log.Add(VOICE_3_FREQUENCY_HIGH, _music.voice3RaisedHigh);
          _log.Add(VOICE_3_FREQUENCY_LOW, _music.voice3RaisedLow);
        }
        else
        {
          // The labelled half, BDlab23: the note's own frequency, and the operand to the other.
          _log.Add(VOICE_3_FREQUENCY_HIGH, _music.voice3NoteHigh);
          _log.Add(VOICE_3_FREQUENCY_LOW, _music.voice3NoteLow);
        }
        _music.vibrato3Raised = !_music.vibrato3Raised;
        EndPass(_music, _log);
        return;
      }

      // 6502: BDbeqmod1 -- voice 2's counter stepped and compared against its period.
      _music.vibrato2Count = static_cast<std::uint8_t>(_music.vibrato2Count + 1u);
      if (_music.vibrato2Count == VIBRATO_PERIOD_VOICE_2)
      {
        _music.vibrato2Count = 0u;
        if (_music.vibrato2Raised)
        {
          _log.Add(VOICE_2_FREQUENCY_HIGH, _music.voice2RaisedHigh);
          _log.Add(VOICE_2_FREQUENCY_LOW, _music.voice2RaisedLow);
        }
        else
        {
          // BDlab24, the labelled half.
          _log.Add(VOICE_2_FREQUENCY_HIGH, _music.voice2NoteHigh);
          _log.Add(VOICE_2_FREQUENCY_LOW, _music.voice2NoteLow);
        }
        _music.vibrato2Raised = !_music.vibrato2Raised;
      }

      EndPass(_music, _log);
    }

    /*
     * 6502: startat2 and what follows it -- the checks every start goes through.
     *
     * The start address is stored FIRST, so a call that then declines to play still remembers it.
     * Then three tests in order: already playing means do nothing; forced means play regardless of
     * the option; otherwise the "no docking music" option decides. `april16` maps the I/O page in,
     * starts the tune and marks it playing.
     */
    void StartAt(MusicPlayer& _music, std::uint16_t _tuneStart, MemoryMap& _map, SidWriteLog& _log) noexcept
    {
      _music.tuneStart = _tuneStart;

      if (IsSet(_music.playing))
      {
        return;
      }
      if (!IsSet(_music.options.dockingMusicForced) && IsSet(_music.options.dockingMusicOff))
      {
        return;
      }

      StartDockingMusicNow(_music, _map, _log); // 6502: the fall-through into `april16`
    }
  } // namespace

  void StartDockingMusicNow(MusicPlayer& _music, MemoryMap& _map, SidWriteLog& _log) noexcept
  {
    // 6502: april16 -- the I/O page in, so the SID exists to be written.
    SetMemoryMap(_map, MEMORY_MAP_IO);

    BeginTune(_music, _log); // 6502: BDENTRY
    _music.playing = 0xFFu;  // 6502: MUPLA set

    // 6502: coffeeex -- the map back to RAM, shared with `stopat`'s exit.
    SetMemoryMap(_map, MEMORY_MAP_RAM);
  }

  void StartDockingMusic(MusicPlayer& _music, MemoryMap& _map, SidWriteLog& _log) noexcept
  {
    // 6502: startbd -- the "docking plays the theme" option picks which of the two tunes starts.
    if (IsSet(_music.options.dockingPlaysTheme))
    {
      StartTheme(_music, _map, _log);
      return;
    }
    StartAt(_music, MUSIC_DOCKING_OFFSET, _map, _log);
  }

  void StartTheme(MusicPlayer& _music, MemoryMap& _map, SidWriteLog& _log) noexcept
  {
    // 6502: startat -- the theme's address, one byte back because the fetch pre-increments.
    StartAt(_music, MUSIC_THEME_OFFSET, _map, _log);
  }

  void StopDockingMusic(MusicPlayer& _music, std::uint8_t _titleReset, SoundBuffer& _buffer, MemoryMap& _map,
                        SidWriteLog& _log) noexcept
  {
    // 6502: stopbd -- a RESET reached from inside `TITLE` stops nothing, so the title music
    // survives it.
    if (IsSet(_titleReset))
    {
      return;
    }

    // 6502: forced music is not stopped, it is started.
    if (IsSet(_music.options.dockingMusicForced))
    {
      StartDockingMusic(_music, _map, _log);
      return;
    }

    StopMusic(_music, _buffer, _map, _log);
  }

  void StopMusic(MusicPlayer& _music, SoundBuffer& _buffer, MemoryMap& _map, SidWriteLog& _log) noexcept
  {
    // 6502: stopat -- nothing to stop unless it is playing.
    if (!IsSet(_music.playing))
    {
      return;
    }

    // 6502: the effects flushed, the I/O page in, and `MUPLA` cleared.
    FlushSoundEffects(_buffer);
    SetMemoryMap(_map, MEMORY_MAP_IO);
    _music.playing = 0u;

    // 6502: coffeeloop -- twenty-five zeros with interrupts off, from the top register down to the
    // first.
    for (int reg = SID_REGISTER_COUNT - 1; reg >= 0; --reg)
    {
      _log.Add(static_cast<std::uint8_t>(reg), 0u);
    }

    // 6502: the volume back to full, and interrupts on again.
    _log.Add(SID_VOLUME, FULL_VOLUME);

    // 6502: coffeeex -- the map back to RAM, and `april16` shares this exit.
    SetMemoryMap(_map, MEMORY_MAP_RAM);
  }

  void BeginTune(MusicPlayer& _music, SidWriteLog& _log) noexcept
  {
    // 6502: BDENTRY -- the buffer, the rest counter and both vibrato counters zeroed.
    _music.buffer = 0u;
    _music.counter = 0u;
    _music.vibrato2Count = 0u;
    _music.vibrato3Count = 0u;

    // 6502: BDloop2 -- the registers zeroed from the top down, and the loop STOPS AT 1, so the
    // first register is the one this does not clear.
    for (int reg = SID_REGISTER_COUNT - 1; reg >= 1; --reg)
    {
      _log.Add(static_cast<std::uint8_t>(reg), 0u);
    }

    // 6502: the start address into both pointer pairs -- the one that reads and the one that
    // restarts.
    _music.pointer = _music.tuneStart;
    _music.restart = _music.tuneStart;

    // 6502: the volume to full on the way out.
    _log.Add(SID_VOLUME, FULL_VOLUME);
  }

  void RunMusic(MusicPlayer& _music, SidWriteLog& _log) noexcept
  {
    // 6502: a non-zero rest counter comes down by one and the pass ends there.
    if (_music.counter != 0u)
    {
      _music.counter = static_cast<std::uint8_t>(_music.counter - 1u);
      Vibrato(_music, _log);
      return;
    }

    for (std::uint32_t commands = 0; commands < COMMANDS_PER_PASS; ++commands)
    {
      /*
       * 6502: BDskip1, BDLABEL and BDLABEL2 -- where the next command comes from.
       *
       * Three ways in: a buffer with a high nibble goes straight to the mask; a buffer holding one
       * nibble is the command as it stands; an empty buffer fetches a byte first. All three end by
       * shifting the buffer down so the high nibble is next.
       */
      std::uint8_t command = _music.buffer;
      if (command < 0x10u)
      {
        if (command == 0u)
        {
          _music.buffer = FetchByte(_music);
          command = static_cast<std::uint8_t>(_music.buffer & 0x0Fu);
        }
      }
      else
      {
        command = static_cast<std::uint8_t>(command & 0x0Fu);
      }
      _music.buffer = static_cast<std::uint8_t>(_music.buffer >> 4);

      // 6502: BDJMPTBL, BDJMPTBH -- the jump table, which is the switch below. Indexing it with
      // X = 0 reads past its start into code, which the shipped tunes never do.
      if (command == 0u)
      {
        return;
      }

      switch (command)
      {
      case 1: // 6502: BDRO1
        SetVoice1Frequency(_music, _log);
        GateVoice(_log, VOICE_1_CONTROL, _music.voice1Control);
        break;

      case 2: // 6502: BDRO2
        SetVoice2Frequency(_music, _log);
        GateVoice(_log, VOICE_2_CONTROL, _music.voice2Control);
        break;

      case 3: // 6502: BDRO3
        SetVoice3Frequency(_music, _log);
        GateVoice(_log, VOICE_3_CONTROL, _music.voice3Control);
        break;

      case 4: // 6502: BDRO4
        SetVoice1Frequency(_music, _log);
        SetVoice2Frequency(_music, _log);
        GateVoice(_log, VOICE_1_CONTROL, _music.voice1Control);
        GateVoice(_log, VOICE_2_CONTROL, _music.voice2Control);
        break;

      case 5: // 6502: BDRO5
        SetVoice1Frequency(_music, _log);
        SetVoice2Frequency(_music, _log);
        SetVoice3Frequency(_music, _log);
        GateVoice(_log, VOICE_1_CONTROL, _music.voice1Control);
        GateVoice(_log, VOICE_2_CONTROL, _music.voice2Control);
        GateVoice(_log, VOICE_3_CONTROL, _music.voice3Control);
        break;

      case 6: // 6502: BDRO6 -- INC value0.
        _music.commandSixTally = static_cast<std::uint8_t>(_music.commandSixTally + 1u);
        break;

      case 7: // 6502: BDRO7 -- the three attack/decay registers, then the three sustain/release.
        _log.Add(SID_ATTACK_DECAY, FetchByte(_music));
        _log.Add(SID_ATTACK_DECAY + SID_VOICE_REGISTERS, FetchByte(_music));
        _log.Add(SID_ATTACK_DECAY + 2 * SID_VOICE_REGISTERS, FetchByte(_music));
        _log.Add(SID_SUSTAIN_RELEASE, FetchByte(_music));
        _log.Add(SID_SUSTAIN_RELEASE + SID_VOICE_REGISTERS, FetchByte(_music));
        _log.Add(SID_SUSTAIN_RELEASE + 2 * SID_VOICE_REGISTERS, FetchByte(_music));
        break;

      case 15:
        /*
         * 6502: BDRO15 -- a 1 rotated in at the bottom and shifted up three, so the buffer gains an
         * 8 below whatever it held, and then BDRO8.
         *
         * Command 8 is slid into the buffer's low nibble ahead of whatever was there, so the rest
         * below is taken twice: once now and once when the inserted 8 is processed.
         */
        _music.buffer = static_cast<std::uint8_t>((_music.buffer << 4) | 0x08u);
        [[fallthrough]];

      case 8: // 6502: BDRO8 -- the rest length into the counter, then the vibrato and out.
        _music.counter = _music.restLength;
        if (_music.counter != 0u)
        {
          _music.counter = static_cast<std::uint8_t>(_music.counter - 1u);
          Vibrato(_music, _log);
          return;
        }
        break; // 6502: a rest of zero falls straight back into BDskip1

      case 9:  // 6502: BDRO9
      case 11: // 6502: BDRO11 -- which is a jump to BDRO9
        _music.buffer = 0u;
        _music.pointer = _music.restart;
        break;

      case 10: // 6502: BDRO10 -- the three pulse widths, low register then high.
        _log.Add(SID_PULSE_WIDTH_LOW, FetchByte(_music));
        _log.Add(SID_PULSE_WIDTH_HIGH, FetchByte(_music));
        _log.Add(SID_PULSE_WIDTH_LOW + SID_VOICE_REGISTERS, FetchByte(_music));
        _log.Add(SID_PULSE_WIDTH_HIGH + SID_VOICE_REGISTERS, FetchByte(_music));
        _log.Add(SID_PULSE_WIDTH_LOW + 2 * SID_VOICE_REGISTERS, FetchByte(_music));
        _log.Add(SID_PULSE_WIDTH_HIGH + 2 * SID_VOICE_REGISTERS, FetchByte(_music));
        break;

      case 12: // 6502: BDRO12
        _music.restLength = FetchByte(_music);
        break;

      case 13: // 6502: BDRO13
        _music.voice1Control = FetchByte(_music);
        _music.voice2Control = FetchByte(_music);
        _music.voice3Control = FetchByte(_music);
        break;

      case 14: // 6502: BDRO14 -- volume and filter mode, filter control, filter cut-off high.
        _log.Add(SID_VOLUME, FetchByte(_music));
        _log.Add(SID_FILTER_CONTROL, FetchByte(_music));
        _log.Add(SID_FILTER_CUTOFF_HIGH, FetchByte(_music));
        break;

      default:
        break;
      }
    }
  }

  void RunSoundInterrupt(SoundBuffer& _buffer, MusicPlayer& _music, SidWriteLog& _log) noexcept
  {
    // 6502: the music runs first when it is playing, and the "effects during music" option decides
    // whether the effects run after it.
    if (IsSet(_music.playing))
    {
      RunMusic(_music, _log);
      if (!IsSet(_music.options.effectsDuringMusic))
      {
        return;
      }
    }

    RunSoundEffects(_buffer, _log);
  }

} // namespace Elite
