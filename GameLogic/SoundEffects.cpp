#include "pch.h"

#include "SoundEffects.h"

#include "LookupTables.h"

/*
 * The sound effects (slice 5a).
 */

namespace Elite
{

  namespace
  {
    /// 6502: the low six bits of SOFLG -- the effect number plus one, without the "new" bit.
    constexpr std::uint8_t FLAG_EFFECT_MASK = 0x3Fu;

    /// 6502: bit 7 of SOFLG -- set by NOISE, cleared by SOINT's first pass over the voice.
    constexpr std::uint8_t FLAG_NEW = 0x80u;

    /// 6502: the priority a stopped voice is left with, which any effect beats.
    constexpr std::uint8_t PRIORITY_FREE = 0;

    /// 6502: SBC #16 -- one step of the sustain volume, which is the byte's high nibble.
    constexpr std::uint8_t SUSTAIN_STEP = 16;

    /// 6502: EOR #%00000100 -- the pulse width's wobble.
    constexpr std::uint8_t PULSE_WIDTH_FLIP = 0x04u;

    /// 6502: sfxbeep.
    constexpr std::uint8_t EFFECT_BEEP = 5;

    /*
     * 6502: LDA SFXPR,Y -- with Y possibly carrying bit 7, so possibly past the table's sixteen.
     *
     * The table is extracted to 136 bytes because 135 is the largest index the game reaches; anything
     * beyond that is not a read the shipped game makes and answers zero, `LineHeap::Read`'s rule.
     */
    [[nodiscard]] std::uint8_t PriorityAt(std::uint8_t _index) noexcept
    {
      return (_index < EFFECT_PRIORITY_TABLE.size()) ? EFFECT_PRIORITY_TABLE[_index] : std::uint8_t{0};
    }

