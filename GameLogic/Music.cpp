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

    /// 6502: LDA #%00001111 / STA SID+&18 -- full volume, no filter.
    constexpr std::uint8_t FULL_VOLUME = 0x0Fu;

    /// 6502: the vibrato periods, in interrupts, as the GMA release has them.
    constexpr std::uint8_t VIBRATO_PERIOD_VOICE_3 = 5;
    constexpr std::uint8_t VIBRATO_PERIOD_VOICE_2 = 4;

    /// 6502: LDA #32 and LDA #37 -- how far above the note each voice's vibrato frequency sits.
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
     * 6502: BDlab19 -- INC BDdataptr1 / BNE BDskipme1 / INC BDdataptr1+1 / LDA (BDdataptr1),Y.
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
     * `CLC / CLD / LDA #32 / ADC voice2hi2 / STA voice2hi2 / BCC / INC voice2lo2`: the second copy is
     * the first plus 32, as a sixteen-bit number whose low byte is the one called "hi".
     */
    void SetVoice2Frequency(MusicPlayer& _music, SidWriteLog& _log) noexcept
    {
      const std::uint8_t high = FetchByte(_music);
      _log.Add(VOICE_2_FREQUENCY_HIGH, high);
      _music.voice2lo1 = high;
      _music.voice2lo2 = high;

      const std::uint8_t low = FetchByte(_music);
      _log.Add(VOICE_2_FREQUENCY_LOW, low);
      _music.voice2hi1 = low;
      _music.voice2hi2 = low;

      const std::uint16_t raised = static_cast<std::uint16_t>(VIBRATO_RISE_VOICE_2 + _music.voice2hi2);
      _music.voice2hi2 = static_cast<std::uint8_t>(raised);
      if (raised > 0xFFu)
      {
        _music.voice2lo2 = static_cast<std::uint8_t>(_music.voice2lo2 + 1u);
      }
    }

    /// 6502: BDlab7 -- the same for voice 3, and 37 rather than 32 in this release.
    void SetVoice3Frequency(MusicPlayer& _music, SidWriteLog& _log) noexcept
    {
      const std::uint8_t high = FetchByte(_music);
      _log.Add(VOICE_3_FREQUENCY_HIGH, high);
      _music.voice3lo1 = high;
      _music.voice3lo2 = high;

      const std::uint8_t low = FetchByte(_music);
      _log.Add(VOICE_3_FREQUENCY_LOW, low);
      _music.voice3hi1 = low;
      _music.voice3hi2 = low;

      const std::uint16_t raised = static_cast<std::uint16_t>(VIBRATO_RISE_VOICE_3 + _music.voice3hi2);
      _music.voice3hi2 = static_cast<std::uint8_t>(raised);
      if (raised > 0xFFu)
      {
        _music.voice3lo2 = static_cast<std::uint8_t>(_music.voice3lo2 + 1u);
      }
    }

    /// 6502: BDlab4, BDlab6, BDlab8 -- STY SID+n / STA SID+n with Y zero: the gate goes down and
    /// then the control register is written, which is what re-triggers the envelope.
    void GateVoice(SidWriteLog& _log, std::uint8_t _register, std::uint8_t _control) noexcept
    {
      _log.Add(_register, 0u);
      _log.Add(_register, _control);
    }

    /*
     * 6502: BDlab21 -- the end of every pass.
     *
     * `LDX counter / CPX #0 / BNE BDexitirq`: on the pass where the rest has just run out, every
     * voice's control register is written with its value minus one -- bit 0 off, the gate down --
     * so the notes release before the next command sounds. Every other pass ends here doing nothing.
     */
    void EndPass(const MusicPlayer& _music, SidWriteLog& _log) noexcept
    {
      if (_music.counter != 0u)
      {
        return;
      }
      _log.Add(VOICE_1_CONTROL, static_cast<std::uint8_t>(_music.value1 - 1u));
      _log.Add(VOICE_2_CONTROL, static_cast<std::uint8_t>(_music.value2 - 1u));
      _log.Add(VOICE_3_CONTROL, static_cast<std::uint8_t>(_music.value3 - 1u));
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
      // 6502: INC vibrato3 / LDA #5 / CMP vibrato3 / .BDbeqmod2 BEQ.
      _music.vibrato3 = static_cast<std::uint8_t>(_music.vibrato3 + 1u);
      if (_music.vibrato3 == VIBRATO_PERIOD_VOICE_3)
      {
        _music.vibrato3 = 0u;
        if (_music.vibrato3Raised)
        {
          // The unlabelled half: the raised copy, and the operand back to the labelled one.
          _log.Add(VOICE_3_FREQUENCY_HIGH, _music.voice3lo2);
          _log.Add(VOICE_3_FREQUENCY_LOW, _music.voice3hi2);
        }
        else
        {
          // The labelled half, BDlab23: the note's own frequency, and the operand to the other.
          _log.Add(VOICE_3_FREQUENCY_HIGH, _music.voice3lo1);
          _log.Add(VOICE_3_FREQUENCY_LOW, _music.voice3hi1);
        }
        _music.vibrato3Raised = !_music.vibrato3Raised;
        EndPass(_music, _log);
        return;
      }

      // 6502: INC vibrato2 / LDA #4 / CMP vibrato2 / .BDbeqmod1 BEQ.
      _music.vibrato2 = static_cast<std::uint8_t>(_music.vibrato2 + 1u);
      if (_music.vibrato2 == VIBRATO_PERIOD_VOICE_2)
      {
        _music.vibrato2 = 0u;
        if (_music.vibrato2Raised)
        {
          _log.Add(VOICE_2_FREQUENCY_HIGH, _music.voice2lo2);
          _log.Add(VOICE_2_FREQUENCY_LOW, _music.voice2hi2);
        }
        else
        {
          // BDlab24, the labelled half.
          _log.Add(VOICE_2_FREQUENCY_HIGH, _music.voice2lo1);
          _log.Add(VOICE_2_FREQUENCY_LOW, _music.voice2hi1);
        }
        _music.vibrato2Raised = !_music.vibrato2Raised;
      }

      EndPass(_music, _log);
    }

    /*
     * 6502: startat2 and what follows it -- the checks every start goes through.
     *
     * `STA value5 / STX value5+1`, then `BIT MUPLA / BMI itsoff`, `BIT MUFOR / BMI april16`,
     * `BIT MUTOK / BMI itsoff`, and `april16` is `SETL1 / JSR BDENTRY / LDA #&FF / STA MUPLA`.
     */
    void StartAt(MusicPlayer& _music, std::uint16_t _tuneStart, SidWriteLog& _log) noexcept
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

      BeginTune(_music, _log);
      _music.playing = 0xFFu;
    }
  } // namespace

  void StartDockingMusic(MusicPlayer& _music, SidWriteLog& _log) noexcept
  {
    // 6502: startbd -- BIT MUDOCK / BMI startat / LDA #LO(musicstart) / LDX #HI(musicstart).
    if (IsSet(_music.options.dockingPlaysTheme))
    {
      StartTheme(_music, _log);
      return;
    }
    StartAt(_music, MUSIC_DOCKING_OFFSET, _log);
  }

  void StartTheme(MusicPlayer& _music, SidWriteLog& _log) noexcept
  {
    // 6502: startat -- LDA #LO(THEME-1) / LDX #HI(THEME-1) / BNE startat2.
    StartAt(_music, MUSIC_THEME_OFFSET, _log);
  }

  void StopDockingMusic(MusicPlayer& _music, std::uint8_t _titleReset, SoundBuffer& _buffer, SidWriteLog& _log) noexcept
  {
    // 6502: stopbd -- BIT MULIE / BMI itsoff.
    if (IsSet(_titleReset))
    {
      return;
    }

    // 6502: BIT MUFOR / BMI startbd -- forced music is not stopped, it is started.
    if (IsSet(_music.options.dockingMusicForced))
    {
      StartDockingMusic(_music, _log);
      return;
    }

    StopMusic(_music, _buffer, _log);
  }

  void StopMusic(MusicPlayer& _music, SoundBuffer& _buffer, SidWriteLog& _log) noexcept
  {
    // 6502: stopat -- BIT MUPLA / BPL itsoff.
    if (!IsSet(_music.playing))
    {
      return;
    }

    // 6502: JSR SOFLUSH / LDA #%101 / JSR SETL1 / LDA #0 / STA MUPLA.
    FlushSoundEffects(_buffer);
    _music.playing = 0u;

    // 6502: LDX #&18 / SEI / .coffeeloop STA SID,X / DEX / BPL coffeeloop -- twenty-five zeros, from
    // the top register down to the first.
    for (int reg = SID_REGISTER_COUNT - 1; reg >= 0; --reg)
    {
      _log.Add(static_cast<std::uint8_t>(reg), 0u);
    }

    // 6502: LDA #%00001111 / STA SID+&18 / CLI, and coffeeex's SETL1.
    _log.Add(SID_VOLUME, FULL_VOLUME);
  }

  void BeginTune(MusicPlayer& _music, SidWriteLog& _log) noexcept
  {
    // 6502: BDENTRY -- LDA #0 / STA BDBUFF / STA counter / STA vibrato2 / STA vibrato3.
    _music.buffer = 0u;
    _music.counter = 0u;
    _music.vibrato2 = 0u;
    _music.vibrato3 = 0u;

    // 6502: LDX #&18 / .BDloop2 STA SID,X / DEX / BNE BDloop2 -- and the BNE stops at 1, so the first
    // register is the one this does not zero.
    for (int reg = SID_REGISTER_COUNT - 1; reg >= 1; --reg)
    {
      _log.Add(static_cast<std::uint8_t>(reg), 0u);
    }

    // 6502: LDA value5 / STA BDdataptr1 / STA BDdataptr3 / LDA value5+1 / STA BDdataptr2 / STA BDdataptr4.
    _music.pointer = _music.tuneStart;
    _music.restart = _music.tuneStart;

    // 6502: LDA #%00001111 / STA SID+&18 / RTS.
    _log.Add(SID_VOLUME, FULL_VOLUME);
  }

  void RunMusic(MusicPlayer& _music, SidWriteLog& _log) noexcept
  {
    // 6502: LDY #0 / CPY counter / BEQ BDskip1 / DEC counter / JMP BDlab1.
    if (_music.counter != 0u)
    {
      _music.counter = static_cast<std::uint8_t>(_music.counter - 1u);
      Vibrato(_music, _log);
      return;
    }

    for (std::uint32_t commands = 0; commands < COMMANDS_PER_PASS; ++commands)
    {
      /*
       * 6502: .BDskip1 LDA BDBUFF / CMP #&10 / BCS BDLABEL2 / TAX / BNE BDLABEL / JSR BDlab19 /
       * STA BDBUFF / .BDLABEL2 AND #&0F / TAX / .BDLABEL LDA BDBUFF / LSR x4 / STA BDBUFF.
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

      // 6502: BDJMPTBL, BDJMPTBH -- the jump table, which is the switch below. `LDA BDJMPTBL-1,X`
      // with X = 0 reads past the table's start into code, which the shipped tunes never do.
      if (command == 0u)
      {
        return;
      }

      switch (command)
      {
      case 1: // 6502: BDRO1
        SetVoice1Frequency(_music, _log);
        GateVoice(_log, VOICE_1_CONTROL, _music.value1);
        break;

      case 2: // 6502: BDRO2
        SetVoice2Frequency(_music, _log);
        GateVoice(_log, VOICE_2_CONTROL, _music.value2);
        break;

      case 3: // 6502: BDRO3
        SetVoice3Frequency(_music, _log);
        GateVoice(_log, VOICE_3_CONTROL, _music.value3);
        break;

      case 4: // 6502: BDRO4
        SetVoice1Frequency(_music, _log);
        SetVoice2Frequency(_music, _log);
        GateVoice(_log, VOICE_1_CONTROL, _music.value1);
        GateVoice(_log, VOICE_2_CONTROL, _music.value2);
        break;

      case 5: // 6502: BDRO5
        SetVoice1Frequency(_music, _log);
        SetVoice2Frequency(_music, _log);
        SetVoice3Frequency(_music, _log);
        GateVoice(_log, VOICE_1_CONTROL, _music.value1);
        GateVoice(_log, VOICE_2_CONTROL, _music.value2);
        GateVoice(_log, VOICE_3_CONTROL, _music.value3);
        break;

      case 6: // 6502: BDRO6 -- INC value0.
        _music.value0 = static_cast<std::uint8_t>(_music.value0 + 1u);
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
         * 6502: BDRO15 -- LDA BDBUFF / SEC / ROL A / ASL A / ASL A / ASL A / STA BDBUFF, then BDRO8.
         *
         * Command 8 is slid into the buffer's low nibble ahead of whatever was there, so the rest
         * below is taken twice: once now and once when the inserted 8 is processed.
         */
        _music.buffer = static_cast<std::uint8_t>((_music.buffer << 4) | 0x08u);
        [[fallthrough]];

      case 8: // 6502: BDRO8 -- LDA value4 / STA counter / JMP BDirqhere.
        _music.counter = _music.value4;
        if (_music.counter != 0u)
        {
          _music.counter = static_cast<std::uint8_t>(_music.counter - 1u);
          Vibrato(_music, _log);
          return;
        }
        break; // 6502: a rest of zero falls straight back into BDskip1

      case 9:  // 6502: BDRO9
      case 11: // 6502: BDRO11 -- JMP BDRO9
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
        _music.value4 = FetchByte(_music);
        break;

      case 13: // 6502: BDRO13
        _music.value1 = FetchByte(_music);
        _music.value2 = FetchByte(_music);
        _music.value3 = FetchByte(_music);
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
    // 6502: BIT MUPLA / BPL SOINT / JSR BDirqhere / BIT MUSILLY / BMI SOINT / JMP coffee.
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
