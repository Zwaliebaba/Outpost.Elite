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
    /// The low six bits of SOFLG -- the effect number plus one, without the "new" bit.
    constexpr std::uint8_t FLAG_EFFECT_MASK = 0x3Fu;

    /// Bit 7 of SOFLG -- set by NOISE, cleared by SOINT's first pass over the voice.
    constexpr std::uint8_t FLAG_NEW = 0x80u;

    /// The priority a stopped voice is left with, which any effect beats.
    constexpr std::uint8_t PRIORITY_FREE = 0;

    /// One step of the sustain volume, which is the byte's high nibble.
    constexpr std::uint8_t SUSTAIN_STEP = 16;

    /// The pulse width's wobble, one bit folded each pass.
    constexpr std::uint8_t PULSE_WIDTH_FLIP = 0x04u;

    /// sfxbeep.
    constexpr std::uint8_t EFFECT_BEEP = 5;

    /*
     * The priority table read with Y possibly carrying bit 7, so possibly past its sixteen.
     *
     * The table is extracted to 136 bytes because 135 is the largest index the game reaches; anything
     * beyond that is not a read the shipped game makes and answers zero, `LineHeap::Read`'s rule.
     */
    [[nodiscard]] std::uint8_t PriorityAt(std::uint8_t _index) noexcept
    {
      return (_index < EFFECT_PRIORITY_TABLE.size()) ? EFFECT_PRIORITY_TABLE[_index] : std::uint8_t{0};
    }

    /*
     * NOISE from its CLV onwards, with the V flag as an argument.
     *
     * `_pitched` is V. When it is set the two `BVS`es take `_sustain` and `_frequency` -- the bytes
     * NOISE2 left in XX15 and XX15+1 -- instead of the effect's table entries. XX15+2, which both
     * routines write, is scratch that nothing reads afterwards, so it is a local here.
     */
    [[nodiscard]] NoiseResult MakeNoise(SoundBuffer& _buffer, std::uint8_t _effect, bool _carryIn, bool _pitched, std::uint8_t _sustain,
                                 std::uint8_t _frequency) noexcept
    {
      // SOUR1 is a bare return, so with sound off the carry is whatever it was.
      if (_buffer.soundOff != 0u)
      {
        return {_carryIn, _buffer.soundOff}; // A still holds `DNOIZ`, and `SOUR1` is a bare RTS
      }

      // The effect number plus one, kept UNMASKED, and the voice index from 2.
      std::size_t voice = 2;
      const std::uint8_t effectPlusOne = static_cast<std::uint8_t>(_effect + 1u);

      // The priority byte's bit 0 says whether to look for a voice already playing this
      // effect. Read before the mask, so index 135 is reachable.
      bool found = false;
      if ((PriorityAt(_effect) & 0x01u) == 0u)
      {
        // Each voice's flag masked to six bits and compared, counting down.
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
         * SOUX9 and SOUX1 -- the three priorities compared in pairs to find the lowest.
         *
         *
         * The lowest priority of the three, and the ties go the way the comparisons fall: a clear
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

      // The flag comes off here and not before.
      const std::uint8_t effect = static_cast<std::uint8_t>(_effect & 0x7Fu);

      // A priority below the voice's own goes back to SOUR1, so the carry is CLEAR.
      const std::uint8_t priority = PriorityAt(effect);
      if (priority < _buffer.priority[voice])
      {
        return {false, priority}; // A is the priority byte the failed comparison was made on
      }

      // The interrupt lock around the store is what the port's single thread gives.
      _buffer.priority[voice] = priority;

      // The sustain from the table, or the caller's when entered through `NOISE2`.
      _buffer.sustain[voice] = _pitched ? _sustain : EFFECT_SUSTAIN_TABLE[effect];

      _buffer.counter[voice] = EFFECT_COUNT_TABLE[effect];                    // Into SOCNT
      _buffer.frequencyChange[voice] = EFFECT_FREQUENCY_CHANGE_TABLE[effect]; // Into SOFRCH
      _buffer.control[voice] = EFFECT_CONTROL_TABLE[effect];                  // Into SOCR

      // The frequency from the table, or the caller's, the same way.
      _buffer.frequency[voice] = _pitched ? _frequency : EFFECT_FREQUENCY_TABLE[effect];

      _buffer.attack[voice] = EFFECT_ATTACK_TABLE[effect];          // Into SOATK
      _buffer.volumeRate[voice] = EFFECT_VOLUME_RATE_TABLE[effect]; // Into SOVCH

      // The effect number plus one with bit 7 forced on, into the flag, and out with the carry set.
      _buffer.flag[voice] = static_cast<std::uint8_t>((effect + 1u) | FLAG_NEW);
      return {true, _buffer.flag[voice]}; // A is the byte the store just wrote
    }

    /// SEVENS,Y -- the voice's register base.
    [[nodiscard]] std::uint8_t VoiceBase(std::size_t _voice) noexcept
    {
      return SEVENS_TABLE[_voice];
    }

    /*
     * The change added to the voice's frequency, and the sixteen-bit result
     * split across the SID's two registers, with the pulse width after it.
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

  NoiseResult PlaySoundEffect(SoundBuffer& _buffer, SoundEffect _effect, bool _carryIn) noexcept
  {
    // CLV, then the routine.
    return MakeNoise(_buffer, static_cast<std::uint8_t>(_effect), _carryIn, false, 0u, 0u);
  }

  NoiseResult PlaySoundEffectPitched(SoundBuffer& _buffer, SoundEffect _effect, std::uint8_t _sustain, std::uint8_t _frequency,
                                     bool _carryIn) noexcept
  {
    // The sustain and frequency staged, then into `NOISE` with the overflow set.
    return MakeNoise(_buffer, static_cast<std::uint8_t>(_effect), _carryIn, true, _sustain, _frequency);
  }

  NoiseResult Beep(SoundBuffer& _buffer, bool _carryIn) noexcept
  {
    // The beep effect, as a branch into `NOISE`.
    return PlaySoundEffect(_buffer, SoundEffect::Beep, _carryIn);
  }

  void StopSoundEffect(SoundBuffer& _buffer, SoundEffect _effect) noexcept
  {
    // NOISEOFF and SOUL1 -- the three voices searched for this effect, and the one that
    // matches has its counter set to 1 so the next pass kills it.
    const std::uint8_t effectPlusOne = static_cast<std::uint8_t>(static_cast<std::uint8_t>(_effect) + 1u);
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
    // SOFLUSH and SOUL2 -- every counter to 1, so the next pass kills all three.
    for (std::uint8_t& counter : _buffer.counter)
    {
      counter = 1u;
    }
  }

  void RunSoundEffects(SoundBuffer& _buffer, SidWriteLog& _log) noexcept
  {
    // Voice 2 down to voice 0.
    for (int index = 2; index >= 0; --index)
    {
      const std::size_t voice = static_cast<std::size_t>(index);
      const std::uint8_t flag = _buffer.flag[voice];

      /*
     * A silent voice takes the OTHER exit.
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
         * SOUL4 and SOUX3 -- the voice's base address written into the store's own operand,
         * then seven registers zeroed.
         *
         * The store is self-modified to the voice's base, and the loop zeroes the seven registers from
         * the top down. Seven writes of zero, in that order, and the first of them is what puts the
         * gate down before the control register puts it up again.
         */
        for (int reg = SID_VOICE_REGISTERS - 1; reg >= 0; --reg)
        {
          _log.Add(static_cast<std::uint8_t>(base + reg), 0u);
        }

        // The control, attack and sustain bytes into the voice's registers, and then
        // `SOUX2` with a change of zero.
        _log.Add(static_cast<std::uint8_t>(base + SID_CONTROL), _buffer.control[voice]);
        _log.Add(static_cast<std::uint8_t>(base + SID_ATTACK_DECAY), _buffer.attack[voice]);
        _log.Add(static_cast<std::uint8_t>(base + SID_SUSTAIN_RELEASE), _buffer.sustain[voice]);
        WriteFrequency(_buffer, _log, voice, 0u);

        // SOUL5 and SOUL6 -- the new-voice bit comes off the flag, then `SOUL3`.
        _buffer.flag[voice] = static_cast<std::uint8_t>(flag & ~FLAG_NEW);
      }
      else
      {
        // A non-zero change goes to SOUX2, and zero to SOUL5.
        if (_buffer.frequencyChange[voice] != 0u)
        {
          WriteFrequency(_buffer, _log, voice, _buffer.frequencyChange[voice]);
        }

        // SOUL5, with the new-voice bit already clear, so the branch is not taken.

        // The priority stepped down, but one that reaches zero is put back to one. A
        // priority that WAS zero goes to 255, which that test does not catch.
        _buffer.priority[voice] = static_cast<std::uint8_t>(_buffer.priority[voice] - 1u);
        if (_buffer.priority[voice] == 0u)
        {
          _buffer.priority[voice] = 1u;
        }

        // The counter stepped down, and zero kills the voice.
        _buffer.counter[voice] = static_cast<std::uint8_t>(_buffer.counter[voice] - 1u);
        if (_buffer.counter[voice] == 0u)
        {
          // The control byte's gate bit cleared, then the flag and priority
          // zeroed, and on to `SOUL3`.
          _log.Add(static_cast<std::uint8_t>(base + SID_CONTROL), static_cast<std::uint8_t>(_buffer.control[voice] & 0xFEu));
          _buffer.flag[voice] = 0u;
          _buffer.priority[voice] = PRIORITY_FREE;
        }
        else if ((_buffer.counter[voice] & _buffer.volumeRate[voice]) == 0u)
        {
          // One step off the sustain, into the voice's own byte and into the register.
          _buffer.sustain[voice] = static_cast<std::uint8_t>(_buffer.sustain[voice] - SUSTAIN_STEP);
          _log.Add(static_cast<std::uint8_t>(base + SID_SUSTAIN_RELEASE), _buffer.sustain[voice]);
        }
        // The counter masked by the volume rate -- otherwise nothing more for this voice.
      }
      // The voice index down, and back to `SOUL8` until it goes negative.
    }

    // The pulse width's bit folded -- reached only through `SOUL3`, see above.
    _buffer.pulseWidth = static_cast<std::uint8_t>(_buffer.pulseWidth ^ PULSE_WIDTH_FLIP);
  }

} // namespace Elite