    /*
     * 6502: NOISE from its CLV onwards, with the V flag as an argument.
     *
     * `_pitched` is V. When it is set the two `BVS`es take `_sustain` and `_frequency` -- the bytes
     * NOISE2 left in XX15 and XX15+1 -- instead of the effect's table entries. XX15+2, which both
     * routines write, is scratch that nothing reads afterwards, so it is a local here.
     */
    [[nodiscard]] bool MakeNoise(SoundBuffer& _buffer, std::uint8_t _effect, bool _carryIn, bool _pitched, std::uint8_t _sustain,
                                 std::uint8_t _frequency) noexcept
    {
      // 6502: LDA DNOIZ / BNE SOUR1 -- and SOUR1 is an RTS, so the carry is whatever it was.
      if (_buffer.soundOff != 0u)
      {
        return _carryIn;
      }

      // 6502: LDX #2 / INY / STY XX15+2 / DEY -- the effect number plus one, UNMASKED.
      std::size_t voice = 2;
      const std::uint8_t effectPlusOne = static_cast<std::uint8_t>(_effect + 1u);

      // 6502: LDA SFXPR,Y / LSR A / BCS SOUX9 -- the priority byte's bit 0 says whether to look for
      // a voice already playing this effect. Read before the mask, so index 135 is reachable.
      bool found = false;
      if ((PriorityAt(_effect) & 0x01u) == 0u)
      {
        // 6502: .SOUX7 LDA SOFLG,X / AND #%00111111 / CMP XX15+2 / BEQ SOUX6 / DEX / BPL SOUX7.
        for (int candidate = 2; candidate >= 0; --candidate)
        {
          if ((_buffer.flag[static_cast<std::size_t>(candidate)] & FLAG_EFFECT_MASK) == effectPlusOne)
          {
            voice = static_cast<std::size_t>(candidate);
            found = true;
            break;
          }
        }
      }

      if (!found)
      {
        /*
         * 6502: .SOUX9 LDX #0 / LDA SOPR / CMP SOPR+1 / BCC SOUX1 / INX / LDA SOPR+1 / .SOUX1 CMP SOPR+2 /
         * BCC P%+4 / LDX #2.
         *
         * The lowest priority of the three, and the ties go the way the comparisons fall: `BCC` is
         * "strictly less", so voice 2 wins a tie with voice 1 and voice 3 wins a tie with either.
         */
        voice = 0;
        std::uint8_t lowest = _buffer.priority[0];
        if (lowest >= _buffer.priority[1])
        {
          voice = 1;
          lowest = _buffer.priority[1];
        }
        if (lowest >= _buffer.priority[2])
        {
          voice = 2;
        }
      }

      // 6502: .SOUX6 TYA / AND #%01111111 / TAY -- the flag comes off here and not before.
      const std::uint8_t effect = static_cast<std::uint8_t>(_effect & 0x7Fu);

      // 6502: LDA SFXPR,Y / CMP SOPR,X / BCC SOUR1 -- a failed comparison, so the carry is CLEAR.
      const std::uint8_t priority = PriorityAt(effect);
      if (priority < _buffer.priority[voice])
      {
        return false;
      }

      // 6502: SEI / STA SOPR,X -- and the interrupt lock is what the port's single thread gives.
      _buffer.priority[voice] = priority;

      // 6502: BVS SOUX4 / LDA SFXSUS,Y / EQUB &CD / .SOUX4 LDA XX15 / STA SOSUS,X.
      _buffer.sustain[voice] = _pitched ? _sustain : EFFECT_SUSTAIN_TABLE[effect];

      _buffer.counter[voice] = EFFECT_COUNT_TABLE[effect];                    // 6502: LDA SFXCNT,Y / STA SOCNT,X
      _buffer.frequencyChange[voice] = EFFECT_FREQUENCY_CHANGE_TABLE[effect]; // 6502: LDA SFXFRCH,Y / STA SOFRCH,X
      _buffer.control[voice] = EFFECT_CONTROL_TABLE[effect];                  // 6502: LDA SFXCR,Y / STA SOCR,X

      // 6502: BVS SOUX5 / LDA SFXFQ,Y / EQUB &CD / .SOUX5 LDA XX15+1 / STA SOFRQ,X.
      _buffer.frequency[voice] = _pitched ? _frequency : EFFECT_FREQUENCY_TABLE[effect];

      _buffer.attack[voice] = EFFECT_ATTACK_TABLE[effect];          // 6502: LDA SFXATK,Y / STA SOATK,X
      _buffer.volumeRate[voice] = EFFECT_VOLUME_RATE_TABLE[effect]; // 6502: LDA SFXVCH,Y / STA SOVCH,X

      // 6502: INY / TYA / ORA #%10000000 / STA SOFLG,X / CLI / SEC / RTS.
      _buffer.flag[voice] = static_cast<std::uint8_t>((effect + 1u) | FLAG_NEW);
      return true;
    }

    /// 6502: SEVENS,Y -- the voice's register base.
    [[nodiscard]] std::uint8_t VoiceBase(std::size_t _voice) noexcept
    {
      return SEVENS_TABLE[_voice];
    }

    /*
     * 6502: .SOUX2 CLC / CLD / ADC SOFRQ,Y / STA SOFRQ,Y / PHA / LSR A / LSR A / STA SID+&1,X / PLA /
     * ASL A x6 / STA SID,X / LDA PULSEW / STA SID+&3,X.
     *
     * `_change` is what A holds on arrival: the frequency change for a running effect, zero for a new
     * one. The frequency byte's top six bits go to the high register and its bottom two to the top of
     * the low register, so the chip's value is the byte times 64.
     */
    void WriteFrequency(SoundBuffer& _buffer, SidWriteLog& _log, std::size_t _voice, std::uint8_t _change) noexcept
    {
      const std::uint8_t base = VoiceBase(_voice);
      const std::uint8_t frequency = static_cast<std::uint8_t>(_buffer.frequency[_voice] + _change);
      _buffer.frequency[_voice] = frequency;

      _log.Add(static_cast<std::uint8_t>(base + SID_FREQUENCY_HIGH), static_cast<std::uint8_t>(frequency >> 2));
      _log.Add(static_cast<std::uint8_t>(base + SID_FREQUENCY_LOW), static_cast<std::uint8_t>(frequency << 6));
      _log.Add(static_cast<std::uint8_t>(base + SID_PULSE_WIDTH_HIGH), _buffer.pulseWidth);
    }
  } // namespace

