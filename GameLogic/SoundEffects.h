#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Elite
{

  /*
   * The sound effects (slice 5a).
   *
   * THE SID IS WRITTEN FROM AN INTERRUPT AND NOT FROM THE GAME. `NOISE` and its relatives put an
   * effect into a BUFFER -- ten bytes a voice, three voices -- and set a flag; the raster interrupt
   * handler `COMIRQ1` runs `SOINT` once a frame, and `SOINT` is the only thing that touches the chip.
   * So the game side of the sound is a set of pure functions over a struct, and the chip side is a
   * tick that turns the struct into register writes. The port keeps that split exactly: the
   * functions here take a `SoundBuffer`, the tick emits a `SidWriteLog`, and what the executable
   * does with the log -- a synthesiser, a sample player, nothing -- is its own business.
   *
   * What makes this comparable is that the log IS the observable. `SOINT` writes the chip in a
   * particular order, and the order matters: a new effect zeroes all seven of a voice's registers
   * and then sets four of them, which a real SID hears as a gate going down and up again. A port
   * that compared the registers' final state would be blind to that, so the oracle test compares
   * the sequence of writes and not the picture they leave.
   */

  /// 6502: SID -- one register write, as the interrupt handler makes it. `reg` is the offset from
  /// SID (&D400), 0 to &18.
  struct SidWrite
  {
    std::uint8_t reg = 0;
    std::uint8_t value = 0;
  };

  /*
   * The writes one interrupt makes, in order.
   *
   * Fixed capacity rather than a vector because the tick runs once per frame for the life of the
   * program and should not allocate. An effect tick writes at most fourteen bytes a voice; the music
   * player can write more in a tick that processes a long command chain, and 512 is above anything
   * the shipped tunes reach. What does not fit is dropped and counted rather than silently lost.
   */
  struct SidWriteLog
  {
    static constexpr std::size_t CAPACITY = 512;

    std::array<SidWrite, CAPACITY> writes{};
    std::size_t count = 0;
    std::size_t dropped = 0;

    void Add(std::uint8_t _reg, std::uint8_t _value) noexcept
    {
      if (count < CAPACITY)
      {
        writes[count] = SidWrite{_reg, _value};
        ++count;
      }
      else
      {
        ++dropped;
      }
    }

    void Clear() noexcept
    {
      count = 0;
      dropped = 0;
    }
  };

  /// 6502: the SID's register map, as `SOINT` and the music player address it.
  inline constexpr std::uint8_t SID_FREQUENCY_LOW = 0;
  inline constexpr std::uint8_t SID_FREQUENCY_HIGH = 1;
  inline constexpr std::uint8_t SID_PULSE_WIDTH_LOW = 2;
  inline constexpr std::uint8_t SID_PULSE_WIDTH_HIGH = 3;
  inline constexpr std::uint8_t SID_CONTROL = 4;
  inline constexpr std::uint8_t SID_ATTACK_DECAY = 5;
  inline constexpr std::uint8_t SID_SUSTAIN_RELEASE = 6;
  inline constexpr std::uint8_t SID_VOICE_REGISTERS = 7;
  inline constexpr std::uint8_t SID_FILTER_CUTOFF_LOW = 0x15;
  inline constexpr std::uint8_t SID_FILTER_CUTOFF_HIGH = 0x16;
  inline constexpr std::uint8_t SID_FILTER_CONTROL = 0x17;
  inline constexpr std::uint8_t SID_VOLUME = 0x18;
  inline constexpr std::uint8_t SID_REGISTER_COUNT = 0x19;

  /// 6502: the three voices, and the sixteen effects `NOISE` can be asked for.
  inline constexpr std::size_t SID_VOICE_COUNT = 3;
  inline constexpr std::uint8_t SOUND_EFFECT_COUNT = 16;

  /// 6502: sfxtrib -- the Trumbles, the one effect no constant elsewhere in the port names.
  inline constexpr std::uint8_t SOUND_TRUMBLES = 14;

  /*
   * 6502: sound_variables -- the buffer between the game and the interrupt.
   *
   * Ten arrays of three, one entry a voice, and one byte on its own. Every one is a label in the
   * original and the names are kept: `flag` is `SOFLG`, whose low six bits hold the effect number
   * PLUS ONE (so that zero means "nothing playing") and whose top bit says "new, not yet started";
   * `priority` is `SOPR`, which `NOISE` compares against to decide whether a new effect may take
   * the voice, and which counts DOWN as the effect plays so that an old sound loses to a new one.
   *
   * `pulseWidth` is `PULSEW`, which flips between 2 and 6 every frame that voice 1 is active and
   * is written to every voice's pulse-width register -- so every pulse effect in the game has the
   * same slow duty-cycle wobble. It starts at 2 because that is the byte the binary loads with.
   *
   * `soundOff` is `DNOIZ`, which is a configuration byte the pause screen toggles rather than sound
   * state, and it is here because `NOISE` is its only reader in this library.
   */
  struct SoundBuffer
  {
    std::array<std::uint8_t, SID_VOICE_COUNT> flag{};            ///< 6502: SOFLG
    std::array<std::uint8_t, SID_VOICE_COUNT> counter{};         ///< 6502: SOCNT
    std::array<std::uint8_t, SID_VOICE_COUNT> priority{};        ///< 6502: SOPR
    std::uint8_t pulseWidth = 2;                                 ///< 6502: PULSEW
    std::array<std::uint8_t, SID_VOICE_COUNT> frequencyChange{}; ///< 6502: SOFRCH
    std::array<std::uint8_t, SID_VOICE_COUNT> frequency{};       ///< 6502: SOFRQ
    std::array<std::uint8_t, SID_VOICE_COUNT> control{};         ///< 6502: SOCR
    std::array<std::uint8_t, SID_VOICE_COUNT> attack{};          ///< 6502: SOATK
    std::array<std::uint8_t, SID_VOICE_COUNT> sustain{};         ///< 6502: SOSUS
    std::array<std::uint8_t, SID_VOICE_COUNT> volumeRate{};      ///< 6502: SOVCH

    std::uint8_t soundOff = 0; ///< 6502: DNOIZ
  };

  /*
   * 6502: NOISE -- put effect `_effect` into the buffer, if it is allowed a voice.
   *
   * THREE ANSWERS, and the caller's carry is one of them (§6.99). The routine ends `SEC / RTS` when
   * it takes a voice; it reaches `SOUR1`, a bare `RTS`, by two different branches, and they leave
   * different flags. `LDA DNOIZ / BNE SOUR1` touches no flag, so with sound switched off the carry
   * that comes back is the one that went in. `CMP SOPR,X / BCC SOUR1` is a comparison that FAILED,
   * so an effect refused for priority comes back with the carry clear. `OUCH` opens its `DORND` on
   * this carry, which is why the difference is worth modelling (§6.88).
   *
   * BIT 7 OF THE EFFECT NUMBER IS A FLAG and it is read late. `HYPNOISE` passes sfxhyp1 + 128 to
   * layer the drive's sound on top of itself, and the routine reads `SFXPR,Y` with the 128 still
   * in Y -- index 135, which is a byte of `COLD` -- before masking it off. Its bit 0 decides whether
   * the "already playing on some voice" scan runs, and the scan compares against the UNMASKED
   * number plus one, so it can never match. The table is extracted to 136 bytes for this one read.
   *
   * WHICH VOICE: the one already playing this effect, else the lowest priority of the three -- a
   * comparison chain that prefers voice 2 on a tie with voice 1 and voice 3 on a tie with either.
   */
  [[nodiscard]] bool PlaySoundEffect(SoundBuffer& _buffer, std::uint8_t _effect, bool _carryIn) noexcept;

  /*
   * 6502: NOISE2 -- NOISE with the sustain byte and the frequency supplied instead of looked up.
   *
   * It is `BIT SOUR1 / STA XX15 / STX XX15+1 / EQUB &50` and then `NOISE` past its `CLV`. The `BIT`
   * on a byte holding `RTS` (&60) sets the overflow flag, and the two `BVS` inside `NOISE` take the
   * supplied bytes instead of the table's. The `EQUB &50` is a `BVC` that cannot branch, swallowing
   * the `CLV` (§6.79's idiom). So this is one routine with a flag, and the port writes it that way.
   */
  [[nodiscard]] bool PlaySoundEffectPitched(SoundBuffer& _buffer, std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t _frequency,
                                            bool _carryIn) noexcept;

  /// 6502: BEEP, BELL -- `LDY #sfxbeep / BNE NOISE`, a tail call, so the carry it returns is NOISE's.
  /// `BELL` is `LDA #7 / JMP CHPR`, and character 7 in `CHPR` is `R5`, which is `JSR BEEP`: the
  /// text printer's `TextEffects::Beep` seam reaches this, so the bell has no routine of its own.
  [[nodiscard]] bool Beep(SoundBuffer& _buffer, bool _carryIn) noexcept;

  /*
   * 6502: NOISEOFF -- find the voice playing `_effect` and run its counter down.
   *
   * It sets `SOCNT` to 1 rather than clearing the flag, so the interrupt handler ends the sound on
   * its next pass the ordinary way -- gate off, flag and priority cleared -- and nothing here writes
   * the chip. A voice not playing the effect is left alone, and so is everything if none is.
   */
  void StopSoundEffect(SoundBuffer& _buffer, std::uint8_t _effect) noexcept;

  /// 6502: SOFLUSH -- the same for all three voices at once: every counter to 1, so every sound
  /// ends on the next interrupt. `stopbd` calls it before silencing the chip.
  void FlushSoundEffects(SoundBuffer& _buffer) noexcept;

  /*
   * 6502: SOINT, SOUL3b -- one interrupt's worth of the effect player.
   *
   * Voice 3 down to voice 1, and for each: nothing to do if the flag is zero; if the flag's top bit
   * is set the effect is NEW, so zero the voice's seven registers and write control, attack and
   * sustain, then the frequency; otherwise apply the frequency change if there is one. Then the
   * housekeeping: the priority steps down but never below one, the counter steps down and ends the
   * sound at zero (gate off, flag and priority cleared), and every time the counter lands on a
   * multiple the volume-rate mask picks out, the sustain volume drops a step.
   *
   * THE FREQUENCY IS ONE BYTE SPREAD ACROSS TWO REGISTERS: `f` becomes `00ffffff ff000000`, so the
   * chip's sixteen-bit frequency is `f * 64`. And the sustain step is `SEC / SBC #16` on a byte
   * whose high nibble is the volume, which WRAPS when the volume is already zero -- a nibble of
   * zero minus one is fifteen, so a quiet effect that keeps stepping comes back at full volume.
   * The port keeps the byte arithmetic and the oracle confirms the wrap is what the game does.
   *
   * THE PULSE WIDTH FLIPS ON ONE EXIT AND NOT THE OTHER. A voice with nothing playing branches to
   * `SOUL3b`, which steps to the next voice and, after voice 1, returns from the interrupt
   * directly; a voice that was processed reaches `SOUL3`, which after voice 1 falls into the
   * `EOR #4` on `PULSEW`. So the width alternates only on frames where voice 1 is active.
   */
  void RunSoundEffects(SoundBuffer& _buffer, SidWriteLog& _log) noexcept;

} // namespace Elite