  bool PlaySoundEffect(SoundBuffer& _buffer, std::uint8_t _effect, bool _carryIn) noexcept
  {
    // 6502: NOISE -- CLV, then the routine.
    return MakeNoise(_buffer, _effect, _carryIn, false, 0u, 0u);
  }

  bool PlaySoundEffectPitched(SoundBuffer& _buffer, std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t _frequency,
                              bool _carryIn) noexcept
  {
    // 6502: NOISE2 -- BIT SOUR1 / STA XX15 / STX XX15+1 / EQUB &50, into NOISE with V set.
    return MakeNoise(_buffer, _effect, _carryIn, true, _sustain, _frequency);
  }

  bool Beep(SoundBuffer& _buffer, bool _carryIn) noexcept
  {
    // 6502: BEEP -- LDY #sfxbeep / BNE NOISE.
    return PlaySoundEffect(_buffer, EFFECT_BEEP, _carryIn);
  }

  void StopSoundEffect(SoundBuffer& _buffer, std::uint8_t _effect) noexcept
  {
    // 6502: NOISEOFF -- LDX #3 / INY / STY XX15+2, then SOUL1: DEX / BMI SOUR1 / LDA SOFLG,X /
    // AND #%00111111 / CMP XX15+2 / BNE SOUL1 / LDA #1 / STA SOCNT,X / RTS.
    const std::uint8_t effectPlusOne = static_cast<std::uint8_t>(_effect + 1u);
    for (int voice = 2; voice >= 0; --voice)
    {
      if ((_buffer.flag[static_cast<std::size_t>(voice)] & FLAG_EFFECT_MASK) == effectPlusOne)
      {
        _buffer.counter[static_cast<std::size_t>(voice)] = 1u;
        return;
      }
    }
  }

  void FlushSoundEffects(SoundBuffer& _buffer) noexcept
  {
    // 6502: SOFLUSH -- LDY #3 / LDA #1 / .SOUL2 STA SOCNT-1,Y / DEY / BNE SOUL2.
    for (std::uint8_t& counter : _buffer.counter)
    {
      counter = 1u;
    }
  }

  void RunSoundEffects(SoundBuffer& _buffer, SidWriteLog& _log) noexcept
  {
    // 6502: LDY #2, and SOUL8 down to voice 0.
    for (int index = 2; index >= 0; --index)
    {
      const std::size_t voice = static_cast<std::size_t>(index);
      const std::uint8_t flag = _buffer.flag[voice];

      /*
       * 6502: LDA SOFLG,Y / BEQ SOUL3b.
       *
       * A silent voice takes the OTHER exit: SOUL3b steps down and, past voice 1, returns from the
       * interrupt without reaching the pulse-width flip. So a frame on which voice 1 is silent leaves
       * PULSEW where it was, whatever voices 2 and 3 did.
       */
      if (flag == 0u)
      {
        if (index == 0)
        {
          return;
        }
        continue;
      }

      const std::uint8_t base = VoiceBase(voice);

      if ((flag & FLAG_NEW) != 0u)
      {
        /*
         * 6502: .SOUL4 -- LDA SEVENS,Y / STA SOUX3+1, then LDA #0 / LDX #6 / .SOUX3 STA SID,X / DEX /
         * BPL SOUX3.
         *
         * The store is self-modified to the voice's base, and the loop zeroes the seven registers from
         * the top down. Seven writes of zero, in that order, and the first of them is what puts the
         * gate down before the control register puts it up again.
         */
        for (int reg = SID_VOICE_REGISTERS - 1; reg >= 0; --reg)
        {
          _log.Add(static_cast<std::uint8_t>(base + reg), 0u);
        }

        // 6502: LDX SEVENS,Y / LDA SOCR,Y / STA SID+&4,X / LDA SOATK,Y / STA SID+&5,X / LDA SOSUS,Y /
        // STA SID+&6,X / LDA #0 -- and then SOUX2 with A = 0.
        _log.Add(static_cast<std::uint8_t>(base + SID_CONTROL), _buffer.control[voice]);
        _log.Add(static_cast<std::uint8_t>(base + SID_ATTACK_DECAY), _buffer.attack[voice]);
        _log.Add(static_cast<std::uint8_t>(base + SID_SUSTAIN_RELEASE), _buffer.sustain[voice]);
        WriteFrequency(_buffer, _log, voice, 0u);

        // 6502: .SOUL5 LDA SOFLG,Y / BMI SOUL6 -- .SOUL6 AND #%01111111 / STA SOFLG,Y, then SOUL3.
        _buffer.flag[voice] = static_cast<std::uint8_t>(flag & ~FLAG_NEW);
      }
      else
      {
        // 6502: LDX SEVENS,Y / LDA SOFRCH,Y / BEQ SOUL5 / BNE SOUX2.
        if (_buffer.frequencyChange[voice] != 0u)
        {
          WriteFrequency(_buffer, _log, voice, _buffer.frequencyChange[voice]);
        }

        // 6502: .SOUL5 LDA SOFLG,Y / BMI SOUL6 -- not taken -- TYA / TAX.

        // 6502: DEC SOPR,X / BNE P%+5 / INC SOPR,X -- down, but a priority that reaches zero is put
        // back to one. A priority that WAS zero goes to 255, which the BNE does not catch.
        _buffer.priority[voice] = static_cast<std::uint8_t>(_buffer.priority[voice] - 1u);
        if (_buffer.priority[voice] == 0u)
        {
          _buffer.priority[voice] = 1u;
        }

        // 6502: DEC SOCNT,X / BEQ SOKILL.
        _buffer.counter[voice] = static_cast<std::uint8_t>(_buffer.counter[voice] - 1u);
        if (_buffer.counter[voice] == 0u)
        {
          // 6502: .SOKILL LDX SEVENS,Y / LDA SOCR,Y / AND #%11111110 / STA SID+&4,X / LDA #0 /
          // STA SOFLG,Y / STA SOPR,Y / BEQ SOUL3.
          _log.Add(static_cast<std::uint8_t>(base + SID_CONTROL), static_cast<std::uint8_t>(_buffer.control[voice] & 0xFEu));
          _buffer.flag[voice] = 0u;
          _buffer.priority[voice] = PRIORITY_FREE;
        }
        else if ((_buffer.counter[voice] & _buffer.volumeRate[voice]) == 0u)
        {
          // 6502: LDA SOSUS,Y / SEC / SBC #16 / STA SOSUS,Y / LDX SEVENS,Y / STA SID+&6,X.
          _buffer.sustain[voice] = static_cast<std::uint8_t>(_buffer.sustain[voice] - SUSTAIN_STEP);
          _log.Add(static_cast<std::uint8_t>(base + SID_SUSTAIN_RELEASE), _buffer.sustain[voice]);
        }
        // 6502: LDA SOCNT,X / AND SOVCH,Y / BNE SOUL3 -- otherwise nothing more for this voice.
      }
      // 6502: .SOUL3 DEY / BMI P%+5 / JMP SOUL8.
    }

    // 6502: LDA PULSEW / EOR #%00000100 / STA PULSEW -- reached only through SOUL3, see above.
    _buffer.pulseWidth = static_cast<std::uint8_t>(_buffer.pulseWidth ^ PULSE_WIDTH_FLIP);
  }

} // namespace Elite
